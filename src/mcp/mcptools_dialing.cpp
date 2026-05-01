#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "mcptools_dialing_helpers.h"
#include "../history/shothistorystorage.h"
#include "../controllers/maincontroller.h"
#include "../controllers/profilemanager.h"
#include "../ai/aimanager.h"
#include "../ai/shotsummarizer.h"
#include "../core/settings.h"
#include "../core/settings_dye.h"
#include "../profile/profile.h"

#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QThread>
#include <QMetaObject>
#include <QCoreApplication>

#include "../core/dbutils.h"

// Data collected on the background thread (pure SQL results, no QObject access)
struct DialingDbResult {
    ShotProjection shotData;
    QString profileKbId;
    QJsonArray dialInSessions;
    QJsonObject grinderContext;
};

// Build the changeFromPrev diff between two adjacent shots in the same
// session — same shape as `shots_compare` produces, computed inline so the
// AI doesn't need a separate round-trip to see what moved.
static QJsonObject changeFromPrev(const ShotProjection& prev, const ShotProjection& curr)
{
    QJsonObject diff;
    auto diffStr = [&](const QString& a, const QString& b, const QString& key) {
        if (!a.isEmpty() && !b.isEmpty() && a != b)
            diff[key] = QString("%1 -> %2").arg(a, b);
    };
    auto diffNum = [&](double a, double b, const QString& key, const QString& unit) {
        if (a != 0 && b != 0 && qAbs(a - b) > 0.01)
            diff[key] = QString("%1 -> %2 %3 (%4%5)")
                .arg(a, 0, 'f', 1).arg(b, 0, 'f', 1).arg(unit)
                .arg(b > a ? "+" : "").arg(b - a, 0, 'f', 1);
    };
    diffStr(prev.grinderSetting, curr.grinderSetting, "grinderSetting");
    diffStr(prev.beanBrand, curr.beanBrand, "beanBrand");
    diffNum(prev.doseWeightG, curr.doseWeightG, "doseG", "g");
    diffNum(prev.finalWeightG, curr.finalWeightG, "yieldG", "g");
    diffNum(prev.durationSec, curr.durationSec, "durationSec", "s");
    diffNum(prev.enjoyment0to100, curr.enjoyment0to100, "enjoyment0to100", "");
    return diff;
}

void registerDialingTools(McpToolRegistry* registry, MainController* mainController,
                          ProfileManager* profileManager,
                          ShotHistoryStorage* shotHistory, Settings* settings)
{
    // dialing_get_context
    registry->registerAsyncTool(
        "dialing_get_context",
        "Get dial-in context: recent shot summary, dial-in history grouped into sessions (runs of "
        "shots on the same profile within ~60 minutes of each other, with within-session "
        "changeFromPrev diffs), profile knowledge for the current shot's profile, bean/grinder "
        "metadata, and grinder context (observed settings range, step size, and burr-swappable flag). "
        "Primary read tool for dial-in conversations — a single call gives everything needed to analyze "
        "a shot and suggest changes. Default profileKnowledge contains only the current profile's "
        "curated KB entry (~1 KB); pass includeFullKnowledge: true to also receive the dial-in system "
        "prompt, reference tables, and the cross-profile catalog (~18 KB total — useful at session start "
        "but redundant on later turns). "
        "Grinder settings are shown as the user entered them — may be numbers, letters, click counts, or "
        "grinder-specific notation like Eureka multi-turn (1+4 = 1 rotation + position 4). The "
        "grinderContext block shows the range and step size observed in the user's own shot history.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shot_id", QJsonObject{{"type", "integer"}, {"description", "Specific shot ID to analyze. If omitted, uses most recent shot."}}},
                {"history_limit", QJsonObject{{"type", "integer"}, {"description", "Number of prior shots with same profile to include (default 5, max 20)"}}},
                {"includeFullKnowledge", QJsonObject{{"type", "boolean"}, {"description", "Include the full dial-in system prompt, reference tables, and profile catalog in profileKnowledge. Default false — return only the current profile's KB entry. Useful at session start; redundant per call."}}}
            }}
        },
        [mainController, profileManager, shotHistory, settings](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Shot history not available"}});
                return;
            }

            int historyLimit = qBound(1, args["history_limit"].toInt(5), 20);
            const bool includeFullKnowledge = args.value("includeFullKnowledge").toBool();

            // Resolve shot ID on the main thread (lastSavedShotId is a simple getter)
            qint64 shotId = args["shot_id"].toInteger(0);
            if (shotId <= 0)
                shotId = shotHistory->lastSavedShotId();

            const QString dbPath = shotHistory->databasePath();

            QThread* thread = QThread::create(
                [dbPath, shotId, historyLimit, includeFullKnowledge, mainController, profileManager, settings, respond]() {
                // --- All SQL runs on this background thread ---
                DialingDbResult dbResult;

                qint64 resolvedShotId = shotId;

                // If no shot saved this session, query DB for most recent
                if (resolvedShotId <= 0) {
                    withTempDb(dbPath, "mcp_dialing_latest", [&](QSqlDatabase& db) {
                        QSqlQuery q(db);
                        if (q.exec("SELECT id FROM shots ORDER BY timestamp DESC LIMIT 1") && q.next())
                            resolvedShotId = q.value(0).toLongLong();
                    });
                }

                if (resolvedShotId <= 0) {
                    QMetaObject::invokeMethod(qApp, [respond]() {
                        respond(QJsonObject{{"error", "No shots available"}});
                    }, Qt::QueuedConnection);
                    return;
                }

                withTempDb(dbPath, "mcp_dialing", [&](QSqlDatabase& db) {
                    ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(db, resolvedShotId);
                    dbResult.shotData = ShotHistoryStorage::convertShotRecord(record);
                    dbResult.profileKbId = record.profileKbId;

                    // --- Dial-in history grouped into sessions (same profile family) ---
                    // History returns DESC (newest first). Within a session
                    // we want ASC order so changeFromPrev reads "older ->
                    // newer" — matching how the user iterates. Sessions
                    // themselves stay newest-first so the most relevant
                    // recent iteration is at the top of the list.
                    if (!dbResult.profileKbId.isEmpty()) {
                        QVariantList history = ShotHistoryStorage::loadRecentShotsByKbIdStatic(db, dbResult.profileKbId, historyLimit, resolvedShotId);

                        QList<ShotProjection> shots;
                        shots.reserve(history.size());
                        for (const auto& v : history)
                            shots.append(ShotProjection::fromVariantMap(v.toMap()));

                        auto shotToJson = [](const ShotProjection& shot) {
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
                            h["grinderModel"] = shot.grinderModel;
                            h["grinderBrand"] = shot.grinderBrand;
                            h["grinderBurrs"] = shot.grinderBurrs;
                            h["notes"] = shot.espressoNotes;
                            h["beanBrand"] = shot.beanBrand;
                            h["beanType"] = shot.beanType;
                            if (shot.temperatureOverrideC > 0)
                                h["temperatureOverrideC"] = shot.temperatureOverrideC;

                            // For shots saved by MainController, targetWeightG is always
                            // populated (user override → profile target_weight → finalWeight).
                            // The profile-JSON fallback below is defensive for shots imported
                            // from external formats (de1app, visualizer.coffee) where the
                            // shot importer leaves targetWeight at 0.
                            if (shot.targetWeightG > 0) {
                                h["targetWeightG"] = shot.targetWeightG;
                            } else if (!shot.profileJson.isEmpty()) {
                                QJsonObject profileObj = QJsonDocument::fromJson(shot.profileJson.toUtf8()).object();
                                QJsonValue tw = profileObj["target_weight"];
                                double twVal = tw.isString() ? tw.toString().toDouble() : tw.toDouble();
                                if (twVal > 0)
                                    h["targetWeightG"] = twVal;
                            }
                            return h;
                        };

                        // Group the DESC-ordered shots into sessions using
                        // the pure helper (unit-tested separately).
                        QList<qint64> timestamps;
                        timestamps.reserve(shots.size());
                        for (const auto& s : shots)
                            timestamps.append(s.timestamp);
                        const auto sessionIndices = McpDialingHelpers::groupSessions(timestamps);

                        for (const auto& indices : sessionIndices) {
                            // Reverse to ASC within the session so
                            // changeFromPrev reads "older -> newer".
                            QList<ShotProjection> ordered;
                            ordered.reserve(indices.size());
                            for (qsizetype i = indices.size() - 1; i >= 0; --i)
                                ordered.append(shots[indices[i]]);

                            QJsonArray sessionShots;
                            for (qsizetype i = 0; i < ordered.size(); ++i) {
                                QJsonObject h = shotToJson(ordered[i]);
                                if (i > 0) {
                                    QJsonObject diff = changeFromPrev(ordered[i-1], ordered[i]);
                                    h["changeFromPrev"] = diff.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(diff);
                                } else {
                                    h["changeFromPrev"] = QJsonValue(QJsonValue::Null);
                                }
                                sessionShots.append(h);
                            }

                            QJsonObject sessionObj;
                            sessionObj["sessionStart"] = ordered.first().timestampIso;
                            sessionObj["sessionEnd"] = ordered.last().timestampIso;
                            sessionObj["shotCount"] = static_cast<int>(ordered.size());
                            sessionObj["shots"] = sessionShots;
                            dbResult.dialInSessions.append(sessionObj);
                        }
                    }

                    // --- Grinder context (shared helper) ---
                    QString grinderModel = dbResult.shotData.grinderModel;
                    QString beverageType = dbResult.shotData.beverageType.isEmpty()
                        ? QStringLiteral("espresso") : dbResult.shotData.beverageType;
                    if (!grinderModel.isEmpty()) {
                        GrinderContext ctx = ShotHistoryStorage::queryGrinderContext(db, grinderModel, beverageType);
                        if (!ctx.settingsObserved.isEmpty()) {
                            QJsonObject grinderCtx;
                            grinderCtx["model"] = ctx.model;
                            grinderCtx["beverageType"] = ctx.beverageType;
                            QJsonArray settingsArr;
                            for (const auto& s : ctx.settingsObserved)
                                settingsArr.append(s);
                            grinderCtx["settingsObserved"] = settingsArr;
                            grinderCtx["isNumeric"] = ctx.allNumeric;
                            if (ctx.allNumeric && ctx.maxSetting > ctx.minSetting) {
                                grinderCtx["minSetting"] = ctx.minSetting;
                                grinderCtx["maxSetting"] = ctx.maxSetting;
                                grinderCtx["smallestStep"] = ctx.smallestStep;
                            }
                            dbResult.grinderContext = grinderCtx;
                        }
                    }
                });

                // --- Deliver results to main thread for final assembly ---
                // Main-thread work: settings access, AI analysis, profile info
                QMetaObject::invokeMethod(qApp,
                    [respond, dbResult, resolvedShotId, includeFullKnowledge, mainController, profileManager, settings]() {

                    if (!dbResult.shotData.isValid()) {
                        respond(QJsonObject{{"error", "Shot not found: " + QString::number(resolvedShotId)}});
                        return;
                    }

                    QJsonObject result;
                    auto now = QDateTime::currentDateTime();
                    result["currentDateTime"] = now.toOffsetFromUtc(now.offsetFromUtc()).toString(Qt::ISODate);
                    result["shotId"] = resolvedShotId;

                    if (!dbResult.dialInSessions.isEmpty())
                        result["dialInSessions"] = dbResult.dialInSessions;
                    if (!dbResult.grinderContext.isEmpty())
                        result["grinderContext"] = dbResult.grinderContext;

                    // --- Shot summary ---
                    const auto& sd = dbResult.shotData;
                    QJsonObject shotSummary;
                    shotSummary["profileName"] = sd.profileName;
                    shotSummary["doseG"] = sd.doseWeightG;
                    shotSummary["yieldG"] = sd.finalWeightG;
                    shotSummary["durationSec"] = sd.durationSec;
                    shotSummary["enjoyment0to100"] = sd.enjoyment0to100 > 0
                        ? QJsonValue(sd.enjoyment0to100)
                        : QJsonValue(QJsonValue::Null);
                    shotSummary["notes"] = sd.espressoNotes;
                    shotSummary["beanBrand"] = sd.beanBrand;
                    shotSummary["beanType"] = sd.beanType;
                    shotSummary["roastLevel"] = sd.roastLevel;
                    shotSummary["grinderModel"] = sd.grinderModel;
                    shotSummary["grinderSetting"] = sd.grinderSetting;
                    shotSummary["grinderBurrs"] = sd.grinderBurrs;
                    if (sd.doseWeightG > 0)
                        shotSummary["ratio"] = QString("1:%1").arg(sd.finalWeightG / sd.doseWeightG, 0, 'f', 2);
                    result["shot"] = shotSummary;

                    // --- AI-generated shot analysis ---
                    if (mainController && mainController->aiManager()) {
                        AIManager* ai = mainController->aiManager();
                        QString analysis = ai->generateHistoryShotSummary(dbResult.shotData);
                        if (!analysis.isEmpty())
                            result["shotAnalysis"] = analysis;
                    }

                    // --- Profile knowledge ---
                    // Default: ship only the current profile's KB section
                    // (~1 KB). The full system prompt + reference tables +
                    // profile catalog (~18 KB total) is opt-in via
                    // includeFullKnowledge — useful at session start, but
                    // redundant on later turns of a multi-call dial-in
                    // conversation. See #987.
                    QString profileTitle = sd.profileName;
                    QString bevType = sd.beverageType.isEmpty() ? QStringLiteral("espresso") : sd.beverageType;
                    // Extract the editor type from the embedded profile JSON
                    // so the fuzzy-match fallback can find a section for
                    // custom-titled D-Flow / A-Flow profiles whose stored
                    // profileKbId is stale or absent. Without this, the
                    // fallback only matches on title alone.
                    QString profileType;
                    if (!sd.profileJson.isEmpty()) {
                        const QJsonObject pj = QJsonDocument::fromJson(sd.profileJson.toUtf8()).object();
                        profileType = pj.value("type").toString();
                    }
                    QString profileKnowledge;
                    if (includeFullKnowledge) {
                        profileKnowledge = ShotSummarizer::shotAnalysisSystemPrompt(
                            bevType, profileTitle, profileType, dbResult.profileKbId);
                    } else {
                        profileKnowledge = ShotSummarizer::profileKnowledgeForKbId(dbResult.profileKbId);
                        if (profileKnowledge.isEmpty())
                            profileKnowledge = ShotSummarizer::findProfileSection(profileTitle, profileType);
                    }
                    if (!profileKnowledge.isEmpty())
                        result["profileKnowledge"] = profileKnowledge;

                    // --- Bean/grinder metadata (current DYE settings) ---
                    if (settings) {
                        QJsonObject bean;
                        bean["brand"] = settings->dye()->dyeBeanBrand();
                        bean["type"] = settings->dye()->dyeBeanType();
                        bean["roastDate"] = settings->dye()->dyeRoastDate();
                        bean["roastLevel"] = settings->dye()->dyeRoastLevel();
                        bean["grinderBrand"] = settings->dye()->dyeGrinderBrand();
                        bean["grinderModel"] = settings->dye()->dyeGrinderModel();
                        bean["grinderBurrs"] = settings->dye()->dyeGrinderBurrs();
                        bean["grinderSetting"] = settings->dye()->dyeGrinderSetting();
                        bean["doseWeightG"] = settings->dye()->dyeBeanWeight();
                        QString roastDateStr = settings->dye()->dyeRoastDate();
                        if (!roastDateStr.isEmpty()) {
                            QDate roastDate = QDate::fromString(roastDateStr, "yyyy-MM-dd");
                            if (!roastDate.isValid()) roastDate = QDate::fromString(roastDateStr, Qt::ISODate);
                            if (!roastDate.isValid()) roastDate = QDate::fromString(roastDateStr, "MM/dd/yyyy");
                            if (!roastDate.isValid()) roastDate = QDate::fromString(roastDateStr, "dd/MM/yyyy");

                            if (roastDate.isValid()) {
                                qint64 days = roastDate.daysTo(QDate::currentDate());
                                bean["daysSinceRoast"] = days;
                                bean["daysSinceRoastNote"] = "Days since roast date, NOT freshness. "
                                    "Many users freeze beans and thaw weekly — ask about storage before assuming degradation.";
                            }
                        }
                        result["currentBean"] = bean;
                    }

                    // --- Current profile info ---
                    if (profileManager) {
                        QJsonObject profileInfo;
                        profileInfo["filename"] = profileManager->currentProfileName();
                        profileInfo["targetWeightG"] = profileManager->profileTargetWeight();
                        profileInfo["targetTemperatureC"] = profileManager->profileTargetTemperature();
                        if (profileManager->profileHasRecommendedDose())
                            profileInfo["recommendedDoseG"] = profileManager->profileRecommendedDose();
                        result["currentProfile"] = profileInfo;
                    }

                    // Note: dial-in reference tables and profile knowledge base are now
                    // embedded in the profileKnowledge system prompt (shared with in-app AI),
                    // so they are no longer sent as separate fields here.

                    respond(result);
                }, Qt::QueuedConnection);
            });

            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "read");
}
