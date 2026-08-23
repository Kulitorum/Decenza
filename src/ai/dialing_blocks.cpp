#include "dialing_blocks.h"
#include "dialing_helpers.h"
#include "shotsummarizer.h"

#include "../history/shothistorystorage.h"
#include "../history/shotprojection.h"
#include "../core/grinderaliases.h"
#include "../core/settings.h"
#include "../core/settings_calibration.h"
#include "../controllers/profilemanager.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QList>
#include <QMap>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringLiteral>
#include <QVariantList>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

DialingHelpers::ShotDiffInputs toDiffInputs(const ShotProjection& s)
{
    DialingHelpers::ShotDiffInputs d;
    d.grinderSetting = s.grinderSetting;
    d.rpm = static_cast<int>(s.rpm);
    d.beanBrand = s.beanBrand;
    d.doseWeightG = s.doseWeightG;
    d.finalWeightG = s.finalWeightG;
    d.durationSec = s.durationSec;
    d.enjoyment0to100 = s.enjoyment0to100;
    return d;
}

QJsonObject changeFromPrev(const ShotProjection& prev, const ShotProjection& curr)
{
    return DialingHelpers::buildShotChangeDiff(toDiffInputs(prev), toDiffInputs(curr));
}

// Effective stop-at-weight target for a shot: the stored value when set,
// else parsed from the embedded profile JSON. The parse branch only runs
// for shots imported from external formats (de1app / visualizer.coffee)
// where the importer left targetWeight at 0; that cohort is the riskiest
// for malformed input, so log parse failures rather than swallow them.
// Returns 0 when neither source yields a positive target. Single source
// of truth so the dialInSessions hoist decision (#1164 finding #3) and
// the per-shot emission cannot disagree.
double effectiveTargetWeightG(const ShotProjection& shot)
{
    if (shot.targetWeightG > 0)
        return shot.targetWeightG;
    if (shot.profileJson.isEmpty())
        return 0.0;
    QJsonParseError err{};
    QJsonObject profileObj = QJsonDocument::fromJson(shot.profileJson.toUtf8(), &err).object();
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "effectiveTargetWeightG: profileJson parse failed for shot" << shot.id
                   << ":" << err.errorString();
        return 0.0;
    }
    QJsonValue tw = profileObj["target_weight"];
    double twVal = tw.isString() ? tw.toString().toDouble() : tw.toDouble();
    return twVal > 0 ? twVal : 0.0;
}

// Per-shot serializer for dialInSessions. Identity/lifecycle overrides come
// from the per-session `hoistSessionContext` output; per-shot entries emit
// the identity and storage-lifecycle fields only when they differ from the
// session context (otherwise the field is hoisted and absent here).
QJsonObject shotToJson(const ShotProjection& shot,
                       const DialingHelpers::ShotIdentity& override)
{
    QJsonObject h;
    h["id"] = shot.id;
    h["timestamp"] = shot.timestampIso;
    h["doseG"] = shot.doseWeightG;
    h["yieldG"] = shot.finalWeightG;
    h["durationSec"] = shot.durationSec;
    // #1158 (`pourControl`) and #1164 finding #3 (`profileName`,
    // `targetWeightG`, `temperatureOverrideC`): these are NOT emitted here.
    // A dial-in session is almost always one profile at one target weight
    // and temperature, so repeating them on every shot is pure bloat.
    // buildDialInSessionsBlock hoists each to the session `context` when
    // every shot in the session shares it (the common case → zero per-shot
    // repetition) and emits a per-shot field only when a session genuinely
    // mixes the value. Same dedup discipline as the grinder/bean identity
    // hoist above.
    h["enjoyment0to100"] = shot.enjoyment0to100 > 0
        ? QJsonValue(shot.enjoyment0to100)
        : QJsonValue(QJsonValue::Null);
    h["grinderSetting"] = shot.grinderSetting;
    // RPM is the second half of the dial-in for variable-RPM grinders and is
    // shot-variable like grinderSetting — so it belongs on the per-shot entry,
    // never the hoisted session context. Sparse: non-RPM shots omit it.
    if (shot.rpm > 0)
        h["rpm"] = shot.rpm;
    if (!override.grinderBrand.isEmpty())
        h["grinderBrand"] = override.grinderBrand;
    if (!override.grinderModel.isEmpty())
        h["grinderModel"] = override.grinderModel;
    if (!override.grinderBurrs.isEmpty())
        h["grinderBurrs"] = override.grinderBurrs;
    if (!override.basketBrand.isEmpty())
        h["basketBrand"] = override.basketBrand;
    if (!override.basketModel.isEmpty())
        h["basketModel"] = override.basketModel;
    if (!override.puckPrep.isEmpty())
        h["puckPrep"] = override.puckPrep;
    if (!override.beanBrand.isEmpty())
        h["beanBrand"] = override.beanBrand;
    if (!override.beanType.isEmpty())
        h["beanType"] = override.beanType;
    // Bean storage lifecycle (bean-freshness-followup): emitted per-shot only
    // when it differs from the session context (e.g. a session spanning a thaw
    // or open event), so the AI can tell a best-rated anchor came from a
    // different, longer-rested portion. Hoisted to context otherwise.
    if (!override.frozenDate.isEmpty())
        h["frozenDate"] = override.frozenDate;
    if (!override.defrostDate.isEmpty())
        h["defrostDate"] = override.defrostDate;
    if (!override.storageHint.isEmpty())
        h["storageHint"] = override.storageHint;
    if (!override.openedDate.isEmpty())
        h["openedDate"] = override.openedDate;
    h["notes"] = shot.espressoNotes;
    // Structured taste taps (add-ai-taste-intake): emitted per history shot so
    // the advisor can see how a prior shot tasted (e.g. "last time you tapped
    // sour, you went finer and it balanced"). "" = untapped → omitted.
    if (!shot.tasteBalance.isEmpty())
        h["tasteBalance"] = shot.tasteBalance;
    if (!shot.tasteBody.isEmpty())
        h["tasteBody"] = shot.tasteBody;
    // #1161: why the shot ended. stoppedBy varies shot-to-shot (a session
    // can mix a SAW shot and a manually-aborted one), so it is NOT hoisted
    // — emit per-shot. Omit the common "profileEnd"/"" (no signal: the AI
    // falls back to yield-vs-targetWeightG there) to keep the payload lean
    // (#1164 discipline). Emit the meaningful ones so the AI knows whether
    // the yield was pinned ("weight"/"volume") or user-chosen and NOT
    // dial-in diagnostic ("manual").
    if (shot.stoppedBy == QStringLiteral("manual")
        || shot.stoppedBy == QStringLiteral("weight")
        || shot.stoppedBy == QStringLiteral("volume"))
        h["stoppedBy"] = shot.stoppedBy;
    return h;
}

} // namespace

namespace DialingBlocks {

QJsonArray buildDialInSessionsBlock(QSqlDatabase& db,
                                    const QString& profileKbId,
                                    qint64 resolvedShotId,
                                    int historyLimit,
                                    const ShotProjection& currentShot)
{
    QJsonArray sessions;
    if (profileKbId.isEmpty()) return sessions;

    QVariantList history = ShotHistoryStorage::loadRecentShotsByKbIdStatic(
        db, profileKbId, historyLimit, resolvedShotId, currentShot.equipmentId);

    QList<ShotProjection> shots;
    shots.reserve(history.size());
    for (const auto& v : history)
        shots.append(ShotProjection::fromVariantMap(v.toMap()));

    QList<qint64> timestamps;
    timestamps.reserve(shots.size());
    for (const auto& s : shots)
        timestamps.append(s.timestamp);
    const auto sessionIndices = DialingHelpers::groupSessions(timestamps);

    for (const auto& indices : sessionIndices) {
        // Reverse indices to ASC within the session so changeFromPrev
        // reads "older -> newer" — matching how the user iterates.
        QList<ShotProjection> ordered;
        ordered.reserve(indices.size());
        for (qsizetype i = indices.size() - 1; i >= 0; --i)
            ordered.append(shots[indices[i]]);

        QList<DialingHelpers::ShotIdentity> identities;
        identities.reserve(ordered.size());
        for (const ShotProjection& s : ordered) {
            DialingHelpers::ShotIdentity id;
            id.grinderBrand = s.grinderBrand;
            id.grinderModel = s.grinderModel;
            id.grinderBurrs = s.grinderBurrs;
            id.basketBrand = s.basketBrand;
            id.basketModel = s.basketModel;
            id.puckPrep = s.puckPrep;
            id.beanBrand = s.beanBrand;
            id.beanType = s.beanType;
            id.frozenDate = s.frozenDate;
            id.defrostDate = s.defrostDate;
            id.storageHint = s.storageHint;
            id.openedDate = s.openedDate;
            identities.append(id);
        }
        const DialingHelpers::HoistedSession hoisted =
            DialingHelpers::hoistSessionContext(identities);

        // Issue #1158: pour control mode per shot, from each shot's own
        // recipe. Hoist to session context when uniform (the common
        // case — a session is usually one profile), emit per-shot only
        // when a session mixes flow/pressure variants of the same kbId
        // family (e.g. D-Flow/Q vs Damian's LM Leva). "" (no usable
        // recipe) breaks uniformity so we never assert a value we
        // didn't derive.
        QStringList pourControls;
        pourControls.reserve(ordered.size());
        for (const ShotProjection& s : ordered)
            pourControls.append(pourControlFromProfileJson(s.profileJson));
        const bool pourControlUniform =
            !pourControls.first().isEmpty()
            && std::all_of(pourControls.cbegin(), pourControls.cend(),
                           [&](const QString& p){ return p == pourControls.first(); });

        // #1164 finding #3: same hoist discipline for profileName /
        // targetWeightG / temperatureOverrideC. A dial-in session is almost
        // always one profile at one target weight and temperature, so these
        // repeated identically on every shot. Hoist to session context when
        // uniform; emit per-shot only when a session genuinely mixes them.
        // "" / 0 means "not set" and breaks uniformity, so we never hoist a
        // value that doesn't apply to every shot in the session.
        QStringList profileNames;
        QList<double> targetWeights;
        QList<double> tempOverrides;
        profileNames.reserve(ordered.size());
        targetWeights.reserve(ordered.size());
        tempOverrides.reserve(ordered.size());
        for (const ShotProjection& s : ordered) {
            profileNames.append(s.profileName);
            targetWeights.append(effectiveTargetWeightG(s));
            tempOverrides.append(s.temperatureOverrideC);
        }
        const bool profileNameUniform =
            !profileNames.first().isEmpty()
            && std::all_of(profileNames.cbegin(), profileNames.cend(),
                           [&](const QString& p){ return p == profileNames.first(); });
        const bool targetWeightUniform =
            targetWeights.first() > 0
            && std::all_of(targetWeights.cbegin(), targetWeights.cend(),
                           [&](double w){ return w == targetWeights.first(); });
        const bool tempOverrideUniform =
            tempOverrides.first() > 0
            && std::all_of(tempOverrides.cbegin(), tempOverrides.cend(),
                           [&](double t){ return t == tempOverrides.first(); });

        QJsonArray sessionShots;
        for (qsizetype i = 0; i < ordered.size(); ++i) {
            QJsonObject h = shotToJson(ordered[i], hoisted.perShotOverrides[i]);
            if (!pourControlUniform && !pourControls[i].isEmpty())
                h["pourControl"] = pourControls[i];
            if (!profileNameUniform && !profileNames[i].isEmpty())
                h["profileName"] = profileNames[i];
            if (!targetWeightUniform && targetWeights[i] > 0)
                h["targetWeightG"] = targetWeights[i];
            if (!tempOverrideUniform && tempOverrides[i] > 0)
                h["temperatureOverrideC"] = tempOverrides[i];
            if (i > 0) {
                QJsonObject diff = changeFromPrev(ordered[i-1], ordered[i]);
                h["changeFromPrev"] = diff.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(diff);
            } else {
                h["changeFromPrev"] = QJsonValue(QJsonValue::Null);
            }
            sessionShots.append(h);
        }

        QJsonObject contextObj;
        if (!hoisted.context.grinderBrand.isEmpty())
            contextObj["grinderBrand"] = hoisted.context.grinderBrand;
        if (!hoisted.context.grinderModel.isEmpty())
            contextObj["grinderModel"] = hoisted.context.grinderModel;
        if (!hoisted.context.grinderBurrs.isEmpty())
            contextObj["grinderBurrs"] = hoisted.context.grinderBurrs;
        // Basket + puck prep name the equipment package this session's shots
        // were pulled on. They land here rather than on a per-shot override
        // whenever the history was package-scoped — which is whenever the
        // bucket resolved. The override path stays live for the case where it
        // did not, so nothing here assumes the scoping ran.
        if (!hoisted.context.basketBrand.isEmpty())
            contextObj["basketBrand"] = hoisted.context.basketBrand;
        if (!hoisted.context.basketModel.isEmpty())
            contextObj["basketModel"] = hoisted.context.basketModel;
        if (!hoisted.context.puckPrep.isEmpty())
            contextObj["puckPrep"] = hoisted.context.puckPrep;
        if (!hoisted.context.beanBrand.isEmpty())
            contextObj["beanBrand"] = hoisted.context.beanBrand;
        if (!hoisted.context.beanType.isEmpty())
            contextObj["beanType"] = hoisted.context.beanType;
        // Bean storage lifecycle (bean-freshness-followup): hoisted to the
        // session context when shared across every shot, overridden per-shot
        // when a session spans a thaw/open event (see shotToJson).
        if (!hoisted.context.frozenDate.isEmpty())
            contextObj["frozenDate"] = hoisted.context.frozenDate;
        if (!hoisted.context.defrostDate.isEmpty())
            contextObj["defrostDate"] = hoisted.context.defrostDate;
        if (!hoisted.context.storageHint.isEmpty())
            contextObj["storageHint"] = hoisted.context.storageHint;
        if (!hoisted.context.openedDate.isEmpty())
            contextObj["openedDate"] = hoisted.context.openedDate;
        // Issue #1158: hoisted pour control mode — one field for the
        // whole session instead of repeating it on every shot.
        if (pourControlUniform)
            contextObj["pourControl"] = pourControls.first();
        // #1164 finding #3: hoisted profile / target weight / temperature
        // override — one field per session instead of one per shot.
        if (profileNameUniform)
            contextObj["profileName"] = profileNames.first();
        if (targetWeightUniform)
            contextObj["targetWeightG"] = targetWeights.first();
        if (tempOverrideUniform)
            contextObj["temperatureOverrideC"] = tempOverrides.first();

        QJsonObject sessionObj;
        sessionObj["sessionStart"] = ordered.first().timestampIso;
        sessionObj["sessionEnd"] = ordered.last().timestampIso;
        sessionObj["shotCount"] = static_cast<int>(ordered.size());
        if (!contextObj.isEmpty())
            sessionObj["context"] = contextObj;
        sessionObj["shots"] = sessionShots;
        sessions.append(sessionObj);
    }

    return sessions;
}

QJsonObject buildBestRecentShotBlock(QSqlDatabase& db,
                                     const QString& profileKbId,
                                     qint64 resolvedShotId,
                                     const ShotProjection& currentShot)
{
    if (profileKbId.isEmpty()) return QJsonObject();

    const qint64 windowFloorSec =
        QDateTime::currentSecsSinceEpoch()
        - kBestRecentShotWindowDays * 24 * 3600;
    QSqlQuery bestQ(db);
    // Highest user-rated shot in the 90-day window for this profile.
    // Falls back to nothing when the user has no rated shots — the
    // elicitation paths (the rating slider, conversational capture) keep
    // this pool populated.
    // Equipment-scoped, unconditionally. This
    // block is presented to the model as the outcome to REPRODUCE, so its
    // dose/yield/duration/grind read as a target. An anchor from other gear is
    // a target the user cannot hit at the settings it reports — strictly worse
    // than no anchor — so a higher-rated shot on another package is passed over
    // rather than offered, and the block is omitted when this package has no
    // rated shot in the window.
    //
    // The bucket comes from `currentShot`, which the caller already loaded from
    // the row `resolvedShotId` names — not from a second lookup. Re-querying it
    // here bought nothing except an unresolved case to degrade on, and the
    // degrade (run unscoped) is the confound this scoping exists to remove.
    // `equipmentId` is 0 for an unpackaged shot, which is a real bucket, so the
    // predicate is unconditional and cannot be skipped.
    const qint64 bestBucket = currentShot.equipmentId;
    const QString bestSql = QStringLiteral("SELECT id FROM shots "
                                     "WHERE profile_kb_id = ? AND enjoyment > 0 "
                                     "AND id != ? AND timestamp >= ? AND ")
                          + ShotHistoryStorage::equipmentBucketSql()
                          + QStringLiteral(" ORDER BY enjoyment DESC, timestamp DESC LIMIT 1");
    bestQ.prepare(bestSql);
    bestQ.addBindValue(profileKbId);
    bestQ.addBindValue(resolvedShotId);
    bestQ.addBindValue(windowFloorSec);
    bestQ.addBindValue(bestBucket);
    // Whitespace before () dodges a permission-hook false-positive on the
    // pattern `.exec(`. Do not auto-format.
    if (!bestQ.exec ()) {
        qWarning() << "buildBestRecentShotBlock: best-shot query failed:"
                   << bestQ.lastError().text() << "kbId=" << profileKbId;
        return QJsonObject();
    }
    if (!bestQ.next()) return QJsonObject();   // no rated shot in window — documented omission

    const qint64 bestId = bestQ.value(0).toLongLong();
    ShotRecord bestRecord = ShotHistoryStorage::loadShotRecordStatic(db, bestId);
    const ShotProjection best = ShotHistoryStorage::convertShotRecord(bestRecord);
    if (!best.isValid()) return QJsonObject();

    QJsonObject b;
    b["id"] = best.id;
    b["timestamp"] = best.timestampIso;
    b["enjoyment0to100"] = best.enjoyment0to100;
    b["doseG"] = best.doseWeightG;
    b["yieldG"] = best.finalWeightG;
    b["durationSec"] = best.durationSec;
    // Issue #1158: same control-mode + stop-at-weight provenance as the
    // dialInSessions entries, so the LLM applies the recipe rule when
    // anchoring on the best shot instead of treating its yield/duration
    // as a dial-in target.
    const QString bestPourControl = DialingBlocks::pourControlFromProfileJson(best.profileJson);
    if (!bestPourControl.isEmpty())
        b["pourControl"] = bestPourControl;
    if (best.targetWeightG > 0)
        b["targetWeightG"] = best.targetWeightG;
    // #1161: surface why the anchor shot ended (same sparse-emit rule as
    // shotToJson). A "manual" best shot's yield is user-chosen, so the AI
    // should not treat it as a yield target to reproduce.
    if (best.stoppedBy == QStringLiteral("manual")
        || best.stoppedBy == QStringLiteral("weight")
        || best.stoppedBy == QStringLiteral("volume"))
        b["stoppedBy"] = best.stoppedBy;
    b["grinderSetting"] = best.grinderSetting;
    if (best.rpm > 0)
        b["rpm"] = best.rpm;  // RPM half of the anchor's dial-in (sparse)
    b["grinderModel"] = best.grinderModel;
    b["beanBrand"] = best.beanBrand;
    b["beanType"] = best.beanType;
    // Bean storage lifecycle (bean-freshness-followup): carry the anchor shot's
    // own snapshotted dates directly (no hoisting — this is a single object).
    // When they differ from the resolved shot's currentBean.beanFreshness, the
    // AI has the raw data to notice the anchor came from a different, longer-
    // rested portion — no precomputed "different portion" flag, the dates are
    // the whole signal. Sparse-emit: legacy shots with no lifecycle recorded
    // carry nothing, same as today.
    if (!best.frozenDate.isEmpty())
        b["frozenDate"] = best.frozenDate;
    if (!best.defrostDate.isEmpty())
        b["defrostDate"] = best.defrostDate;
    if (!best.storageHint.isEmpty())
        b["storageHint"] = best.storageHint;
    if (!best.openedDate.isEmpty())
        b["openedDate"] = best.openedDate;
    b["notes"] = best.espressoNotes;
    // Structured taste taps (add-ai-taste-intake) on the best-rated anchor shot.
    if (!best.tasteBalance.isEmpty())
        b["tasteBalance"] = best.tasteBalance;
    if (!best.tasteBody.isEmpty())
        b["tasteBody"] = best.tasteBody;
    if (best.doseWeightG > 0)
        b["ratio"] = QString("1:%1").arg(best.finalWeightG / best.doseWeightG, 0, 'f', 2);
    if (best.timestamp > 0) {
        const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
        b["daysSinceShot"] = (nowSec - best.timestamp) / (24 * 3600);
    }
    const QJsonObject diff = changeFromPrev(best, currentShot);
    if (!diff.isEmpty())
        b["changeFromBest"] = diff;

    return b;
}

QJsonObject buildGrinderContextBlock(QSqlDatabase& db, const ShotProjection& currentShot)
{
    const QString& grinderModel = currentShot.grinderModel;
    const QString& beanBrand = currentShot.beanBrand;
    const qint64 equipmentBucket = currentShot.equipmentId;
    if (grinderModel.isEmpty()) return QJsonObject();

    const QString bevType = currentShot.beverageType.isEmpty()
        ? QStringLiteral("espresso") : currentShot.beverageType;

    GrinderContext ctx = ShotHistoryStorage::queryGrinderContext(
        db, grinderModel, bevType, beanBrand, equipmentBucket);

    // Cross-bean fallback for sparse OR empty bean-scoped results. It widens
    // the BEAN, never the equipment: the bucket is passed through unchanged, so
    // a sparse package cannot recover a range by borrowing another basket's
    // settings — the whole point of the scoping.
    bool haveCrossBean = false;
    GrinderContext crossBean;
    if (!beanBrand.isEmpty() && ctx.settingsObserved.size() < 2) {
        // Skip the wide step derivations: they read only the grinder model, so this
        // call would re-run two table scans for the answer ctx already holds.
        crossBean = ShotHistoryStorage::queryGrinderContext(
            db, grinderModel, bevType, QString(), equipmentBucket, /*deriveWideSteps=*/false);
        crossBean.stepSize = ctx.stepSize;
        crossBean.rpmStepSize = ctx.rpmStepSize;
        haveCrossBean = !crossBean.settingsObserved.isEmpty();
    }

    if (ctx.settingsObserved.isEmpty() && !haveCrossBean) return QJsonObject();

    QJsonObject grinderCtx;
    const GrinderContext& primary =
        ctx.settingsObserved.isEmpty() ? crossBean : ctx;
    grinderCtx["model"] = primary.model;
    grinderCtx["beverageType"] = primary.beverageType;
    QJsonArray settingsArr;
    for (const auto& s : primary.settingsObserved)
        settingsArr.append(s);
    grinderCtx["settingsObserved"] = settingsArr;
    grinderCtx["isNumeric"] = primary.allNumeric;
    if (primary.allNumeric && primary.maxSetting > primary.minSetting) {
        grinderCtx["observedMinSetting"] = primary.minSetting;
        grinderCtx["observedMaxSetting"] = primary.maxSetting;
    }
    // Noise-filtered typical dial increment. Emitted whenever ≥2 distinct
    // numeric settings yielded a step (deriveGrindStep), independent of the
    // min/max range gate above — a mixed-notation grinder can still have a
    // meaningful numeric step.
    if (primary.stepSize > 0)
        grinderCtx["stepSize"] = primary.stepSize;
    // RPM axis (variable-RPM grinders): the observed RPMs, their range, and the
    // noise-filtered typical RPM step — the RPM counterpart of the fields above.
    // Sparse: absent entirely for grinders with no recorded RPM history.
    if (!primary.rpmsObserved.isEmpty()) {
        QJsonArray rpmArr;
        for (int r : primary.rpmsObserved)
            rpmArr.append(r);
        grinderCtx["rpmsObserved"] = rpmArr;
        if (primary.rpmMax > primary.rpmMin) {
            grinderCtx["observedMinRpm"] = primary.rpmMin;
            grinderCtx["observedMaxRpm"] = primary.rpmMax;
        }
        if (primary.rpmStepSize > 0)
            grinderCtx["rpmStepSize"] = primary.rpmStepSize;
    }
    if (haveCrossBean && !ctx.settingsObserved.isEmpty()) {
        QJsonArray allArr;
        for (const auto& s : crossBean.settingsObserved)
            allArr.append(s);
        grinderCtx["allBeansSettings"] = allArr;
    }
    return grinderCtx;
}

QString sawBasketKeyFor(const ShotProjection& shot, const QString& activeBasketKey)
{
    if (shot.basketBrand.isEmpty() && shot.basketModel.isEmpty())
        return activeBasketKey;
    return SettingsCalibration::sawBasketKey(shot.basketBrand, shot.basketModel);
}

QJsonObject buildSawPredictionBlock(Settings* settings,
                                    ProfileManager* profileManager,
                                    const ShotProjection& currentShot)
{
    // Gate order: cheapest pure-shot gates first, then null-pointer guards,
    // then the Settings/ProfileManager-dependent gates. Putting the
    // pointer guards first would let a non-espresso or no-flow shot
    // short-circuit on a different reason than its name implies and make
    // gate-coverage tests confusingly entangled.
    const QString bevType = currentShot.beverageType.isEmpty()
        ? QStringLiteral("espresso") : currentShot.beverageType;
    if (bevType.compare(QStringLiteral("espresso"), Qt::CaseInsensitive) != 0)
        return QJsonObject();

    const double flowAtCutoff = DialingHelpers::estimateFlowAtCutoff(
        currentShot.flow, currentShot.durationSec);
    if (flowAtCutoff <= 0) return QJsonObject();

    if (!settings || !profileManager) return QJsonObject();

    // Deliberately NOT settings->scaleType(). That is the saved primary, which is not
    // necessarily the scale serving the shot path, and reading it here made the advisor
    // report drip/tier/sample-count for a pool the learner was not training — invisible
    // from the advisor's own output. Empty asks SettingsCalibration to resolve, which is
    // the same answer the learner, the Calibration tab and the MCP reset tool all get.
    const QString scaleType = settings->calibration()->currentScaleType();

    // THIS SHOT'S profile, not the active one — same reason as the basket below.
    // The shot records a TITLE; the catalog maps it to the filename SAW learned
    // under. Falls back to the active profile when the shot names none, or names
    // one this device no longer has (an imported archive, a deleted profile).
    const QString shotProfile = currentShot.profileName.isEmpty()
        ? QString() : profileManager->findProfileByTitle(currentShot.profileName);
    const QString profileFilename = shotProfile.isEmpty() ? profileManager->baseProfileName()
                                                          : shotProfile;
    if (scaleType.isEmpty() || profileFilename.isEmpty()) return QJsonObject();

    const QString basketKey =
        sawBasketKeyFor(currentShot, settings->calibration()->currentBasketKey());
    const double predictedDripG =
        settings->calibration()->getExpectedDripFor(profileFilename, scaleType, flowAtCutoff, basketKey);
    const QString sourceTier =
        settings->calibration()->sawModelSource(profileFilename, scaleType, basketKey);
    const double learnedLagSec =
        settings->calibration()->sawLearnedLagFor(profileFilename, scaleType, basketKey);
    const qsizetype sampleCount =
        settings->calibration()->perProfileSawHistory(profileFilename, scaleType, basketKey).size();

    QJsonObject sawPrediction;
    sawPrediction["profileFilename"] = profileFilename;
    sawPrediction["scaleType"] = scaleType;
    sawPrediction["basket"] = basketKey;
    sawPrediction["flowAtCutoffMlPerSec"] =
        QString::number(flowAtCutoff, 'f', 2).toDouble();
    sawPrediction["predictedDripG"] =
        QString::number(predictedDripG, 'f', 2).toDouble();
    sawPrediction["learnedLagSec"] =
        QString::number(learnedLagSec, 'f', 2).toDouble();
    sawPrediction["sampleCount"] = sampleCount;
    sawPrediction["sourceTier"] = sourceTier;
    // Which of the three key parts describe the SHOT. Shots do not record a
    // scale, so that one is always the currently-connected one — say so rather
    // than let a block sitting among shot-scoped data imply otherwise.
    sawPrediction["keyedOn"] = QStringLiteral(
        "this shot's profile and basket; scale is the one connected now "
        "(shots do not record a scale)");
    if (predictedDripG >= 0.2) {
        sawPrediction["recommendation"] = QString(
            "Set the stop-at-weight target ~%1 g lower than your aim "
            "to land near goal — that's the typical post-cutoff drip "
            "on this (profile, scale, basket) combination.")
                .arg(predictedDripG, 0, 'f', 1);
    }
    return sawPrediction;
}

// buildCurrentBeanBlock is defined inline in the header so test binaries
// that link only `shotsummarizer.cpp` don't drag in this TU's DB-dependent
// block builders (loadShotRecordStatic et al.).

// ---------------------------------------------------------------------
// recentAdvice block (issue #1053) — closed-loop coaching attribution.
// ---------------------------------------------------------------------

namespace {

// Adherence tolerance. Grinder matches as exact string OR numerically
// within ±0.25 of a step (covers quarter-step grinder click rounding).
// The "no movement" failure mode — recommendation 4.75, prior 5.0,
// actual 5.0 — is caught by the prior-movement guard inside
// grinderMatches, NOT by tightening this tolerance. Dose tolerance is
// ±0.3g — tighter than measurement noise but wider than the user's
// typical scale precision.
constexpr double kGrinderStepTolerance = 0.25;
constexpr double kDoseToleranceG = 0.3;

// What a `structuredNext` field asks of adherence scoring. The fields are
// authored by an LLM, so "the model recommended something we can check" is not
// a given and must be a distinct state from "recommended nothing".
enum class RecommendationKind {
    None,         // field absent, or explicitly empty — nothing was asked for
    Scoreable,    // a value we can compare against the shot
    Unscoreable,  // something was asked for, but not in a form we can check
};

// Classify `structuredNext.grinderSetting`.
//
// Models do write prose here — "a touch coarser than 9" (GPT-5.6 Terra),
// "slightly coarser than 9" (GPT-5.4 mini), observed live 2026-07-30 — and
// prose matches no recorded setting, so it cannot be scored either way.
//
// Two traps this has to avoid, both found in review of the first attempt:
//
//   1. Whitespace does NOT mean prose. Compound notation writes "1 + 4" as
//      readily as "1+4", and every Eureka Mignon and 1Zpresso entry in the
//      catalog uses it. Rejecting on any space silently stopped scoring all of
//      them. GrinderAliases::looksLikeSetting() is the shared authority for the
//      syntax, and grinderMatches() compares through the same header so the
//      gate and the comparator agree on what a setting is.
//   2. The JSON type is not guaranteed. The schema says string, but a model may
//      emit `"grinderSetting": 4.75` unquoted, and QJsonValue::toString()
//      returns an EMPTY QString for a non-string type
//      (qtbase/src/corelib/serialization/qjsonvalue.cpp:790 — "If type() is not
//      String, a null QString will be returned"). Read as empty that reads as
//      "no grind change", and grinderMatches() returns true on its isEmpty()
//      early-out — scoring a recommendation nobody can check as fully followed.
//      classifyPositiveNumberField()/classifyStringField() apply the same rule
//      to the other axes; see the note there about rpm having failed open.
RecommendationKind classifyGrinderRecommendation(const QJsonObject& sn,
                                                 QString& outRecommended)
{
    outRecommended.clear();
    if (!sn.contains(QStringLiteral("grinderSetting")))
        return RecommendationKind::None;

    const QJsonValue v = sn.value(QStringLiteral("grinderSetting"));
    if (!v.isString()) {
        qWarning() << "computeAdherence: grinderSetting is not a JSON string (type"
                   << int(v.type()) << ") — cannot score it";
        return RecommendationKind::Unscoreable;
    }

    // Trim before anything else: a padded "4.75 " is the same dial position as
    // "4.75", and grinderMatches() compares against untrimmed database values.
    const QString s = v.toString().trimmed();
    if (s.isEmpty())
        return RecommendationKind::None;   // explicit "grind unchanged"

    if (!GrinderAliases::looksLikeSetting(s)) {
        qWarning() << "computeAdherence: grinderSetting is prose, not a setting —"
                   << "cannot score it:" << s;
        return RecommendationKind::Unscoreable;
    }

    outRecommended = s;
    return RecommendationKind::Scoreable;
}

// Classify a structuredNext field that must be a POSITIVE NUMBER (rpm, doseG).
//
// Same hazard as grinderSetting and it must be handled the same way, because
// the first version of this guard fixed only grinderSetting and left rpm
// failing OPEN: QJsonValue::toInt() returns 0 for a JSON string ("rpm":
// "1400"), for null, and for a model that writes 0 meaning "unchanged" (the
// schema says OMIT when unchanged, and models violate the schema — that is
// why any of this exists). rpmMatches() then treated <= 0 as a free match, so
// a malformed rpm scored "followed": "the experiment ran". Fail closed to
// Unscoreable instead.
RecommendationKind classifyPositiveNumberField(const QJsonObject& sn, const char* key,
                                               double& outValue)
{
    outValue = 0.0;
    const QString k = QString::fromLatin1(key);
    if (!sn.contains(k))
        return RecommendationKind::None;

    const QJsonValue v = sn.value(k);
    if (!v.isDouble()) {
        qWarning() << "computeAdherence:" << key << "is not a JSON number (type"
                   << int(v.type()) << ") — cannot score it";
        return RecommendationKind::Unscoreable;
    }
    const double d = v.toDouble();
    if (d <= 0.0) {
        qWarning() << "computeAdherence:" << key << "is" << d
                   << "— not a usable recommendation, cannot score it";
        return RecommendationKind::Unscoreable;
    }
    outValue = d;
    return RecommendationKind::Scoreable;
}

// Classify a structuredNext field that must be a NON-EMPTY STRING
// (profileTitle).
RecommendationKind classifyStringField(const QJsonObject& sn, const char* key,
                                       QString& outValue)
{
    outValue.clear();
    const QString k = QString::fromLatin1(key);
    if (!sn.contains(k))
        return RecommendationKind::None;

    const QJsonValue v = sn.value(k);
    if (!v.isString()) {
        qWarning() << "computeAdherence:" << key << "is not a JSON string (type"
                   << int(v.type()) << ") — cannot score it";
        return RecommendationKind::Unscoreable;
    }
    const QString s = v.toString().trimmed();
    if (s.isEmpty())
        return RecommendationKind::None;   // explicit "unchanged"
    outValue = s;
    return RecommendationKind::Scoreable;
}

// Are two recorded dial settings the same position? Accepts every form
// looksLikeSetting() admits, so notation cannot decide it: "1 + 4" is "1+4",
// and "23.5 1400rpm" is "23.5".
//
// Unknown compares as "same". Only a POSITIVE difference counts as a change,
// because the one caller that asks this question (setupChangedFromPrior)
// downgrades a verdict on the answer, and a blank grinderSetting — common on
// older shots — is absence of evidence, not evidence the user regrinded.
//
// grinderMatches() below asks a related but different question and keeps its
// own guards: it compares against what was RECOMMENDED, not merely whether
// two shots sit on the same setting.
bool sameGrinderSetting(const QString& aRaw, const QString& bRaw)
{
    const QString a = aRaw.trimmed();
    const QString b = bRaw.trimmed();
    if (a.isEmpty() || b.isEmpty()) return true;   // unknown — no change proven
    if (a == b) return true;

    // Compound notation compares normalized, so spacing cannot decide it. If
    // either side is compound they both must be, or they are not comparable
    // and we decline to call it a change.
    const QString aKey = GrinderAliases::compoundKey(a);
    const QString bKey = GrinderAliases::compoundKey(b);
    if (!aKey.isEmpty() || !bKey.isEmpty())
        return aKey.isEmpty() || bKey.isEmpty() || aKey == bKey;

    const std::optional<double> an = GrinderAliases::leadingDialNumber(a);
    const std::optional<double> bn = GrinderAliases::leadingDialNumber(b);
    if (!an || !bn) {
        // Lettered dials ("3F" vs "3C") parse as no number — leadingDialNumber
        // only knows numRe and compoundRe, not looksLikeSetting()'s third
        // shape. They are still two REAL settings that plainly differ, so
        // compare them the way grinderMatches() does: exact string equality.
        //
        // Only when both sides are recognisable settings. Prose and free text
        // fail looksLikeSetting() and keep the conservative "unknown is not a
        // change" answer, which is what the spec's both-shots-record-it rule
        // requires. Returning "same" for everything incomparable — as this did
        // first — made a lettered regrind invisible and scored it "followed",
        // the exact defect this function exists to catch.
        if (GrinderAliases::looksLikeSetting(a) && GrinderAliases::looksLikeSetting(b))
            return false;   // trimmed, and a != b by the early-out above
        return true;
    }
    return std::abs(*an - *bn) <= kGrinderStepTolerance + 1e-9;
}

// Match `actual` against `recommended` for adherence purposes. Also
// guard against "the user kept the prior shot's setting" registering
// as followed when the recommendation happens to be within tolerance
// of the prior — a no-movement shot is NOT "followed" even when the
// recommendation was close to where the user already was.
//
// Comparison goes through GrinderAliases so it accepts every form
// looksLikeSetting() admits. It previously used a bare QString::toDouble(),
// which rejects trailing text — so a recommended "23.5" against a recorded
// "23.5 1400rpm" (the variable-RPM annotation cohort) and a recommended
// "1 + 4" against a recorded "1+4" both scored "ignored" despite the user
// having dialled exactly what was asked. Widening the gate without widening
// the comparator is what made that possible.
bool grinderMatches(const QString& recommendedRaw, const QString& actualRaw,
                     const QString& priorRaw)
{
    const QString recommended = recommendedRaw.trimmed();
    const QString actual = actualRaw.trimmed();
    const QString prior = priorRaw.trimmed();

    if (recommended.isEmpty()) return true;
    if (recommended == actual && recommended != prior) return true;

    // Compound notation: compare normalized, so spacing cannot decide it.
    const QString rKey = GrinderAliases::compoundKey(recommended);
    if (!rKey.isEmpty()) {
        const QString aKey = GrinderAliases::compoundKey(actual);
        if (aKey.isEmpty()) return false;
        if (rKey != aKey) return false;
        return rKey != GrinderAliases::compoundKey(prior);   // no-movement guard
    }

    const std::optional<double> r = GrinderAliases::leadingDialNumber(recommended);
    const std::optional<double> a = GrinderAliases::leadingDialNumber(actual);
    const std::optional<double> p = GrinderAliases::leadingDialNumber(prior);
    if (!r || !a) return false;
    if (std::abs(*r - *a) > kGrinderStepTolerance + 1e-9) return false;
    // If the user didn't move from the prior setting, this is NOT
    // adherence — they ignored the recommendation, even though the
    // prior happens to be close to the recommended value.
    if (p && std::abs(*a - *p) <= kGrinderStepTolerance + 1e-9 && !qFuzzyCompare(*a, *r))
        return false;
    return true;
}

// RPM adherence: matched when the actual RPM lands within tolerance of the
// recommended RPM AND the user actually moved from the prior RPM (same
// no-movement guard as grinderMatches). RPM dials are coarse, so a ±25 RPM
// window absorbs rounding without rewarding a shot that ignored the advice.
//
// `recommended` is guaranteed positive by classifyPositiveNumberField(); the
// guard below is defence in depth, and returns FALSE rather than the free
// match it used to give.
constexpr int kRpmTolerance = 25;
bool rpmMatches(int recommended, int actual, int prior)
{
    if (recommended <= 0) return false;
    if (actual <= 0) return false;
    if (std::abs(recommended - actual) > kRpmTolerance) return false;
    if (prior > 0 && std::abs(actual - prior) <= kRpmTolerance && actual != recommended)
        return false;
    return true;
}

// Did the user change the setup between the prior shot and this one?
//
// Only for the ranges-only case, where nothing was recommended and the
// implicit instruction is "repeat this shot, here is what I predict". Any
// deliberate change means the predicted repeat did not happen.
//
// Tolerances are the same ones adherence scoring uses elsewhere, so scale
// noise and RPM rounding do not read as a decision. Every field requires
// evidence on BOTH sides before it can report a change: an unrecorded dose or
// an unrecorded RPM is missing data, and treating it as a change would flip
// long-settled turns to "ignored" on nothing.
//
// The PROFILE is deliberately not compared. buildRecentAdviceBlock only pairs
// shots that share `profile_kb_id` — the prior is skipped when its kb id does
// not match, and the follow-up is selected `WHERE profile_kb_id = ?` — so both
// shots are always on the same profile by construction. Comparing the stored
// `profileName` on top of that cannot detect a profile switch (there is none
// to detect); it can only fire when the snapshot titles differ for the SAME
// profile, which means the user renamed it between the two shots. A rename is
// not a setup change, so that check was false-positive-only and is gone.
bool setupChangedFromPrior(const ShotProjection& prior, const ShotProjection& actual)
{
    if (!sameGrinderSetting(prior.grinderSetting, actual.grinderSetting))
        return true;
    if (prior.rpm > 0 && actual.rpm > 0
        && std::abs(actual.rpm - prior.rpm) > kRpmTolerance)
        return true;
    if (prior.doseWeightG > 0.0 && actual.doseWeightG > 0.0
        && std::abs(actual.doseWeightG - prior.doseWeightG) > kDoseToleranceG + 1e-9)
        return true;
    return false;
}

QString computeAdherence(const QJsonObject& sn, const ShotProjection& actual,
                          const ShotProjection& prior)
{
    bool anyRecommendation = false;
    bool anyUnscoreable = false;
    int matched = 0;
    int total = 0;

    // EVERY field goes through a classify step, and the outcome is folded in
    // here rather than at each call site. That uniformity is the point: the
    // first version of this guard classified grinderSetting only, and rpm went
    // on failing open — the same bug, one field over. A fifth field added
    // below without a classify step should look obviously wrong.
    const auto fold = [&](RecommendationKind kind, auto&& scoreIt) {
        switch (kind) {
        case RecommendationKind::None:
            break;
        case RecommendationKind::Unscoreable:
            anyRecommendation = true;   // something WAS asked for
            anyUnscoreable = true;
            break;
        case RecommendationKind::Scoreable:
            anyRecommendation = true;
            ++total;
            if (scoreIt()) ++matched;
            break;
        }
    };

    QString recommendedGrind;
    fold(classifyGrinderRecommendation(sn, recommendedGrind), [&] {
        return grinderMatches(recommendedGrind, actual.grinderSetting, prior.grinderSetting);
    });

    double recommendedRpm = 0.0;
    fold(classifyPositiveNumberField(sn, "rpm", recommendedRpm), [&] {
        return rpmMatches(static_cast<int>(recommendedRpm),
                          static_cast<int>(actual.rpm), static_cast<int>(prior.rpm));
    });

    double recommendedDose = 0.0;
    fold(classifyPositiveNumberField(sn, "doseG", recommendedDose), [&] {
        const bool inTolerance =
            std::abs(recommendedDose - actual.doseWeightG) <= kDoseToleranceG + 1e-9;
        // Same no-movement guard as grinderMatches.
        const bool moved =
            std::abs(actual.doseWeightG - prior.doseWeightG) > kDoseToleranceG + 1e-9
            || std::abs(recommendedDose - prior.doseWeightG) > kDoseToleranceG + 1e-9;
        return inTolerance && moved;
    });

    QString recommendedProfile;
    fold(classifyStringField(sn, "profileTitle", recommendedProfile), [&] {
        // ShotProjection stores profile_name (the title); structuredNext
        // recommends a profileTitle so both ends use the same identifier.
        return recommendedProfile == actual.profileName
            && recommendedProfile != prior.profileName;
    });
    if (!anyRecommendation) {
        // Ranges-only: no parameter changes were requested, so the implicit
        // instruction is "run this again, here is the range I expect". That
        // IS an experiment, and "followed" tells the model it ran.
        //
        // So a repeat on the same setup is "followed", but a shot the user
        // regrinded or redosed is "ignored" — the prediction was made about a
        // shot that never happened. Returning "followed" there made the model
        // REVISE DIRECTION ("the experiment ran and failed", per the prompt)
        // on an outcome its prediction never covered. Same false-"followed"
        // class the unscoreable guard below prevents; this was the one path
        // still exempt from it.
        if (setupChangedFromPrior(prior, actual))
            return QStringLiteral("ignored");
        return QStringLiteral("followed");
    }
    // Something was recommended in a form we cannot check, so we cannot claim
    // the experiment ran. This must NOT fall through to the ranges-only
    // "followed" above: the system prompt reads "followed" as "the experiment
    // ran" and tells the model to revise direction or commit harder on that
    // basis, when in fact the user may have changed nothing. "unclear" is the
    // conservative verdict and has its own instruction in the prompt.
    if (anyUnscoreable) return QStringLiteral("unclear");
    if (matched == total) return QStringLiteral("followed");
    if (matched == 0) return QStringLiteral("ignored");
    return QStringLiteral("partial");
}

bool inRange(double value, const QJsonArray& range)
{
    if (range.size() != 2) return false;
    const double low = range.at(0).toDouble();
    const double high = range.at(1).toDouble();
    return value >= low - 1e-9 && value <= high + 1e-9;
}

QJsonObject computeOutcomeInPredictedRange(const QJsonObject& sn,
                                            const ShotProjection& actual)
{
    QJsonObject out;
    out["duration"] = inRange(actual.durationSec,
        sn.value("expectedDurationSec").toArray());

    // Average flow during pour, in ml/s. ShotProjection doesn't carry a
    // peak/main flow rate field — computing one would require parsing
    // the samples blob, which is too expensive for an attribution path.
    // The model's expectedFlowMlPerSec is realistically targeting average
    // pour flow; use yield/duration as a defensible proxy.
    const double avgFlow = actual.durationSec > 0
        ? (actual.finalWeightG / actual.durationSec)
        : 0.0;
    out["flow"] = inRange(avgFlow,
        sn.value("expectedFlowMlPerSec").toArray());

    // Pressure: only emit when expectedPeakPressureBar was on the prior
    // turn AND we have peak-pressure data for the actual shot. The latter
    // is currently not on ShotProjection, so we omit `pressure` for now —
    // the spec was tightened to OPTIONAL precisely so this expensive
    // path can be filled in by a future change.
    return out;
}

// Build a one-sentence summary derived from a structuredNext block, used
// when the model omitted `reasoning` for some reason (older saved
// conversations, off-spec providers).
QString synthesizeRecommendationSummary(const QJsonObject& sn)
{
    const StructuredNextSummary s = summarizeStructuredNext(sn);
    QString head = s.predictedParts.isEmpty()
        ? QStringLiteral("Hold settings")
        : QStringLiteral("Try ") + s.predictedParts.join(QStringLiteral(", "));
    // Preserve the original both-or-nothing gate: only state an "expect"
    // window when BOTH duration and flow are present together, not from a
    // single asymmetric range — summarizeStructuredNext's expectedParts is
    // per-field independent (correct for the fuller recentAdvice rendering
    // this now shares with), so gate on the raw fields here instead of on
    // s.expectedParts to keep this one-line fallback's wording unchanged.
    const bool hasDuration = sn.contains(QStringLiteral("expectedDurationSec"));
    const bool hasFlow = sn.contains(QStringLiteral("expectedFlowMlPerSec"));
    if (hasDuration && hasFlow && s.expectedParts.size() >= 2)
        head += QStringLiteral("; expect ") + s.expectedParts.mid(0, 2).join(QStringLiteral(", "));
    return head;
}

} // namespace

QJsonArray buildRecentAdviceBlock(QSqlDatabase& db,
                                  const RecentAdviceInputs& in)
{
    QJsonArray out;
    if (in.turns.isEmpty() || in.currentProfileKbId.isEmpty()) return out;

    int turnsAgo = 0;  // 1-indexed; only incremented when a turn qualifies (spec).
    for (const AIConversation::HistoricalAssistantTurn& turn : in.turns) {
        if (turn.shotId == 0) continue;
        if (turn.structuredNext.isEmpty()) continue;

        // 1. Look up prior turn's shot's profile + timestamp + equipment bucket.
        QSqlQuery q(db);
        q.prepare("SELECT profile_kb_id, timestamp, COALESCE(equipment_id, 0) "
                  "FROM shots WHERE id = ?");
        q.addBindValue(static_cast<qint64>(turn.shotId));
        if (!q.exec ()) {
            qWarning() << "buildRecentAdviceBlock: prior-shot lookup failed:"
                       << q.lastError().text() << "id=" << turn.shotId;
            continue;
        }
        if (!q.next()) continue;  // shot deleted from history; skip

        const QString priorKbId = q.value(0).toString();
        const qint64 priorTs = q.value(1).toLongLong();
        const qint64 priorBucket = q.value(2).toLongLong();
        if (priorKbId != in.currentProfileKbId) continue;  // cross-profile filter
        if (priorTs <= 0) continue;

        // 2. Find the next shot postdating the prior turn's shot on the
        // same profile AND the same equipment package, excluding the current
        // shot under analysis. Without the equipment match, a user who
        // followed the advice on one basket and then pulled a shot on the
        // other has the OTHER basket's shot scored as their response: the
        // adherence line then reports a grind move the user never made,
        // and the model is told its own advice was ignored or overshot.
        QSqlQuery nextQ(db);
        nextQ.prepare(
            "SELECT id FROM shots "
            "WHERE profile_kb_id = ? AND timestamp > ? AND id != ? "
            "  AND " + ShotHistoryStorage::equipmentBucketSql() + " "
            "ORDER BY timestamp ASC LIMIT 1");
        nextQ.addBindValue(in.currentProfileKbId);
        nextQ.addBindValue(priorTs);
        nextQ.addBindValue(static_cast<qint64>(in.currentShotId));
        nextQ.addBindValue(priorBucket);
        if (!nextQ.exec ()) {
            qWarning() << "buildRecentAdviceBlock: follow-up shot lookup failed:"
                       << nextQ.lastError().text();
            continue;
        }
        if (!nextQ.next()) continue;  // user hasn't pulled a follow-up yet

        const qint64 nextId = nextQ.value(0).toLongLong();
        ShotRecord nextRec = ShotHistoryStorage::loadShotRecordStatic(db, nextId);
        const ShotProjection actual = ShotHistoryStorage::convertShotRecord(nextRec);
        if (!actual.isValid()) continue;

        // Load the prior turn's shot too — adherence uses it to detect
        // "the user didn't move" cases where the recommendation happens
        // to be within tolerance of where the user already was.
        ShotRecord priorRec = ShotHistoryStorage::loadShotRecordStatic(db, turn.shotId);
        const ShotProjection prior = ShotHistoryStorage::convertShotRecord(priorRec);

        ++turnsAgo;  // turn qualifies — claim its slot.

        QJsonObject userResponse;
        userResponse["actualNextShotId"] = static_cast<double>(actual.id);
        userResponse["grinderSetting"] = actual.grinderSetting;
        if (actual.rpm > 0)
            userResponse["rpm"] = actual.rpm;  // RPM half of what the user actually did
        userResponse["doseG"] = actual.doseWeightG;
        userResponse["adherence"] = computeAdherence(turn.structuredNext, actual, prior);
        if (actual.enjoyment0to100 > 0)
            userResponse["outcomeRating0to100"] = actual.enjoyment0to100;
        if (!actual.espressoNotes.isEmpty())
            userResponse["outcomeNotes"] = actual.espressoNotes;
        userResponse["outcomeInPredictedRange"] =
            computeOutcomeInPredictedRange(turn.structuredNext, actual);

        const QString reasoning = turn.structuredNext.value("reasoning").toString();
        const QString recommendation = !reasoning.isEmpty()
            ? reasoning
            : synthesizeRecommendationSummary(turn.structuredNext);

        QJsonObject entry;
        entry["turnsAgo"] = turnsAgo;
        entry["recommendation"] = recommendation;
        entry["structuredNext"] = turn.structuredNext;
        entry["userResponse"] = userResponse;
        out.append(entry);
    }
    return out;
}

namespace {

static double computeMedian(QList<double>& values)
{
    if (values.isEmpty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort(values.begin(), values.end());
    const qsizetype n = values.size();
    if (n % 2 == 1) return values[n / 2];
    return (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

// Phase-1 grinder-calibration constants. Provenance and tuning are in the
// openspec change `fix-grinder-calibration-cross-profile` (design.md
// Open-Questions resolution); the offline harness is tools/calib_analysis.py.
// `kCalibCap` is the only literature-backed constant (Al-Shemmeri/Gagne;
// docs/kb_sources/UNIVERSAL_GRIND_SETTING.md) and alone defeats #1223.
// `kCalibConversionKey` is NEVER hardcoded — slope is a per-grinder runtime
// output; only the *gates* are constants.
constexpr double    kCalibMinPairSpanUgs    = 0.75;  // ΔUGS floor: tiny denominators amplify rounding noise
constexpr qsizetype kCalibMinEndpointN      = 2;     // a 1-shot "median" is one noisy point (review S3)
constexpr qsizetype kCalibMinValidatedPairs = 3;     // below this → directional only
constexpr double kCalibMaxSpreadRatio   = 0.6;   // dimensionless: IQR ≤ ratio·|key| (grinder-portable, D1a)
constexpr double kCalibCap              = 1.5;   // UGS beyond validated range before going directional
constexpr qint64 kCalibUndatedBatchDays = 90;    // single-linkage gap for undated roast batches (D1b)

} // unnamed namespace (calibration helpers)

QJsonObject buildGrinderCalibrationBlock(QSqlDatabase& db,
                                         const QString& grinderModel,
                                         const QString& grinderBurrs,
                                         const QString& beverageType,
                                         qint64 resolvedShotId)
{
    // Rewritten for openspec `fix-grinder-calibration-cross-profile`
    // (issue #1223). The old pooled-all-coffee two-anchor slope produced
    // wrong-signed/out-of-range numbers presented as fact. New model:
    //   grind(profile, coffeeBatch) ≈ batchBaseline + UGS·conversionKey
    // conversionKey is mined from WITHIN-BATCH paired slopes (cancels the
    // dominant per-batch baseline), gated dimensionlessly, anchored on a
    // recent dialed-in shot of the CURRENT roast batch, and never
    // extrapolated past a hard UGS cap. No usable signal → directional
    // (finer/coarser from KB ordering only), never a fabricated number.
    if (grinderModel.isEmpty()) {
        qDebug() << "buildGrinderCalibrationBlock: skipped — grinderModel empty";
        return QJsonObject();
    }
    const QString bev = beverageType.trimmed().toLower();
    if (bev == QStringLiteral("filter") || bev == QStringLiteral("pourover")) {
        qDebug() << "buildGrinderCalibrationBlock: skipped — beverageType is" << beverageType;
        return QJsonObject();
    }

    // Resolved shot supplies the current roast batch (the intercept the
    // numbers anchor on) and the current profile's UGS (the reference for
    // directional finer/coarser). Internal load — no signature change, one
    // extra indexed read on the caller's background thread.
    const ShotRecord curRec = ShotHistoryStorage::loadShotRecordStatic(db, resolvedShotId);
    const ShotProjection cur = ShotHistoryStorage::convertShotRecord(curRec);
    if (!cur.isValid()) {
        qDebug() << "buildGrinderCalibrationBlock: resolved shot invalid → empty";
        return QJsonObject();
    }

    // Per-grinder notation: how to parse `grinder_setting` into a linear
    // scalar and how to render a recommended setting back. Unknown
    // grinders fall back to a default entry (NumericWithSuffix), which
    // accepts plain "25" and "8.5" and tolerates ignorable trailing text
    // like "24 1400rpm" or "30 clicks". Compound (Eureka Mignon family,
    // 1Zpresso) round-trips as `a+b`.
    const GrinderAliases::GrinderEntry* gEntryPtr =
        GrinderAliases::findEntryByAlias(grinderModel);
    static const GrinderAliases::GrinderEntry kDefaultGrinder{};
    const GrinderAliases::GrinderEntry& gEntry =
        gEntryPtr ? *gEntryPtr : kDefaultGrinder;

    auto normEq = [](const QString& a, const QString& b) {
        return QString::compare(a.trimmed(), b.trimmed(), Qt::CaseInsensitive) == 0;
    };
    auto isDated = [](const QString& rd) {
        const QString t = rd.trimmed();
        return !t.isEmpty() && t != QStringLiteral("--");
    };
    // Bean identity must be non-empty to assert a roast batch. With no
    // bean metadata, two shots cannot be proven to be the same coffee, so
    // pooling them re-creates the cross-batch baseline confound (#1223)
    // this block exists to remove. A row/shot with empty bean is
    // "batch-unknowable": excluded from within-batch pairing and from the
    // current-batch anchor → the block degrades to directional, the
    // correct honest outcome (review #1236).
    auto beanResolved = [](const QString& brand, const QString& type) {
        return !brand.trimmed().isEmpty() || !type.trimmed().isEmpty();
    };
    const bool curBeanResolved = beanResolved(cur.beanBrand, cur.beanType);
    const QString curBean = cur.beanBrand + QStringLiteral(" / ") + cur.beanType;
    const bool curDated = isDated(cur.roastDate);
    const qint64 batchGapSec = kCalibUndatedBatchDays * 24 * 3600;

    // Resolve the current profile's UGS for directional reference (D5a).
    QString curKbId = ShotSummarizer::resolveKbId(cur.profileKbId);
    if (curKbId.isEmpty())
        curKbId = ShotSummarizer::computeProfileKbId(cur.profileName);
    const double curUgs = curKbId.isEmpty()
        ? std::numeric_limits<double>::quiet_NaN()
        : ShotSummarizer::ugsForKbId(curKbId);
    const bool curUgsPlaced = !std::isnan(curUgs);

    // Dialed-in qualification: ≥15g + no quality badge AND (rated ≥50 OR
    // landed within 10% of stop-at-weight target OR has a refractometer
    // reading). Replaces the old "≥5g, no-badge-only" filter that admitted
    // undershoot/aborted experiments and corrupted the medians.
    // `cur` came from loadShotRecordStatic and passed the isValid() early return
    // at the head of this function's calibration setup, and carries this shot's
    // package. An earlier draft re-queried the bucket here and FAILED CLOSED
    // when the second read came back empty — a branch that needed the one-column
    // lookup to fail on a connection that had just succeeded a whole
    // loadShotRecordStatic on the same row, i.e. unreachable without fault
    // injection. Taking it from `cur` removes the branch rather than guarding
    // it, and makes the predicate unconditional, which is a stronger guarantee
    // than failing closed: there is no longer a path on which it is skipped.
    // 0 = unpackaged, which is a real bucket that matches other unpackaged shots.
    const qint64 calBucket = cur.equipmentId;

    QSqlQuery q(db);
    // Scoped to the resolved shot's own equipment PACKAGE, not to every package
    // sharing a grinder model + burrs. The endpoint medians below are pooled by
    // (batch, kbId) BEFORE any pair is formed, so pooling packages corrupts the
    // ENDPOINT — not merely a pair that straddles two baskets — and the anchor
    // comes from these same rows. This block publishes a number the user dials
    // in, so it degrades to directional rather than widening to other gear.
    QString calSql = QStringLiteral(
            "SELECT profile_kb_id, profile_name, grinder_setting, timestamp, "
            "       bean_brand, bean_type, roast_date, final_weight, "
            "       COALESCE(enjoyment,0), COALESCE(drink_tds,0), "
            "       COALESCE(yield_override, 0), "
            "       json_extract(profile_json,'$.target_weight'), "
            "       COALESCE(rpm, 0) "
            "FROM shots WHERE "
            "      (beverage_type IS NULL OR beverage_type = '' OR LOWER(beverage_type) = 'espresso') "
            "  AND COALESCE(final_weight, 0) >= 15 "
            "  AND COALESCE(grind_issue_detected, 0) = 0 "
            "  AND COALESCE(channeling_detected, 0) = 0 "
            "  AND COALESCE(pour_truncated_detected, 0) = 0 "
            "  AND COALESCE(skip_first_frame_detected, 0) = 0 ");
    calSql += QStringLiteral(" AND ") + ShotHistoryStorage::equipmentBucketSql()
            + QStringLiteral(" ORDER BY timestamp DESC");
    q.prepare(calSql);
    q.addBindValue(calBucket);
    if (!q.exec ()) {
        qWarning() << "buildGrinderCalibrationBlock: history query failed:" << q.lastError().text();
        return QJsonObject();
    }

    struct CalRow {
        qint64 ts = 0;
        QString kbId;
        double ugs = std::numeric_limits<double>::quiet_NaN();
        double setting = 0.0;
        int rpm = 0;            // motor RPM (variable-RPM grinders); 0 = none
        QString bean;
        QString roast;
        bool dated = false;
        bool beanOk = false;    // bean identity non-empty (batch-knowable)
        QString batch;          // assigned below
        bool currentBatch = false;
    };
    QList<CalRow> rows;

    while (q.next()) {
        const QString rawKbId   = q.value(0).toString().trimmed();
        const QString profName  = q.value(1).toString().trimmed();
        const QString setStr    = q.value(2).toString().trimmed();
        const qint64  ts        = q.value(3).toLongLong();
        const QString bBrand    = q.value(4).toString().trimmed();
        const QString bType     = q.value(5).toString().trimmed();
        const QString roast     = q.value(6).toString().trimmed();
        const double  finalW    = q.value(7).toDouble();
        const int     enj       = q.value(8).toInt();
        const double  tds       = q.value(9).toDouble();
        // Stop-at-weight target with the SAME precedence as
        // effectiveTargetWeightG(): the stored yield_override column wins
        // (native Decenza SAW shots persist it there, NOT in profile_json),
        // profile_json target_weight is only the fallback for imported
        // shots. Reading json_extract alone dropped the common SAW dial-in
        // cohort (no rating, no refractometer) — review #1236.
        const double  yieldOv   = q.value(10).toDouble();
        const QVariant twv      = q.value(11);
        const double  jsonTw    = twv.isNull() ? 0.0 : twv.toString().toDouble();
        const double  targetW   = yieldOv > 0.0 ? yieldOv : jsonTw;
        const int     rpmVal    = q.value(12).toInt();

        // Dialed-in gate.
        const bool ratedOk  = enj >= 50;
        const bool onTarget = targetW > 0.0 && std::abs(finalW - targetW) <= 0.10 * targetW;
        const bool hasTds   = tds > 0.0;
        if (!ratedOk && !onTarget && !hasTds) continue;

        // Notation-aware parse: numeric (incl. "24 1400rpm" suffix on
        // variable-RPM grinders) or compound "a+b" for Eureka/1Zpresso.
        // Truly non-numeric notation (letters, "1 + 4" with spaces, etc.)
        // still excluded — those rows are served by the directional path.
        const auto setOpt = GrinderAliases::parseGrinderSetting(gEntry, setStr);
        if (!setOpt) continue;
        const double setVal = *setOpt;

        QString id = ShotSummarizer::resolveKbId(rawKbId);
        if (id.isEmpty())
            id = ShotSummarizer::computeProfileKbId(profName);

        CalRow r;
        r.ts = ts;
        r.kbId = id;
        r.ugs = id.isEmpty() ? std::numeric_limits<double>::quiet_NaN()
                             : ShotSummarizer::ugsForKbId(id);
        r.setting = setVal;
        r.rpm = rpmVal;
        r.bean = bBrand + QStringLiteral(" / ") + bType;
        r.roast = roast;
        r.dated = isDated(roast);
        r.beanOk = beanResolved(bBrand, bType);
        rows.append(r);
    }

    if (rows.isEmpty()) {
        qDebug() << "buildGrinderCalibrationBlock: no dialed-in shots in equipment package"
                 << calBucket << "(grinder" << grinderModel << grinderBurrs << ")";
        return QJsonObject();
    }

    // Batch identity (D1b). Dated: bean + roastDate. Undated: per-bean
    // single-linkage clustering with a 90-day gap (sliding window, NOT a
    // fixed calendar bucket — review S5b).
    {
        QHash<QString, QList<qsizetype>> undatedByBean;  // bean → row indices
        for (qsizetype i = 0; i < rows.size(); ++i) {
            CalRow& r = rows[i];
            if (!r.beanOk) continue;  // batch-unknowable → never paired/anchored
            if (r.dated) {
                r.batch = r.bean + QStringLiteral(" @ ") + r.roast;
            } else {
                undatedByBean[r.bean].append(i);
            }
        }
        for (auto it = undatedByBean.begin(); it != undatedByBean.end(); ++it) {
            QList<qsizetype>& idx = it.value();
            std::sort(idx.begin(), idx.end(),
                      [&](qsizetype a, qsizetype b){ return rows[a].ts < rows[b].ts; });
            int cluster = 0;
            qint64 prevTs = -1;
            for (qsizetype j : idx) {
                if (prevTs >= 0 && rows[j].ts - prevTs > batchGapSec)
                    ++cluster;
                rows[j].batch = it.key()
                    + QStringLiteral(" ~undated#") + QString::number(cluster);
                prevTs = rows[j].ts;
            }
        }
    }

    // Current-batch membership for the anchor + history medians: same bean,
    // and (both dated → same roastDate) else within the 90-day window of
    // the current shot. Matches the spec's "within 90 days of one another".
    for (CalRow& r : rows) {
        if (!curBeanResolved || !r.beanOk) continue;  // batch-unknowable
        if (!normEq(r.bean, curBean)) continue;
        if (curDated && r.dated)
            r.currentBatch = normEq(r.roast, cur.roastDate);
        else
            r.currentBatch = qAbs(r.ts - cur.timestamp) <= batchGapSec;
    }

    // ---- Within-batch paired conversion key (D1) ----
    // Per (batch, kbId): median setting + sample count, UGS-placed only.
    struct PB { QList<double> settings; double ugs = 0.0; };
    QHash<QString, QHash<QString, PB>> byBatch;  // batch → kbId → PB
    for (const CalRow& r : rows) {
        if (std::isnan(r.ugs) || !r.beanOk || r.batch.isEmpty()) continue;
        PB& pb = byBatch[r.batch][r.kbId];
        pb.settings.append(r.setting);
        pb.ugs = r.ugs;
    }

    QList<double> slopes;
    double validLo =  std::numeric_limits<double>::infinity();
    double validHi = -std::numeric_limits<double>::infinity();
    for (auto bit = byBatch.constBegin(); bit != byBatch.constEnd(); ++bit) {
        const QHash<QString, PB>& profs = bit.value();
        QList<QString> ids = profs.keys();
        for (qsizetype i = 0; i < ids.size(); ++i) {
            for (qsizetype j = i + 1; j < ids.size(); ++j) {
                PB a = profs[ids[i]];
                PB b = profs[ids[j]];
                if (a.settings.size() < kCalibMinEndpointN
                    || b.settings.size() < kCalibMinEndpointN) continue;
                const double dUgs = b.ugs - a.ugs;
                if (std::abs(dUgs) < kCalibMinPairSpanUgs) continue;
                const double mA = computeMedian(a.settings);
                const double mB = computeMedian(b.settings);
                slopes.append((mB - mA) / dUgs);
                validLo = std::min({validLo, a.ugs, b.ugs});
                validHi = std::max({validHi, a.ugs, b.ugs});
            }
        }
    }

    double conversionKey = std::numeric_limits<double>::quiet_NaN();
    bool keyValid = false;
    if (!slopes.isEmpty()) {
        QList<double> s = slopes;
        conversionKey = computeMedian(s);  // Theil–Sen (median of pairwise)
        std::sort(s.begin(), s.end());
        // Conservative spread: floor-index quartiles. At small n (n≤4,
        // incl. the minimum kCalibMinValidatedPairs=3) q1=s[0], q3=s[max]
        // so this is the FULL RANGE, not a textbook interquartile range —
        // intentionally tighter (always fails safe → more directional,
        // never a fabricated number). kCalibMaxSpreadRatio was tuned
        // against this exact estimator in tools/calib_analysis.py, so the
        // two must change together; do not "fix" to true IQR in isolation.
        const double q1 = s[s.size() / 4];
        const double q3 = s[(3 * s.size()) / 4];
        const double spread = q3 - q1;
        // Dimensionless spread gate (D1a) — grinder-portable; an absolute
        // steps/UGS threshold is not (slope magnitude is grinder-specific).
        keyValid = s.size() >= kCalibMinValidatedPairs
                   && std::abs(conversionKey) > 1e-9
                   && spread <= kCalibMaxSpreadRatio * std::abs(conversionKey);
    }

    // ---- Per-current-batch anchor (intercept, D3) ----
    // Most recent dialed-in shot on the current roast batch on a
    // UGS-placed profile.
    bool haveAnchor = false;
    qint64 anchorTs = -1;
    double anchorSetting = 0.0, anchorUgs = 0.0;
    int anchorRpm = 0;
    QString anchorName;
    for (const CalRow& r : rows) {
        if (!r.currentBatch || std::isnan(r.ugs)) continue;
        if (r.ts > anchorTs) {
            anchorTs = r.ts;
            anchorSetting = r.setting;
            anchorRpm = r.rpm;
            anchorUgs = r.ugs;
            anchorName = ShotSummarizer::canonicalNameForKbId(r.kbId);
            haveAnchor = true;
        }
    }

    // Current-batch per-profile medians → history rgs (numbers only when
    // the block is publishing; directional emits no numbers at all).
    QMap<QString, double> currentBatchMedian;  // kbId → median
    {
        QHash<QString, QList<double>> acc;
        for (const CalRow& r : rows)
            if (r.currentBatch && !std::isnan(r.ugs))
                acc[r.kbId].append(r.setting);
        for (auto it = acc.begin(); it != acc.end(); ++it)
            currentBatchMedian[it.key()] = computeMedian(it.value());
    }

    const bool approximate = keyValid && haveAnchor;
    const QString confidence = approximate
        ? QStringLiteral("approximate") : QStringLiteral("directional");

    qDebug() << "buildGrinderCalibrationBlock:" << confidence
             << "pairs=" << slopes.size()
             << "key=" << conversionKey << "keyValid=" << keyValid
             << "anchor=" << haveAnchor
             << "validUGS=[" << validLo << "," << validHi << "]"
             << "curUgsPlaced=" << curUgsPlaced;

    // ---- Assemble profiles[] : one entry per KB profile with a UGS ----
    QList<ShotSummarizer::KbUgsEntry> entries = ShotSummarizer::allKbUgsEntries();
    std::sort(entries.begin(), entries.end(),
              [](const ShotSummarizer::KbUgsEntry& a,
                 const ShotSummarizer::KbUgsEntry& b){ return a.ugs < b.ugs; });

    QJsonArray profilesArr;
    for (const ShotSummarizer::KbUgsEntry& e : entries) {
        QJsonObject p;
        p["profileName"] = e.name;
        p["ugs"] = e.ugs;

        bool emittedNumber = false;
        if (approximate) {
            if (currentBatchMedian.contains(e.kbId)) {
                p["source"] = QStringLiteral("history");
                p["rgs"] = GrinderAliases::formatGrinderSetting(
                    gEntry, currentBatchMedian[e.kbId]);
                emittedNumber = true;
            } else if (e.ugs >= validLo - kCalibCap - 1e-9
                       && e.ugs <= validHi + kCalibCap + 1e-9) {
                p["source"] = QStringLiteral("derived");
                p["rgs"] = GrinderAliases::formatGrinderSetting(
                    gEntry, anchorSetting + (e.ugs - anchorUgs) * conversionKey);
                emittedNumber = true;
            }
        }
        if (!emittedNumber) {
            // Directional: anchor-free, KB-ordering-only, grinder-
            // convention-free (D5a). Reference is the CURRENT profile's
            // UGS, not an anchor (there may be none).
            p["source"] = QStringLiteral("directional");
            if (curUgsPlaced) {
                if (e.ugs > curUgs + 1e-9)
                    p["direction"] = QStringLiteral("coarser");
                else if (e.ugs < curUgs - 1e-9)
                    p["direction"] = QStringLiteral("finer");
                // equal → no direction (same grind position)
            }
            // current profile not UGS-placed → no direction; block-level
            // flag tells the renderer to say it can't order the two.
        }
        profilesArr.append(p);
    }

    // ---- Block ----
    QJsonObject block;
    block["grinderModel"] = grinderModel;
    block["confidence"] = confidence;
    block["currentProfileUgsPlaced"] = curUgsPlaced;
    QString usage = QStringLiteral(
        "UGS is a relative ordering of profiles by grind coarseness, not "
        "grinder clicks or a dial position. Numeric settings are valid only "
        "within calibratedUgsRange. For any profile with source "
        "\"directional\" give finer/coarser only and tell the user to pull a "
        "reference shot on that profile — never a number, never a click "
        "delta. Do not multiply a UGS distance by any factor of your own.");
    // Variable-RPM grinders: dial + RPM are two independent grind axes
    // and shots at the same dial / different RPM are NOT the same grind.
    // The parser tolerates the "<dial> <rpm>rpm" suffix but does NOT yet
    // split pairs by RPM (Phase-2 work), so any approximate `rgs` is
    // anchored at whatever RPM the user's recent dialed-in shot used.
    // Surface this caveat so the model qualifies numeric recommendations
    // and does NOT pool dial values across RPMs in its own reasoning.
    block["variableRpm"] = gEntry.variableRpm;
    if (gEntry.variableRpm && approximate) {
        usage += QStringLiteral(
            " This grinder has variable RPM: the recommended setting "
            "applies only at the SAME RPM as the user's recent dialed-in "
            "shot. State this caveat when quoting a number, and never "
            "treat shots at different RPM as the same grind.");
    }
    block["usageConstraint"] = usage;
    if (approximate) {
        block["conversionKey"] = std::round(conversionKey * 100.0) / 100.0;
        block["calibratedUgsRange"] = QJsonArray{ validLo, validHi };
        QJsonObject anchor;
        anchor["profileName"] = anchorName;
        anchor["ugs"] = anchorUgs;
        anchor["setting"] = GrinderAliases::formatGrinderSetting(
            gEntry, anchorSetting);
        // The anchor shot's actual RPM, so a variable-RPM recommendation can name
        // the concrete RPM the setting is anchored at rather than only the prose
        // "same RPM as your recent shot" caveat. Sparse: absent for non-RPM shots.
        if (anchorRpm > 0)
            anchor["rpm"] = anchorRpm;
        anchor["coffee"] = curBean;
        block["coffeeAnchor"] = anchor;
    }
    block["profiles"] = profilesArr;
    return block;
}


} // namespace DialingBlocks
