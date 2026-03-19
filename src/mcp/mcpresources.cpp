#include "mcpserver.h"
#include "mcpresourceregistry.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../controllers/maincontroller.h"
#include "../history/shothistorystorage.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QAtomicInt>

static QAtomicInt s_mcpResConnCounter{0};

void registerMcpResources(McpResourceRegistry* registry, DE1Device* device,
                          MachineState* machineState, MainController* mainController,
                          ShotHistoryStorage* shotHistory)
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
}
