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
    h["profileName"] = shot.profileName;
    h["doseG"] = shot.doseWeightG;
    h["yieldG"] = shot.finalWeightG;
    h["durationSec"] = shot.durationSec;
    h["enjoyment0to100"] = shot.enjoyment0to100 > 0
        ? QJsonValue(shot.enjoyment0to100)
        : QJsonValue(QJsonValue::Null);
    // Issue #1055 Layer 3: per-shot enjoymentSource only when "inferred"
    // — sparse so the LLM only sees the field when it carries meaning.
    if (shot.enjoymentSource == QStringLiteral("inferred"))
        h["enjoymentSource"] = QStringLiteral("inferred");
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
    if (shot.temperatureOverrideC > 0)
        h["temperatureOverrideC"] = shot.temperatureOverrideC;

    if (shot.targetWeightG > 0) {
        h["targetWeightG"] = shot.targetWeightG;
    } else if (!shot.profileJson.isEmpty()) {
        // Defensive parse: this branch only runs for shots imported from
        // external formats (de1app / visualizer.coffee) where the importer
        // left targetWeight at 0. Their profileJson is the riskiest cohort
        // for malformed input, so log parse failures rather than swallow
        // them — `targetWeightG` will simply be omitted from this shot.
        QJsonParseError err{};
        QJsonObject profileObj = QJsonDocument::fromJson(shot.profileJson.toUtf8(), &err).object();
        if (err.error != QJsonParseError::NoError) {
            qWarning() << "shotToJson: profileJson parse failed for shot" << shot.id
                       << ":" << err.errorString();
        } else {
            QJsonValue tw = profileObj["target_weight"];
            double twVal = tw.isString() ? tw.toString().toDouble() : tw.toDouble();
            if (twVal > 0)
                h["targetWeightG"] = twVal;
        }
    }
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

        QJsonArray sessionShots;
        for (qsizetype i = 0; i < ordered.size(); ++i) {
            QJsonObject h = shotToJson(ordered[i], hoisted.perShotOverrides[i]);
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
    // Issue #1055 Layer 3: prefer user-rated candidates over inferred
    // ones regardless of score. The CASE expression sorts user-rated
    // rows before inferred rows; within each tier, highest enjoyment
    // wins, then most recent. The existing enjoyment > 0 gate keeps
    // 'none'-source rows out (they have enjoyment == 0 by construction).
    bestQ.prepare(
        "SELECT id, enjoyment_source FROM shots "
        "WHERE profile_kb_id = ? AND enjoyment > 0 "
        "AND id != ? AND timestamp >= ? "
        "ORDER BY CASE WHEN enjoyment_source = 'user' THEN 0 ELSE 1 END, "
        "         enjoyment DESC, timestamp DESC LIMIT 1");
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
    const QString bestEnjoymentSource = bestQ.value(1).toString();
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

    // Issue #1055 Layer 3: surface the rating provenance so the LLM
    // knows whether to anchor on this shot ("user_rated") or treat it
    // as a hint requiring user confirmation ("inferred"). The query's
    // CASE-based ORDER BY guarantees user-rated wins when both tiers
    // have candidates, so seeing "inferred" here means no user-rated
    // shot exists in the 90-day window for this profile.
    b["confidence"] = bestEnjoymentSource == QStringLiteral("user")
        ? QStringLiteral("user_rated")
        : QStringLiteral("inferred");
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

} // unnamed namespace (calibration helpers)

QJsonObject buildGrinderCalibrationBlock(QSqlDatabase& db,
                                         const QString& grinderModel,
                                         const QString& grinderBurrs,
                                         const QString& beverageType,
                                         qint64 resolvedShotId)
{
    if (grinderModel.isEmpty()) {
        qDebug() << "buildGrinderCalibrationBlock: skipped — grinderModel empty";
        return QJsonObject();
    }
    const QString bev = beverageType.trimmed().toLower();
    if (bev == QStringLiteral("filter") || bev == QStringLiteral("pourover")) {
        qDebug() << "buildGrinderCalibrationBlock: skipped — beverageType is" << beverageType;
        return QJsonObject();
    }

    // Query all shots on the same grinder+burrs combination, espresso only.
    // No time window — the conversion key (clicks per UGS unit) is a physical
    // property of the grinder+burrs pair and doesn't go stale. An all-time
    // query also lets the block activate for users who've only pulled two
    // qualifying profiles even if one was many months ago.
    //
    // Only include shots with clean extraction:
    //   - final_weight >= 5g (not aborted)
    //   - no quality badge set (grind issue, channeling, pour truncated, temp unstable)
    // Badge columns default to 0 on old shots (pre-migration), so the filter
    // is safe on all DB versions.
    QSqlQuery q(db);
    q.prepare(
        "SELECT profile_kb_id, profile_name, grinder_setting "
        "FROM shots "
        "WHERE grinder_model = ? "
        "  AND COALESCE(grinder_burrs, '') = COALESCE(?, '') "
        "  AND (beverage_type IS NULL OR beverage_type = '' OR LOWER(beverage_type) = 'espresso') "
        "  AND COALESCE(final_weight, 0) >= 5 "
        "  AND COALESCE(grind_issue_detected, 0) = 0 "
        "  AND COALESCE(channeling_detected, 0) = 0 "
        "  AND COALESCE(pour_truncated_detected, 0) = 0 "
        "  AND COALESCE(temperature_unstable, 0) = 0 "
        "  AND COALESCE(skip_first_frame_detected, 0) = 0 "
        "ORDER BY timestamp DESC");
    q.addBindValue(grinderModel);
    q.addBindValue(grinderBurrs);
    if (!q.exec ()) {
        qWarning() << "buildGrinderCalibrationBlock: history query failed:" << q.lastError().text();
        return QJsonObject();
    }

    // Group numeric settings by canonical KB name.
    // canonicalKey: pk.name if kbId found in KB, else profile_name.
    struct ProfileGroup {
        QString kbId;           // first non-empty kbId seen for this canonical name
        QString canonicalName;
        QList<double> settings;
    };
    QMap<QString, ProfileGroup> groups;  // key = canonicalName

    int rowCount = 0;
    int nonNumericCount = 0;
    while (q.next()) {
        ++rowCount;
        const QString kbId = q.value(0).toString().trimmed();
        const QString profileName = q.value(1).toString().trimmed();
        const QString settingStr = q.value(2).toString().trimmed();

        bool numericOk = false;
        const double settingVal = settingStr.toDouble(&numericOk);
        if (!numericOk) { ++nonNumericCount; continue; }

        QString canonName = ShotSummarizer::canonicalNameForKbId(kbId);
        if (canonName.isEmpty())
            canonName = profileName.isEmpty() ? kbId : profileName;
        if (canonName.isEmpty()) continue;

        auto& g = groups[canonName];
        g.canonicalName = canonName;
        if (g.kbId.isEmpty() && !kbId.isEmpty())
            g.kbId = kbId;
        g.settings.append(settingVal);
    }

    qDebug() << "buildGrinderCalibrationBlock: query rows=" << rowCount
             << "nonNumeric=" << nonNumericCount
             << "groups=" << groups.size()
             << "grinder=" << grinderModel << "burrs=" << grinderBurrs;

    if (groups.isEmpty()) {
        if (rowCount > 0 && nonNumericCount == rowCount)
            qDebug() << "buildGrinderCalibrationBlock: no groups → empty (all" << rowCount
                     << "shots have non-numeric grinder settings — calibration requires numeric notation)";
        else
            qDebug() << "buildGrinderCalibrationBlock: no groups → empty"
                     << "(no matching shots for grinder" << grinderModel << "burrs" << grinderBurrs << ")";
        return QJsonObject();
    }

    // Compute medians and collect anchor candidates.
    // Canonical (non-inferred UGS, exact KB name) are preferred; inferred-UGS
    // profiles are kept as a fallback pool used only when the canonical-only
    // selection produces a degenerate pair (setting difference < 0.5).
    struct AnchorCandidate {
        QString canonicalName;
        QString kbId;
        double ugs           = std::numeric_limits<double>::quiet_NaN();
        double medianSetting = std::numeric_limits<double>::quiet_NaN();
        qsizetype sampleCount = 0;
    };
    QList<AnchorCandidate> canonicalCandidates;
    QList<AnchorCandidate> inferredCandidates;
    QMap<QString, double> medianByName;  // canonical name → median

    for (auto& g : groups) {
        const double med = computeMedian(g.settings);
        if (std::isnan(med)) continue;
        medianByName[g.canonicalName] = med;

        // kbId may be empty for shots predating migration 9 — fall back to
        // computing it from the canonical name via the KB matcher.
        QString kbId = g.kbId;
        if (kbId.isEmpty())
            kbId = ShotSummarizer::computeProfileKbId(g.canonicalName);

        if (kbId.isEmpty()) {
            qDebug() << "buildGrinderCalibrationBlock: group" << g.canonicalName
                     << "skipped — no kbId resolvable";
            continue;
        }
        const double ugs = ShotSummarizer::ugsForKbId(kbId);
        if (std::isnan(ugs)) {
            qDebug() << "buildGrinderCalibrationBlock: group" << g.canonicalName
                     << "kbId=" << kbId << "skipped — NaN UGS";
            continue;
        }

        // Only the canonical KB name qualifies as an anchor. Variant profiles
        // (e.g. "D-Flow / Q - Jeff") resolve to a kbId via normalization but
        // their settings are tuned for specific beans — they don't represent the
        // canonical UGS position and should not anchor the calibration.
        const QString kbCanonicalName = ShotSummarizer::canonicalNameForKbId(kbId);
        if (!kbCanonicalName.isEmpty() && g.canonicalName != kbCanonicalName) {
            qDebug() << "buildGrinderCalibrationBlock: group" << g.canonicalName
                     << "kbId=" << kbId << "skipped — variant of" << kbCanonicalName;
            continue;
        }

        const bool inferred = ShotSummarizer::ugsInferredForKbId(kbId);
        qDebug() << "buildGrinderCalibrationBlock: anchor candidate" << g.canonicalName
                 << "kbId=" << kbId << "ugs=" << ugs
                 << (inferred ? "(inferred)" : "(canonical)")
                 << "median=" << med << "n=" << g.settings.size();

        AnchorCandidate c;
        c.canonicalName = g.canonicalName;
        c.kbId = kbId;
        c.ugs = ugs;
        c.medianSetting = med;
        c.sampleCount = g.settings.size();
        if (inferred)
            inferredCandidates.append(c);
        else
            canonicalCandidates.append(c);
    }

    qDebug() << "buildGrinderCalibrationBlock: canonical candidates=" << canonicalCandidates.size()
             << "inferred candidates=" << inferredCandidates.size();

    // Anchor selection: pick the widest-UGS-span pair consistent with the
    // majority setting direction (rejects outliers where the user shot with a
    // stale or wrong grinder setting). Returns nullptr/nullptr on failure.
    // Minimum setting difference of 0.5 is required — a degenerate pair (both
    // anchors at the same setting) gives conversionKey ≈ 0 and useless RGS.
    auto selectAnchors = [](QList<AnchorCandidate>& pool)
        -> std::pair<const AnchorCandidate*, const AnchorCandidate*>
    {
        if (pool.size() < 2) return {nullptr, nullptr};
        std::sort(pool.begin(), pool.end(),
                  [](const AnchorCandidate& a, const AnchorCandidate& b) { return a.ugs < b.ugs; });

        int positiveCount = 0, negativeCount = 0;
        for (int i = 0; i < pool.size(); ++i) {
            for (int j = i + 1; j < pool.size(); ++j) {
                const double diff = pool[j].medianSetting - pool[i].medianSetting;
                if (diff > 1e-9)       ++positiveCount;
                else if (diff < -1e-9) ++negativeCount;
            }
        }
        const int majoritySign = (positiveCount > negativeCount) ? 1
                               : (negativeCount > positiveCount) ? -1 : 0;

        const AnchorCandidate* fine   = nullptr;
        const AnchorCandidate* coarse = nullptr;
        double bestSpan = -1.0;
        for (int f = 0; f < pool.size(); ++f) {
            for (int c = pool.size() - 1; c > f; --c) {
                const double ugsDiff     = pool[c].ugs - pool[f].ugs;
                const double settingDiff = pool[c].medianSetting - pool[f].medianSetting;
                const bool consistent = (majoritySign == 0)
                                     || (settingDiff * majoritySign > 0);
                // Require at least 0.5 setting-unit difference — a degenerate
                // pair (both anchors at the same setting) gives conversionKey ≈ 0.
                if (consistent && ugsDiff > bestSpan
                        && std::abs(settingDiff) >= 0.5) {
                    bestSpan = ugsDiff;
                    fine     = &pool[f];
                    coarse   = &pool[c];
                }
            }
        }
        return {fine, coarse};
    };

    // First pass: canonical-only. Fall back to inferred pool when canonical-only
    // yields < 2 candidates or no non-degenerate pair (setting diff < 0.5).
    auto [fineAnchor, coarseAnchor] = selectAnchors(canonicalCandidates);
    bool usedInferred = false;
    if (!fineAnchor || !coarseAnchor) {
        QList<AnchorCandidate> combined = canonicalCandidates + inferredCandidates;
        auto [f2, c2] = selectAnchors(combined);
        if (f2 && c2) {
            // f2/c2 point into `combined` which is about to go out of scope.
            // Copy combined into canonicalCandidates for pointer stability, then
            // re-run selectAnchors on the stable copy. Deterministic — same data,
            // same result.
            canonicalCandidates = combined;
            auto [f3, c3] = selectAnchors(canonicalCandidates);
            if (!f3 || !c3) {
                // Should be unreachable: deterministic re-run on identical data.
                qWarning() << "buildGrinderCalibrationBlock: inferred fallback succeeded on"
                           << "combined pool but failed on stable copy — returning empty";
                return QJsonObject();
            }
            fineAnchor   = f3;
            coarseAnchor = c3;
            usedInferred = true;
        }
    }

    if (!fineAnchor || !coarseAnchor) {
        qDebug() << "buildGrinderCalibrationBlock: no non-degenerate anchor pair → empty";
        return QJsonObject();
    }
    qDebug() << "buildGrinderCalibrationBlock: anchors selected:"
             << fineAnchor->canonicalName << "(UGS" << fineAnchor->ugs
             << "setting" << fineAnchor->medianSetting << ") →"
             << coarseAnchor->canonicalName << "(UGS" << coarseAnchor->ugs
             << "setting" << coarseAnchor->medianSetting << ")"
             << (usedInferred ? "[used inferred fallback]" : "[canonical]");

    const double ugsSpan = coarseAnchor->ugs - fineAnchor->ugs;
    if (ugsSpan < 1e-9) {
        qWarning() << "buildGrinderCalibrationBlock: degenerate anchor pair — both at UGS"
                   << fineAnchor->ugs << "→ empty";
        return QJsonObject();
    }

    const double conversionKey =
        (coarseAnchor->medianSetting - fineAnchor->medianSetting) / ugsSpan;
    const double conversionKeyRounded =
        std::round(conversionKey * 100.0) / 100.0;

    // Build the profiles array from all KB entries with a known UGS.
    // Additionally, append any history profiles not in the KB UGS list.
    QJsonArray profilesArr;
    QSet<QString> coveredNames;

    const QList<ShotSummarizer::KbUgsEntry> kbEntries = ShotSummarizer::allKbUgsEntries();

    // Sort KB entries by UGS ascending for the output order.
    QList<ShotSummarizer::KbUgsEntry> sortedEntries = kbEntries;
    std::sort(sortedEntries.begin(), sortedEntries.end(),
              [](const ShotSummarizer::KbUgsEntry& a, const ShotSummarizer::KbUgsEntry& b) {
                  return a.ugs < b.ugs;
              });

    for (const ShotSummarizer::KbUgsEntry& e : sortedEntries) {
        coveredNames.insert(e.name);

        const bool hasHistory = medianByName.contains(e.name);
        const bool withinRange = !e.ugsInferred
            && e.ugs >= fineAnchor->ugs - 1e-9
            && e.ugs <= coarseAnchor->ugs + 1e-9;

        QString source;
        QString rgs;
        if (hasHistory) {
            source = QStringLiteral("history");
            rgs = formatGrinderSetting(medianByName[e.name]);
        } else if (withinRange) {
            source = QStringLiteral("derived");
            const double computed = fineAnchor->medianSetting
                + (e.ugs - fineAnchor->ugs) * conversionKeyRounded;
            rgs = formatGrinderSetting(computed);
        } else {
            source = QStringLiteral("extrapolated");
            const double computed = fineAnchor->medianSetting
                + (e.ugs - fineAnchor->ugs) * conversionKeyRounded;
            rgs = formatGrinderSetting(computed);
        }

        QJsonObject p;
        p["profileName"] = e.name;
        p["ugs"] = e.ugs;
        p["rgs"] = rgs;
        p["source"] = source;
        profilesArr.append(p);
    }

    // Append history profiles that have no KB UGS entry (custom titles, etc.).
    for (auto it = medianByName.constBegin(); it != medianByName.constEnd(); ++it) {
        if (coveredNames.contains(it.key())) continue;
        QJsonObject p;
        p["profileName"] = it.key();
        p["rgs"] = formatGrinderSetting(it.value());
        p["source"] = QStringLiteral("history");
        profilesArr.append(p);
    }

    // Assemble the block.
    auto makeAnchorObj = [](const AnchorCandidate* a) -> QJsonObject {
        QJsonObject o;
        o["profileName"] = a->canonicalName;
        o["ugs"] = a->ugs;
        o["medianSetting"] = formatGrinderSetting(a->medianSetting);
        o["sampleCount"] = static_cast<int>(a->sampleCount);
        return o;
    };

    QJsonObject block;
    block["grinderModel"] = grinderModel;
    block["fineAnchor"] = makeAnchorObj(fineAnchor);
    block["coarseAnchor"] = makeAnchorObj(coarseAnchor);
    block["conversionKey"] = conversionKeyRounded;
    block["calibratedUgsRange"] = QJsonArray{ fineAnchor->ugs, coarseAnchor->ugs };
    block["profiles"] = profilesArr;
    return block;
}

} // namespace DialingBlocks
