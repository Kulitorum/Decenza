#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../controllers/maincontroller.h"
#include "../controllers/profilemanager.h"
#include "../core/settings.h"
#include "../core/databasebackupmanager.h"
#include "../network/mqttclient.h"

#include <QJsonObject>
#include <QJsonArray>

void registerControlTools(McpToolRegistry* registry, DE1Device* device, MachineState* machineState,
                          ProfileManager* profileManager, MainController* mainController,
                          Settings* settings)
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

    // backup_now
    registry->registerTool(
        "backup_now",
        "Create an immediate backup of the database, settings, profiles, and media. "
        "Returns the backup file path on success.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
        }}},
        [mainController](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!mainController || !mainController->backupManager()) {
                result["error"] = "Backup manager not available";
                return result;
            }
            bool ok = mainController->backupManager()->createBackup(true);
            if (ok) {
                result["success"] = true;
                result["message"] = "Backup created successfully";
            } else {
                result["error"] = "Backup creation failed";
            }
            return result;
        },
        "control");

    // reset_saw_learning
    registry->registerTool(
        "reset_saw_learning",
        "Reset stop-at-weight learning data. Clears the global pool, all per-(profile, scale) "
        "histories and pending batches, and the global bootstrap. Useful when switching beans "
        "or grind settings, as the learned flow deceleration curve may not apply to the new setup.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
        }}},
        [settings](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!settings) {
                result["error"] = "Settings not available";
                return result;
            }
            settings->resetSawLearning();
            result["success"] = true;
            result["message"] = "SAW learning data reset";
            return result;
        },
        "settings");

    // reset_saw_learning_for_profile
    registry->registerTool(
        "reset_saw_learning_for_profile",
        "Reset stop-at-weight learning for a single (profile, scale) pair only. Other pairs "
        "and the global bootstrap are preserved. Defaults to the active profile and the "
        "configured scale type when arguments are omitted.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"profileFilename", QJsonObject{{"type", "string"}, {"description", "Profile filename (defaults to active profile)"}}},
            {"scaleType", QJsonObject{{"type", "string"}, {"description", "Scale type (defaults to configured scaleType)"}}},
            {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
        }}},
        [settings, profileManager](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) {
                result["error"] = "Settings not available";
                return result;
            }
            QString filename = args["profileFilename"].toString();
            if (filename.isEmpty() && profileManager) {
                filename = profileManager->baseProfileName();
            }
            if (filename.isEmpty()) {
                result["error"] = "No profile filename specified and no active profile";
                return result;
            }
            QString scale = args["scaleType"].toString();
            if (scale.isEmpty()) scale = settings->scaleType();
            settings->resetSawLearningForProfile(filename, scale);
            result["success"] = true;
            result["profileFilename"] = filename;
            result["scaleType"] = scale;
            result["message"] = QString("SAW learning reset for %1 on %2").arg(filename, scale);
            return result;
        },
        "settings");

    // clear_flow_calibration
    registry->registerTool(
        "clear_flow_calibration",
        "Clear the per-profile flow calibration multiplier. The calibration will be re-learned "
        "from subsequent shots. If no profile is specified, clears for the current profile.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"profileFilename", QJsonObject{{"type", "string"}, {"description", "Profile filename to clear calibration for (defaults to current profile)"}}}
        }}},
        [settings, profileManager](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) {
                result["error"] = "Settings not available";
                return result;
            }
            QString filename = args["profileFilename"].toString();
            if (filename.isEmpty() && profileManager) {
                filename = profileManager->baseProfileName();
            }
            if (filename.isEmpty()) {
                result["error"] = "No profile filename specified and no active profile";
                return result;
            }
            settings->clearProfileFlowCalibration(filename);
            result["success"] = true;
            result["message"] = "Flow calibration cleared for " + filename;
            return result;
        },
        "settings");

    // apply_theme
    registry->registerTool(
        "apply_theme",
        "Apply a preset theme. Built-in themes: 'Default Dark', 'Default Light'. "
        "User-created themes are also available.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"name", QJsonObject{{"type", "string"}, {"description", "Theme name to apply (e.g. 'Default Dark', 'Default Light')"}}}
            }},
            {"required", QJsonArray{"name"}}
        },
        [settings](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) {
                result["error"] = "Settings not available";
                return result;
            }
            QString name = args["name"].toString();
            if (name.isEmpty()) {
                result["error"] = "Theme name is required";
                return result;
            }
            settings->applyPresetTheme(name);
            result["success"] = true;
            result["message"] = "Applied theme: " + name;
            return result;
        },
        "settings");

    // mqtt_connect
    registry->registerTool(
        "mqtt_connect",
        "Connect to the configured MQTT broker for Home Assistant integration. "
        "Broker settings (host, port, credentials) must be configured first via settings_set.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [mainController](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!mainController || !mainController->mqttClient()) {
                result["error"] = "MQTT client not available";
                return result;
            }
            MqttClient* mqtt = mainController->mqttClient();
            if (mqtt->isConnected()) {
                result["message"] = "Already connected to MQTT broker";
                return result;
            }
            mqtt->connectToBroker();
            result["success"] = true;
            result["message"] = "MQTT connection initiated";
            return result;
        },
        "control");

    // mqtt_disconnect
    registry->registerTool(
        "mqtt_disconnect",
        "Disconnect from the MQTT broker.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [mainController](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!mainController || !mainController->mqttClient()) {
                result["error"] = "MQTT client not available";
                return result;
            }
            MqttClient* mqtt = mainController->mqttClient();
            if (!mqtt->isConnected()) {
                result["message"] = "Not connected to MQTT broker";
                return result;
            }
            mqtt->disconnectFromBroker();
            result["success"] = true;
            result["message"] = "MQTT disconnected";
            return result;
        },
        "control");

    // mqtt_publish_discovery
    registry->registerTool(
        "mqtt_publish_discovery",
        "Publish Home Assistant MQTT discovery messages. The broker must be connected first.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [mainController](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!mainController || !mainController->mqttClient()) {
                result["error"] = "MQTT client not available";
                return result;
            }
            MqttClient* mqtt = mainController->mqttClient();
            if (!mqtt->isConnected()) {
                result["error"] = "Not connected to MQTT broker. Call mqtt_connect first.";
                return result;
            }
            mqtt->publishDiscovery();
            result["success"] = true;
            result["message"] = "Home Assistant discovery messages published";
            return result;
        },
        "control");
}
