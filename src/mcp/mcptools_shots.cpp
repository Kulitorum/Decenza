// TODO: Move SQL queries to background thread per CLAUDE.md design principle.
// Current tool handler architecture (synchronous QJsonObject return) prevents this.
// Requires refactoring McpToolHandler to support async responses.

#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../history/shothistorystorage.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QThread>
#include <QAtomicInt>

static QAtomicInt s_mcpShotConnCounter{0};

void registerShotTools(McpToolRegistry* registry, ShotHistoryStorage* shotHistory)
{
    // shots_list
    registry->registerTool(
        "shots_list",
        "List recent shots with optional filters. Returns summary data (no time-series).",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"limit", QJsonObject{{"type", "integer"}, {"description", "Max shots to return (default 20, max 100)"}}},
                {"offset", QJsonObject{{"type", "integer"}, {"description", "Offset for pagination"}}},
                {"profileName", QJsonObject{{"type", "string"}, {"description", "Filter by profile name (substring match)"}}},
                {"beanBrand", QJsonObject{{"type", "string"}, {"description", "Filter by bean brand"}}},
                {"minEnjoyment", QJsonObject{{"type", "integer"}, {"description", "Minimum enjoyment rating (0-100)"}}}
            }}
        },
        [shotHistory](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!shotHistory || !shotHistory->isReady()) {
                result["error"] = "Shot history not available";
                return result;
            }

            int limit = qBound(1, args["limit"].toInt(20), 100);
            int offset = qMax(0, args["offset"].toInt(0));
            QString profileFilter = args["profileName"].toString();
            QString beanFilter = args["beanBrand"].toString();
            int minEnjoyment = args["minEnjoyment"].toInt(-1);

            const QString dbPath = shotHistory->databasePath();
            const QString connName = QString("mcp_shots_list_%1").arg(s_mcpShotConnCounter.fetchAndAddRelaxed(1));

            QJsonArray shots;
            int totalCount = 0;
            {
                QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
                db.setDatabaseName(dbPath);
                if (db.open()) {
                    QString sql = "SELECT id, timestamp, profile_name, dose_weight, final_weight, "
                                  "duration_seconds, enjoyment, grinder_setting, grinder_model, "
                                  "espresso_notes, bean_brand, bean_type, profile_kb_id "
                                  "FROM shots WHERE 1=1 ";
                    QString countSql = "SELECT COUNT(*) FROM shots WHERE 1=1 ";

                    if (!profileFilter.isEmpty()) {
                        sql += " AND profile_name LIKE :profileFilter";
                        countSql += " AND profile_name LIKE :profileFilter";
                    }
                    if (!beanFilter.isEmpty()) {
                        sql += " AND bean_brand LIKE :beanFilter";
                        countSql += " AND bean_brand LIKE :beanFilter";
                    }
                    if (minEnjoyment >= 0) {
                        sql += " AND enjoyment >= :minEnjoyment";
                        countSql += " AND enjoyment >= :minEnjoyment";
                    }
                    sql += " ORDER BY timestamp DESC LIMIT " + QString::number(limit) + " OFFSET " + QString::number(offset);

                    QSqlQuery query(db);
                    query.prepare(sql);
                    if (!profileFilter.isEmpty())
                        query.bindValue(":profileFilter", "%" + profileFilter + "%");
                    if (!beanFilter.isEmpty())
                        query.bindValue(":beanFilter", "%" + beanFilter + "%");
                    if (minEnjoyment >= 0)
                        query.bindValue(":minEnjoyment", minEnjoyment);

                    if (query.exec()) {
                        while (query.next()) {
                            QJsonObject shot;
                            shot["id"] = query.value("id").toLongLong();
                            shot["timestamp"] = query.value("timestamp").toLongLong();
                            shot["profileName"] = query.value("profile_name").toString();
                            shot["dose"] = query.value("dose_weight").toDouble();
                            shot["yield"] = query.value("final_weight").toDouble();
                            shot["duration"] = query.value("duration_seconds").toDouble();
                            shot["enjoyment"] = query.value("enjoyment").toInt();
                            shot["grinderSetting"] = query.value("grinder_setting").toString();
                            shot["grinderModel"] = query.value("grinder_model").toString();
                            shot["notes"] = query.value("espresso_notes").toString();
                            shot["beanBrand"] = query.value("bean_brand").toString();
                            shot["beanType"] = query.value("bean_type").toString();
                            shots.append(shot);
                        }
                    }

                    QSqlQuery countQuery(db);
                    countQuery.prepare(countSql);
                    if (!profileFilter.isEmpty())
                        countQuery.bindValue(":profileFilter", "%" + profileFilter + "%");
                    if (!beanFilter.isEmpty())
                        countQuery.bindValue(":beanFilter", "%" + beanFilter + "%");
                    if (minEnjoyment >= 0)
                        countQuery.bindValue(":minEnjoyment", minEnjoyment);
                    if (countQuery.exec() && countQuery.next())
                        totalCount = countQuery.value(0).toInt();
                }
            }
            QSqlDatabase::removeDatabase(connName);

            result["shots"] = shots;
            result["count"] = shots.size();
            result["total"] = totalCount;
            result["offset"] = offset;
            return result;
        },
        "read");

    // shots_get_detail
    registry->registerTool(
        "shots_get_detail",
        "Get full shot record including time-series data (pressure, flow, temperature, weight curves)",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shotId", QJsonObject{{"type", "integer"}, {"description", "Shot ID"}}}
            }},
            {"required", QJsonArray{"shotId"}}
        },
        [shotHistory](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!shotHistory || !shotHistory->isReady()) {
                result["error"] = "Shot history not available";
                return result;
            }

            qint64 shotId = args["shotId"].toInteger();
            if (shotId <= 0) {
                result["error"] = "Valid shotId is required";
                return result;
            }

            const QString dbPath = shotHistory->databasePath();
            const QString connName = QString("mcp_shot_detail_%1").arg(s_mcpShotConnCounter.fetchAndAddRelaxed(1));

            {
                QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
                db.setDatabaseName(dbPath);
                if (db.open()) {
                    ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
                    QVariantMap shotMap = ShotHistoryStorage::convertShotRecord(record);
                    if (!shotMap.isEmpty()) {
                        result = QJsonObject::fromVariantMap(shotMap);
                    } else {
                        result["error"] = "Shot not found: " + QString::number(shotId);
                    }
                }
            }
            QSqlDatabase::removeDatabase(connName);

            return result;
        },
        "read");

    // shots_compare
    registry->registerTool(
        "shots_compare",
        "Side-by-side comparison of 2 or more shots. Returns summary data for each shot.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shotIds", QJsonObject{
                    {"type", "array"},
                    {"items", QJsonObject{{"type", "integer"}}},
                    {"description", "Array of shot IDs to compare (2-10)"}
                }}
            }},
            {"required", QJsonArray{"shotIds"}}
        },
        [shotHistory](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!shotHistory || !shotHistory->isReady()) {
                result["error"] = "Shot history not available";
                return result;
            }

            QJsonArray idArray = args["shotIds"].toArray();
            if (idArray.size() < 2 || idArray.size() > 10) {
                result["error"] = "Provide 2-10 shot IDs for comparison";
                return result;
            }

            const QString dbPath = shotHistory->databasePath();
            const QString connName = QString("mcp_compare_%1").arg(s_mcpShotConnCounter.fetchAndAddRelaxed(1));

            QJsonArray shots;
            {
                QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
                db.setDatabaseName(dbPath);
                if (db.open()) {
                    for (const auto& idVal : idArray) {
                        qint64 shotId = idVal.toInteger();
                        ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
                        QVariantMap shotMap = ShotHistoryStorage::convertShotRecord(record);
                        if (!shotMap.isEmpty())
                            shots.append(QJsonObject::fromVariantMap(shotMap));
                    }
                }
            }
            QSqlDatabase::removeDatabase(connName);

            result["shots"] = shots;
            result["count"] = shots.size();
            return result;
        },
        "read");
}
