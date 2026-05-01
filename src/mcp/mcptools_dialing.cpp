#include "mcpserver.h"
#include "mcptoolregistry.h"
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
    QJsonArray dialInHistory;
    QJsonObject grinderContext;
};

void registerDialingTools(McpToolRegistry* registry, MainController* mainController,
                          ProfileManager* profileManager,
                          ShotHistoryStorage* shotHistory, Settings* settings)
{
    // dialing_get_context
    registry->registerAsyncTool(
        "dialing_get_context",
        "Get full dial-in context: recent shot summary, dial-in history (last N shots with same profile), "
        "profile knowledge (includes system prompt, dial-in reference tables, profile catalog with cross-profile recommendation guidance, and profile-specific KB), "
        "bean/grinder metadata, and grinder context (observed settings range, step size, and burr-swappable flag). "
        "This is the primary read tool for dial-in conversations — a single call gives "
        "everything needed to analyze a shot and suggest changes. Grinder settings are shown as the user "
        "entered them — may be numbers, letters, click counts, or grinder-specific notation like Eureka "
        "multi-turn (1+4 = 1 rotation + position 4). The grinderContext block shows the range and step "
        "size observed in the user's own shot history.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shot_id", QJsonObject{{"type", "integer"}, {"description", "Specific shot ID to analyze. If omitted, uses most recent shot."}}},
                {"history_limit", QJsonObject{{"type", "integer"}, {"description", "Number of prior shots with same profile to include (default 5, max 20)"}}}
            }}
        },
        [mainController, profileManager, shotHistory, settings](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Shot history not available"}});
                return;
            }

            int historyLimit = qBound(1, args["history_limit"].toInt(5), 20);

            // Resolve shot ID on the main thread (lastSavedShotId is a simple getter)
            qint64 shotId = args["shot_id"].toInteger(0);
            if (shotId <= 0)
                shotId = shotHistory->lastSavedShotId();

            const QString dbPath = shotHistory->databasePath();

            QThread* thread = QThread::create(
                [dbPath, shotId, historyLimit, mainController, profileManager, settings, respond]() {
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

                    // --- Dial-in history (same profile family) ---
                    if (!dbResult.profileKbId.isEmpty()) {
                        QVariantList history = ShotHistoryStorage::loadRecentShotsByKbIdStatic(db, dbResult.profileKbId, historyLimit, resolvedShotId);
                        for (const auto& v : history) {
                            const ShotProjection shot = ShotProjection::fromVariantMap(v.toMap());
                            QJsonObject h;
                            h["id"] = shot.id;
                            h["timestamp"] = shot.dateTime;
                            h["profileName"] = shot.profileName;
                            h["doseG"] = shot.doseWeightG;
                            h["yieldG"] = shot.finalWeightG;
                            h["durationSec"] = shot.durationSec;
                            h["enjoyment0to100"] = shot.enjoyment;
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
                            dbResult.dialInHistory.append(h);
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
                    [respond, dbResult, resolvedShotId, mainController, profileManager, settings]() {

                    if (!dbResult.shotData.isValid()) {
                        respond(QJsonObject{{"error", "Shot not found: " + QString::number(resolvedShotId)}});
                        return;
                    }

                    QJsonObject result;
                    auto now = QDateTime::currentDateTime();
                    result["currentDateTime"] = now.toOffsetFromUtc(now.offsetFromUtc()).toString(Qt::ISODate);
                    result["shotId"] = resolvedShotId;

                    if (!dbResult.dialInHistory.isEmpty())
                        result["dialInHistory"] = dbResult.dialInHistory;
                    if (!dbResult.grinderContext.isEmpty())
                        result["grinderContext"] = dbResult.grinderContext;

                    // --- Shot summary ---
                    const auto& sd = dbResult.shotData;
                    QJsonObject shotSummary;
                    shotSummary["profileName"] = sd.profileName;
                    shotSummary["doseG"] = sd.doseWeightG;
                    shotSummary["yieldG"] = sd.finalWeightG;
                    shotSummary["durationSec"] = sd.durationSec;
                    shotSummary["enjoyment0to100"] = sd.enjoyment;
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
                    QString profileTitle = sd.profileName;
                    QString bevType = sd.beverageType.isEmpty() ? QStringLiteral("espresso") : sd.beverageType;
                    QString profileKnowledge = ShotSummarizer::shotAnalysisSystemPrompt(
                        bevType, profileTitle, QString(), dbResult.profileKbId);
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
