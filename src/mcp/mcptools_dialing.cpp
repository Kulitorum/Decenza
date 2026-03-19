// TODO: Move SQL queries to background thread per CLAUDE.md design principle.
// Current tool handler architecture (synchronous QJsonObject return) prevents this.
// Requires refactoring McpToolHandler to support async responses.

#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../history/shothistorystorage.h"
#include "../controllers/maincontroller.h"
#include "../ai/aimanager.h"
#include "../ai/shotsummarizer.h"
#include "../core/settings.h"
#include "../profile/profile.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QAtomicInt>

static QAtomicInt s_mcpDialConnCounter{0};

void registerDialingTools(McpToolRegistry* registry, MainController* mainController,
                          ShotHistoryStorage* shotHistory, Settings* settings)
{
    // dialing_get_context
    registry->registerTool(
        "dialing_get_context",
        "Get full dial-in context: recent shot summary, dial-in history (last N shots with same profile), "
        "profile knowledge, bean/grinder metadata, and reference tables. This is the primary read tool "
        "for dial-in conversations — a single call gives everything needed to analyze a shot and suggest changes.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shot_id", QJsonObject{{"type", "integer"}, {"description", "Specific shot ID to analyze. If omitted, uses most recent shot."}}},
                {"history_limit", QJsonObject{{"type", "integer"}, {"description", "Number of prior shots with same profile to include (default 5, max 20)"}}}
            }}
        },
        [mainController, shotHistory, settings](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;

            if (!shotHistory || !shotHistory->isReady()) {
                result["error"] = "Shot history not available";
                return result;
            }

            int historyLimit = qBound(1, args["history_limit"].toInt(5), 20);

            // --- 1. Load the target shot ---
            qint64 shotId = args["shot_id"].toInteger(0);
            if (shotId <= 0)
                shotId = shotHistory->lastSavedShotId();

            // If no shot saved this session, query DB for most recent
            if (shotId <= 0) {
                const QString connName2 = QString("mcp_dialing_latest_%1").arg(s_mcpDialConnCounter.fetchAndAddRelaxed(1));
                {
                    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName2);
                    db.setDatabaseName(shotHistory->databasePath());
                    if (db.open()) {
                        QSqlQuery q(db);
                        if (q.exec("SELECT id FROM shots ORDER BY timestamp DESC LIMIT 1") && q.next())
                            shotId = q.value(0).toLongLong();
                    }
                }
                QSqlDatabase::removeDatabase(connName2);
            }

            if (shotId <= 0) {
                result["error"] = "No shots available";
                return result;
            }

            const QString dbPath = shotHistory->databasePath();
            const QString connName = QString("mcp_dialing_%1").arg(s_mcpDialConnCounter.fetchAndAddRelaxed(1));

            QVariantMap shotData;
            QString profileKbId;
            {
                QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
                db.setDatabaseName(dbPath);
                if (db.open()) {
                    ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
                    shotData = ShotHistoryStorage::convertShotRecord(record);
                    profileKbId = record.profileKbId;

                    // --- 2. Load dial-in history (same profile family) ---
                    if (!profileKbId.isEmpty()) {
                        QJsonArray historyArr;
                        QSqlQuery hQuery(db);
                        QString hSql = "SELECT id, timestamp, profile_name, dose_weight, final_weight, "
                                       "duration_seconds, enjoyment, grinder_setting, grinder_model, "
                                       "espresso_notes, bean_brand, bean_type "
                                       "FROM shots WHERE profile_kb_id = '" + profileKbId + "' "
                                       "AND id != " + QString::number(shotId) + " "
                                       "ORDER BY timestamp DESC LIMIT " + QString::number(historyLimit);
                        if (hQuery.exec(hSql)) {
                            while (hQuery.next()) {
                                QJsonObject h;
                                h["id"] = hQuery.value("id").toLongLong();
                                h["timestamp"] = hQuery.value("timestamp").toLongLong();
                                h["profileName"] = hQuery.value("profile_name").toString();
                                h["dose"] = hQuery.value("dose_weight").toDouble();
                                h["yield"] = hQuery.value("final_weight").toDouble();
                                h["duration"] = hQuery.value("duration_seconds").toDouble();
                                h["enjoyment"] = hQuery.value("enjoyment").toInt();
                                h["grinderSetting"] = hQuery.value("grinder_setting").toString();
                                h["grinderModel"] = hQuery.value("grinder_model").toString();
                                h["notes"] = hQuery.value("espresso_notes").toString();
                                h["beanBrand"] = hQuery.value("bean_brand").toString();
                                h["beanType"] = hQuery.value("bean_type").toString();
                                historyArr.append(h);
                            }
                        }
                        result["dialInHistory"] = historyArr;
                    }
                }
            }
            QSqlDatabase::removeDatabase(connName);

            if (shotData.isEmpty()) {
                result["error"] = "Shot not found: " + QString::number(shotId);
                return result;
            }

            // --- Shot summary ---
            result["shotId"] = shotId;
            QJsonObject shotSummary;
            shotSummary["profileName"] = shotData["profileName"].toString();
            shotSummary["dose"] = shotData["doseWeight"].toDouble();
            shotSummary["yield"] = shotData["drinkWeight"].toDouble();
            shotSummary["duration"] = shotData["duration"].toDouble();
            shotSummary["enjoyment"] = shotData["enjoyment"].toInt();
            shotSummary["notes"] = shotData["espressoNotes"].toString();
            shotSummary["beanBrand"] = shotData["beanBrand"].toString();
            shotSummary["beanType"] = shotData["beanType"].toString();
            shotSummary["roastLevel"] = shotData["roastLevel"].toString();
            shotSummary["grinderModel"] = shotData["grinderModel"].toString();
            shotSummary["grinderSetting"] = shotData["grinderSetting"].toString();
            shotSummary["grinderBurrs"] = shotData["grinderBurrs"].toString();
            double dose = shotData["doseWeight"].toDouble();
            double yield = shotData["drinkWeight"].toDouble();
            if (dose > 0)
                shotSummary["ratio"] = QString("1:%1").arg(yield / dose, 0, 'f', 2);
            result["shot"] = shotSummary;

            // --- AI-generated shot analysis ---
            if (mainController && mainController->aiManager()) {
                AIManager* ai = mainController->aiManager();
                QString analysis = ai->generateHistoryShotSummary(shotData);
                if (!analysis.isEmpty())
                    result["shotAnalysis"] = analysis;
            }

            // --- 3. Profile knowledge ---
            QString profileTitle = shotData["profileName"].toString();
            QString profileKnowledge = ShotSummarizer::shotAnalysisSystemPrompt(
                "espresso", profileTitle, QString(), profileKbId);
            if (!profileKnowledge.isEmpty())
                result["profileKnowledge"] = profileKnowledge;

            // --- 4. Bean/grinder metadata (current DYE settings) ---
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
                bean["doseWeight"] = settings->dyeBeanWeight();
                result["currentBean"] = bean;
            }

            // --- 5. Current profile info ---
            if (mainController) {
                QJsonObject profileInfo;
                profileInfo["filename"] = mainController->currentProfileName();
                profileInfo["targetWeight"] = mainController->profileTargetWeight();
                profileInfo["targetTemperature"] = mainController->profileTargetTemperature();
                if (mainController->profileHasRecommendedDose())
                    profileInfo["recommendedDose"] = mainController->profileRecommendedDose();
                result["currentProfile"] = profileInfo;
            }

            // --- 6. Dial-in reference tables ---
            QFile refFile("docs/ESPRESSO_DIAL_IN_REFERENCE.md");
            if (refFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                result["referenceGuide"] = QString::fromUtf8(refFile.readAll());
                refFile.close();
            }

            // --- 7. Full profile knowledge base (all profiles) ---
            QFile kbFile("docs/PROFILE_KNOWLEDGE_BASE.md");
            if (kbFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                result["profileKnowledgeBase"] = QString::fromUtf8(kbFile.readAll());
                kbFile.close();
            }

            return result;
        },
        "read");
}
