#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../models/shotdatamodel.h"
#include "../controllers/maincontroller.h"
#include "../controllers/profilemanager.h"

#include <QJsonObject>
#include <QJsonArray>

void registerMachineTools(McpToolRegistry* registry, DE1Device* device,
                          MachineState* machineState, MainController* mainController,
                          ProfileManager* profileManager)
{
    // machine_get_state
    registry->registerTool(
        "machine_get_state",
        "Get current machine state: phase, connection status, readiness, heating, water level, firmware version",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [device, machineState, profileManager](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (profileManager) {
                result["activeProfile"] = profileManager->currentProfileName();
            }
            if (machineState) {
                result["phase"] = machineState->phaseString();
                result["isHeating"] = machineState->isHeating();
                result["isReady"] = machineState->isReady();
                result["isFlowing"] = machineState->isFlowing();
                result["shotTimeSec"] = machineState->shotTime();
                result["targetWeightG"] = machineState->targetWeight();
                result["targetVolumeMl"] = machineState->targetVolume();
                result["scaleWeightG"] = machineState->scaleWeight();
            }
            if (device) {
                result["connected"] = device->isConnected();
                result["stateString"] = device->stateString();
                result["waterLevelMl"] = device->waterLevelMl();
                result["waterLevelMm"] = device->waterLevelMm();
                result["firmwareVersion"] = device->firmwareVersion();
                result["isHeadless"] = device->isHeadless();
                result["pressureBar"] = device->pressure();
                result["temperatureC"] = device->temperature();
                result["steamTemperatureC"] = device->steamTemperature();
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
                result["pressureBar"] = device->pressure();
                result["flowMlPerSec"] = device->flow();
                result["temperatureC"] = device->temperature();
                result["mixTemperatureC"] = device->mixTemperature();
                result["steamTemperatureC"] = device->steamTemperature();
                result["goalPressureBar"] = device->goalPressure();
                result["goalFlowMlPerSec"] = device->goalFlow();
                result["goalTemperatureC"] = device->goalTemperature();
            }
            if (machineState) {
                result["scaleWeightG"] = machineState->scaleWeight();
                result["scaleFlowRateMlPerSec"] = machineState->scaleFlowRate();
                result["shotTimeSec"] = machineState->shotTime();
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
