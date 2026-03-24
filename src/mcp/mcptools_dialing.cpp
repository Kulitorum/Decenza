#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../history/shothistorystorage.h"
#include "../controllers/maincontroller.h"
#include "../controllers/profilemanager.h"
#include "../ai/aimanager.h"
#include "../ai/shotsummarizer.h"
#include "../core/settings.h"
#include "../profile/profile.h"

#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
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
    QString referenceGuide;
    QString profileKnowledgeBase;
};

void registerDialingTools(McpToolRegistry* registry, MainController* mainController,
                          ProfileManager* profileManager,
                          ShotHistoryStorage* shotHistory, Settings* settings)
{
    // dialing_get_context
    registry->registerAsyncTool(
        "dialing_get_context",
        "Get full dial-in context: recent shot summary, dial-in history (last N shots with same profile), "
        "profile knowledge, bean/grinder metadata, grinder context (observed settings range and step size), "
        "and reference tables. This is the primary read tool for dial-in conversations — a single call gives "
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
                        QSqlQuery hQuery(db);
                        hQuery.prepare("SELECT id, timestamp, profile_name, dose_weight, final_weight, "
                                       "duration_seconds, enjoyment, grinder_setting, grinder_model, "
                                       "espresso_notes, bean_brand, bean_type "
                                       "FROM shots WHERE profile_kb_id = ? "
                                       "AND id != ? "
                                       "ORDER BY timestamp DESC LIMIT ?");
                        hQuery.bindValue(0, dbResult.profileKbId);
                        hQuery.bindValue(1, resolvedShotId);
                        hQuery.bindValue(2, historyLimit);
                        if (hQuery.exec()) {
                            while (hQuery.next()) {
                                QJsonObject h;
                                h["id"] = hQuery.value("id").toLongLong();
                                auto dt = QDateTime::fromSecsSinceEpoch(hQuery.value("timestamp").toLongLong());
                                h["timestamp"] = dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODate);
                                h["profileName"] = hQuery.value("profile_name").toString();
                                h["doseG"] = hQuery.value("dose_weight").toDouble();
                                h["yieldG"] = hQuery.value("final_weight").toDouble();
                                h["durationSec"] = hQuery.value("duration_seconds").toDouble();
                                h["enjoyment0to100"] = hQuery.value("enjoyment").toInt();
                                h["grinderSetting"] = hQuery.value("grinder_setting").toString();
                                h["grinderModel"] = hQuery.value("grinder_model").toString();
                                h["notes"] = hQuery.value("espresso_notes").toString();
                                h["beanBrand"] = hQuery.value("bean_brand").toString();
                                h["beanType"] = hQuery.value("bean_type").toString();
                                dbResult.dialInHistory.append(h);
                            }
                        }
                    }

                    // --- Grinder context ---
                    QString grinderModel = dbResult.shotData.contains("grinderModel")
                        ? dbResult.shotData["grinderModel"].toString() : QString();
                    QString beverageType = dbResult.shotData.value("beverageType", "espresso").toString();
                    if (beverageType.isEmpty()) beverageType = "espresso";
                    if (!grinderModel.isEmpty()) {
                        QSqlQuery gQuery(db);
                        gQuery.prepare("SELECT DISTINCT grinder_setting FROM shots "
                                       "WHERE grinder_model = :model AND beverage_type = :bev "
                                       "AND grinder_setting != ''");
                        gQuery.bindValue(":model", grinderModel);
                        gQuery.bindValue(":bev", beverageType);
                        if (gQuery.exec()) {
                            QJsonArray settingsArr;
                            QSet<double> numericSet;
                            bool allNumeric = true;
                            bool hasAny = false;

                            while (gQuery.next()) {
                                QString s = gQuery.value(0).toString().trimmed();
                                if (s.isEmpty()) continue;
                                hasAny = true;
                                settingsArr.append(s);
                                bool ok;
                                double v = s.toDouble(&ok);
                                if (ok) {
                                    numericSet.insert(v);
                                } else {
                                    allNumeric = false;
                                }
                            }

                            QJsonObject grinderCtx;
                            grinderCtx["model"] = grinderModel;
                            grinderCtx["beverageType"] = beverageType;
                            grinderCtx["settingsObserved"] = settingsArr;
                            if (hasAny) grinderCtx["isNumeric"] = allNumeric;

                            QList<double> numeric(numericSet.begin(), numericSet.end());
                            if (allNumeric && numeric.size() >= 2) {
                                std::sort(numeric.begin(), numeric.end());
                                grinderCtx["minSetting"] = numeric.first();
                                grinderCtx["maxSetting"] = numeric.last();

                                double smallestStep = numeric.last() - numeric.first();
                                for (qsizetype i = 1; i < numeric.size(); ++i) {
                                    double diff = numeric[i] - numeric[i-1];
                                    if (diff > 0 && diff < smallestStep)
                                        smallestStep = diff;
                                }
                                grinderCtx["smallestStep"] = smallestStep;
                            }

                            dbResult.grinderContext = grinderCtx;
                        }
                    }
                });

                // --- File I/O on background thread (avoid blocking main thread) ---
                QFile refFile("docs/ESPRESSO_DIAL_IN_REFERENCE.md");
                if (refFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    dbResult.referenceGuide = QString::fromUtf8(refFile.readAll());
                    refFile.close();
                }
                QFile kbFile("docs/PROFILE_KNOWLEDGE_BASE.md");
                if (kbFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    dbResult.profileKnowledgeBase = QString::fromUtf8(kbFile.readAll());
                    kbFile.close();
                }

                // --- Deliver results to main thread for final assembly ---
                // Main-thread work: settings access, AI analysis, profile info
                QMetaObject::invokeMethod(qApp,
                    [respond, dbResult, resolvedShotId, mainController, profileManager, settings]() {

                    if (dbResult.shotData.isEmpty()) {
                        respond(QJsonObject{{"error", "Shot not found: " + QString::number(resolvedShotId)}});
                        return;
                    }

                    QJsonObject result;
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
                    QString profileKnowledge = ShotSummarizer::shotAnalysisSystemPrompt(
                        "espresso", profileTitle, QString(), dbResult.profileKbId);
                    if (!profileKnowledge.isEmpty())
                        result["profileKnowledge"] = profileKnowledge;

                    // --- Bean/grinder metadata (current DYE settings) ---
                    if (settings) {
                        QJsonObject bean;
                        bean["brand"] = settings->dyeBeanBrand();
                        bean["type"] = settings->dyeBeanType();
                        bean["roastDate"] = settings->dyeRoastDate();
                        bean["roastLevel"] = settings->dyeRoastLevel();
                        bean["grinderBrand"] = settings->dyeGrinderBrand();
                        bean["grinderModel"] = settings->dyeGrinderModel();
                        bean["grinderBurrs"] = settings->dyeGrinderBurrs();
                        bean["grinderSetting"] = settings->dyeGrinderSetting();
                        bean["doseWeightG"] = settings->dyeBeanWeight();
                        QString roastDateStr = settings->dyeRoastDate();
                        if (!roastDateStr.isEmpty()) {
                            QDate roastDate = QDate::fromString(roastDateStr, "yyyy-MM-dd");
                            if (roastDate.isValid()) {
                                qint64 daysSinceRoast = roastDate.daysTo(QDate::currentDate());
                                bean["beanAgeDays"] = daysSinceRoast;
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

                    // --- Dial-in reference tables (read on background thread) ---
                    if (!dbResult.referenceGuide.isEmpty())
                        result["referenceGuide"] = dbResult.referenceGuide;
                    if (!dbResult.profileKnowledgeBase.isEmpty())
                        result["profileKnowledgeBase"] = dbResult.profileKnowledgeBase;

                    respond(result);
                }, Qt::QueuedConnection);
            });

            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "read");
}
