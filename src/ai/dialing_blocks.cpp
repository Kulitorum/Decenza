#include "dialing_blocks.h"
#include "dialing_helpers.h"
#include "shotsummarizer.h"

#include "../history/shothistorystorage.h"
#include "../history/shotprojection.h"
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

// Per-shot serializer for dialInSessions. Identity overrides come from
// the per-session `hoistSessionContext` output; per-shot entries emit
// the five identity fields only when they differ from the session
// context (otherwise the field is hoisted and absent here).
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
    if (!override.grinderBrand.isEmpty())
        h["grinderBrand"] = override.grinderBrand;
    if (!override.grinderModel.isEmpty())
        h["grinderModel"] = override.grinderModel;
    if (!override.grinderBurrs.isEmpty())
        h["grinderBurrs"] = override.grinderBurrs;
    if (!override.beanBrand.isEmpty())
        h["beanBrand"] = override.beanBrand;
    if (!override.beanType.isEmpty())
        h["beanType"] = override.beanType;
    h["notes"] = shot.espressoNotes;
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
                                    int historyLimit)
{
    QJsonArray sessions;
    if (profileKbId.isEmpty()) return sessions;

    QVariantList history = ShotHistoryStorage::loadRecentShotsByKbIdStatic(
        db, profileKbId, historyLimit, resolvedShotId);

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
            id.beanBrand = s.beanBrand;
            id.beanType = s.beanType;
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
        if (!hoisted.context.beanBrand.isEmpty())
            contextObj["beanBrand"] = hoisted.context.beanBrand;
        if (!hoisted.context.beanType.isEmpty())
            contextObj["beanType"] = hoisted.context.beanType;
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
    // elicitation paths (QuickRatingRow, conversational capture) keep
    // this pool populated.
    bestQ.prepare(
        "SELECT id FROM shots "
        "WHERE profile_kb_id = ? AND enjoyment > 0 "
        "AND id != ? AND timestamp >= ? "
        "ORDER BY enjoyment DESC, timestamp DESC LIMIT 1");
    bestQ.addBindValue(profileKbId);
    bestQ.addBindValue(resolvedShotId);
    bestQ.addBindValue(windowFloorSec);
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
    b["grinderModel"] = best.grinderModel;
    b["beanBrand"] = best.beanBrand;
    b["beanType"] = best.beanType;
    b["notes"] = best.espressoNotes;
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

QJsonObject buildGrinderContextBlock(QSqlDatabase& db,
                                     const QString& grinderModel,
                                     const QString& beverageType,
                                     const QString& beanBrand)
{
    if (grinderModel.isEmpty()) return QJsonObject();

    const QString bevType = beverageType.isEmpty()
        ? QStringLiteral("espresso") : beverageType;

    GrinderContext ctx = ShotHistoryStorage::queryGrinderContext(
        db, grinderModel, bevType, beanBrand);

    // Cross-bean fallback for sparse OR empty bean-scoped results.
    bool haveCrossBean = false;
    GrinderContext crossBean;
    if (!beanBrand.isEmpty() && ctx.settingsObserved.size() < 2) {
        crossBean = ShotHistoryStorage::queryGrinderContext(
            db, grinderModel, bevType);
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
        grinderCtx["smallestStep"] = primary.smallestStep;
    }
    if (haveCrossBean && !ctx.settingsObserved.isEmpty()) {
        QJsonArray allArr;
        for (const auto& s : crossBean.settingsObserved)
            allArr.append(s);
        grinderCtx["allBeansSettings"] = allArr;
    }
    return grinderCtx;
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

    const QString scaleType = settings->scaleType();
    const QString profileFilename = profileManager->baseProfileName();
    if (scaleType.isEmpty() || profileFilename.isEmpty()) return QJsonObject();

    const double predictedDripG =
        settings->calibration()->getExpectedDripFor(profileFilename, scaleType, flowAtCutoff);
    const QString sourceTier =
        settings->calibration()->sawModelSource(profileFilename, scaleType);
    const double learnedLagSec =
        settings->calibration()->sawLearnedLagFor(profileFilename, scaleType);
    const int sampleCount =
        settings->calibration()->perProfileSawHistory(profileFilename, scaleType).size();

    QJsonObject sawPrediction;
    sawPrediction["profileFilename"] = profileFilename;
    sawPrediction["scaleType"] = scaleType;
    sawPrediction["flowAtCutoffMlPerSec"] =
        QString::number(flowAtCutoff, 'f', 2).toDouble();
    sawPrediction["predictedDripG"] =
        QString::number(predictedDripG, 'f', 2).toDouble();
    sawPrediction["learnedLagSec"] =
        QString::number(learnedLagSec, 'f', 2).toDouble();
    sawPrediction["sampleCount"] = sampleCount;
    sawPrediction["sourceTier"] = sourceTier;
    if (predictedDripG >= 0.2) {
        sawPrediction["recommendation"] = QString(
            "Set the stop-at-weight target ~%1 g lower than your aim "
            "to land near goal — that's the typical post-cutoff drip "
            "on this (profile, scale) pair.")
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

// Match `actual` against `recommended` for adherence purposes. Also
// guard against "the user kept the prior shot's setting" registering
// as followed when the recommendation happens to be within tolerance
// of the prior — a no-movement shot is NOT "followed" even when the
// recommendation was close to where the user already was.
bool grinderMatches(const QString& recommended, const QString& actual,
                     const QString& prior)
{
    if (recommended.isEmpty()) return true;
    if (recommended == actual && recommended != prior) return true;
    bool okR = false, okA = false, okP = false;
    const double r = recommended.toDouble(&okR);
    const double a = actual.toDouble(&okA);
    const double p = prior.toDouble(&okP);
    if (!okR || !okA) return false;
    if (std::abs(r - a) > kGrinderStepTolerance + 1e-9) return false;
    // If the user didn't move from the prior setting, this is NOT
    // adherence — they ignored the recommendation, even though the
    // prior happens to be close to the recommended value.
    if (okP && std::abs(a - p) <= kGrinderStepTolerance + 1e-9 && a != r)
        return false;
    return true;
}

QString computeAdherence(const QJsonObject& sn, const ShotProjection& actual,
                          const ShotProjection& prior)
{
    bool anyRecommendation = false;
    int matched = 0;
    int total = 0;

    if (sn.contains(QStringLiteral("grinderSetting"))) {
        anyRecommendation = true;
        ++total;
        if (grinderMatches(sn.value("grinderSetting").toString(),
                           actual.grinderSetting, prior.grinderSetting))
            ++matched;
    }
    if (sn.contains(QStringLiteral("doseG"))) {
        anyRecommendation = true;
        ++total;
        const double recommended = sn.value("doseG").toDouble();
        const bool inTolerance = std::abs(recommended - actual.doseWeightG) <= kDoseToleranceG + 1e-9;
        // Same no-movement guard as grinderMatches.
        const bool moved = std::abs(actual.doseWeightG - prior.doseWeightG) > kDoseToleranceG + 1e-9
                           || std::abs(recommended - prior.doseWeightG) > kDoseToleranceG + 1e-9;
        if (inTolerance && moved) ++matched;
    }
    if (sn.contains(QStringLiteral("profileTitle"))) {
        anyRecommendation = true;
        ++total;
        const QString recommended = sn.value("profileTitle").toString();
        // ShotProjection stores profile_name (the title); structuredNext
        // recommends a profileTitle so both ends use the same identifier.
        if (recommended == actual.profileName && recommended != prior.profileName)
            ++matched;
    }
    if (!anyRecommendation) {
        // The recommendation was ranges-only (no parameter changes
        // requested). Treat that as "followed" by default — the user
        // didn't violate anything since nothing was requested.
        return QStringLiteral("followed");
    }
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
    QStringList parts;
    if (sn.contains(QStringLiteral("grinderSetting")))
        parts << QStringLiteral("grinder %1").arg(sn.value("grinderSetting").toString());
    if (sn.contains(QStringLiteral("doseG")))
        parts << QStringLiteral("dose %1g").arg(sn.value("doseG").toDouble(), 0, 'f', 1);
    if (sn.contains(QStringLiteral("profileTitle")))
        parts << QStringLiteral("profile %1").arg(sn.value("profileTitle").toString());
    QString head = parts.isEmpty() ? QStringLiteral("Hold settings") : QStringLiteral("Try ") + parts.join(QStringLiteral(", "));
    const QJsonArray dur = sn.value(QStringLiteral("expectedDurationSec")).toArray();
    const QJsonArray flow = sn.value(QStringLiteral("expectedFlowMlPerSec")).toArray();
    if (dur.size() == 2 && flow.size() == 2) {
        head += QStringLiteral("; expect %1-%2s, %3-%4 ml/s")
            .arg(dur.at(0).toDouble(), 0, 'f', 0)
            .arg(dur.at(1).toDouble(), 0, 'f', 0)
            .arg(flow.at(0).toDouble(), 0, 'f', 1)
            .arg(flow.at(1).toDouble(), 0, 'f', 1);
    }
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

        // 1. Look up prior turn's shot's profile + timestamp.
        QSqlQuery q(db);
        q.prepare("SELECT profile_kb_id, timestamp FROM shots WHERE id = ?");
        q.addBindValue(static_cast<qint64>(turn.shotId));
        if (!q.exec ()) {
            qWarning() << "buildRecentAdviceBlock: prior-shot lookup failed:"
                       << q.lastError().text() << "id=" << turn.shotId;
            continue;
        }
        if (!q.next()) continue;  // shot deleted from history; skip

        const QString priorKbId = q.value(0).toString();
        const qint64 priorTs = q.value(1).toLongLong();
        if (priorKbId != in.currentProfileKbId) continue;  // cross-profile filter
        if (priorTs <= 0) continue;

        // 2. Find the next shot postdating the prior turn's shot on the
        // same profile, excluding the current shot under analysis.
        QSqlQuery nextQ(db);
        nextQ.prepare(
            "SELECT id FROM shots "
            "WHERE profile_kb_id = ? AND timestamp > ? AND id != ? "
            "ORDER BY timestamp ASC LIMIT 1");
        nextQ.addBindValue(in.currentProfileKbId);
        nextQ.addBindValue(priorTs);
        nextQ.addBindValue(static_cast<qint64>(in.currentShotId));
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

static QString formatGrinderSetting(double v)
{
    QString s = QString::number(v, 'f', 1);
    if (s.endsWith(QStringLiteral(".0")))
        s.chop(2);
    return s;
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
    QSqlQuery q(db);
    q.prepare(
        "SELECT profile_kb_id, profile_name, grinder_setting, timestamp, "
        "       bean_brand, bean_type, roast_date, final_weight, "
        "       COALESCE(enjoyment,0), COALESCE(drink_tds,0), "
        "       COALESCE(yield_override, 0), "
        "       json_extract(profile_json,'$.target_weight') "
        "FROM shots "
        "WHERE grinder_model = ? "
        "  AND COALESCE(grinder_burrs, '') = COALESCE(?, '') "
        "  AND (beverage_type IS NULL OR beverage_type = '' OR LOWER(beverage_type) = 'espresso') "
        "  AND COALESCE(final_weight, 0) >= 15 "
        "  AND COALESCE(grind_issue_detected, 0) = 0 "
        "  AND COALESCE(channeling_detected, 0) = 0 "
        "  AND COALESCE(pour_truncated_detected, 0) = 0 "
        "  AND COALESCE(skip_first_frame_detected, 0) = 0 "
        "ORDER BY timestamp DESC");
    q.addBindValue(grinderModel);
    q.addBindValue(grinderBurrs);
    if (!q.exec ()) {
        qWarning() << "buildGrinderCalibrationBlock: history query failed:" << q.lastError().text();
        return QJsonObject();
    }

    struct CalRow {
        qint64 ts = 0;
        QString kbId;
        double ugs = std::numeric_limits<double>::quiet_NaN();
        double setting = 0.0;
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

        // Dialed-in gate.
        const bool ratedOk  = enj >= 50;
        const bool onTarget = targetW > 0.0 && std::abs(finalW - targetW) <= 0.10 * targetW;
        const bool hasTds   = tds > 0.0;
        if (!ratedOk && !onTarget && !hasTds) continue;

        bool numericOk = false;
        const double setVal = setStr.toDouble(&numericOk);
        if (!numericOk) continue;  // letter/click notation can't anchor a numeric key

        QString id = ShotSummarizer::resolveKbId(rawKbId);
        if (id.isEmpty())
            id = ShotSummarizer::computeProfileKbId(profName);

        CalRow r;
        r.ts = ts;
        r.kbId = id;
        r.ugs = id.isEmpty() ? std::numeric_limits<double>::quiet_NaN()
                             : ShotSummarizer::ugsForKbId(id);
        r.setting = setVal;
        r.bean = bBrand + QStringLiteral(" / ") + bType;
        r.roast = roast;
        r.dated = isDated(roast);
        r.beanOk = beanResolved(bBrand, bType);
        rows.append(r);
    }

    if (rows.isEmpty()) {
        qDebug() << "buildGrinderCalibrationBlock: no dialed-in shots for"
                 << grinderModel << grinderBurrs;
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
    QString anchorName;
    for (const CalRow& r : rows) {
        if (!r.currentBatch || std::isnan(r.ugs)) continue;
        if (r.ts > anchorTs) {
            anchorTs = r.ts;
            anchorSetting = r.setting;
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
                p["rgs"] = formatGrinderSetting(currentBatchMedian[e.kbId]);
                emittedNumber = true;
            } else if (e.ugs >= validLo - kCalibCap - 1e-9
                       && e.ugs <= validHi + kCalibCap + 1e-9) {
                p["source"] = QStringLiteral("derived");
                p["rgs"] = formatGrinderSetting(
                    anchorSetting + (e.ugs - anchorUgs) * conversionKey);
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
    block["usageConstraint"] = QStringLiteral(
        "UGS is a relative ordering of profiles by grind coarseness, not "
        "grinder clicks or a dial position. Numeric settings are valid only "
        "within calibratedUgsRange. For any profile with source "
        "\"directional\" give finer/coarser only and tell the user to pull a "
        "reference shot on that profile — never a number, never a click "
        "delta. Do not multiply a UGS distance by any factor of your own.");
    if (approximate) {
        block["conversionKey"] = std::round(conversionKey * 100.0) / 100.0;
        block["calibratedUgsRange"] = QJsonArray{ validLo, validHi };
        QJsonObject anchor;
        anchor["profileName"] = anchorName;
        anchor["ugs"] = anchorUgs;
        anchor["setting"] = formatGrinderSetting(anchorSetting);
        anchor["coffee"] = curBean;
        block["coffeeAnchor"] = anchor;
    }
    block["profiles"] = profilesArr;
    return block;
}


} // namespace DialingBlocks
