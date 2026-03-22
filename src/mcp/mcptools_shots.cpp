// TODO: Move SQL queries to background thread per CLAUDE.md design principle.
// Current tool handler architecture (synchronous QJsonObject return) prevents this.
// Requires refactoring McpToolHandler to support async responses.

#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../history/shothistorystorage.h"

#include <QDateTime>
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
                {"minEnjoyment", QJsonObject{{"type", "integer"}, {"description", "Minimum enjoyment rating (0-100)"}}},
                {"after", QJsonObject{{"type", "string"}, {"description", "Only shots after this ISO timestamp (e.g. 2026-03-15T00:00:00)"}}},
                {"before", QJsonObject{{"type", "string"}, {"description", "Only shots before this ISO timestamp (e.g. 2026-03-21T23:59:59)"}}}
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
            qint64 afterEpoch = 0, beforeEpoch = 0;
            if (args.contains("after")) {
                QDateTime dt = QDateTime::fromString(args["after"].toString(), Qt::ISODate);
                if (dt.isValid()) afterEpoch = dt.toSecsSinceEpoch();
            }
            if (args.contains("before")) {
                QDateTime dt = QDateTime::fromString(args["before"].toString(), Qt::ISODate);
                if (dt.isValid()) beforeEpoch = dt.toSecsSinceEpoch();
            }

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
                    if (afterEpoch > 0) {
                        sql += " AND timestamp >= :after";
                        countSql += " AND timestamp >= :after";
                    }
                    if (beforeEpoch > 0) {
                        sql += " AND timestamp <= :before";
                        countSql += " AND timestamp <= :before";
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
                    if (afterEpoch > 0)
                        query.bindValue(":after", afterEpoch);
                    if (beforeEpoch > 0)
                        query.bindValue(":before", beforeEpoch);

                    if (query.exec()) {
                        while (query.next()) {
                            QJsonObject shot;
                            shot["id"] = query.value("id").toLongLong();
                            auto dt = QDateTime::fromSecsSinceEpoch(query.value("timestamp").toLongLong());
                            shot["timestamp"] = dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODate);
                            shot["profileName"] = query.value("profile_name").toString();
                            shot["doseG"] = query.value("dose_weight").toDouble();
                            shot["yieldG"] = query.value("final_weight").toDouble();
                            shot["durationSec"] = query.value("duration_seconds").toDouble();
                            shot["enjoyment0to100"] = query.value("enjoyment").toInt();
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
                    if (afterEpoch > 0)
                        countQuery.bindValue(":after", afterEpoch);
                    if (beforeEpoch > 0)
                        countQuery.bindValue(":before", beforeEpoch);
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

            // Compute changes between consecutive shots
            if (shots.size() >= 2) {
                QJsonArray changes;
                for (qsizetype i = 1; i < shots.size(); ++i) {
                    QJsonObject prev = shots[i-1].toObject();
                    QJsonObject curr = shots[i].toObject();
                    QJsonObject diff;
                    diff["fromShotId"] = prev["id"];
                    diff["toShotId"] = curr["id"];

                    // Compare key dial-in fields
                    auto diffStr = [&](const QString& key) {
                        QString a = prev[key].toString(), b = curr[key].toString();
                        if (!a.isEmpty() && !b.isEmpty() && a != b)
                            diff[key] = QString("%1 -> %2").arg(a, b);
                    };
                    auto diffNum = [&](const QString& key, const QString& unit) {
                        double a = prev[key].toDouble(), b = curr[key].toDouble();
                        if (a != 0 && b != 0 && qAbs(a - b) > 0.01)
                            diff[key] = QString("%1 -> %2 %3 (%4%5)")
                                .arg(a, 0, 'f', 1).arg(b, 0, 'f', 1).arg(unit)
                                .arg(b > a ? "+" : "").arg(b - a, 0, 'f', 1);
                    };

                    diffStr("grinderSetting");
                    diffStr("profileName");
                    diffStr("beanBrand");
                    diffNum("doseWeight", "g");
                    diffNum("finalWeight", "g");
                    diffNum("duration", "s");
                    diffNum("enjoyment", "");

                    // Only include if something changed
                    if (diff.size() > 2) // more than just fromShotId/toShotId
                        changes.append(diff);
                }
                if (!changes.isEmpty())
                    result["changes"] = changes;
            }

            return result;
        },
        "read");

    // shots_get_debug_log — read the per-shot debug log with pagination
    registry->registerTool(
        "shots_get_debug_log",
        "Read the debug log captured during a shot extraction. Contains BLE frames, "
        "phase transitions, stop-at-weight events, flow calibration, and all qDebug output "
        "from the shot. Supports pagination for large logs.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shotId", QJsonObject{{"type", "integer"}, {"description", "Shot ID"}}},
                {"offset", QJsonObject{{"type", "integer"}, {"description", "Line number to start from (0-based). Default: 0"}}},
                {"limit", QJsonObject{{"type", "integer"}, {"description", "Maximum lines to return (1-2000). Default: 500"}}}
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

            qsizetype offset = qMax(qsizetype(0), static_cast<qsizetype>(args["offset"].toInt(0)));
            qsizetype limit = qBound(qsizetype(1), static_cast<qsizetype>(args["limit"].toInt(500)), qsizetype(2000));

            const QString dbPath = shotHistory->databasePath();
            const QString connName = QString("mcp_shot_debug_%1").arg(s_mcpShotConnCounter.fetchAndAddRelaxed(1));

            {
                QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
                db.setDatabaseName(dbPath);
                if (db.open()) {
                    QSqlQuery query(db);
                    query.prepare("SELECT debug_log FROM shots WHERE id = ?");
                    query.addBindValue(shotId);
                    if (query.exec() && query.next()) {
                        QString debugLog = query.value(0).toString();
                        if (debugLog.isEmpty()) {
                            result["error"] = "No debug log for shot " + QString::number(shotId);
                        } else {
                            QStringList allLines = debugLog.split('\n');
                            qsizetype totalLines = allLines.size();

                            QStringList chunk;
                            for (qsizetype i = offset; i < qMin(offset + limit, totalLines); ++i)
                                chunk.append(allLines[i]);

                            result["shotId"] = shotId;
                            result["offsetLines"] = static_cast<int>(offset);
                            result["limitLines"] = static_cast<int>(limit);
                            result["totalLines"] = static_cast<int>(totalLines);
                            result["returnedLines"] = static_cast<int>(chunk.size());
                            result["hasMore"] = (offset + chunk.size()) < totalLines;
                            result["log"] = chunk.join('\n');
                        }
                    } else {
                        result["error"] = "Shot not found: " + QString::number(shotId);
                    }
                } else {
                    result["error"] = "Failed to open shot database";
                }
            }
            QSqlDatabase::removeDatabase(connName);

            return result;
        },
        "read");
}
