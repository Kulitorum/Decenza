#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../controllers/maincontroller.h"
#include "../controllers/profilemanager.h"
#include "../core/settings.h"
#include "../core/settings_brew.h"
#include "../core/settings_dye.h"
#include "../core/settings_theme.h"
#include "../core/settings_calibration.h"
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
        [device](const QJsonObject&) -> QJsonObject {
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
        "Optional brew overrides (dose, yield, temperature, grind, rpm) are applied for this shot only — they are "
        "automatically cleared when the shot ends, matching the QML BrewDialog behavior.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"dose", QJsonObject{{"type", "number"}, {"description", "Override dose weight for this shot (grams)"}}},
            {"yield", QJsonObject{{"type", "number"}, {"description", "Override target yield for this shot (grams)"}}},
            {"temperature", QJsonObject{{"type", "number"}, {"description", "Override temperature for this shot (Celsius)"}}},
            {"grind", QJsonObject{{"type", "string"}, {"description", "Override grind setting for this shot"}}},
            {"rpm", QJsonObject{{"type", "integer"}, {"description", "Override grinder motor RPM for this shot (variable-RPM grinders only); the second half of the dial-in alongside grind"}}}
        }}},
        [device, machineState, profileManager, settings](const QJsonObject& args) -> QJsonObject {
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

            // Apply brew overrides if provided — same as QML BrewDialog.
            // Absent arguments default to the CURRENT effective values, never
            // to 0: the old code passed a missing dose as 0 straight into
            // activateBrewWithOverrides, wiping the live dose — merely a
            // mislabeled shot record before, but a 0 g stop target under a
            // ratio anchor (add-yield-ratio-anchor). Same for a missing
            // temperature, which armed a 0 °C override.
            bool hasOverrides = args.contains("dose") || args.contains("yield") ||
                                args.contains("temperature") || args.contains("grind") ||
                                args.contains("rpm");
            if (hasOverrides && profileManager && settings) {
                const double dose = args.contains("dose") ? args["dose"].toDouble()
                                                          : profileManager->brewByRatioDose();
                double yieldValue;
                QString yieldMode;
                if (args.contains("yield")) {
                    yieldValue = args["yield"].toDouble();
                    yieldMode = QStringLiteral("absolute");
                } else if (profileManager->brewByRatioActive()) {
                    // Preserve an armed ratio anchor rather than flattening
                    // it to the grams it happens to derive right now.
                    yieldValue = profileManager->brewByRatio();
                    yieldMode = QStringLiteral("ratio");
                } else {
                    yieldValue = profileManager->targetWeight();
                    yieldMode = QStringLiteral("absolute");
                }
                const double temperature = args.contains("temperature")
                    ? args["temperature"].toDouble()
                    : (settings->brew()->hasTemperatureOverride()
                           ? settings->brew()->temperatureOverride()
                           : profileManager->profileTargetTemperature());
                const QString grind = args.contains("grind")
                    ? args["grind"].toString()
                    : settings->dye()->dyeGrinderSetting();
                // RPM override: -1 leaves the live RPM untouched (the common case);
                // only a supplied rpm changes it. Independent of the grind override.
                const int rpm = args.contains("rpm") ? args["rpm"].toInt() : -1;
                profileManager->activateBrewWithOverrides(dose, yieldValue, yieldMode,
                                                          temperature, grind, rpm);
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
            // "In progress" means the machine is RUNNING an operation, which
            // is not the same as liquid moving. isFlowing() excludes espresso
            // preheat, Ending, and every non-flowing steam substate — so a
            // stop during preheat used to be refused with "no operation in
            // progress" while the machine went right on to pour a shot the
            // caller had explicitly asked to abort. That is the one moment a
            // stop is most likely to be wanted and most likely to be
            // automated. requestIdle() is safe from any state (it just asks
            // for Idle), so gate on the operation phases instead.
            using Phase = MachineState::Phase;
            const Phase phase = machineState ? machineState->phase() : Phase::Disconnected;
            const bool operationRunning =
                phase == Phase::EspressoPreheating || phase == Phase::Preinfusion ||
                phase == Phase::Pouring || phase == Phase::Ending ||
                phase == Phase::Steaming || phase == Phase::HotWater ||
                phase == Phase::Flushing || phase == Phase::Descaling ||
                phase == Phase::Cleaning;
            if (!operationRunning) {
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
            settings->calibration()->resetSawLearning();
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
            settings->calibration()->resetSawLearningForProfile(filename, scale);
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
            settings->calibration()->clearProfileFlowCalibration(filename);
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
            settings->theme()->applyPresetTheme(name);
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
