#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../history/shothistorystorage.h"
#include "../core/dbutils.h"

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

// Drop heavy fields (time-series, debugLog, profileJson) from a shot projection
// JSON object so it fits within typical LLM context windows. The full payload
// for a single shot is ~85K chars (mostly time-series); the summary is ~3K and
// covers every dialing/comparison use case (scalars, phaseSummaries,
// summaryLines, detectorResults, ratings).
static void stripTimeSeriesFields(QJsonObject& obj)
{
    static const char* heavyFields[] = {
        "pressure", "flow", "temperature", "temperatureMix",
        "resistance", "conductance", "darcyResistance", "conductanceDerivative",
        "waterDispensed", "pressureGoal", "flowGoal", "temperatureGoal",
        "weight", "weightFlowRate",
        "debugLog", "profileJson"
    };
    for (const char* key : heavyFields)
        obj.remove(QString::fromLatin1(key));
}

// Strip per-detector implementation-detail blocks (currently `gates`) from
// detectorResults so MCP responses only expose the user-facing scalars the
// detectors commit to externally. Threshold values inside `gates` (e.g.
// chokedFlowMaxMlPerSec) are scratch input/output for the detector and
// invite the LLM to reason about them as if they were dialing parameters.
static void stripDetectorInternals(QJsonObject& obj)
{
    if (!obj.contains("detectorResults"))
        return;
    QJsonObject detectorResults = obj.value("detectorResults").toObject();
    static const char* detectorKeys[] = {
        "grind", "channeling", "flowTrend", "tempStability", "preinfusion"
    };
    for (const char* key : detectorKeys) {
        const QString k = QString::fromLatin1(key);
        if (!detectorResults.contains(k))
            continue;
        QJsonObject d = detectorResults.value(k).toObject();
        d.remove(QStringLiteral("gates"));
        detectorResults[k] = d;
    }
    obj["detectorResults"] = detectorResults;
}

// Resolve the detail argument. Default "summary" — drops time-series, debugLog,
// profileJson. "full" — return the complete projection. Unknown values fall
// back to summary so the LLM gets a usable response rather than the 200K-char
// firehose.
static bool wantsFullDetail(const QJsonObject& args)
{
    return args.value("detail").toString() == QStringLiteral("full");
}

// Replace 0 with null for enjoyment0to100/drinkTdsPct/drinkEyPct so MCP
// consumers can distinguish unrated shots (and shots without TDS/EY
// measurements) from a deliberate zero. The QML UI already does this
// implicitly with `(value || 0) > 0 ? value : "-"` on display; this gives
// the LLM the same signal.
static void nullifyUnratedFields(QJsonObject& obj)
{
    static const char* ratingFields[] = {
        "enjoyment0to100", "drinkTdsPct", "drinkEyPct"
    };
    for (const char* key : ratingFields) {
        const QString k = QString::fromLatin1(key);
        if (!obj.contains(k))
            continue;
        const double v = obj.value(k).toDouble();
        if (v <= 0.0)
            obj[k] = QJsonValue(QJsonValue::Null);
    }
}

void registerShotTools(McpToolRegistry* registry, ShotHistoryStorage* shotHistory)
{
    // shots_list
    registry->registerAsyncTool(
        "shots_list",
        "List recent shots with optional filters. Returns summary data (no time-series).",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"limit", QJsonObject{{"type", "integer"}, {"description", "Max shots to return (default 20, max 100)"}}},
                {"offset", QJsonObject{{"type", "integer"}, {"description", "Offset for pagination"}}},
                {"profileName", QJsonObject{{"type", "string"}, {"description", "Filter by profile name (substring match)"}}},
                {"beanBrand", QJsonObject{{"type", "string"}, {"description", "Filter by bean brand"}}},
                {"minEnjoyment", QJsonObject{{"type", "integer"}, {"description", "Minimum enjoyment rating (1-100, 0 or omit means no filter)"}}},
                {"after", QJsonObject{{"type", "string"}, {"description", "Only shots after this ISO timestamp (e.g. 2026-03-15T00:00:00)"}}},
                {"before", QJsonObject{{"type", "string"}, {"description", "Only shots before this ISO timestamp (e.g. 2026-03-21T23:59:59)"}}}
            }}
        },
        [shotHistory](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Shot history not available"}});
                return;
            }

            // Capture current time on the main thread before spawning background work
            auto now = QDateTime::currentDateTime();
            QString currentDateTime = now.toOffsetFromUtc(now.offsetFromUtc()).toString(Qt::ISODate);

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

            QThread* thread = QThread::create(
                [dbPath, limit, offset, profileFilter, beanFilter,
                 minEnjoyment, afterEpoch, beforeEpoch, currentDateTime, respond]() {
                QJsonObject result;
                QJsonArray shots;
                qint64 totalCount = 0;

                if (!withTempDb(dbPath, "mcp_shots_list", [&](QSqlDatabase& db) {
                    QString sql = "SELECT id, timestamp, profile_name, dose_weight, final_weight, "
                                  "duration_seconds, enjoyment, grinder_setting, grinder_model, "
                                  "espresso_notes, bean_brand, bean_type, yield_override, profile_json "
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
                    if (minEnjoyment > 0) {
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
                    if (minEnjoyment > 0)
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
                            const int enjoyment = query.value("enjoyment").toInt();
                            shot["enjoyment0to100"] = enjoyment > 0 ? QJsonValue(enjoyment) : QJsonValue(QJsonValue::Null);
                            shot["grinderSetting"] = query.value("grinder_setting").toString();
                            shot["grinderModel"] = query.value("grinder_model").toString();
                            shot["notes"] = query.value("espresso_notes").toString();
                            shot["beanBrand"] = query.value("bean_brand").toString();
                            shot["beanType"] = query.value("bean_type").toString();
                            // Use the saved target weight (from yield_override column) if set,
                            // else fall back to the profile snapshot's target_weight.
                            double targetWeight = query.value("yield_override").toDouble();
                            if (targetWeight > 0) {
                                shot["targetWeightG"] = targetWeight;
                            } else {
                                QString profileJson = query.value("profile_json").toString();
                                if (!profileJson.isEmpty()) {
                                    QJsonObject profileObj = QJsonDocument::fromJson(profileJson.toUtf8()).object();
                                    QJsonValue tw = profileObj["target_weight"];
                                    double twVal = tw.isString() ? tw.toString().toDouble() : tw.toDouble();
                                    if (twVal > 0)
                                        shot["targetWeightG"] = twVal;
                                }
                            }
                            shots.append(shot);
                        }
                    }

                    QSqlQuery countQuery(db);
                    countQuery.prepare(countSql);
                    if (!profileFilter.isEmpty())
                        countQuery.bindValue(":profileFilter", "%" + profileFilter + "%");
                    if (!beanFilter.isEmpty())
                        countQuery.bindValue(":beanFilter", "%" + beanFilter + "%");
                    if (minEnjoyment > 0)
                        countQuery.bindValue(":minEnjoyment", minEnjoyment);
                    if (afterEpoch > 0)
                        countQuery.bindValue(":after", afterEpoch);
                    if (beforeEpoch > 0)
                        countQuery.bindValue(":before", beforeEpoch);
                    if (countQuery.exec() && countQuery.next())
                        totalCount = countQuery.value(0).toLongLong();
                })) {
                    result["error"] = "Failed to open shot database";
                }

                if (!result.contains("error")) {
                    result["currentDateTime"] = currentDateTime;
                    result["shots"] = shots;
                    result["count"] = shots.size();
                    result["total"] = totalCount;
                    result["offset"] = offset;
                    const qint64 returned = shots.size();
                    const bool hasMore = (static_cast<qint64>(offset) + returned) < totalCount;
                    result["hasMore"] = hasMore;
                    result["nextOffset"] = hasMore
                        ? QJsonValue(static_cast<qint64>(offset) + returned)
                        : QJsonValue(QJsonValue::Null);

                    // Per MCP 2025-06-18: emit a resource_link block per shot
                    // pointing at decenza://shots/{id} so subscribing clients
                    // can correlate the result with future resource updates.
                    QJsonArray links;
                    for (const QJsonValue& v : std::as_const(shots)) {
                        const QJsonObject s = v.toObject();
                        QJsonObject link;
                        link["uri"] = QStringLiteral("decenza://shots/")
                            + QString::number(s.value("id").toVariant().toLongLong());
                        link["title"] = QStringLiteral("Shot #%1 — %2")
                            .arg(s.value("id").toVariant().toLongLong())
                            .arg(s.value("profileName").toString(QStringLiteral("(unknown profile)")));
                        link["mimeType"] = "application/json";
                        links.append(link);
                    }
                    result["_resourceLinks"] = links;
                }

                QMetaObject::invokeMethod(qApp, [respond, result]() {
                    respond(result);
                }, Qt::QueuedConnection);
            });

            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "read");

    // shots_get_detail
    registry->registerAsyncTool(
        "shots_get_detail",
        "Get a shot record. Default detail='summary' returns scalars, phase summaries, "
        "summary lines, detector results, and ratings (~3K chars). Pass detail='full' to "
        "include time-series curves (pressure, flow, temperature, weight), debug log, "
        "and embedded profile JSON (~85K chars — only useful for curve-aware analysis).",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shotId", QJsonObject{{"type", "integer"}, {"description", "Shot ID"}}},
                {"detail", QJsonObject{
                    {"type", "string"},
                    {"enum", QJsonArray{"summary", "full"}},
                    {"description", "summary (default): omit time-series, debugLog, profileJson. full: include everything."}
                }}
            }},
            {"required", QJsonArray{"shotId"}}
        },
        [shotHistory](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Shot history not available"}});
                return;
            }

            qint64 shotId = args["shotId"].toInteger();
            if (shotId <= 0) {
                respond(QJsonObject{{"error", "Valid shotId is required"}});
                return;
            }

            const bool fullDetail = wantsFullDetail(args);
            const QString dbPath = shotHistory->databasePath();

            QThread* thread = QThread::create([dbPath, shotId, fullDetail, respond]() {
                QJsonObject result;

                if (!withTempDb(dbPath, "mcp_shot_detail", [&](QSqlDatabase& db) {
                    ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
                    ShotProjection shot = ShotHistoryStorage::convertShotRecord(record);
                    if (shot.isValid()) {
                        result = shot.toJsonObject();
                        if (!fullDetail)
                            stripTimeSeriesFields(result);
                        stripDetectorInternals(result);
                        nullifyUnratedFields(result);
                    } else {
                        result["error"] = "Shot not found: " + QString::number(shotId);
                    }
                })) {
                    result["error"] = "Failed to open shot database";
                }

                if (!result.contains("error")) {
                    QJsonObject link;
                    link["uri"] = QStringLiteral("decenza://shots/") + QString::number(shotId);
                    link["title"] = QStringLiteral("Shot #%1").arg(shotId);
                    link["mimeType"] = "application/json";
                    result["_resourceLinks"] = QJsonArray{ link };
                }

                QMetaObject::invokeMethod(qApp, [respond, result]() {
                    respond(result);
                }, Qt::QueuedConnection);
            });

            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "read");

    // shots_compare
    registry->registerAsyncTool(
        "shots_compare",
        "Side-by-side comparison of 2 or more shots. Default detail='summary' returns "
        "scalars + phase summaries per shot plus a changes diff between consecutive shots "
        "(~3K chars/shot). When all shots share a profile, profileName/profileKbId/"
        "profileNotes are hoisted to a top-level `sharedProfile` block and omitted from "
        "each shot. Pass detail='full' to include time-series curves and debug logs "
        "(~85K chars/shot — exceeds typical LLM context with more than 1-2 shots).",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shotIds", QJsonObject{
                    {"type", "array"},
                    {"items", QJsonObject{{"type", "integer"}}},
                    {"description", "Array of shot IDs to compare (2-10)"}
                }},
                {"detail", QJsonObject{
                    {"type", "string"},
                    {"enum", QJsonArray{"summary", "full"}},
                    {"description", "summary (default): omit time-series, debugLog, profileJson per shot. full: include everything."}
                }}
            }},
            {"required", QJsonArray{"shotIds"}}
        },
        [shotHistory](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Shot history not available"}});
                return;
            }

            QJsonArray idArray = args["shotIds"].toArray();
            if (idArray.size() < 2 || idArray.size() > 10) {
                respond(QJsonObject{{"error", "Provide 2-10 shot IDs for comparison"}});
                return;
            }

            const bool fullDetail = wantsFullDetail(args);
            const QString dbPath = shotHistory->databasePath();

            QThread* thread = QThread::create([dbPath, idArray, fullDetail, respond]() {
                QJsonObject result;
                QJsonArray shots;
                QList<ShotProjection> projections;

                if (!withTempDb(dbPath, "mcp_compare", [&](QSqlDatabase& db) {
                    for (const auto& idVal : idArray) {
                        qint64 shotId = idVal.toInteger();
                        ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
                        ShotProjection shot = ShotHistoryStorage::convertShotRecord(record);
                        if (shot.isValid()) {
                            QJsonObject shotJson = shot.toJsonObject();
                            if (!fullDetail)
                                stripTimeSeriesFields(shotJson);
                            stripDetectorInternals(shotJson);
                            nullifyUnratedFields(shotJson);
                            shots.append(shotJson);
                            projections.append(std::move(shot));
                        }
                    }
                })) {
                    result["error"] = "Failed to open shot database";
                }

                if (!result.contains("error")) {
                    // Dedupe shared profile metadata. Comparing dial-in
                    // iterations on a single recipe is the common case, and
                    // profileNotes is ~700 chars per shot — hoisting it to
                    // sharedProfile saves ~20% of payload at N=2 and scales
                    // with N. When shots span multiple profiles, leave the
                    // per-shot fields in place.
                    if (shots.size() >= 2) {
                        const QJsonObject first = shots.first().toObject();
                        const QString sharedName = first.value("profileName").toString();
                        const QString sharedKbId = first.value("profileKbId").toString();
                        const QString sharedNotes = first.value("profileNotes").toString();
                        bool allShare = !sharedName.isEmpty();
                        for (const QJsonValue& v : std::as_const(shots)) {
                            const QJsonObject s = v.toObject();
                            if (s.value("profileName").toString() != sharedName ||
                                s.value("profileKbId").toString() != sharedKbId ||
                                s.value("profileNotes").toString() != sharedNotes) {
                                allShare = false;
                                break;
                            }
                        }
                        if (allShare) {
                            QJsonArray dedupedShots;
                            for (const QJsonValue& v : std::as_const(shots)) {
                                QJsonObject s = v.toObject();
                                s.remove("profileNotes");
                                s.remove("profileKbId");
                                dedupedShots.append(s);
                            }
                            shots = dedupedShots;
                            QJsonObject sharedProfile;
                            sharedProfile["profileName"] = sharedName;
                            if (!sharedKbId.isEmpty())
                                sharedProfile["profileKbId"] = sharedKbId;
                            if (!sharedNotes.isEmpty())
                                sharedProfile["profileNotes"] = sharedNotes;
                            result["sharedProfile"] = sharedProfile;
                        }
                    }
                    result["shots"] = shots;
                    result["count"] = shots.size();
                }

                // Compute changes between consecutive shots. Field-pointer
                // accessors keep the diff loop compile-time-safe — renaming
                // ShotProjection::doseWeightG turns the diffNum() call into a
                // compile error rather than a silent missed-rename bug. The
                // outKey strings are the MCP-facing schema (doseG, yieldG,
                // enjoyment0to100), distinct from the projection's field names.
                if (projections.size() >= 2) {
                    QJsonArray changes;
                    for (qsizetype i = 1; i < projections.size(); ++i) {
                        const ShotProjection& prev = projections[i-1];
                        const ShotProjection& curr = projections[i];
                        QJsonObject diff;
                        diff["fromShotId"] = prev.id;
                        diff["toShotId"] = curr.id;

                        auto diffStr = [&](QString ShotProjection::*field, const QString& outKey) {
                            const QString& a = prev.*field;
                            const QString& b = curr.*field;
                            if (!a.isEmpty() && !b.isEmpty() && a != b)
                                diff[outKey] = QString("%1 -> %2").arg(a, b);
                        };
                        auto diffNum = [&](double a, double b, const QString& outKey, const QString& unit) {
                            if (a != 0 && b != 0 && qAbs(a - b) > 0.01)
                                diff[outKey] = QString("%1 -> %2 %3 (%4%5)")
                                    .arg(a, 0, 'f', 1).arg(b, 0, 'f', 1).arg(unit)
                                    .arg(b > a ? "+" : "").arg(b - a, 0, 'f', 1);
                        };

                        diffStr(&ShotProjection::grinderSetting, "grinderSetting");
                        diffStr(&ShotProjection::profileName, "profileName");
                        diffStr(&ShotProjection::beanBrand, "beanBrand");
                        diffNum(prev.doseWeightG, curr.doseWeightG, "doseG", "g");
                        diffNum(prev.finalWeightG, curr.finalWeightG, "yieldG", "g");
                        diffNum(prev.durationSec, curr.durationSec, "durationSec", "s");
                        diffNum(prev.enjoyment0to100, curr.enjoyment0to100, "enjoyment0to100", "");

                        if (diff.size() > 2)
                            changes.append(diff);
                    }
                    if (!changes.isEmpty())
                        result["changes"] = changes;
                }

                QMetaObject::invokeMethod(qApp, [respond, result]() {
                    respond(result);
                }, Qt::QueuedConnection);
            });

            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "read");

    // shots_get_debug_log — read the per-shot debug log with pagination
    registry->registerAsyncTool(
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
        [shotHistory](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Shot history not available"}});
                return;
            }

            qint64 shotId = args["shotId"].toInteger();
            if (shotId <= 0) {
                respond(QJsonObject{{"error", "Valid shotId is required"}});
                return;
            }

            qsizetype offset = qMax(qsizetype(0), static_cast<qsizetype>(args["offset"].toInt(0)));
            qsizetype limit = qBound(qsizetype(1), static_cast<qsizetype>(args["limit"].toInt(500)), qsizetype(2000));

            const QString dbPath = shotHistory->databasePath();

            QThread* thread = QThread::create([dbPath, shotId, offset, limit, respond]() {
                QJsonObject result;

                if (!withTempDb(dbPath, "mcp_shot_debug", [&](QSqlDatabase& db) {
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
                })) {
                    if (!result.contains("error"))
                        result["error"] = "Failed to open shot database";
                }

                QMetaObject::invokeMethod(qApp, [respond, result]() {
                    respond(result);
                }, Qt::QueuedConnection);
            });

            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "read");
}
