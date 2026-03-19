#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../models/shotdatamodel.h"
#include "../controllers/maincontroller.h"

#include <QJsonObject>
#include <QJsonArray>

void registerMachineTools(McpToolRegistry* registry, DE1Device* device,
                          MachineState* machineState, MainController* mainController)
{
    // machine_get_state
    registry->registerTool(
        "machine_get_state",
        "Get current machine state: phase, connection status, readiness, heating, water level, firmware version",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [device, machineState](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (machineState) {
                result["phase"] = machineState->phaseString();
                result["isHeating"] = machineState->isHeating();
                result["isReady"] = machineState->isReady();
                result["isFlowing"] = machineState->isFlowing();
                result["shotTime"] = machineState->shotTime();
                result["targetWeight"] = machineState->targetWeight();
                result["targetVolume"] = machineState->targetVolume();
                result["scaleWeight"] = machineState->scaleWeight();
            }
            if (device) {
                result["connected"] = device->isConnected();
                result["stateString"] = device->stateString();
                result["waterLevelMl"] = device->waterLevelMl();
                result["waterLevelMm"] = device->waterLevelMm();
                result["firmwareVersion"] = device->firmwareVersion();
                result["isHeadless"] = device->isHeadless();
                result["pressure"] = device->pressure();
                result["temperature"] = device->temperature();
                result["steamTemperature"] = device->steamTemperature();
            }
            return result;
        },
        "read");

    // machine_get_telemetry
    registry->registerTool(
        "machine_get_telemetry",
        "Get live telemetry: pressure, flow, temperature, weight, goal values. During a shot, also returns time-series data so far.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [device, machineState, mainController](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (device) {
                result["pressure"] = device->pressure();
                result["flow"] = device->flow();
                result["temperature"] = device->temperature();
                result["mixTemperature"] = device->mixTemperature();
                result["steamTemperature"] = device->steamTemperature();
                result["goalPressure"] = device->goalPressure();
                result["goalFlow"] = device->goalFlow();
                result["goalTemperature"] = device->goalTemperature();
            }
            if (machineState) {
                result["scaleWeight"] = machineState->scaleWeight();
                result["scaleFlowRate"] = machineState->scaleFlowRate();
                result["shotTime"] = machineState->shotTime();
            }

            // Include time-series data during active shot
            if (mainController && machineState && machineState->isFlowing()) {
                auto* model = mainController->shotDataModel();
                if (model) {
                    auto pointsToArray = [](const QVector<QPointF>& points) -> QJsonArray {
                        QJsonArray arr;
                        for (const auto& p : points) {
                            QJsonArray pt;
                            pt.append(p.x());
                            pt.append(p.y());
                            arr.append(pt);
                        }
                        return arr;
                    };
                    result["pressureData"] = pointsToArray(model->pressureData());
                    result["flowData"] = pointsToArray(model->flowData());
                    result["temperatureData"] = pointsToArray(model->temperatureData());
                    result["weightData"] = pointsToArray(model->weightData());
                }
            }
            return result;
        },
        "read");
}
