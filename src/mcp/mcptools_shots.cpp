#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../history/shothistorystorage.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QThread>

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
            const QString connName = QString("mcp_shots_list_%1")
                .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);

            QJsonArray shots;
            int totalCount = 0;
            {
                QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
                db.setDatabaseName(dbPath);
                if (db.open()) {
                    QString sql = "SELECT id, created_at, profile_title, bean_weight, drink_weight, "
                                  "duration, espresso_enjoyment, grinder_setting, grinder_model, "
                                  "espresso_notes, bean_brand, bean_type, profile_kb_id "
                                  "FROM shots WHERE 1=1 ";
                    QString countSql = "SELECT COUNT(*) FROM shots WHERE 1=1 ";
                    QStringList conditions;
                    QVariantList bindValues;

                    if (!profileFilter.isEmpty()) {
                        conditions << "profile_title LIKE ?";
                        bindValues << ("%" + profileFilter + "%");
                    }
                    if (!beanFilter.isEmpty()) {
                        conditions << "bean_brand LIKE ?";
                        bindValues << ("%" + beanFilter + "%");
                    }
                    if (minEnjoyment >= 0) {
                        conditions << "espresso_enjoyment >= ?";
                        bindValues << minEnjoyment;
                    }

                    for (const auto& cond : conditions) {
                        sql += " AND " + cond;
                        countSql += " AND " + cond;
                    }
                    sql += " ORDER BY created_at DESC LIMIT ? OFFSET ?";

                    QSqlQuery query(db);
                    query.prepare(sql);
                    for (qsizetype i = 0; i < bindValues.size(); ++i)
                        query.bindValue(static_cast<int>(i), bindValues[i]);
                    query.bindValue(static_cast<int>(bindValues.size()), limit);
                    query.bindValue(static_cast<int>(bindValues.size() + 1), offset);

                    if (query.exec()) {
                        while (query.next()) {
                            QJsonObject shot;
                            shot["id"] = query.value("id").toLongLong();
                            shot["createdAt"] = query.value("created_at").toString();
                            shot["profileName"] = query.value("profile_title").toString();
                            shot["dose"] = query.value("bean_weight").toDouble();
                            shot["yield"] = query.value("drink_weight").toDouble();
                            shot["duration"] = query.value("duration").toDouble();
                            shot["enjoyment"] = query.value("espresso_enjoyment").toInt();
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
                    for (qsizetype i = 0; i < bindValues.size(); ++i)
                        countQuery.bindValue(static_cast<int>(i), bindValues[i]);
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
            const QString connName = QString("mcp_shot_detail_%1")
                .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);

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
            const QString connName = QString("mcp_compare_%1")
                .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);

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
