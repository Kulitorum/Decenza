#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../controllers/profilemanager.h"

#include <QJsonObject>

void registerControlTools(McpToolRegistry* registry, DE1Device* device, MachineState* machineState,
                          ProfileManager* profileManager)
{
    // machine_wake
    registry->registerTool(
        "machine_wake",
        "Wake the machine from sleep mode",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
        }}},
        [device, machineState](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!device || !device->isConnected()) {
                result["error"] = "Machine not connected";
                return result;
            }
            device->wakeUp();
            result["success"] = true;
            result["message"] = "Wake command sent";
            return result;
        },
        "control");

    // machine_sleep
    registry->registerTool(
        "machine_sleep",
        "Put the machine to sleep",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
        }}},
        [device, machineState](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!device || !device->isConnected()) {
                result["error"] = "Machine not connected";
                return result;
            }
            if (machineState && machineState->isFlowing()) {
                result["error"] = "Cannot sleep while operation is in progress";
                return result;
            }
            device->goToSleep();
            result["success"] = true;
            result["message"] = "Sleep command sent";
            return result;
        },
        "control");

    // machine_start_espresso
    registry->registerTool(
        "machine_start_espresso",
        "Start pulling an espresso shot. Machine must be in Ready state. Only works on DE1 v1.0 headless machines — "
        "most machines with GHC require a physical button press. Do not offer this unless the user explicitly asks. "
        "Optional brew overrides (dose, yield, temperature, grind) are applied for this shot only — they are "
        "automatically cleared when the shot ends, matching the QML BrewDialog behavior.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"dose", QJsonObject{{"type", "number"}, {"description", "Override dose weight for this shot (grams)"}}},
            {"yield", QJsonObject{{"type", "number"}, {"description", "Override target yield for this shot (grams)"}}},
            {"temperature", QJsonObject{{"type", "number"}, {"description", "Override temperature for this shot (Celsius)"}}},
            {"grind", QJsonObject{{"type", "string"}, {"description", "Override grind setting for this shot"}}}
        }}},
        [device, machineState, profileManager](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!device || !device->isConnected()) {
                result["error"] = "Machine not connected";
                return result;
            }
            if (!machineState) {
                result["error"] = "Machine state not available";
                return result;
            }
            if (!machineState->isReady()) {
                result["error"] = "Machine not ready (current phase: " + machineState->phaseString() + ")";
                return result;
            }

            // Apply brew overrides if provided — same as QML BrewDialog
            bool hasOverrides = args.contains("dose") || args.contains("yield") ||
                                args.contains("temperature") || args.contains("grind");
            if (hasOverrides && profileManager) {
                double dose = args.contains("dose") ? args["dose"].toDouble() : 0;
                double yield = args.contains("yield") ? args["yield"].toDouble() : 0;
                double temperature = args.contains("temperature") ? args["temperature"].toDouble() : 0;
                QString grind = args["grind"].toString();
                profileManager->activateBrewWithOverrides(dose, yield, temperature, grind);
            }

            device->startEspresso();
            result["success"] = true;
            result["message"] = hasOverrides ? "Espresso started with brew overrides" : "Espresso started";
            return result;
        },
        "control");

    // machine_start_steam
    registry->registerTool(
        "machine_start_steam",
        "Start steaming milk. Machine must be in Ready state. Only works on DE1 v1.0 headless machines — most machines with GHC require a physical button press. Do not offer this unless the user explicitly asks.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [device, machineState](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!device || !device->isConnected()) {
                result["error"] = "Machine not connected";
                return result;
            }
            if (!machineState) {
                result["error"] = "Machine state not available";
                return result;
            }
            if (!machineState->isReady()) {
                result["error"] = "Machine not ready (current phase: " + machineState->phaseString() + ")";
                return result;
            }
            device->startSteam();
            result["success"] = true;
            result["message"] = "Steam started";
            return result;
        },
        "control");

    // machine_start_hot_water
    registry->registerTool(
        "machine_start_hot_water",
        "Dispense hot water. Machine must be in Ready state. Only works on DE1 v1.0 headless machines — most machines with GHC require a physical button press. Do not offer this unless the user explicitly asks.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [device, machineState](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!device || !device->isConnected()) {
                result["error"] = "Machine not connected";
                return result;
            }
            if (!machineState) {
                result["error"] = "Machine state not available";
                return result;
            }
            if (!machineState->isReady()) {
                result["error"] = "Machine not ready (current phase: " + machineState->phaseString() + ")";
                return result;
            }
            device->startHotWater();
            result["success"] = true;
            result["message"] = "Hot water started";
            return result;
        },
        "control");

    // machine_start_flush
    registry->registerTool(
        "machine_start_flush",
        "Flush the group head. Machine must be in Ready state. Only works on DE1 v1.0 headless machines — most machines with GHC require a physical button press. Do not offer this unless the user explicitly asks.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [device, machineState](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!device || !device->isConnected()) {
                result["error"] = "Machine not connected";
                return result;
            }
            if (!machineState) {
                result["error"] = "Machine state not available";
                return result;
            }
            if (!machineState->isReady()) {
                result["error"] = "Machine not ready (current phase: " + machineState->phaseString() + ")";
                return result;
            }
            device->startFlush();
            result["success"] = true;
            result["message"] = "Flush started";
            return result;
        },
        "control");

    // machine_stop
    registry->registerTool(
        "machine_stop",
        "Stop the current operation (espresso, steam, hot water, or flush)",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
        }}},
        [device, machineState](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!device || !device->isConnected()) {
                result["error"] = "Machine not connected";
                return result;
            }
            if (!machineState || !machineState->isFlowing()) {
                result["error"] = "No operation in progress";
                return result;
            }
            device->requestIdle();
            result["success"] = true;
            result["message"] = "Stop command sent";
            return result;
        },
        "control");

    // machine_skip_frame
    registry->registerTool(
        "machine_skip_frame",
        "Skip to the next profile frame during espresso extraction",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
        }}},
        [device, machineState](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!device || !device->isConnected()) {
                result["error"] = "Machine not connected";
                return result;
            }
            if (!machineState || !machineState->isFlowing()) {
                result["error"] = "No extraction in progress";
                return result;
            }
            device->skipToNextFrame();
            result["success"] = true;
            result["message"] = "Skipped to next frame";
            return result;
        },
        "control");
}
