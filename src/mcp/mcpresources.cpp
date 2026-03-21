// TODO: Move SQL queries and disk I/O (debug log reads) to background thread
// per CLAUDE.md design principle. Current tool handler architecture (synchronous
// QJsonObject return) prevents this. Requires refactoring McpToolHandler to
// support async responses.

#include "mcpserver.h"
#include "mcpresourceregistry.h"
#include "mcptoolregistry.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../controllers/maincontroller.h"
#include "../history/shothistorystorage.h"
#include "../core/memorymonitor.h"
#include "../network/webdebuglogger.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QAtomicInt>

static QAtomicInt s_mcpResConnCounter{0};

void registerMcpResources(McpResourceRegistry* registry, DE1Device* device,
                          MachineState* machineState, MainController* mainController,
                          ShotHistoryStorage* shotHistory, MemoryMonitor* memoryMonitor)
{
    // decenza://machine/state
    registry->registerResource(
        "decenza://machine/state",
        "Machine State",
        "Current phase, connection status, and water level",
        "application/json",
        [device, machineState]() -> QJsonObject {
            QJsonObject result;
            if (machineState) {
                result["phase"] = machineState->phaseString();
                result["isHeating"] = machineState->isHeating();
                result["isReady"] = machineState->isReady();
                result["isFlowing"] = machineState->isFlowing();
            }
            if (device) {
                result["connected"] = device->isConnected();
                result["waterLevelMl"] = device->waterLevelMl();
                result["waterLevelMm"] = device->waterLevelMm();
            }
            return result;
        });

    // decenza://machine/telemetry
    registry->registerResource(
        "decenza://machine/telemetry",
        "Machine Telemetry",
        "Live pressure, flow, temperature, and weight readings",
        "application/json",
        [device, machineState]() -> QJsonObject {
            QJsonObject result;
            if (device) {
                result["pressure"] = device->pressure();
                result["flow"] = device->flow();
                result["temperature"] = device->temperature();
                result["goalPressure"] = device->goalPressure();
                result["goalFlow"] = device->goalFlow();
                result["goalTemperature"] = device->goalTemperature();
            }
            if (machineState) {
                result["scaleWeight"] = machineState->scaleWeight();
                result["scaleFlowRate"] = machineState->scaleFlowRate();
                result["shotTime"] = machineState->shotTime();
            }
            return result;
        });

    // decenza://profiles/active
    registry->registerResource(
        "decenza://profiles/active",
        "Active Profile",
        "Currently loaded profile name and settings",
        "application/json",
        [mainController]() -> QJsonObject {
            QJsonObject result;
            if (mainController) {
                result["filename"] = mainController->currentProfileName();
                result["targetWeight"] = mainController->profileTargetWeight();
                result["targetTemperature"] = mainController->profileTargetTemperature();
            }
            return result;
        });

    // decenza://shots/recent
    registry->registerResource(
        "decenza://shots/recent",
        "Recent Shots",
        "Last 10 shots with summary data",
        "application/json",
        [shotHistory]() -> QJsonObject {
            QJsonObject result;
            if (!shotHistory || !shotHistory->isReady()) return result;

            const QString dbPath = shotHistory->databasePath();
            const QString connName = QString("mcp_res_recent_%1").arg(s_mcpResConnCounter.fetchAndAddRelaxed(1));

            QJsonArray shots;
            {
                QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
                db.setDatabaseName(dbPath);
                if (db.open()) {
                    QSqlQuery query(db);
                    if (query.exec("SELECT id, timestamp, profile_name, dose_weight, final_weight, "
                                   "duration_seconds, enjoyment, bean_brand, bean_type "
                                   "FROM shots ORDER BY timestamp DESC LIMIT 10")) {
                        while (query.next()) {
                            QJsonObject shot;
                            shot["id"] = query.value("id").toLongLong();
                            shot["timestamp"] = query.value("timestamp").toLongLong();
                            shot["profileName"] = query.value("profile_name").toString();
                            shot["dose"] = query.value("dose_weight").toDouble();
                            shot["yield"] = query.value("final_weight").toDouble();
                            shot["duration"] = query.value("duration_seconds").toDouble();
                            shot["enjoyment"] = query.value("enjoyment").toInt();
                            shot["beanBrand"] = query.value("bean_brand").toString();
                            shot["beanType"] = query.value("bean_type").toString();
                            shots.append(shot);
                        }
                    }
                }
            }
            QSqlDatabase::removeDatabase(connName);

            result["shots"] = shots;
            result["count"] = shots.size();
            return result;
        });

    // decenza://profiles/list
    registry->registerResource(
        "decenza://profiles/list",
        "All Profiles",
        "List of all available profiles",
        "application/json",
        [mainController]() -> QJsonObject {
            QJsonObject result;
            if (!mainController) return result;

            QJsonArray profiles;
            for (const QVariant& v : mainController->availableProfiles()) {
                QVariantMap pm = v.toMap();
                QJsonObject p;
                p["filename"] = pm["name"].toString();
                p["title"] = pm["title"].toString();
                profiles.append(p);
            }
            result["profiles"] = profiles;
            result["count"] = profiles.size();
            return result;
        });

    // decenza://debug/log
    registry->registerResource(
        "decenza://debug/log",
        "Debug Log",
        "Full persisted debug log with memory snapshot (survives crashes)",
        "application/json",
        [memoryMonitor]() -> QJsonObject {
            QJsonObject result;
            auto* logger = WebDebugLogger::instance();
            QString log = logger ? logger->getPersistedLog() : QString();
            if (memoryMonitor)
                log += memoryMonitor->toSummaryString();
            result["log"] = log;
            result["path"] = logger ? logger->logFilePath() : QString();
            result["lineCount"] = log.count('\n');
            return result;
        });

    // decenza://debug/memory
    registry->registerResource(
        "decenza://debug/memory",
        "Memory Stats",
        "Current RSS, peak RSS, QObject count, and recent memory samples",
        "application/json",
        [memoryMonitor]() -> QJsonObject {
            if (!memoryMonitor) return QJsonObject();
            return memoryMonitor->toJson();
        });
}

void registerDebugTools(McpToolRegistry* registry, MemoryMonitor* memoryMonitor)
{
    // debug_get_log — chunked access to the persisted debug log with session awareness
    registry->registerTool(
        "debug_get_log",
        "Read the persisted debug log. Supports three modes: "
        "(1) sessions=true: list all sessions with index, start line, timestamp, and line count. "
        "(2) session=N: return lines from session N (-1=most recent, -2=previous, 0=first). "
        "Combine with offset/limit for pagination within a session. "
        "(3) Default: raw line-based pagination with offset/limit.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"offset", QJsonObject{
                    {"type", "integer"},
                    {"description", "Line number to start from (0-based, or relative within session). Default: 0"}
                }},
                {"limit", QJsonObject{
                    {"type", "integer"},
                    {"description", "Maximum lines to return (1-2000). Default: 500"}
                }},
                {"sessions", QJsonObject{
                    {"type", "boolean"},
                    {"description", "If true, return a list of sessions instead of log lines"}
                }},
                {"session", QJsonObject{
                    {"type", "integer"},
                    {"description", "Return lines from this session only. Negative indexes count from end (-1=most recent)"}
                }}
            }}
        },
        [memoryMonitor](const QJsonObject& args) -> QJsonObject {
            auto* logger = WebDebugLogger::instance();
            if (!logger) {
                return QJsonObject{{"error", "Debug logger not available"}};
            }

            static const QString sessionMarker = QStringLiteral("========== SESSION START:");

            // For sessions or session mode, we need to scan the full log for session boundaries
            if (args.contains("sessions") || args.contains("session")) {
                // Read all lines to find session boundaries
                qsizetype totalLines = 0;
                QStringList allLines = logger->getPersistedLogChunk(0, 100000, &totalLines);

                // Find session start lines
                struct SessionInfo {
                    qsizetype startLine;
                    QString timestamp;
                    qsizetype lineCount; // filled in after scan
                };
                QList<SessionInfo> sessions;

                for (qsizetype i = 0; i < allLines.size(); ++i) {
                    if (allLines[i].contains(sessionMarker)) {
                        // Extract timestamp from "========== SESSION START: 2026-03-20T... =========="
                        QString ts;
                        qsizetype tsStart = allLines[i].indexOf(sessionMarker) + sessionMarker.size();
                        qsizetype tsEnd = allLines[i].indexOf(QStringLiteral("=========="), tsStart);
                        if (tsEnd > tsStart)
                            ts = allLines[i].mid(tsStart, tsEnd - tsStart).trimmed();
                        sessions.append({i, ts, 0});
                    }
                }

                // Calculate line counts
                for (qsizetype i = 0; i < sessions.size(); ++i) {
                    qsizetype nextStart = (i + 1 < sessions.size()) ? sessions[i + 1].startLine : totalLines;
                    sessions[i].lineCount = nextStart - sessions[i].startLine;
                }

                // Mode 1: list sessions
                if (args["sessions"].toBool()) {
                    QJsonArray sessionList;
                    for (qsizetype i = 0; i < sessions.size(); ++i) {
                        QJsonObject s;
                        s["index"] = static_cast<int>(i);
                        s["negativeIndex"] = static_cast<int>(i - sessions.size());
                        s["startLine"] = static_cast<int>(sessions[i].startLine);
                        s["lineCount"] = static_cast<int>(sessions[i].lineCount);
                        s["timestamp"] = sessions[i].timestamp;
                        sessionList.append(s);
                    }
                    return QJsonObject{
                        {"sessions", sessionList},
                        {"sessionCount", static_cast<int>(sessions.size())},
                        {"totalLines", static_cast<int>(totalLines)}
                    };
                }

                // Mode 2: return lines from a specific session
                qsizetype sessionIdx = static_cast<qsizetype>(args["session"].toInt(0));
                if (sessionIdx < 0)
                    sessionIdx = sessions.size() + sessionIdx;
                if (sessionIdx < 0 || sessionIdx >= sessions.size()) {
                    return QJsonObject{{"error", "Session index out of range"},
                                       {"sessionCount", static_cast<int>(sessions.size())}};
                }

                qsizetype sessStart = sessions[sessionIdx].startLine;
                qsizetype sessLines = sessions[sessionIdx].lineCount;
                qsizetype offset = qMax(qsizetype(0), static_cast<qsizetype>(args["offset"].toInt(0)));
                qsizetype limit = qBound(qsizetype(1), static_cast<qsizetype>(args["limit"].toInt(500)), qsizetype(2000));

                // Clamp to session bounds
                qsizetype absStart = sessStart + offset;
                qsizetype absEnd = qMin(absStart + limit, sessStart + sessLines);
                QStringList sessionLines;
                for (qsizetype i = absStart; i < absEnd && i < allLines.size(); ++i)
                    sessionLines.append(allLines[i]);

                QJsonObject result;
                result["session"] = static_cast<int>(sessionIdx);
                result["sessionTimestamp"] = sessions[sessionIdx].timestamp;
                result["offset"] = static_cast<int>(offset);
                result["limit"] = static_cast<int>(limit);
                result["sessionLines"] = static_cast<int>(sessLines);
                result["returnedLines"] = static_cast<int>(sessionLines.size());
                result["hasMore"] = (offset + sessionLines.size()) < sessLines;
                result["log"] = sessionLines.join('\n');

                if (!result["hasMore"].toBool() && memoryMonitor)
                    result["memorySummary"] = memoryMonitor->toSummaryString();

                return result;
            }

            // Mode 3: raw offset/limit (original behavior)
            qsizetype offset = qMax(qsizetype(0), static_cast<qsizetype>(args["offset"].toInt(0)));
            qsizetype limit = qBound(qsizetype(1), static_cast<qsizetype>(args["limit"].toInt(500)), qsizetype(2000));
            qsizetype totalLines = 0;

            QStringList lines = logger->getPersistedLogChunk(offset, limit, &totalLines);

            QJsonObject result;
            result["offset"] = static_cast<int>(offset);
            result["limit"] = static_cast<int>(limit);
            result["totalLines"] = static_cast<int>(totalLines);
            result["returnedLines"] = static_cast<int>(lines.size());
            result["hasMore"] = (offset + lines.size()) < totalLines;
            result["log"] = lines.join('\n');

            // Append memory summary if this is the last chunk
            if (!result["hasMore"].toBool() && memoryMonitor) {
                result["memorySummary"] = memoryMonitor->toSummaryString();
            }

            return result;
        },
        "read");
}
