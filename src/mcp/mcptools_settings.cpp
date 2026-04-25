#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../core/settings.h"
#include "../core/settings_mqtt.h"
#include "../core/settings_autowake.h"
#include "../core/settings_hardware.h"
#include "../core/settings_ai.h"
#include "../core/settings_theme.h"
#include "../core/settings_visualizer.h"
#include "../core/settings_mcp.h"
#include "../core/accessibilitymanager.h"
#include "../core/translationmanager.h"
#include "../core/batterymanager.h"
#include "../screensaver/screensavervideomanager.h"

#include <QJsonObject>
#include <QJsonArray>

void registerSettingsReadTools(McpToolRegistry* registry, Settings* settings,
                               AccessibilityManager* accessibility,
                               ScreensaverVideoManager* screensaver,
                               TranslationManager* translation,
                               BatteryManager* battery)
{
    // settings_get
    registry->registerTool(
        "settings_get",
        "Read app settings. Returns all settings, specific keys, or a category of settings. "
        "Categories match QML settings tabs: machine, calibration, connections, screensaver, accessibility, "
        "ai, espresso, steam, water, flush, dye, mqtt, themes, visualizer, update, data, "
        "history, language, debug, battery, heater, autofavorites",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"keys", QJsonObject{
                    {"type", "array"},
                    {"items", QJsonObject{{"type", "string"}}},
                    {"description", "Specific setting keys to return. If empty, returns all (or filtered by category)."}
                }},
                {"category", QJsonObject{
                    {"type", "string"},
                    {"description", "Return only settings from this category. "
                     "One of: machine, calibration, connections, screensaver, accessibility, ai, "
                     "espresso, steam, water, flush, dye, mqtt, themes, visualizer, "
                     "update, data, history, language, debug, battery, heater, autofavorites"}
                }}
            }}
        },
        [settings, accessibility, screensaver, translation, battery](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) return result;

            QJsonArray keys = args["keys"].toArray();
            QString category = args["category"].toString();
            bool hasKeys = !keys.isEmpty();

            // If keys are specified, return those regardless of category.
            // If category is specified, return all settings in that category.
            // If neither, return all settings.
            auto include = [&](const QString& key, const QString& cat) -> bool {
                if (hasKeys) {
                    for (const auto& k : keys)
                        if (k.toString() == key) return true;
                    return false;
                }
                if (!category.isEmpty()) return category == cat;
                return true;
            };

            // === Machine ===
            if (include("themeMode", "machine")) result["themeMode"] = settings->theme()->themeMode();
            if (include("darkThemeName", "machine")) result["darkThemeName"] = settings->theme()->darkThemeName();
            if (include("lightThemeName", "machine")) result["lightThemeName"] = settings->theme()->lightThemeName();
            if (include("autoSleepMinutes", "machine")) result["autoSleepMinutes"] = settings->value("autoSleepMinutes", 60).toInt();
            if (include("postShotReviewTimeout", "machine")) result["postShotReviewTimeout"] = settings->value("postShotReviewTimeout", 31).toInt();
            if (include("keepSteamHeaterOn", "machine")) result["keepSteamHeaterOn"] = settings->keepSteamHeaterOn();
            if (include("steamAutoFlushSeconds", "machine")) result["steamAutoFlushSeconds"] = settings->steamAutoFlushSeconds();
            if (include("refillKitOverride", "machine")) result["refillKitOverride"] = settings->refillKitOverride();
            if (include("waterRefillPoint", "machine")) result["waterRefillPoint"] = settings->waterRefillPoint();
            if (include("waterLevelDisplayUnit", "machine")) result["waterLevelDisplayUnit"] = settings->waterLevelDisplayUnit();
            if (include("screenBrightness", "machine")) result["screenBrightness"] = settings->theme()->screenBrightness();
            if (include("defaultShotRating", "machine")) result["defaultShotRating"] = settings->visualizer()->defaultShotRating();
            if (include("launcherMode", "machine")) result["launcherMode"] = settings->launcherMode();
            {
                auto* aw = settings->autoWake();
                if (include("autoWakeEnabled", "machine")) result["autoWakeEnabled"] = aw->autoWakeEnabled();
                if (include("autoWakeStayAwakeEnabled", "machine")) result["autoWakeStayAwakeEnabled"] = aw->autoWakeStayAwakeEnabled();
                if (include("autoWakeStayAwakeMinutes", "machine")) result["autoWakeStayAwakeMinutes"] = aw->autoWakeStayAwakeMinutes();
            }

            // === Calibration ===
            if (include("useFlowScale", "calibration")) result["useFlowScale"] = settings->useFlowScale();
            if (include("flowCalibrationMultiplier", "calibration")) result["flowCalibrationMultiplier"] = settings->flowCalibrationMultiplier();
            if (include("autoFlowCalibration", "calibration")) result["autoFlowCalibration"] = settings->autoFlowCalibration();
            if (include("ignoreVolumeWithScale", "calibration")) result["ignoreVolumeWithScale"] = settings->ignoreVolumeWithScale();
            if (include("steamTwoTapStop", "machine")) result["steamTwoTapStop"] = settings->hardware()->steamTwoTapStop();

            // === Connections ===
            if (include("machineAddress", "connections")) result["machineAddress"] = settings->machineAddress();
            if (include("scaleAddress", "connections")) result["scaleAddress"] = settings->scaleAddress();
            if (include("scaleType", "connections")) result["scaleType"] = settings->scaleType();
            if (include("scaleName", "connections")) result["scaleName"] = settings->scaleName();
            if (include("usbSerialEnabled", "connections")) result["usbSerialEnabled"] = settings->usbSerialEnabled();
            if (include("showScaleDialogs", "connections")) result["showScaleDialogs"] = settings->showScaleDialogs();

            // === Screensaver ===
            if (screensaver) {
                if (include("screensaverType", "screensaver")) result["screensaverType"] = screensaver->screensaverType();
                if (include("dimDelayMinutes", "screensaver")) result["dimDelayMinutes"] = screensaver->dimDelayMinutes();
                if (include("dimPercent", "screensaver")) result["dimPercent"] = screensaver->dimPercent();
                if (include("pipesSpeed", "screensaver")) result["pipesSpeed"] = screensaver->pipesSpeed();
                if (include("pipesCameraSpeed", "screensaver")) result["pipesCameraSpeed"] = screensaver->pipesCameraSpeed();
                if (include("pipesShowClock", "screensaver")) result["pipesShowClock"] = screensaver->pipesShowClock();
                if (include("flipClockUse3D", "screensaver")) result["flipClockUse3D"] = screensaver->flipClockUse3D();
                if (include("videosShowClock", "screensaver")) result["videosShowClock"] = screensaver->videosShowClock();
                if (include("cacheEnabled", "screensaver")) result["cacheEnabled"] = screensaver->cacheEnabled();
                if (include("attractorShowClock", "screensaver")) result["attractorShowClock"] = screensaver->attractorShowClock();
                if (include("imageDisplayDuration", "screensaver")) result["imageDisplayDuration"] = screensaver->imageDisplayDuration();
                if (include("showDateOnPersonal", "screensaver")) result["showDateOnPersonal"] = screensaver->showDateOnPersonal();
                if (include("shotMapShape", "screensaver")) result["shotMapShape"] = screensaver->shotMapShape();
                if (include("shotMapTexture", "screensaver")) result["shotMapTexture"] = screensaver->shotMapTexture();
                if (include("shotMapShowClock", "screensaver")) result["shotMapShowClock"] = screensaver->shotMapShowClock();
                if (include("shotMapShowProfiles", "screensaver")) result["shotMapShowProfiles"] = screensaver->shotMapShowProfiles();
                if (include("shotMapShowTerminator", "screensaver")) result["shotMapShowTerminator"] = screensaver->shotMapShowTerminator();
            }

            // === Accessibility ===
            if (accessibility) {
                if (include("accessibilityEnabled", "accessibility")) result["accessibilityEnabled"] = accessibility->enabled();
                if (include("ttsEnabled", "accessibility")) result["ttsEnabled"] = accessibility->ttsEnabled();
                if (include("tickEnabled", "accessibility")) result["tickEnabled"] = accessibility->tickEnabled();
                if (include("tickSoundIndex", "accessibility")) result["tickSoundIndex"] = accessibility->tickSoundIndex();
                if (include("tickVolume", "accessibility")) result["tickVolume"] = accessibility->tickVolume();
                if (include("extractionAnnouncementsEnabled", "accessibility")) result["extractionAnnouncementsEnabled"] = accessibility->extractionAnnouncementsEnabled();
                if (include("extractionAnnouncementMode", "accessibility")) result["extractionAnnouncementMode"] = accessibility->extractionAnnouncementMode();
                if (include("extractionAnnouncementInterval", "accessibility")) result["extractionAnnouncementInterval"] = accessibility->extractionAnnouncementInterval();
            }

            // === AI ===
            {
                auto* a = settings->ai();
                if (include("aiProvider", "ai")) result["aiProvider"] = a->aiProvider();
                if (include("ollamaEndpoint", "ai")) result["ollamaEndpoint"] = a->ollamaEndpoint();
                if (include("ollamaModel", "ai")) result["ollamaModel"] = a->ollamaModel();
                if (include("openrouterModel", "ai")) result["openrouterModel"] = a->openrouterModel();
            }
            if (include("mcpEnabled", "ai")) result["mcpEnabled"] = settings->mcp()->mcpEnabled();
            if (include("mcpAccessLevel", "ai")) result["mcpAccessLevel"] = settings->mcp()->mcpAccessLevel();
            if (include("mcpConfirmationLevel", "ai")) result["mcpConfirmationLevel"] = settings->mcp()->mcpConfirmationLevel();
            if (include("discussShotApp", "ai")) result["discussShotApp"] = settings->discussShotApp();
            if (include("discussShotCustomUrl", "ai")) result["discussShotCustomUrl"] = settings->discussShotCustomUrl();
            // API keys excluded — sensitive

            // === Espresso ===
            if (include("espressoTemperature", "espresso")) result["espressoTemperatureC"] = settings->espressoTemperature();
            if (include("targetWeight", "espresso")) result["targetWeightG"] = settings->targetWeight();
            if (include("lastUsedRatio", "espresso")) result["lastUsedRatio"] = settings->lastUsedRatio();
            if (include("currentProfile", "espresso")) result["currentProfile"] = settings->currentProfile();

            // === Steam ===
            if (include("steamTemperature", "steam")) result["steamTemperatureC"] = settings->steamTemperature();
            if (include("steamTimeout", "steam")) result["steamTimeoutSec"] = settings->steamTimeout();
            if (include("steamFlow", "steam")) result["steamFlowMlPerSec"] = settings->steamFlow() / 100.0;
            if (include("steamDisabled", "steam")) result["steamDisabled"] = settings->steamDisabled();

            // === Hot Water ===
            if (include("waterTemperature", "water")) result["waterTemperatureC"] = settings->waterTemperature();
            if (include("waterVolume", "water")) result["waterVolumeMl"] = settings->waterVolume();
            if (include("waterVolumeMode", "water")) result["waterVolumeMode"] = settings->waterVolumeMode();
            if (include("hotWaterFlowRate", "water")) result["hotWaterFlowRateMlPerSec"] = settings->hardware()->hotWaterFlowRate() / 10.0;

            // === Flush ===
            if (include("flushFlow", "flush")) result["flushFlowMlPerSec"] = settings->flushFlow();
            if (include("flushSeconds", "flush")) result["flushSeconds"] = settings->flushSeconds();

            // === DYE (bean/grinder metadata) ===
            if (include("dyeBeanBrand", "dye")) result["dyeBeanBrand"] = settings->dyeBeanBrand();
            if (include("dyeBeanType", "dye")) result["dyeBeanType"] = settings->dyeBeanType();
            if (include("dyeRoastDate", "dye")) result["dyeRoastDate"] = settings->dyeRoastDate();
            if (include("dyeRoastLevel", "dye")) result["dyeRoastLevel"] = settings->dyeRoastLevel();
            if (include("dyeGrinderBrand", "dye")) result["dyeGrinderBrand"] = settings->dyeGrinderBrand();
            if (include("dyeGrinderModel", "dye")) result["dyeGrinderModel"] = settings->dyeGrinderModel();
            if (include("dyeGrinderBurrs", "dye")) result["dyeGrinderBurrs"] = settings->dyeGrinderBurrs();
            if (include("dyeGrinderSetting", "dye")) result["dyeGrinderSetting"] = settings->dyeGrinderSetting();
            if (include("dyeBeanWeight", "dye")) result["dyeBeanWeight"] = settings->dyeBeanWeight();
            if (include("dyeDrinkWeight", "dye")) result["dyeDrinkWeight"] = settings->dyeDrinkWeight();
            if (include("dyeDrinkTds", "dye")) result["dyeDrinkTds"] = settings->dyeDrinkTds();
            if (include("dyeDrinkEy", "dye")) result["dyeDrinkEy"] = settings->dyeDrinkEy();
            if (include("dyeEspressoEnjoyment", "dye")) result["dyeEspressoEnjoyment"] = settings->dyeEspressoEnjoyment();
            if (include("dyeShotNotes", "dye")) result["dyeShotNotes"] = settings->dyeShotNotes();
            if (include("dyeBarista", "dye")) result["dyeBarista"] = settings->dyeBarista();

            // === MQTT ===
            {
                auto* m = settings->mqtt();
                if (include("mqttEnabled", "mqtt")) result["mqttEnabled"] = m->mqttEnabled();
                if (include("mqttBrokerHost", "mqtt")) result["mqttBrokerHost"] = m->mqttBrokerHost();
                if (include("mqttBrokerPort", "mqtt")) result["mqttBrokerPort"] = m->mqttBrokerPort();
                if (include("mqttUsername", "mqtt")) result["mqttUsername"] = m->mqttUsername();
                if (include("mqttBaseTopic", "mqtt")) result["mqttBaseTopic"] = m->mqttBaseTopic();
                if (include("mqttPublishInterval", "mqtt")) result["mqttPublishInterval"] = m->mqttPublishInterval();
                if (include("mqttRetainMessages", "mqtt")) result["mqttRetainMessages"] = m->mqttRetainMessages();
                if (include("mqttHomeAssistantDiscovery", "mqtt")) result["mqttHomeAssistantDiscovery"] = m->mqttHomeAssistantDiscovery();
                if (include("mqttClientId", "mqtt")) result["mqttClientId"] = m->mqttClientId();
                // mqttPassword excluded — sensitive
            }

            // === Themes ===
            if (include("activeThemeName", "themes")) result["activeThemeName"] = settings->theme()->activeThemeName();
            if (include("themeNames", "themes")) result["themeNames"] = QJsonArray::fromStringList(settings->theme()->themeNames());
            if (include("activeShader", "themes")) result["activeShader"] = settings->theme()->activeShader();
            if (include("isDarkMode", "themes")) result["isDarkMode"] = settings->theme()->isDarkMode();

            // === Visualizer ===
            if (include("visualizerAutoUpload", "visualizer")) result["visualizerAutoUpload"] = settings->visualizer()->visualizerAutoUpload();
            if (include("visualizerMinDuration", "visualizer")) result["visualizerMinDuration"] = settings->visualizer()->visualizerMinDuration();
            if (include("visualizerExtendedMetadata", "visualizer")) result["visualizerExtendedMetadata"] = settings->visualizer()->visualizerExtendedMetadata();
            if (include("visualizerShowAfterShot", "visualizer")) result["visualizerShowAfterShot"] = settings->visualizer()->visualizerShowAfterShot();
            if (include("visualizerClearNotesOnStart", "visualizer")) result["visualizerClearNotesOnStart"] = settings->visualizer()->visualizerClearNotesOnStart();
            // visualizerUsername/Password excluded — sensitive

            // === Update ===
            if (include("autoCheckUpdates", "update")) result["autoCheckUpdates"] = settings->autoCheckUpdates();
            if (include("betaUpdatesEnabled", "update")) result["betaUpdatesEnabled"] = settings->betaUpdatesEnabled();

            // === Data ===
            if (include("webSecurityEnabled", "data")) result["webSecurityEnabled"] = settings->webSecurityEnabled();
            if (include("dailyBackupHour", "data")) result["dailyBackupHour"] = settings->dailyBackupHour();
            if (include("shotServerEnabled", "data")) result["shotServerEnabled"] = settings->shotServerEnabled();
            if (include("shotServerPort", "data")) result["shotServerPort"] = settings->shotServerPort();

            // === History ===
            if (include("shotHistorySortField", "history")) result["shotHistorySortField"] = settings->shotHistorySortField();
            if (include("shotHistorySortDirection", "history")) result["shotHistorySortDirection"] = settings->shotHistorySortDirection();

            // === Language ===
            if (translation) {
                if (include("currentLanguage", "language")) result["currentLanguage"] = translation->currentLanguage();
            }

            // === Debug ===
            if (include("simulationMode", "debug")) result["simulationMode"] = settings->simulationMode();
            if (include("hideGhcSimulator", "debug")) result["hideGhcSimulator"] = settings->hideGhcSimulator();

            // === Battery ===
            if (battery) {
                if (include("batteryPercent", "battery")) result["batteryPercent"] = battery->batteryPercent();
                if (include("isCharging", "battery")) result["isCharging"] = battery->isCharging();
                if (include("chargingMode", "battery")) result["chargingMode"] = battery->chargingMode();
            }

            // === Heater calibration (stored as tenths internally — divide by 10 to match QML display) ===
            {
                auto* hw = settings->hardware();
                if (include("heaterIdleTemp", "heater")) result["heaterIdleTempC"] = hw->heaterIdleTemp() / 10.0;
                if (include("heaterWarmupFlow", "heater")) result["heaterWarmupFlowMlPerSec"] = hw->heaterWarmupFlow() / 10.0;
                if (include("heaterTestFlow", "heater")) result["heaterTestFlowMlPerSec"] = hw->heaterTestFlow() / 10.0;
                if (include("heaterWarmupTimeout", "heater")) result["heaterWarmupTimeoutSec"] = hw->heaterWarmupTimeout() / 10.0;
            }

            // === Auto-favorites ===
            if (include("autoFavoritesGroupBy", "autofavorites")) result["autoFavoritesGroupBy"] = settings->autoFavoritesGroupBy();
            if (include("autoFavoritesMaxItems", "autofavorites")) result["autoFavoritesMaxItems"] = settings->autoFavoritesMaxItems();
            if (include("autoFavoritesOpenBrewSettings", "autofavorites")) result["autoFavoritesOpenBrewSettings"] = settings->autoFavoritesOpenBrewSettings();
            if (include("autoFavoritesHideUnrated", "autofavorites")) result["autoFavoritesHideUnrated"] = settings->autoFavoritesHideUnrated();

            return result;
        },
        "read");
}
