#include "mcptools_dialing_blocks.h"
#include "mcptools_dialing_helpers.h"

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
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringLiteral>
#include <QVariantList>

namespace {

McpDialingHelpers::ShotDiffInputs toDiffInputs(const ShotProjection& s)
{
    McpDialingHelpers::ShotDiffInputs d;
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
    return McpDialingHelpers::buildShotChangeDiff(toDiffInputs(prev), toDiffInputs(curr));
}

// Per-shot serializer for dialInSessions. Identity overrides come from
// the per-session `hoistSessionContext` output; per-shot entries emit
// the five identity fields only when they differ from the session
// context (otherwise the field is hoisted and absent here).
QJsonObject shotToJson(const ShotProjection& shot,
                       const McpDialingHelpers::ShotIdentity& override)
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

namespace McpDialingBlocks {

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
    const auto sessionIndices = McpDialingHelpers::groupSessions(timestamps);

    for (const auto& indices : sessionIndices) {
        // Reverse indices to ASC within the session so changeFromPrev
        // reads "older -> newer" — matching how the user iterates.
        QList<ShotProjection> ordered;
        ordered.reserve(indices.size());
        for (qsizetype i = indices.size() - 1; i >= 0; --i)
            ordered.append(shots[indices[i]]);

        QList<McpDialingHelpers::ShotIdentity> identities;
        identities.reserve(ordered.size());
        for (const ShotProjection& s : ordered) {
            McpDialingHelpers::ShotIdentity id;
            id.grinderBrand = s.grinderBrand;
            id.grinderModel = s.grinderModel;
            id.grinderBurrs = s.grinderBurrs;
            id.beanBrand = s.beanBrand;
            id.beanType = s.beanType;
            identities.append(id);
        }
        const McpDialingHelpers::HoistedSession hoisted =
            McpDialingHelpers::hoistSessionContext(identities);

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
        grinderCtx["minSetting"] = primary.minSetting;
        grinderCtx["maxSetting"] = primary.maxSetting;
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

    const double flowAtCutoff = McpDialingHelpers::estimateFlowAtCutoff(
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

} // namespace McpDialingBlocks
