#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../history/shothistorystorage.h"
#include "../controllers/maincontroller.h"
#include "../core/settings.h"
#include "../core/accessibilitymanager.h"
#include "../core/translationmanager.h"
#include "../core/batterymanager.h"
#include "../screensaver/screensavervideomanager.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThread>
#include <QMetaObject>
#include <QCoreApplication>

#include "../core/dbutils.h"

void registerWriteTools(McpToolRegistry* registry, MainController* mainController,
                        ShotHistoryStorage* shotHistory, Settings* settings,
                        AccessibilityManager* accessibility,
                        ScreensaverVideoManager* screensaver,
                        TranslationManager* translation,
                        BatteryManager* battery)
{
    // shots_update — replaces shots_set_feedback with full metadata editing (same as QML)
    registry->registerAsyncTool(
        "shots_update",
        "Update any metadata field on a shot. Supports all fields the QML shot editor can change: "
        "enjoyment, notes, dose, yield, bean info, grinder info, barista, TDS, EY.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shotId", QJsonObject{{"type", "integer"}, {"description", "Shot ID"}}},
                {"enjoyment", QJsonObject{{"type", "integer"}, {"description", "Enjoyment rating 0-100"}}},
                {"notes", QJsonObject{{"type", "string"}, {"description", "Tasting notes"}}},
                {"doseWeight", QJsonObject{{"type", "number"}, {"description", "Dose weight in grams"}}},
                {"drinkWeight", QJsonObject{{"type", "number"}, {"description", "Yield/drink weight in grams"}}},
                {"beanBrand", QJsonObject{{"type", "string"}, {"description", "Bean brand"}}},
                {"beanType", QJsonObject{{"type", "string"}, {"description", "Bean type/name"}}},
                {"roastLevel", QJsonObject{{"type", "string"}, {"description", "Roast level"}}},
                {"roastDate", QJsonObject{{"type", "string"}, {"description", "Roast date (YYYY-MM-DD)"}}},
                {"grinderBrand", QJsonObject{{"type", "string"}, {"description", "Grinder brand"}}},
                {"grinderModel", QJsonObject{{"type", "string"}, {"description", "Grinder model"}}},
                {"grinderBurrs", QJsonObject{{"type", "string"}, {"description", "Grinder burrs"}}},
                {"grinderSetting", QJsonObject{{"type", "string"}, {"description", "Grinder setting"}}},
                {"barista", QJsonObject{{"type", "string"}, {"description", "Barista name"}}},
                {"drinkTds", QJsonObject{{"type", "number"}, {"description", "TDS measurement"}}},
                {"drinkEy", QJsonObject{{"type", "number"}, {"description", "Extraction yield percentage"}}}
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

            // Keys must match what updateShotMetadataStatic() reads (camelCase)
            QVariantMap metadata;
            if (args.contains("enjoyment"))
                metadata["enjoyment"] = qBound(0, args["enjoyment"].toInt(), 100);
            if (args.contains("notes"))
                metadata["espressoNotes"] = args["notes"].toString();
            if (args.contains("doseWeight"))
                metadata["doseWeight"] = args["doseWeight"].toDouble();
            if (args.contains("drinkWeight"))
                metadata["finalWeight"] = args["drinkWeight"].toDouble();
            if (args.contains("beanBrand"))
                metadata["beanBrand"] = args["beanBrand"].toString();
            if (args.contains("beanType"))
                metadata["beanType"] = args["beanType"].toString();
            if (args.contains("roastLevel"))
                metadata["roastLevel"] = args["roastLevel"].toString();
            if (args.contains("roastDate"))
                metadata["roastDate"] = args["roastDate"].toString();
            if (args.contains("grinderBrand"))
                metadata["grinderBrand"] = args["grinderBrand"].toString();
            if (args.contains("grinderModel"))
                metadata["grinderModel"] = args["grinderModel"].toString();
            if (args.contains("grinderBurrs"))
                metadata["grinderBurrs"] = args["grinderBurrs"].toString();
            if (args.contains("grinderSetting"))
                metadata["grinderSetting"] = args["grinderSetting"].toString();
            if (args.contains("barista"))
                metadata["barista"] = args["barista"].toString();
            if (args.contains("drinkTds"))
                metadata["drinkTds"] = args["drinkTds"].toDouble();
            if (args.contains("drinkEy"))
                metadata["drinkEy"] = args["drinkEy"].toDouble();

            if (metadata.isEmpty()) {
                respond(QJsonObject{{"error", "Provide at least one field to update"}});
                return;
            }

            const QString dbPath = shotHistory->databasePath();

            QThread* thread = QThread::create([dbPath, shotId, metadata, respond, shotHistory]() {
                bool ok = false;
                withTempDb(dbPath, "mcp_update", [&](QSqlDatabase& db) {
                    ok = ShotHistoryStorage::updateShotMetadataStatic(db, shotId, metadata);
                });

                QJsonObject result;
                if (ok) {
                    result["success"] = true;
                    QStringList fields;
                    for (auto it = metadata.begin(); it != metadata.end(); ++it)
                        fields << it.key();
                    result["updated"] = QJsonArray::fromStringList(fields);
                    result["message"] = "Shot " + QString::number(shotId) + " updated";
                } else {
                    result["error"] = "Failed to update shot " + QString::number(shotId);
                }

                QMetaObject::invokeMethod(qApp, [respond, result, shotHistory, ok]() {
                    if (ok)
                        shotHistory->invalidateDistinctCache();
                    respond(result);
                }, Qt::QueuedConnection);
            });

            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "control");

    // shots_delete
    registry->registerTool(
        "shots_delete",
        "Delete a shot by ID. This is permanent and cannot be undone.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shotId", QJsonObject{{"type", "integer"}, {"description", "Shot ID to delete"}}},
                {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
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

            // requestDeleteShot is async — queues the delete on a background thread
            shotHistory->requestDeleteShot(shotId);

            result["success"] = true;
            result["message"] = "Shot " + QString::number(shotId) + " deletion queued";
            return result;
        },
        "settings");

    // profiles_set_active
    registry->registerTool(
        "profiles_set_active",
        "Load and activate a profile on the machine by filename. "
        "IMPORTANT: Only call this when the user explicitly asks to change the active profile on the machine.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"filename", QJsonObject{{"type", "string"}, {"description", "Profile filename to activate"}}},
                {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
            }},
            {"required", QJsonArray{"filename"}}
        },
        [mainController](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!mainController) {
                result["error"] = "Controller not available";
                return result;
            }

            QString filename = args["filename"].toString();
            if (filename.isEmpty()) {
                result["error"] = "filename is required";
                return result;
            }

            if (!mainController->profileExists(filename)) {
                result["error"] = "Profile not found: " + filename;
                return result;
            }

            QMetaObject::invokeMethod(mainController, [mainController, filename]() {
                mainController->loadProfile(filename);
            }, Qt::QueuedConnection);

            result["success"] = true;
            result["message"] = "Profile activation queued: " + filename;
            return result;
        },
        "settings");

    // settings_set
    registry->registerTool(
        "settings_set",
        "Update any app setting on the device. This is the tool to use when the user asks to change "
        "grind size (dyeGrinderSetting), dose weight (dyeBeanWeight), drink/yield weight (targetWeight), "
        "brew temperature (espressoTemperature), or any other setting. "
        "Covers all QML settings tabs: preferences, connections, screensaver, accessibility, AI, "
        "espresso, steam, water, flush, DYE metadata, MQTT, themes, visualizer, update, data, "
        "history, language, debug, battery, heater, auto-favorites. "
        "API keys and passwords are excluded (sensitive). "
        "For temperature and weight changes on the active profile, this tool handles the profile update automatically. "
        "IMPORTANT: Only call this when the user explicitly asks to change settings on the machine. "
        "For discussion and recommendations, respond in chat instead.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                // Espresso / profile
                {"espressoTemperature", QJsonObject{{"type", "number"}, {"description", "Brew temperature in Celsius"}}},
                {"targetWeight", QJsonObject{{"type", "number"}, {"description", "Target shot weight in grams"}}},
                // Steam
                {"steamTemperature", QJsonObject{{"type", "number"}, {"description", "Steam temperature in Celsius"}}},
                {"steamTimeout", QJsonObject{{"type", "integer"}, {"description", "Steam timeout in seconds"}}},
                {"steamFlowMlPerSec", QJsonObject{{"type", "number"}, {"description", "Steam flow rate in mL/s"}}},
                {"keepSteamHeaterOn", QJsonObject{{"type", "boolean"}, {"description", "Keep steam heater on between operations"}}},
                {"steamAutoFlushSeconds", QJsonObject{{"type", "integer"}, {"description", "Auto-flush after steam (0 to disable)"}}},
                {"steamTwoTapStop", QJsonObject{{"type", "boolean"}, {"description", "Require two taps to stop steaming"}}},
                // Hot water
                {"waterTemperature", QJsonObject{{"type", "number"}, {"description", "Hot water temperature in Celsius"}}},
                {"waterVolume", QJsonObject{{"type", "integer"}, {"description", "Hot water volume in ml"}}},
                {"waterVolumeMode", QJsonObject{{"type", "string"}, {"description", "Hot water mode: 'weight' or 'volume'"}}},
                {"hotWaterFlowRateMlPerSec", QJsonObject{{"type", "number"}, {"description", "Hot water flow rate in mL/s (0.5-10.0)"}}},
                // Flush
                {"flushFlowMlPerSec", QJsonObject{{"type", "number"}, {"description", "Flush flow rate in mL/s (0-10)"}}},
                {"flushSeconds", QJsonObject{{"type", "number"}, {"description", "Flush duration in seconds"}}},
                // DYE metadata
                {"dyeBeanBrand", QJsonObject{{"type", "string"}, {"description", "Bean brand"}}},
                {"dyeBeanType", QJsonObject{{"type", "string"}, {"description", "Bean type/name"}}},
                {"dyeRoastDate", QJsonObject{{"type", "string"}, {"description", "Roast date"}}},
                {"dyeRoastLevel", QJsonObject{{"type", "string"}, {"description", "Roast level"}}},
                {"dyeGrinderBrand", QJsonObject{{"type", "string"}, {"description", "Grinder brand"}}},
                {"dyeGrinderModel", QJsonObject{{"type", "string"}, {"description", "Grinder model"}}},
                {"dyeGrinderBurrs", QJsonObject{{"type", "string"}, {"description", "Grinder burrs"}}},
                {"dyeGrinderSetting", QJsonObject{{"type", "string"}, {"description", "Grinder setting"}}},
                {"dyeBeanWeight", QJsonObject{{"type", "number"}, {"description", "Dose weight in grams"}}},
                {"dyeDrinkWeight", QJsonObject{{"type", "number"}, {"description", "Drink weight in grams"}}},
                {"dyeDrinkTds", QJsonObject{{"type", "number"}, {"description", "TDS measurement"}}},
                {"dyeDrinkEy", QJsonObject{{"type", "number"}, {"description", "Extraction yield percentage"}}},
                {"dyeEspressoEnjoyment", QJsonObject{{"type", "integer"}, {"description", "Enjoyment rating 0-100"}}},
                {"dyeShotNotes", QJsonObject{{"type", "string"}, {"description", "Shot notes"}}},
                {"dyeBarista", QJsonObject{{"type", "string"}, {"description", "Barista name"}}},
                // Preferences
                {"themeMode", QJsonObject{{"type", "string"}, {"description", "Theme mode: 'dark', 'light', or 'system'"}}},
                {"darkThemeName", QJsonObject{{"type", "string"}, {"description", "Dark mode theme name"}}},
                {"lightThemeName", QJsonObject{{"type", "string"}, {"description", "Light mode theme name"}}},
                {"autoSleepMinutes", QJsonObject{{"type", "integer"}, {"description", "Auto-sleep timeout in minutes"}}},
                {"postShotReviewTimeout", QJsonObject{{"type", "integer"}, {"description", "Post-shot review timeout in seconds"}}},
                {"refillKitOverride", QJsonObject{{"type", "integer"}, {"description", "Refill kit override: 0=off, 1=on, 2=auto"}}},
                {"waterRefillPoint", QJsonObject{{"type", "integer"}, {"description", "Water refill warning threshold in mm"}}},
                {"waterLevelDisplayUnit", QJsonObject{{"type", "string"}, {"description", "Water level display unit"}}},
                {"useFlowScale", QJsonObject{{"type", "boolean"}, {"description", "Use virtual flow scale"}}},
                {"screenBrightness", QJsonObject{{"type", "number"}, {"description", "Screen brightness 0.0-1.0"}}},
                {"defaultShotRating", QJsonObject{{"type", "integer"}, {"description", "Default shot enjoyment rating 0-100"}}},
                {"headlessSkipPurgeConfirm", QJsonObject{{"type", "boolean"}, {"description", "Skip purge confirmation on headless machines"}}},
                {"launcherMode", QJsonObject{{"type", "boolean"}, {"description", "Enable kiosk/launcher mode (Android only)"}}},
                {"flowCalibrationMultiplier", QJsonObject{{"type", "number"}, {"description", "Flow calibration multiplier"}}},
                {"autoFlowCalibration", QJsonObject{{"type", "boolean"}, {"description", "Enable automatic flow calibration"}}},
                {"autoWakeEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable auto-wake schedule"}}},
                {"autoWakeStayAwakeEnabled", QJsonObject{{"type", "boolean"}, {"description", "Stay awake after auto-wake"}}},
                {"autoWakeStayAwakeMinutes", QJsonObject{{"type", "integer"}, {"description", "Stay awake duration in minutes"}}},
                // Connections
                {"usbSerialEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable USB serial polling for DE1"}}},
                {"showScaleDialogs", QJsonObject{{"type", "boolean"}, {"description", "Show scale connection alert dialogs"}}},
                // Screensaver
                {"screensaverType", QJsonObject{{"type", "string"}, {"description", "Screensaver type"}}},
                {"dimDelayMinutes", QJsonObject{{"type", "integer"}, {"description", "Screen dim delay in minutes"}}},
                {"dimPercent", QJsonObject{{"type", "integer"}, {"description", "Screen dim percentage 0-100"}}},
                {"pipesSpeed", QJsonObject{{"type", "number"}, {"description", "Pipes screensaver speed"}}},
                {"pipesCameraSpeed", QJsonObject{{"type", "number"}, {"description", "Pipes camera speed"}}},
                {"pipesShowClock", QJsonObject{{"type", "boolean"}, {"description", "Show clock in pipes screensaver"}}},
                {"flipClockUse3D", QJsonObject{{"type", "boolean"}, {"description", "Use 3D flip clock"}}},
                {"videosShowClock", QJsonObject{{"type", "boolean"}, {"description", "Show clock in video screensaver"}}},
                {"cacheEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable screensaver video cache"}}},
                {"attractorShowClock", QJsonObject{{"type", "boolean"}, {"description", "Show clock in attractor screensaver"}}},
                {"imageDisplayDuration", QJsonObject{{"type", "integer"}, {"description", "Image display duration in seconds"}}},
                {"showDateOnPersonal", QJsonObject{{"type", "boolean"}, {"description", "Show date on personal media"}}},
                {"shotMapShape", QJsonObject{{"type", "string"}, {"description", "Shot map globe shape"}}},
                {"shotMapTexture", QJsonObject{{"type", "string"}, {"description", "Shot map globe texture"}}},
                {"shotMapShowClock", QJsonObject{{"type", "boolean"}, {"description", "Show clock in shot map"}}},
                {"shotMapShowProfiles", QJsonObject{{"type", "boolean"}, {"description", "Show profiles in shot map"}}},
                {"shotMapShowTerminator", QJsonObject{{"type", "boolean"}, {"description", "Show day/night terminator in shot map"}}},
                // Accessibility
                {"accessibilityEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable accessibility features"}}},
                {"ttsEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable text-to-speech"}}},
                {"tickEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable tick sounds"}}},
                {"tickSoundIndex", QJsonObject{{"type", "integer"}, {"description", "Tick sound index 1-4"}}},
                {"tickVolume", QJsonObject{{"type", "integer"}, {"description", "Tick volume 0-100"}}},
                {"extractionAnnouncementsEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable extraction announcements"}}},
                {"extractionAnnouncementMode", QJsonObject{{"type", "string"}, {"description", "Announcement mode: 'timed', 'milestones_only', 'both'"}}},
                {"extractionAnnouncementInterval", QJsonObject{{"type", "integer"}, {"description", "Announcement interval in seconds"}}},
                // AI
                {"aiProvider", QJsonObject{{"type", "string"}, {"description", "AI provider name"}}},
                {"mcpEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable MCP server"}}},
                {"mcpAccessLevel", QJsonObject{{"type", "integer"}, {"description", "MCP access level: 0=monitor, 1=control, 2=full"}}},
                {"mcpConfirmationLevel", QJsonObject{{"type", "integer"}, {"description", "MCP confirmation: 0=none, 1=dangerous, 2=all"}}},
                {"discussShotApp", QJsonObject{{"type", "integer"}, {"description", "Discuss Shot app: 0=Claude, 1=Claude Web, 2=ChatGPT, 3=Gemini, 4=Grok, 5=Custom"}}},
                {"discussShotCustomUrl", QJsonObject{{"type", "string"}, {"description", "Custom URL for Discuss Shot"}}},
                {"ollamaEndpoint", QJsonObject{{"type", "string"}, {"description", "Ollama endpoint URL"}}},
                {"ollamaModel", QJsonObject{{"type", "string"}, {"description", "Ollama model name"}}},
                {"openrouterModel", QJsonObject{{"type", "string"}, {"description", "OpenRouter model name"}}},
                // MQTT
                {"mqttEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable MQTT"}}},
                {"mqttBrokerHost", QJsonObject{{"type", "string"}, {"description", "MQTT broker hostname"}}},
                {"mqttBrokerPort", QJsonObject{{"type", "integer"}, {"description", "MQTT broker port"}}},
                {"mqttUsername", QJsonObject{{"type", "string"}, {"description", "MQTT username"}}},
                {"mqttBaseTopic", QJsonObject{{"type", "string"}, {"description", "MQTT base topic"}}},
                {"mqttPublishInterval", QJsonObject{{"type", "integer"}, {"description", "MQTT publish interval in seconds"}}},
                {"mqttRetainMessages", QJsonObject{{"type", "boolean"}, {"description", "Retain MQTT messages"}}},
                {"mqttHomeAssistantDiscovery", QJsonObject{{"type", "boolean"}, {"description", "Enable Home Assistant MQTT discovery"}}},
                {"mqttClientId", QJsonObject{{"type", "string"}, {"description", "MQTT client ID"}}},
                // Themes
                {"activeThemeName", QJsonObject{{"type", "string"}, {"description", "Active theme name"}}},
                {"activeShader", QJsonObject{{"type", "string"}, {"description", "Active screen shader (empty for none, 'crt' for CRT)"}}},
                // Visualizer
                {"visualizerAutoUpload", QJsonObject{{"type", "boolean"}, {"description", "Auto-upload shots to visualizer.coffee"}}},
                {"visualizerMinDuration", QJsonObject{{"type", "number"}, {"description", "Minimum shot duration for upload (seconds)"}}},
                {"visualizerExtendedMetadata", QJsonObject{{"type", "boolean"}, {"description", "Upload extended metadata"}}},
                {"visualizerShowAfterShot", QJsonObject{{"type", "boolean"}, {"description", "Show visualizer after shot"}}},
                {"visualizerClearNotesOnStart", QJsonObject{{"type", "boolean"}, {"description", "Clear notes when starting a shot"}}},
                // Update
                {"autoCheckUpdates", QJsonObject{{"type", "boolean"}, {"description", "Auto-check for updates"}}},
                {"betaUpdatesEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable beta update channel"}}},
                // Data
                {"webSecurityEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable web security (TOTP auth)"}}},
                {"dailyBackupHour", QJsonObject{{"type", "integer"}, {"description", "Daily backup hour (0-23)"}}},
                {"shotServerEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable web server"}}},
                {"shotServerPort", QJsonObject{{"type", "integer"}, {"description", "Web server port"}}},
                // History
                {"shotHistorySortField", QJsonObject{{"type", "string"}, {"description", "Shot history sort field"}}},
                {"shotHistorySortDirection", QJsonObject{{"type", "string"}, {"description", "Shot history sort direction"}}},
                // Language
                {"currentLanguage", QJsonObject{{"type", "string"}, {"description", "App language code (e.g., 'en', 'de', 'ja')"}}},
                // Debug
                {"simulationMode", QJsonObject{{"type", "boolean"}, {"description", "Enable DE1 simulator"}}},
                // Battery
                {"chargingMode", QJsonObject{{"type", "integer"}, {"description", "Smart charging mode"}}},
                // Heater calibration (values in display units — same as QML sliders)
                {"heaterIdleTempC", QJsonObject{{"type", "number"}, {"description", "Heater idle temperature in Celsius (0.0-99.0)"}}},
                {"heaterWarmupFlowMlPerSec", QJsonObject{{"type", "number"}, {"description", "Heater warmup flow rate in mL/s (0.5-6.0)"}}},
                {"heaterTestFlowMlPerSec", QJsonObject{{"type", "number"}, {"description", "Heater test flow rate in mL/s (0.5-8.0)"}}},
                {"heaterWarmupTimeoutSec", QJsonObject{{"type", "number"}, {"description", "Heater warmup timeout in seconds (1.0-30.0)"}}},
                // Auto-favorites
                {"autoFavoritesGroupBy", QJsonObject{{"type", "string"}, {"description", "Auto-favorites group by field"}}},
                {"autoFavoritesMaxItems", QJsonObject{{"type", "integer"}, {"description", "Max auto-favorites items"}}},
                {"autoFavoritesOpenBrewSettings", QJsonObject{{"type", "boolean"}, {"description", "Open brew settings on favorite select"}}},
                {"autoFavoritesHideUnrated", QJsonObject{{"type", "boolean"}, {"description", "Hide unrated shots from auto-favorites"}}},
                // Confirmation
                {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
            }}
        },
        [mainController, settings, accessibility, screensaver, translation, battery](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) {
                result["error"] = "Settings not available";
                return result;
            }

            QStringList updated;

            // === Espresso temperature / target weight (profile-aware) ===
            bool needsProfileUpdate = args.contains("espressoTemperature") || args.contains("targetWeight");
            if (needsProfileUpdate && mainController) {
                QString editorType = mainController->currentEditorType();
                if (editorType == "advanced") {
                    QVariantMap profileData = mainController->getCurrentProfile();
                    if (args.contains("espressoTemperature")) {
                        profileData["espresso_temperature"] = args["espressoTemperature"].toDouble();
                        updated << "espressoTemperature";
                    }
                    if (args.contains("targetWeight")) {
                        profileData["target_weight"] = args["targetWeight"].toDouble();
                        updated << "targetWeight";
                    }
                    mainController->uploadProfile(profileData);
                } else {
                    QVariantMap currentParams = mainController->getOrConvertRecipeParams();
                    if (args.contains("espressoTemperature")) {
                        double v = args["espressoTemperature"].toDouble();
                        currentParams["fillTemperature"] = v;
                        currentParams["pourTemperature"] = v;
                        currentParams["tempStart"] = v;
                        currentParams["tempPreinfuse"] = v;
                        currentParams["tempHold"] = v;
                        currentParams["tempDecline"] = v;
                        updated << "espressoTemperature";
                    }
                    if (args.contains("targetWeight")) {
                        currentParams["targetWeight"] = args["targetWeight"].toDouble();
                        updated << "targetWeight";
                    }
                    mainController->uploadRecipeProfile(currentParams);
                }

                // Sync QSettings so settings_get reads back the updated values.
                // uploadRecipeProfile/uploadProfile update the profile object but
                // don't write to QSettings (issue #527).
                if (args.contains("espressoTemperature") && settings)
                    settings->setEspressoTemperature(args["espressoTemperature"].toDouble());
                if (args.contains("targetWeight") && settings)
                    settings->setTargetWeight(args["targetWeight"].toDouble());
            }

            // === Steam ===
            if (args.contains("steamTemperature")) {
                double v = args["steamTemperature"].toDouble();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setSteamTemperature(v); }, Qt::QueuedConnection);
                updated << "steamTemperature";
            }
            if (args.contains("steamTimeout")) {
                int v = args["steamTimeout"].toInt();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setSteamTimeout(v); }, Qt::QueuedConnection);
                updated << "steamTimeout";
            }
            if (args.contains("steamFlowMlPerSec")) {
                int v = static_cast<int>(args["steamFlowMlPerSec"].toDouble() * 100.0);
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setSteamFlow(v); }, Qt::QueuedConnection);
                updated << "steamFlowMlPerSec";
            }
            if (args.contains("keepSteamHeaterOn")) {
                bool v = args["keepSteamHeaterOn"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setKeepSteamHeaterOn(v); }, Qt::QueuedConnection);
                updated << "keepSteamHeaterOn";
            }
            if (args.contains("steamAutoFlushSeconds")) {
                int v = args["steamAutoFlushSeconds"].toInt();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setSteamAutoFlushSeconds(v); }, Qt::QueuedConnection);
                updated << "steamAutoFlushSeconds";
            }
            if (args.contains("steamTwoTapStop")) {
                bool v = args["steamTwoTapStop"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setSteamTwoTapStop(v); }, Qt::QueuedConnection);
                updated << "steamTwoTapStop";
            }

            // === Hot water ===
            if (args.contains("waterTemperature")) {
                double v = args["waterTemperature"].toDouble();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setWaterTemperature(v); }, Qt::QueuedConnection);
                updated << "waterTemperature";
            }
            if (args.contains("waterVolume")) {
                int v = args["waterVolume"].toInt();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setWaterVolume(v); }, Qt::QueuedConnection);
                updated << "waterVolume";
            }
            if (args.contains("waterVolumeMode")) {
                QString v = args["waterVolumeMode"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setWaterVolumeMode(v); }, Qt::QueuedConnection);
                updated << "waterVolumeMode";
            }
            if (args.contains("hotWaterFlowRateMlPerSec")) {
                int v = static_cast<int>(args["hotWaterFlowRateMlPerSec"].toDouble() * 10.0);
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setHotWaterFlowRate(v); }, Qt::QueuedConnection);
                updated << "hotWaterFlowRateMlPerSec";
            }

            // === Flush ===
            if (args.contains("flushFlowMlPerSec")) {
                double v = args["flushFlowMlPerSec"].toDouble();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setFlushFlow(v); }, Qt::QueuedConnection);
                updated << "flushFlowMlPerSec";
            }
            if (args.contains("flushSeconds")) {
                double v = args["flushSeconds"].toDouble();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setFlushSeconds(v); }, Qt::QueuedConnection);
                updated << "flushSeconds";
            }

            // === DYE metadata ===
            if (args.contains("dyeBeanBrand")) {
                QString v = args["dyeBeanBrand"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeBeanBrand(v); }, Qt::QueuedConnection);
                updated << "dyeBeanBrand";
            }
            if (args.contains("dyeBeanType")) {
                QString v = args["dyeBeanType"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeBeanType(v); }, Qt::QueuedConnection);
                updated << "dyeBeanType";
            }
            if (args.contains("dyeRoastDate")) {
                QString v = args["dyeRoastDate"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeRoastDate(v); }, Qt::QueuedConnection);
                updated << "dyeRoastDate";
            }
            if (args.contains("dyeRoastLevel")) {
                QString v = args["dyeRoastLevel"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeRoastLevel(v); }, Qt::QueuedConnection);
                updated << "dyeRoastLevel";
            }
            if (args.contains("dyeGrinderBrand")) {
                QString v = args["dyeGrinderBrand"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeGrinderBrand(v); }, Qt::QueuedConnection);
                updated << "dyeGrinderBrand";
            }
            if (args.contains("dyeGrinderModel")) {
                QString v = args["dyeGrinderModel"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeGrinderModel(v); }, Qt::QueuedConnection);
                updated << "dyeGrinderModel";
            }
            if (args.contains("dyeGrinderBurrs")) {
                QString v = args["dyeGrinderBurrs"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeGrinderBurrs(v); }, Qt::QueuedConnection);
                updated << "dyeGrinderBurrs";
            }
            if (args.contains("dyeGrinderSetting")) {
                QString v = args["dyeGrinderSetting"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeGrinderSetting(v); }, Qt::QueuedConnection);
                updated << "dyeGrinderSetting";
            }
            if (args.contains("dyeBeanWeight")) {
                double v = args["dyeBeanWeight"].toDouble();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeBeanWeight(v); }, Qt::QueuedConnection);
                updated << "dyeBeanWeight";
            }
            if (args.contains("dyeDrinkWeight")) {
                double v = args["dyeDrinkWeight"].toDouble();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeDrinkWeight(v); }, Qt::QueuedConnection);
                updated << "dyeDrinkWeight";
            }
            if (args.contains("dyeDrinkTds")) {
                double v = args["dyeDrinkTds"].toDouble();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeDrinkTds(v); }, Qt::QueuedConnection);
                updated << "dyeDrinkTds";
            }
            if (args.contains("dyeDrinkEy")) {
                double v = args["dyeDrinkEy"].toDouble();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeDrinkEy(v); }, Qt::QueuedConnection);
                updated << "dyeDrinkEy";
            }
            if (args.contains("dyeEspressoEnjoyment")) {
                int v = qBound(0, args["dyeEspressoEnjoyment"].toInt(), 100);
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeEspressoEnjoyment(v); }, Qt::QueuedConnection);
                updated << "dyeEspressoEnjoyment";
            }
            if (args.contains("dyeShotNotes")) {
                QString v = args["dyeShotNotes"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeShotNotes(v); }, Qt::QueuedConnection);
                updated << "dyeShotNotes";
            }
            if (args.contains("dyeBarista")) {
                QString v = args["dyeBarista"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDyeBarista(v); }, Qt::QueuedConnection);
                updated << "dyeBarista";
            }

            // === Preferences ===
            if (args.contains("themeMode")) {
                QString v = args["themeMode"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setThemeMode(v); }, Qt::QueuedConnection);
                updated << "themeMode";
            }
            if (args.contains("darkThemeName")) {
                QString v = args["darkThemeName"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDarkThemeName(v); }, Qt::QueuedConnection);
                updated << "darkThemeName";
            }
            if (args.contains("lightThemeName")) {
                QString v = args["lightThemeName"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setLightThemeName(v); }, Qt::QueuedConnection);
                updated << "lightThemeName";
            }
            if (args.contains("autoSleepMinutes")) {
                int v = args["autoSleepMinutes"].toInt();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setValue("autoSleepMinutes", v); }, Qt::QueuedConnection);
                updated << "autoSleepMinutes";
            }
            if (args.contains("postShotReviewTimeout")) {
                int v = args["postShotReviewTimeout"].toInt();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setValue("postShotReviewTimeout", v); }, Qt::QueuedConnection);
                updated << "postShotReviewTimeout";
            }
            if (args.contains("refillKitOverride")) {
                int v = args["refillKitOverride"].toInt();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setRefillKitOverride(v); }, Qt::QueuedConnection);
                updated << "refillKitOverride";
            }
            if (args.contains("waterRefillPoint")) {
                int v = args["waterRefillPoint"].toInt();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setWaterRefillPoint(v); }, Qt::QueuedConnection);
                updated << "waterRefillPoint";
            }
            if (args.contains("waterLevelDisplayUnit")) {
                QString v = args["waterLevelDisplayUnit"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setWaterLevelDisplayUnit(v); }, Qt::QueuedConnection);
                updated << "waterLevelDisplayUnit";
            }
            if (args.contains("useFlowScale")) {
                bool v = args["useFlowScale"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setUseFlowScale(v); }, Qt::QueuedConnection);
                updated << "useFlowScale";
            }
            if (args.contains("screenBrightness")) {
                double v = args["screenBrightness"].toDouble();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setScreenBrightness(v); }, Qt::QueuedConnection);
                updated << "screenBrightness";
            }
            if (args.contains("defaultShotRating")) {
                int v = qBound(0, args["defaultShotRating"].toInt(), 100);
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDefaultShotRating(v); }, Qt::QueuedConnection);
                updated << "defaultShotRating";
            }
            if (args.contains("headlessSkipPurgeConfirm")) {
                bool v = args["headlessSkipPurgeConfirm"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setHeadlessSkipPurgeConfirm(v); }, Qt::QueuedConnection);
                updated << "headlessSkipPurgeConfirm";
            }
            if (args.contains("launcherMode")) {
                bool v = args["launcherMode"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setLauncherMode(v); }, Qt::QueuedConnection);
                updated << "launcherMode";
            }
            if (args.contains("flowCalibrationMultiplier")) {
                double v = args["flowCalibrationMultiplier"].toDouble();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setFlowCalibrationMultiplier(v); }, Qt::QueuedConnection);
                updated << "flowCalibrationMultiplier";
            }
            if (args.contains("autoFlowCalibration")) {
                bool v = args["autoFlowCalibration"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setAutoFlowCalibration(v); }, Qt::QueuedConnection);
                updated << "autoFlowCalibration";
            }
            if (args.contains("autoWakeEnabled")) {
                bool v = args["autoWakeEnabled"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setAutoWakeEnabled(v); }, Qt::QueuedConnection);
                updated << "autoWakeEnabled";
            }
            if (args.contains("autoWakeStayAwakeEnabled")) {
                bool v = args["autoWakeStayAwakeEnabled"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setAutoWakeStayAwakeEnabled(v); }, Qt::QueuedConnection);
                updated << "autoWakeStayAwakeEnabled";
            }
            if (args.contains("autoWakeStayAwakeMinutes")) {
                int v = args["autoWakeStayAwakeMinutes"].toInt();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setAutoWakeStayAwakeMinutes(v); }, Qt::QueuedConnection);
                updated << "autoWakeStayAwakeMinutes";
            }

            // === Connections ===
            if (args.contains("usbSerialEnabled")) {
                bool v = args["usbSerialEnabled"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setUsbSerialEnabled(v); }, Qt::QueuedConnection);
                updated << "usbSerialEnabled";
            }
            if (args.contains("showScaleDialogs")) {
                bool v = args["showScaleDialogs"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setShowScaleDialogs(v); }, Qt::QueuedConnection);
                updated << "showScaleDialogs";
            }

            // === Screensaver ===
            if (screensaver) {
                if (args.contains("screensaverType")) {
                    QString v = args["screensaverType"].toString();
                    QMetaObject::invokeMethod(screensaver, [screensaver, v]() { screensaver->setScreensaverType(v); }, Qt::QueuedConnection);
                    updated << "screensaverType";
                }
                if (args.contains("dimDelayMinutes")) {
                    int v = args["dimDelayMinutes"].toInt();
                    QMetaObject::invokeMethod(screensaver, [screensaver, v]() { screensaver->setDimDelayMinutes(v); }, Qt::QueuedConnection);
                    updated << "dimDelayMinutes";
                }
                if (args.contains("dimPercent")) {
                    int v = args["dimPercent"].toInt();
                    QMetaObject::invokeMethod(screensaver, [screensaver, v]() { screensaver->setDimPercent(v); }, Qt::QueuedConnection);
                    updated << "dimPercent";
                }
                if (args.contains("pipesSpeed")) {
                    double v = args["pipesSpeed"].toDouble();
                    QMetaObject::invokeMethod(screensaver, [screensaver, v]() { screensaver->setPipesSpeed(v); }, Qt::QueuedConnection);
                    updated << "pipesSpeed";
                }
                if (args.contains("pipesCameraSpeed")) {
                    double v = args["pipesCameraSpeed"].toDouble();
                    QMetaObject::invokeMethod(screensaver, [screensaver, v]() { screensaver->setPipesCameraSpeed(v); }, Qt::QueuedConnection);
                    updated << "pipesCameraSpeed";
                }
                if (args.contains("pipesShowClock")) {
                    bool v = args["pipesShowClock"].toBool();
                    QMetaObject::invokeMethod(screensaver, [screensaver, v]() { screensaver->setPipesShowClock(v); }, Qt::QueuedConnection);
                    updated << "pipesShowClock";
                }
                if (args.contains("flipClockUse3D")) {
                    bool v = args["flipClockUse3D"].toBool();
                    QMetaObject::invokeMethod(screensaver, [screensaver, v]() { screensaver->setFlipClockUse3D(v); }, Qt::QueuedConnection);
                    updated << "flipClockUse3D";
                }
                if (args.contains("videosShowClock")) {
                    bool v = args["videosShowClock"].toBool();
                    QMetaObject::invokeMethod(screensaver, [screensaver, v]() { screensaver->setVideosShowClock(v); }, Qt::QueuedConnection);
                    updated << "videosShowClock";
                }
                if (args.contains("cacheEnabled")) {
                    bool v = args["cacheEnabled"].toBool();
                    QMetaObject::invokeMethod(screensaver, [screensaver, v]() { screensaver->setCacheEnabled(v); }, Qt::QueuedConnection);
                    updated << "cacheEnabled";
                }
                if (args.contains("attractorShowClock")) {
                    bool v = args["attractorShowClock"].toBool();
                    QMetaObject::invokeMethod(screensaver, [screensaver, v]() { screensaver->setAttractorShowClock(v); }, Qt::QueuedConnection);
                    updated << "attractorShowClock";
                }
                if (args.contains("imageDisplayDuration")) {
                    int v = args["imageDisplayDuration"].toInt();
                    QMetaObject::invokeMethod(screensaver, [screensaver, v]() { screensaver->setImageDisplayDuration(v); }, Qt::QueuedConnection);
                    updated << "imageDisplayDuration";
                }
                if (args.contains("showDateOnPersonal")) {
                    bool v = args["showDateOnPersonal"].toBool();
                    QMetaObject::invokeMethod(screensaver, [screensaver, v]() { screensaver->setShowDateOnPersonal(v); }, Qt::QueuedConnection);
                    updated << "showDateOnPersonal";
                }
                if (args.contains("shotMapShape")) {
                    QString v = args["shotMapShape"].toString();
                    QMetaObject::invokeMethod(screensaver, [screensaver, v]() { screensaver->setShotMapShape(v); }, Qt::QueuedConnection);
                    updated << "shotMapShape";
                }
                if (args.contains("shotMapTexture")) {
                    QString v = args["shotMapTexture"].toString();
                    QMetaObject::invokeMethod(screensaver, [screensaver, v]() { screensaver->setShotMapTexture(v); }, Qt::QueuedConnection);
                    updated << "shotMapTexture";
                }
                if (args.contains("shotMapShowClock")) {
                    bool v = args["shotMapShowClock"].toBool();
                    QMetaObject::invokeMethod(screensaver, [screensaver, v]() { screensaver->setShotMapShowClock(v); }, Qt::QueuedConnection);
                    updated << "shotMapShowClock";
                }
                if (args.contains("shotMapShowProfiles")) {
                    bool v = args["shotMapShowProfiles"].toBool();
                    QMetaObject::invokeMethod(screensaver, [screensaver, v]() { screensaver->setShotMapShowProfiles(v); }, Qt::QueuedConnection);
                    updated << "shotMapShowProfiles";
                }
                if (args.contains("shotMapShowTerminator")) {
                    bool v = args["shotMapShowTerminator"].toBool();
                    QMetaObject::invokeMethod(screensaver, [screensaver, v]() { screensaver->setShotMapShowTerminator(v); }, Qt::QueuedConnection);
                    updated << "shotMapShowTerminator";
                }
            }

            // === Accessibility ===
            if (accessibility) {
                if (args.contains("accessibilityEnabled")) {
                    bool v = args["accessibilityEnabled"].toBool();
                    QMetaObject::invokeMethod(accessibility, [accessibility, v]() { accessibility->setEnabled(v); }, Qt::QueuedConnection);
                    updated << "accessibilityEnabled";
                }
                if (args.contains("ttsEnabled")) {
                    bool v = args["ttsEnabled"].toBool();
                    QMetaObject::invokeMethod(accessibility, [accessibility, v]() { accessibility->setTtsEnabled(v); }, Qt::QueuedConnection);
                    updated << "ttsEnabled";
                }
                if (args.contains("tickEnabled")) {
                    bool v = args["tickEnabled"].toBool();
                    QMetaObject::invokeMethod(accessibility, [accessibility, v]() { accessibility->setTickEnabled(v); }, Qt::QueuedConnection);
                    updated << "tickEnabled";
                }
                if (args.contains("tickSoundIndex")) {
                    int v = args["tickSoundIndex"].toInt();
                    QMetaObject::invokeMethod(accessibility, [accessibility, v]() { accessibility->setTickSoundIndex(v); }, Qt::QueuedConnection);
                    updated << "tickSoundIndex";
                }
                if (args.contains("tickVolume")) {
                    int v = qBound(0, args["tickVolume"].toInt(), 100);
                    QMetaObject::invokeMethod(accessibility, [accessibility, v]() { accessibility->setTickVolume(v); }, Qt::QueuedConnection);
                    updated << "tickVolume";
                }
                if (args.contains("extractionAnnouncementsEnabled")) {
                    bool v = args["extractionAnnouncementsEnabled"].toBool();
                    QMetaObject::invokeMethod(accessibility, [accessibility, v]() { accessibility->setExtractionAnnouncementsEnabled(v); }, Qt::QueuedConnection);
                    updated << "extractionAnnouncementsEnabled";
                }
                if (args.contains("extractionAnnouncementMode")) {
                    QString v = args["extractionAnnouncementMode"].toString();
                    QMetaObject::invokeMethod(accessibility, [accessibility, v]() { accessibility->setExtractionAnnouncementMode(v); }, Qt::QueuedConnection);
                    updated << "extractionAnnouncementMode";
                }
                if (args.contains("extractionAnnouncementInterval")) {
                    int v = args["extractionAnnouncementInterval"].toInt();
                    QMetaObject::invokeMethod(accessibility, [accessibility, v]() { accessibility->setExtractionAnnouncementInterval(v); }, Qt::QueuedConnection);
                    updated << "extractionAnnouncementInterval";
                }
            }

            // === AI ===
            if (args.contains("aiProvider")) {
                QString v = args["aiProvider"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setAiProvider(v); }, Qt::QueuedConnection);
                updated << "aiProvider";
            }
            if (args.contains("mcpEnabled")) {
                bool v = args["mcpEnabled"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setMcpEnabled(v); }, Qt::QueuedConnection);
                updated << "mcpEnabled";
            }
            if (args.contains("mcpAccessLevel")) {
                int v = qBound(0, args["mcpAccessLevel"].toInt(), 2);
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setMcpAccessLevel(v); }, Qt::QueuedConnection);
                updated << "mcpAccessLevel";
            }
            if (args.contains("mcpConfirmationLevel")) {
                int v = qBound(0, args["mcpConfirmationLevel"].toInt(), 2);
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setMcpConfirmationLevel(v); }, Qt::QueuedConnection);
                updated << "mcpConfirmationLevel";
            }
            if (args.contains("discussShotApp")) {
                int v = qBound(0, args["discussShotApp"].toInt(), 5);
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDiscussShotApp(v); }, Qt::QueuedConnection);
                updated << "discussShotApp";
            }
            if (args.contains("discussShotCustomUrl")) {
                QString v = args["discussShotCustomUrl"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDiscussShotCustomUrl(v); }, Qt::QueuedConnection);
                updated << "discussShotCustomUrl";
            }
            if (args.contains("ollamaEndpoint")) {
                QString v = args["ollamaEndpoint"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setOllamaEndpoint(v); }, Qt::QueuedConnection);
                updated << "ollamaEndpoint";
            }
            if (args.contains("ollamaModel")) {
                QString v = args["ollamaModel"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setOllamaModel(v); }, Qt::QueuedConnection);
                updated << "ollamaModel";
            }
            if (args.contains("openrouterModel")) {
                QString v = args["openrouterModel"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setOpenrouterModel(v); }, Qt::QueuedConnection);
                updated << "openrouterModel";
            }

            // === MQTT ===
            if (args.contains("mqttEnabled")) {
                bool v = args["mqttEnabled"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setMqttEnabled(v); }, Qt::QueuedConnection);
                updated << "mqttEnabled";
            }
            if (args.contains("mqttBrokerHost")) {
                QString v = args["mqttBrokerHost"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setMqttBrokerHost(v); }, Qt::QueuedConnection);
                updated << "mqttBrokerHost";
            }
            if (args.contains("mqttBrokerPort")) {
                int v = args["mqttBrokerPort"].toInt();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setMqttBrokerPort(v); }, Qt::QueuedConnection);
                updated << "mqttBrokerPort";
            }
            if (args.contains("mqttUsername")) {
                QString v = args["mqttUsername"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setMqttUsername(v); }, Qt::QueuedConnection);
                updated << "mqttUsername";
            }
            if (args.contains("mqttBaseTopic")) {
                QString v = args["mqttBaseTopic"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setMqttBaseTopic(v); }, Qt::QueuedConnection);
                updated << "mqttBaseTopic";
            }
            if (args.contains("mqttPublishInterval")) {
                int v = args["mqttPublishInterval"].toInt();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setMqttPublishInterval(v); }, Qt::QueuedConnection);
                updated << "mqttPublishInterval";
            }
            if (args.contains("mqttRetainMessages")) {
                bool v = args["mqttRetainMessages"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setMqttRetainMessages(v); }, Qt::QueuedConnection);
                updated << "mqttRetainMessages";
            }
            if (args.contains("mqttHomeAssistantDiscovery")) {
                bool v = args["mqttHomeAssistantDiscovery"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setMqttHomeAssistantDiscovery(v); }, Qt::QueuedConnection);
                updated << "mqttHomeAssistantDiscovery";
            }
            if (args.contains("mqttClientId")) {
                QString v = args["mqttClientId"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setMqttClientId(v); }, Qt::QueuedConnection);
                updated << "mqttClientId";
            }
            // mqttPassword excluded — sensitive

            // === Themes ===
            if (args.contains("activeThemeName")) {
                QString v = args["activeThemeName"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setActiveThemeName(v); }, Qt::QueuedConnection);
                updated << "activeThemeName";
            }
            if (args.contains("activeShader")) {
                QString v = args["activeShader"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setActiveShader(v); }, Qt::QueuedConnection);
                updated << "activeShader";
            }

            // === Visualizer ===
            if (args.contains("visualizerAutoUpload")) {
                bool v = args["visualizerAutoUpload"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setVisualizerAutoUpload(v); }, Qt::QueuedConnection);
                updated << "visualizerAutoUpload";
            }
            if (args.contains("visualizerMinDuration")) {
                double v = args["visualizerMinDuration"].toDouble();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setVisualizerMinDuration(v); }, Qt::QueuedConnection);
                updated << "visualizerMinDuration";
            }
            if (args.contains("visualizerExtendedMetadata")) {
                bool v = args["visualizerExtendedMetadata"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setVisualizerExtendedMetadata(v); }, Qt::QueuedConnection);
                updated << "visualizerExtendedMetadata";
            }
            if (args.contains("visualizerShowAfterShot")) {
                bool v = args["visualizerShowAfterShot"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setVisualizerShowAfterShot(v); }, Qt::QueuedConnection);
                updated << "visualizerShowAfterShot";
            }
            if (args.contains("visualizerClearNotesOnStart")) {
                bool v = args["visualizerClearNotesOnStart"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setVisualizerClearNotesOnStart(v); }, Qt::QueuedConnection);
                updated << "visualizerClearNotesOnStart";
            }
            // visualizerUsername/Password excluded — sensitive

            // === Update ===
            if (args.contains("autoCheckUpdates")) {
                bool v = args["autoCheckUpdates"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setAutoCheckUpdates(v); }, Qt::QueuedConnection);
                updated << "autoCheckUpdates";
            }
            if (args.contains("betaUpdatesEnabled")) {
                bool v = args["betaUpdatesEnabled"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setBetaUpdatesEnabled(v); }, Qt::QueuedConnection);
                updated << "betaUpdatesEnabled";
            }

            // === Data ===
            if (args.contains("webSecurityEnabled")) {
                bool v = args["webSecurityEnabled"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setWebSecurityEnabled(v); }, Qt::QueuedConnection);
                updated << "webSecurityEnabled";
            }
            if (args.contains("dailyBackupHour")) {
                int v = qBound(0, args["dailyBackupHour"].toInt(), 23);
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setDailyBackupHour(v); }, Qt::QueuedConnection);
                updated << "dailyBackupHour";
            }
            if (args.contains("shotServerEnabled")) {
                bool v = args["shotServerEnabled"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setShotServerEnabled(v); }, Qt::QueuedConnection);
                updated << "shotServerEnabled";
            }
            if (args.contains("shotServerPort")) {
                int v = args["shotServerPort"].toInt();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setShotServerPort(v); }, Qt::QueuedConnection);
                updated << "shotServerPort";
            }

            // === History ===
            if (args.contains("shotHistorySortField")) {
                QString v = args["shotHistorySortField"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setShotHistorySortField(v); }, Qt::QueuedConnection);
                updated << "shotHistorySortField";
            }
            if (args.contains("shotHistorySortDirection")) {
                QString v = args["shotHistorySortDirection"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setShotHistorySortDirection(v); }, Qt::QueuedConnection);
                updated << "shotHistorySortDirection";
            }

            // === Language ===
            if (translation && args.contains("currentLanguage")) {
                QString v = args["currentLanguage"].toString();
                QMetaObject::invokeMethod(translation, [translation, v]() { translation->setCurrentLanguage(v); }, Qt::QueuedConnection);
                updated << "currentLanguage";
            }

            // === Debug ===
            if (args.contains("simulationMode")) {
                bool v = args["simulationMode"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setSimulationMode(v); }, Qt::QueuedConnection);
                updated << "simulationMode";
            }

            // === Battery ===
            if (battery && args.contains("chargingMode")) {
                int v = args["chargingMode"].toInt();
                QMetaObject::invokeMethod(battery, [battery, v]() { battery->setChargingMode(v); }, Qt::QueuedConnection);
                updated << "chargingMode";
            }

            // === Heater calibration (display units × 10 = internal storage) ===
            if (args.contains("heaterIdleTempC")) {
                int v = static_cast<int>(args["heaterIdleTempC"].toDouble() * 10.0);
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setHeaterIdleTemp(v); }, Qt::QueuedConnection);
                updated << "heaterIdleTempC";
            }
            if (args.contains("heaterWarmupFlowMlPerSec")) {
                int v = static_cast<int>(args["heaterWarmupFlowMlPerSec"].toDouble() * 10.0);
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setHeaterWarmupFlow(v); }, Qt::QueuedConnection);
                updated << "heaterWarmupFlowMlPerSec";
            }
            if (args.contains("heaterTestFlowMlPerSec")) {
                int v = static_cast<int>(args["heaterTestFlowMlPerSec"].toDouble() * 10.0);
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setHeaterTestFlow(v); }, Qt::QueuedConnection);
                updated << "heaterTestFlowMlPerSec";
            }
            if (args.contains("heaterWarmupTimeoutSec")) {
                int v = static_cast<int>(args["heaterWarmupTimeoutSec"].toDouble() * 10.0);
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setHeaterWarmupTimeout(v); }, Qt::QueuedConnection);
                updated << "heaterWarmupTimeoutSec";
            }

            // === Auto-favorites ===
            if (args.contains("autoFavoritesGroupBy")) {
                QString v = args["autoFavoritesGroupBy"].toString();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setAutoFavoritesGroupBy(v); }, Qt::QueuedConnection);
                updated << "autoFavoritesGroupBy";
            }
            if (args.contains("autoFavoritesMaxItems")) {
                int v = args["autoFavoritesMaxItems"].toInt();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setAutoFavoritesMaxItems(v); }, Qt::QueuedConnection);
                updated << "autoFavoritesMaxItems";
            }
            if (args.contains("autoFavoritesOpenBrewSettings")) {
                bool v = args["autoFavoritesOpenBrewSettings"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setAutoFavoritesOpenBrewSettings(v); }, Qt::QueuedConnection);
                updated << "autoFavoritesOpenBrewSettings";
            }
            if (args.contains("autoFavoritesHideUnrated")) {
                bool v = args["autoFavoritesHideUnrated"].toBool();
                QMetaObject::invokeMethod(settings, [settings, v]() { settings->setAutoFavoritesHideUnrated(v); }, Qt::QueuedConnection);
                updated << "autoFavoritesHideUnrated";
            }

            if (updated.isEmpty()) {
                result["error"] = "No valid settings provided";
                return result;
            }

            result["success"] = true;
            result["updated"] = QJsonArray::fromStringList(updated);
            return result;
        },
        "settings");

}
