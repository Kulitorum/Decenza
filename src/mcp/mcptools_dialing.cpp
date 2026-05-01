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
    QVariantMap shotData;
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
                            QVariantMap shot = v.toMap();
                            QJsonObject h;
                            h["id"] = shot["id"].toLongLong();
                            h["timestamp"] = shot["dateTime"].toString();
                            h["profileName"] = shot["profileName"].toString();
                            h["doseG"] = shot["doseWeight"].toDouble();
                            h["yieldG"] = shot["finalWeight"].toDouble();
                            h["durationSec"] = shot["duration"].toDouble();
                            h["enjoyment0to100"] = shot["enjoyment"].toInt();
                            h["grinderSetting"] = shot["grinderSetting"].toString();
                            h["grinderModel"] = shot["grinderModel"].toString();
                            h["grinderBrand"] = shot["grinderBrand"].toString();
                            h["grinderBurrs"] = shot["grinderBurrs"].toString();
                            h["notes"] = shot["espressoNotes"].toString();
                            h["beanBrand"] = shot["beanBrand"].toString();
                            h["beanType"] = shot["beanType"].toString();
                            double tempOverride = shot["temperatureOverrideC"].toDouble();
                            if (tempOverride > 0)
                                h["temperatureOverrideC"] = tempOverride;

                            // For shots saved by MainController, targetWeightG is always
                            // populated (user override → profile target_weight → finalWeight).
                            // The profile-JSON fallback below is defensive for shots imported
                            // from external formats (de1app, visualizer.coffee) where the
                            // shot importer leaves targetWeight at 0.
                            double targetWeight = shot["targetWeightG"].toDouble();
                            if (targetWeight > 0) {
                                h["targetWeightG"] = targetWeight;
                            } else {
                                QString profileJson = shot["profileJson"].toString();
                                if (!profileJson.isEmpty()) {
                                    QJsonObject profileObj = QJsonDocument::fromJson(profileJson.toUtf8()).object();
                                    QJsonValue tw = profileObj["target_weight"];
                                    double twVal = tw.isString() ? tw.toString().toDouble() : tw.toDouble();
                                    if (twVal > 0)
                                        h["targetWeightG"] = twVal;
                                }
                            }
                            dbResult.dialInHistory.append(h);
                        }
                    }

                    // --- Grinder context (shared helper) ---
                    QString grinderModel = dbResult.shotData.contains("grinderModel")
                        ? dbResult.shotData["grinderModel"].toString() : QString();
                    QString beverageType = dbResult.shotData.value("beverageType", "espresso").toString();
                    if (beverageType.isEmpty()) beverageType = "espresso";
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

                    if (dbResult.shotData.isEmpty()) {
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
                    shotSummary["profileName"] = sd["profileName"].toString();
                    shotSummary["doseG"] = sd["doseWeight"].toDouble();
                    shotSummary["yieldG"] = sd["finalWeight"].toDouble();
                    shotSummary["durationSec"] = sd["duration"].toDouble();
                    shotSummary["enjoyment0to100"] = sd["enjoyment"].toInt();
                    shotSummary["notes"] = sd["espressoNotes"].toString();
                    shotSummary["beanBrand"] = sd["beanBrand"].toString();
                    shotSummary["beanType"] = sd["beanType"].toString();
                    shotSummary["roastLevel"] = sd["roastLevel"].toString();
                    shotSummary["grinderModel"] = sd["grinderModel"].toString();
                    shotSummary["grinderSetting"] = sd["grinderSetting"].toString();
                    shotSummary["grinderBurrs"] = sd["grinderBurrs"].toString();
                    double dose = sd["doseWeight"].toDouble();
                    double yield = sd["finalWeight"].toDouble();
                    if (dose > 0)
                        shotSummary["ratio"] = QString("1:%1").arg(yield / dose, 0, 'f', 2);
                    result["shot"] = shotSummary;

                    // --- AI-generated shot analysis ---
                    if (mainController && mainController->aiManager()) {
                        AIManager* ai = mainController->aiManager();
                        QString analysis = ai->generateHistoryShotSummary(dbResult.shotData);
                        if (!analysis.isEmpty())
                            result["shotAnalysis"] = analysis;
                    }

                    // --- Profile knowledge ---
                    QString profileTitle = sd["profileName"].toString();
                    QString bevType = sd.value("beverageType", "espresso").toString();
                    if (bevType.isEmpty()) bevType = "espresso";
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
