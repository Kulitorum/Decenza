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
    // debug_get_log — chunked access to the persisted debug log
    registry->registerTool(
        "debug_get_log",
        "Read the persisted debug log in chunks. Returns lines from offset to offset+limit. "
        "Use offset=0, limit=500 to start, then increment offset to page through. "
        "Also returns totalLines so you know when you've reached the end.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"offset", QJsonObject{
                    {"type", "integer"},
                    {"description", "Line number to start from (0-based). Default: 0"}
                }},
                {"limit", QJsonObject{
                    {"type", "integer"},
                    {"description", "Maximum lines to return (1-2000). Default: 500"}
                }}
            }}
        },
        [memoryMonitor](const QJsonObject& args) -> QJsonObject {
            auto* logger = WebDebugLogger::instance();
            if (!logger) {
                return QJsonObject{{"error", "Debug logger not available"}};
            }

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
