#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../core/settings.h"
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
        "Categories match QML settings tabs: preferences, connections, screensaver, accessibility, "
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
                     "One of: preferences, connections, screensaver, accessibility, ai, "
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

            // === Preferences ===
            if (include("themeMode", "preferences")) result["themeMode"] = settings->themeMode();
            if (include("darkThemeName", "preferences")) result["darkThemeName"] = settings->darkThemeName();
            if (include("lightThemeName", "preferences")) result["lightThemeName"] = settings->lightThemeName();
            if (include("autoSleepMinutes", "preferences")) result["autoSleepMinutes"] = settings->value("autoSleepMinutes", 60).toInt();
            if (include("postShotReviewTimeout", "preferences")) result["postShotReviewTimeout"] = settings->value("postShotReviewTimeout", 31).toInt();
            if (include("keepSteamHeaterOn", "preferences")) result["keepSteamHeaterOn"] = settings->keepSteamHeaterOn();
            if (include("steamAutoFlushSeconds", "preferences")) result["steamAutoFlushSeconds"] = settings->steamAutoFlushSeconds();
            if (include("refillKitOverride", "preferences")) result["refillKitOverride"] = settings->refillKitOverride();
            if (include("waterRefillPoint", "preferences")) result["waterRefillPoint"] = settings->waterRefillPoint();
            if (include("waterLevelDisplayUnit", "preferences")) result["waterLevelDisplayUnit"] = settings->waterLevelDisplayUnit();
            if (include("useFlowScale", "preferences")) result["useFlowScale"] = settings->useFlowScale();
            if (include("screenBrightness", "preferences")) result["screenBrightness"] = settings->screenBrightness();
            if (include("defaultShotRating", "preferences")) result["defaultShotRating"] = settings->defaultShotRating();
            if (include("headlessSkipPurgeConfirm", "preferences")) result["headlessSkipPurgeConfirm"] = settings->headlessSkipPurgeConfirm();
            if (include("launcherMode", "preferences")) result["launcherMode"] = settings->launcherMode();
            if (include("flowCalibrationMultiplier", "preferences")) result["flowCalibrationMultiplier"] = settings->flowCalibrationMultiplier();
            if (include("autoFlowCalibration", "preferences")) result["autoFlowCalibration"] = settings->autoFlowCalibration();
            if (include("autoWakeEnabled", "preferences")) result["autoWakeEnabled"] = settings->autoWakeEnabled();
            if (include("autoWakeStayAwakeEnabled", "preferences")) result["autoWakeStayAwakeEnabled"] = settings->autoWakeStayAwakeEnabled();
            if (include("autoWakeStayAwakeMinutes", "preferences")) result["autoWakeStayAwakeMinutes"] = settings->autoWakeStayAwakeMinutes();
            if (include("steamTwoTapStop", "preferences")) result["steamTwoTapStop"] = settings->steamTwoTapStop();

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
            if (include("aiProvider", "ai")) result["aiProvider"] = settings->aiProvider();
            if (include("mcpEnabled", "ai")) result["mcpEnabled"] = settings->mcpEnabled();
            if (include("mcpAccessLevel", "ai")) result["mcpAccessLevel"] = settings->mcpAccessLevel();
            if (include("mcpConfirmationLevel", "ai")) result["mcpConfirmationLevel"] = settings->mcpConfirmationLevel();
            if (include("discussShotApp", "ai")) result["discussShotApp"] = settings->discussShotApp();
            if (include("discussShotCustomUrl", "ai")) result["discussShotCustomUrl"] = settings->discussShotCustomUrl();
            if (include("ollamaEndpoint", "ai")) result["ollamaEndpoint"] = settings->ollamaEndpoint();
            if (include("ollamaModel", "ai")) result["ollamaModel"] = settings->ollamaModel();
            if (include("openrouterModel", "ai")) result["openrouterModel"] = settings->openrouterModel();
            // API keys excluded — sensitive

            // === Espresso ===
            if (include("espressoTemperature", "espresso")) result["espressoTemperature"] = settings->espressoTemperature();
            if (include("targetWeight", "espresso")) result["targetWeight"] = settings->targetWeight();
            if (include("lastUsedRatio", "espresso")) result["lastUsedRatio"] = settings->lastUsedRatio();
            if (include("currentProfile", "espresso")) result["currentProfile"] = settings->currentProfile();

            // === Steam ===
            if (include("steamTemperature", "steam")) result["steamTemperature"] = settings->steamTemperature();
            if (include("steamTimeout", "steam")) result["steamTimeout"] = settings->steamTimeout();
            if (include("steamFlow", "steam")) result["steamFlow"] = settings->steamFlow();
            if (include("steamDisabled", "steam")) result["steamDisabled"] = settings->steamDisabled();

            // === Hot Water ===
            if (include("waterTemperature", "water")) result["waterTemperature"] = settings->waterTemperature();
            if (include("waterVolume", "water")) result["waterVolume"] = settings->waterVolume();
            if (include("waterVolumeMode", "water")) result["waterVolumeMode"] = settings->waterVolumeMode();
            if (include("hotWaterFlowRate", "water")) result["hotWaterFlowRate"] = settings->hotWaterFlowRate();

            // === Flush ===
            if (include("flushFlow", "flush")) result["flushFlow"] = settings->flushFlow();
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
            if (include("mqttEnabled", "mqtt")) result["mqttEnabled"] = settings->mqttEnabled();
            if (include("mqttBrokerHost", "mqtt")) result["mqttBrokerHost"] = settings->mqttBrokerHost();
            if (include("mqttBrokerPort", "mqtt")) result["mqttBrokerPort"] = settings->mqttBrokerPort();
            if (include("mqttUsername", "mqtt")) result["mqttUsername"] = settings->mqttUsername();
            if (include("mqttBaseTopic", "mqtt")) result["mqttBaseTopic"] = settings->mqttBaseTopic();
            if (include("mqttPublishInterval", "mqtt")) result["mqttPublishInterval"] = settings->mqttPublishInterval();
            if (include("mqttRetainMessages", "mqtt")) result["mqttRetainMessages"] = settings->mqttRetainMessages();
            if (include("mqttHomeAssistantDiscovery", "mqtt")) result["mqttHomeAssistantDiscovery"] = settings->mqttHomeAssistantDiscovery();
            if (include("mqttClientId", "mqtt")) result["mqttClientId"] = settings->mqttClientId();
            // mqttPassword excluded — sensitive

            // === Themes ===
            if (include("activeThemeName", "themes")) result["activeThemeName"] = settings->activeThemeName();
            if (include("themeNames", "themes")) result["themeNames"] = QJsonArray::fromStringList(settings->themeNames());
            if (include("activeShader", "themes")) result["activeShader"] = settings->activeShader();
            if (include("isDarkMode", "themes")) result["isDarkMode"] = settings->isDarkMode();

            // === Visualizer ===
            if (include("visualizerAutoUpload", "visualizer")) result["visualizerAutoUpload"] = settings->visualizerAutoUpload();
            if (include("visualizerMinDuration", "visualizer")) result["visualizerMinDuration"] = settings->visualizerMinDuration();
            if (include("visualizerExtendedMetadata", "visualizer")) result["visualizerExtendedMetadata"] = settings->visualizerExtendedMetadata();
            if (include("visualizerShowAfterShot", "visualizer")) result["visualizerShowAfterShot"] = settings->visualizerShowAfterShot();
            if (include("visualizerClearNotesOnStart", "visualizer")) result["visualizerClearNotesOnStart"] = settings->visualizerClearNotesOnStart();
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

            // === Heater calibration ===
            if (include("heaterIdleTemp", "heater")) result["heaterIdleTemp"] = settings->heaterIdleTemp();
            if (include("heaterWarmupFlow", "heater")) result["heaterWarmupFlow"] = settings->heaterWarmupFlow();
            if (include("heaterTestFlow", "heater")) result["heaterTestFlow"] = settings->heaterTestFlow();
            if (include("heaterWarmupTimeout", "heater")) result["heaterWarmupTimeout"] = settings->heaterWarmupTimeout();

            // === Auto-favorites ===
            if (include("autoFavoritesGroupBy", "autofavorites")) result["autoFavoritesGroupBy"] = settings->autoFavoritesGroupBy();
            if (include("autoFavoritesMaxItems", "autofavorites")) result["autoFavoritesMaxItems"] = settings->autoFavoritesMaxItems();
            if (include("autoFavoritesOpenBrewSettings", "autofavorites")) result["autoFavoritesOpenBrewSettings"] = settings->autoFavoritesOpenBrewSettings();
            if (include("autoFavoritesHideUnrated", "autofavorites")) result["autoFavoritesHideUnrated"] = settings->autoFavoritesHideUnrated();

            return result;
        },
        "read");
}
