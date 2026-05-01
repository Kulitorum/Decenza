#include "settingsserializer.h"
#include "settings.h"
#include "settings_app.h"
#include "settings_brew.h"
#include "settings_dye.h"
#include "settings_network.h"
#include "settings_mqtt.h"
#include "settings_autowake.h"
#include "settings_hardware.h"
#include "settings_ai.h"
#include "settings_theme.h"
#include "settings_visualizer.h"
#include "settings_calibration.h"
#include <QJsonArray>
#include <QDebug>

QStringList SettingsSerializer::sensitiveKeys()
{
    return {
        "visualizerPassword",
        "openaiApiKey",
        "anthropicApiKey",
        "geminiApiKey",
        "openrouterApiKey",
        "mqttPassword"
    };
}

QJsonObject SettingsSerializer::exportToJson(Settings* settings, bool includeSensitive)
{
    QJsonObject root;

    // Machine settings
    QJsonObject machine;
    machine["address"] = settings->machineAddress();
    root["machine"] = machine;

    // Scale settings
    QJsonObject scale;
    scale["address"] = settings->scaleAddress();
    scale["type"] = settings->scaleType();
    scale["name"] = settings->scaleName();
    scale["useFlowScale"] = settings->useFlowScale();
    root["scale"] = scale;

    // Refractometer settings
    QJsonObject refractometer;
    refractometer["address"] = settings->savedRefractometerAddress();
    refractometer["name"] = settings->savedRefractometerName();
    root["refractometer"] = refractometer;

    // Espresso settings
    QJsonObject espresso;
    espresso["temperature"] = settings->brew()->espressoTemperature();
    espresso["targetWeight"] = settings->brew()->targetWeight();
    espresso["lastUsedRatio"] = settings->brew()->lastUsedRatio();
    root["espresso"] = espresso;

    // Steam settings
    QJsonObject steam;
    steam["temperature"] = settings->brew()->steamTemperature();
    steam["timeout"] = settings->brew()->steamTimeout();
    steam["flow"] = settings->brew()->steamFlow();
    steam["keepHeaterOn"] = settings->brew()->keepSteamHeaterOn();
    steam["disabled"] = settings->brew()->steamDisabled();
    steam["autoFlushSeconds"] = settings->brew()->steamAutoFlushSeconds();
    steam["twoTapStop"] = settings->hardware()->steamTwoTapStop();
    steam["selectedPitcher"] = settings->brew()->selectedSteamPitcher();

    // Steam pitcher presets
    QJsonArray pitcherPresets;
    for (const QVariant& preset : settings->brew()->steamPitcherPresets()) {
        QJsonObject p;
        QVariantMap m = preset.toMap();
        p["name"] = m["name"].toString();
        p["duration"] = m["duration"].toInt();
        p["flow"] = m["flow"].toInt();
        pitcherPresets.append(p);
    }
    steam["pitcherPresets"] = pitcherPresets;
    root["steam"] = steam;

    // Launcher mode
    root["launcherMode"] = settings->app()->launcherMode();

    // Hot water settings
    QJsonObject water;
    water["temperature"] = settings->brew()->waterTemperature();
    water["volume"] = settings->brew()->waterVolume();
    water["volumeMode"] = settings->brew()->waterVolumeMode();
    water["selectedVessel"] = settings->brew()->selectedWaterVessel();

    // Water vessel presets
    QJsonArray vesselPresets;
    for (const QVariant& preset : settings->brew()->waterVesselPresets()) {
        QJsonObject p;
        QVariantMap m = preset.toMap();
        p["name"] = m["name"].toString();
        p["volume"] = m["volume"].toInt();
        p["mode"] = m["mode"].toString();
        p["flowRate"] = m["flowRate"].toInt();
        vesselPresets.append(p);
    }
    water["vesselPresets"] = vesselPresets;
    root["water"] = water;

    // Flush settings
    QJsonObject flush;
    flush["flow"] = settings->brew()->flushFlow();
    flush["seconds"] = settings->brew()->flushSeconds();
    flush["selectedPreset"] = settings->brew()->selectedFlushPreset();

    // Flush presets
    QJsonArray flushPresets;
    for (const QVariant& preset : settings->brew()->flushPresets()) {
        QJsonObject p;
        QVariantMap m = preset.toMap();
        p["name"] = m["name"].toString();
        p["flow"] = m["flow"].toDouble();
        p["seconds"] = m["seconds"].toDouble();
        flushPresets.append(p);
    }
    flush["presets"] = flushPresets;
    root["flush"] = flush;

    // Bean presets
    QJsonArray beanPresets;
    for (const QVariant& preset : settings->dye()->beanPresets()) {
        QJsonObject p;
        QVariantMap m = preset.toMap();
        p["name"] = m["name"].toString();
        p["brand"] = m["brand"].toString();
        p["type"] = m["type"].toString();
        p["roastDate"] = m["roastDate"].toString();
        p["roastLevel"] = m["roastLevel"].toString();
        p["grinderBrand"] = m["grinderBrand"].toString();
        p["grinderModel"] = m["grinderModel"].toString();
        p["grinderBurrs"] = m["grinderBurrs"].toString();
        p["grinderSetting"] = m["grinderSetting"].toString();
        p["barista"] = m["barista"].toString();
        beanPresets.append(p);
    }
    QJsonObject beans;
    beans["presets"] = beanPresets;
    beans["selectedPreset"] = settings->dye()->selectedBeanPreset();
    root["beans"] = beans;

    // Profile favorites
    QJsonObject profile;
    profile["current"] = settings->app()->currentProfile();
    profile["selectedFavorite"] = settings->app()->selectedFavoriteProfile();

    QJsonArray favorites;
    for (const QVariant& fav : settings->app()->favoriteProfiles()) {
        QJsonObject f;
        QVariantMap m = fav.toMap();
        f["name"] = m["name"].toString();
        f["filename"] = m["filename"].toString();
        favorites.append(f);
    }
    profile["favorites"] = favorites;

    QJsonArray selectedBuiltIns;
    for (const QString& s : settings->app()->selectedBuiltInProfiles()) {
        selectedBuiltIns.append(s);
    }
    profile["selectedBuiltIns"] = selectedBuiltIns;

    QJsonArray hiddenProfiles;
    for (const QString& s : settings->app()->hiddenProfiles()) {
        hiddenProfiles.append(s);
    }
    profile["hiddenProfiles"] = hiddenProfiles;
    root["profile"] = profile;

    // Shot history settings
    QJsonObject shotHistory;
    QJsonArray savedSearchesArr;
    for (const QString& s : settings->network()->savedSearches()) {
        savedSearchesArr.append(s);
    }
    shotHistory["savedSearches"] = savedSearchesArr;
    shotHistory["sortField"] = settings->network()->shotHistorySortField();
    shotHistory["sortDirection"] = settings->network()->shotHistorySortDirection();
    root["shotHistory"] = shotHistory;

    // UI settings
    QJsonObject ui;
    ui["skin"] = settings->theme()->skin();
    // screenBrightness is intentionally NOT exported — it's a device-local
    // runtime state (e.g. dimmed during screensaver) that would cause the
    // importing device to inherit an inappropriate brightness level (#495)
    ui["waterLevelDisplayUnit"] = settings->app()->waterLevelDisplayUnit();

    // Custom font sizes
    QJsonObject fontSizes;
    QVariantMap sizes = settings->theme()->customFontSizes();
    for (auto it = sizes.begin(); it != sizes.end(); ++it) {
        fontSizes[it.key()] = it.value().toDouble();
    }
    ui["customFontSizes"] = fontSizes;

    // Shader settings
    ui["activeShader"] = settings->theme()->activeShader();
    QJsonObject shaderParams;
    QVariantMap params = settings->theme()->shaderParams();
    for (auto it = params.begin(); it != params.end(); ++it) {
        shaderParams[it.key()] = QJsonValue::fromVariant(it.value());
    }
    ui["shaderParams"] = shaderParams;
    root["ui"] = ui;

    // Theme settings
    QJsonObject theme;
    theme["activeThemeName"] = settings->theme()->activeThemeName();
    theme["themeMode"] = settings->theme()->themeMode();

    // Export active palette as customColors (backward compat) plus both palettes
    QJsonObject customColors;
    QVariantMap colors = settings->theme()->customThemeColors();
    for (auto it = colors.begin(); it != colors.end(); ++it) {
        customColors[it.key()] = it.value().toString();
    }
    theme["customColors"] = customColors;

    // Also export raw dark/light storage for full dual-palette backup
    QByteArray darkData = settings->value("theme/customColorsDark").toByteArray();
    if (!darkData.isEmpty()) {
        theme["customColorsDark"] = QJsonDocument::fromJson(darkData).object();
    }
    QByteArray lightData = settings->value("theme/customColorsLight").toByteArray();
    if (!lightData.isEmpty()) {
        theme["customColorsLight"] = QJsonDocument::fromJson(lightData).object();
    }

    // Color groups
    QJsonArray colorGroups;
    for (const QVariant& group : settings->theme()->colorGroups()) {
        colorGroups.append(QJsonValue::fromVariant(group));
    }
    theme["colorGroups"] = colorGroups;
    root["theme"] = theme;

    // Visualizer settings
    QJsonObject visualizer;
    visualizer["username"] = settings->visualizer()->visualizerUsername();
    if (includeSensitive) {
        visualizer["password"] = settings->visualizer()->visualizerPassword();
    }
    visualizer["autoUpload"] = settings->visualizer()->visualizerAutoUpload();
    visualizer["minDuration"] = settings->visualizer()->visualizerMinDuration();
    visualizer["extendedMetadata"] = settings->visualizer()->visualizerExtendedMetadata();
    visualizer["showAfterShot"] = settings->visualizer()->visualizerShowAfterShot();
    visualizer["clearNotesOnStart"] = settings->visualizer()->visualizerClearNotesOnStart();
    visualizer["defaultShotRating"] = settings->visualizer()->defaultShotRating();
    root["visualizer"] = visualizer;

    // AI settings
    QJsonObject ai;
    auto* aiSettings = settings->ai();
    ai["provider"] = aiSettings->aiProvider();
    if (includeSensitive) {
        ai["openaiApiKey"] = aiSettings->openaiApiKey();
        ai["anthropicApiKey"] = aiSettings->anthropicApiKey();
        ai["geminiApiKey"] = aiSettings->geminiApiKey();
    }
    ai["ollamaEndpoint"] = aiSettings->ollamaEndpoint();
    ai["ollamaModel"] = aiSettings->ollamaModel();
    if (includeSensitive) {
        ai["openrouterApiKey"] = aiSettings->openrouterApiKey();
    }
    ai["openrouterModel"] = aiSettings->openrouterModel();
    root["ai"] = ai;

    // DYE (Describe Your Espresso) metadata
    QJsonObject dye;
    dye["beanBrand"] = settings->dye()->dyeBeanBrand();
    dye["beanType"] = settings->dye()->dyeBeanType();
    dye["roastDate"] = settings->dye()->dyeRoastDate();
    dye["roastLevel"] = settings->dye()->dyeRoastLevel();
    dye["grinderBrand"] = settings->dye()->dyeGrinderBrand();
    dye["grinderModel"] = settings->dye()->dyeGrinderModel();
    dye["grinderBurrs"] = settings->dye()->dyeGrinderBurrs();
    dye["grinderSetting"] = settings->dye()->dyeGrinderSetting();
    dye["beanWeight"] = settings->dye()->dyeBeanWeight();
    dye["drinkWeight"] = settings->dye()->dyeDrinkWeight();
    dye["drinkTds"] = settings->dye()->dyeDrinkTds();
    dye["drinkEy"] = settings->dye()->dyeDrinkEy();
    dye["espressoEnjoyment"] = settings->dye()->dyeEspressoEnjoyment();
    dye["shotNotes"] = settings->dye()->dyeShotNotes();
    dye["barista"] = settings->dye()->dyeBarista();
    dye["shotDateTime"] = settings->dye()->dyeShotDateTime();
    root["dye"] = dye;

    // Shot server settings
    QJsonObject shotServer;
    shotServer["enabled"] = settings->network()->shotServerEnabled();
    shotServer["hostname"] = settings->network()->shotServerHostname();
    shotServer["port"] = settings->network()->shotServerPort();
    shotServer["webSecurityEnabled"] = settings->network()->webSecurityEnabled();
    root["shotServer"] = shotServer;

    // Auto-update settings
    QJsonObject updates;
    updates["autoCheck"] = settings->app()->autoCheckUpdates();
    updates["betaUpdatesEnabled"] = settings->app()->betaUpdatesEnabled();
    root["updates"] = updates;

    // Developer settings - intentionally not exported (session-only Easter eggs)

    // Auto-wake schedule
    QJsonObject autoWake;
    auto* autoWakeSettings = settings->autoWake();
    autoWake["enabled"] = autoWakeSettings->autoWakeEnabled();
    QJsonArray schedule;
    for (const QVariant& day : autoWakeSettings->autoWakeSchedule()) {
        schedule.append(QJsonValue::fromVariant(day));
    }
    autoWake["schedule"] = schedule;
    autoWake["stayAwakeEnabled"] = autoWakeSettings->autoWakeStayAwakeEnabled();
    autoWake["stayAwakeMinutes"] = autoWakeSettings->autoWakeStayAwakeMinutes();
    root["autoWake"] = autoWake;

    // Auto-favorites settings
    QJsonObject autoFavorites;
    autoFavorites["groupBy"] = settings->network()->autoFavoritesGroupBy();
    autoFavorites["maxItems"] = settings->network()->autoFavoritesMaxItems();
    autoFavorites["openBrewSettings"] = settings->network()->autoFavoritesOpenBrewSettings();
    autoFavorites["hideUnrated"] = settings->network()->autoFavoritesHideUnrated();
    root["autoFavorites"] = autoFavorites;

    // MQTT settings (Home Automation)
    QJsonObject mqtt;
    auto* mqttSettings = settings->mqtt();
    mqtt["enabled"] = mqttSettings->mqttEnabled();
    mqtt["brokerHost"] = mqttSettings->mqttBrokerHost();
    mqtt["brokerPort"] = mqttSettings->mqttBrokerPort();
    mqtt["username"] = mqttSettings->mqttUsername();
    if (includeSensitive) {
        mqtt["password"] = mqttSettings->mqttPassword();
    }
    mqtt["baseTopic"] = mqttSettings->mqttBaseTopic();
    mqtt["publishInterval"] = mqttSettings->mqttPublishInterval();
    mqtt["retainMessages"] = mqttSettings->mqttRetainMessages();
    mqtt["homeAssistantDiscovery"] = mqttSettings->mqttHomeAssistantDiscovery();
    mqtt["clientId"] = mqttSettings->mqttClientId();
    root["mqtt"] = mqtt;

    // Layout configuration
    root["layoutConfiguration"] = settings->network()->layoutConfiguration();

    // Machine tuning
    QJsonObject machineTuning;
    machineTuning["waterRefillPoint"] = settings->app()->waterRefillPoint();
    machineTuning["refillKitOverride"] = settings->app()->refillKitOverride();
    {
        auto* hw = settings->hardware();
        machineTuning["heaterIdleTemp"] = hw->heaterIdleTemp();
        machineTuning["heaterWarmupFlow"] = hw->heaterWarmupFlow();
        machineTuning["heaterTestFlow"] = hw->heaterTestFlow();
        machineTuning["heaterWarmupTimeout"] = hw->heaterWarmupTimeout();
        machineTuning["hotWaterFlowRate"] = hw->hotWaterFlowRate();
    }
    machineTuning["flowCalibrationMultiplier"] = settings->calibration()->flowCalibrationMultiplier();
    machineTuning["autoFlowCalibration"] = settings->calibration()->autoFlowCalibration();
    machineTuning["ignoreVolumeWithScale"] = settings->brew()->ignoreVolumeWithScale();
    QJsonObject perProfileMap = settings->calibration()->allProfileFlowCalibrations();
    if (!perProfileMap.isEmpty()) {
        machineTuning["perProfileFlowCalibration"] = perProfileMap;
    }
    root["machineTuning"] = machineTuning;

    // Daily backup hour
    root["dailyBackupHour"] = settings->app()->dailyBackupHour();

    return root;
}

bool SettingsSerializer::importFromJson(Settings* settings, const QJsonObject& json,
                                        const QStringList& excludeKeys)
{
    // Machine settings
    if (json.contains("machine") && !excludeKeys.contains("machine")) {
        QJsonObject machine = json["machine"].toObject();
        if (machine.contains("address")) {
            settings->setMachineAddress(machine["address"].toString());
        }
    }

    // Scale settings
    if (json.contains("scale") && !excludeKeys.contains("scale")) {
        QJsonObject scale = json["scale"].toObject();
        if (scale.contains("address")) settings->setScaleAddress(scale["address"].toString());
        if (scale.contains("type")) settings->setScaleType(scale["type"].toString());
        if (scale.contains("name")) settings->setScaleName(scale["name"].toString());
        if (scale.contains("useFlowScale")) settings->setUseFlowScale(scale["useFlowScale"].toBool());
    }

    // Refractometer settings
    if (json.contains("refractometer") && !excludeKeys.contains("refractometer")) {
        QJsonObject refractometer = json["refractometer"].toObject();
        if (refractometer.contains("address")) settings->setSavedRefractometerAddress(refractometer["address"].toString());
        if (refractometer.contains("name")) settings->setSavedRefractometerName(refractometer["name"].toString());
    }

    // Espresso settings
    if (json.contains("espresso") && !excludeKeys.contains("espresso")) {
        QJsonObject espresso = json["espresso"].toObject();
        if (espresso.contains("temperature")) settings->brew()->setEspressoTemperature(espresso["temperature"].toDouble());
        if (espresso.contains("targetWeight")) settings->brew()->setTargetWeight(espresso["targetWeight"].toDouble());
        if (espresso.contains("lastUsedRatio")) settings->brew()->setLastUsedRatio(espresso["lastUsedRatio"].toDouble());
    }

    // Steam settings
    if (json.contains("steam") && !excludeKeys.contains("steam")) {
        QJsonObject steam = json["steam"].toObject();
        if (steam.contains("temperature")) settings->brew()->setSteamTemperature(steam["temperature"].toDouble());
        if (steam.contains("timeout")) settings->brew()->setSteamTimeout(steam["timeout"].toInt());
        if (steam.contains("flow")) settings->brew()->setSteamFlow(steam["flow"].toInt());
        if (steam.contains("keepHeaterOn")) settings->brew()->setKeepSteamHeaterOn(steam["keepHeaterOn"].toBool());
        if (steam.contains("disabled")) settings->brew()->setSteamDisabled(steam["disabled"].toBool());
        if (steam.contains("autoFlushSeconds")) settings->brew()->setSteamAutoFlushSeconds(steam["autoFlushSeconds"].toInt());
        if (steam.contains("twoTapStop")) settings->hardware()->setSteamTwoTapStop(steam["twoTapStop"].toBool());
        if (steam.contains("selectedPitcher")) settings->brew()->setSelectedSteamCup(steam["selectedPitcher"].toInt());

        // Import pitcher presets
        if (steam.contains("pitcherPresets")) {
            // Clear existing presets first by removing them in reverse order
            QVariantList existingPresets = settings->brew()->steamPitcherPresets();
            for (qsizetype i = existingPresets.size() - 1; i >= 0; --i) {
                settings->brew()->removeSteamPitcherPreset(static_cast<int>(i));
            }
            // Add imported presets
            QJsonArray presets = steam["pitcherPresets"].toArray();
            for (const QJsonValue& v : presets) {
                QJsonObject p = v.toObject();
                settings->brew()->addSteamPitcherPreset(p["name"].toString(), p["duration"].toInt(), p["flow"].toInt());
            }
        }
    }

    // Back-compat: a JSON exported by an older version may carry the legacy
    // "headless.skipPurgeConfirm" field. The current writer emits the unified
    // setting under "steam.twoTapStop"; if the new key was already imported
    // above, it takes precedence (it's the more deliberate choice). Otherwise
    // map the legacy field through the same polarity inversion used by the
    // in-process migration in Settings::Settings(). Skip if the caller asked
    // to exclude either the "headless" source or the "steam" target.
    const QJsonObject steamObj = json["steam"].toObject();
    if (json.contains("headless") && !excludeKeys.contains("headless")
            && !excludeKeys.contains("steam")
            && !steamObj.contains("twoTapStop")) {
        QJsonObject headless = json["headless"].toObject();
        if (headless.contains("skipPurgeConfirm")) {
            settings->hardware()->setSteamTwoTapStop(!headless["skipPurgeConfirm"].toBool());
        }
    }

    // Launcher mode
    if (json.contains("launcherMode") && !excludeKeys.contains("launcherMode")) {
        settings->app()->setLauncherMode(json["launcherMode"].toBool());
    }

    // Hot water settings
    if (json.contains("water") && !excludeKeys.contains("water")) {
        QJsonObject water = json["water"].toObject();
        if (water.contains("temperature")) settings->brew()->setWaterTemperature(water["temperature"].toDouble());
        if (water.contains("volume")) settings->brew()->setWaterVolume(water["volume"].toInt());
        if (water.contains("volumeMode")) settings->brew()->setWaterVolumeMode(water["volumeMode"].toString());
        if (water.contains("selectedVessel")) settings->brew()->setSelectedWaterCup(water["selectedVessel"].toInt());

        // Import vessel presets
        if (water.contains("vesselPresets")) {
            QVariantList existingPresets = settings->brew()->waterVesselPresets();
            for (qsizetype i = existingPresets.size() - 1; i >= 0; --i) {
                settings->brew()->removeWaterVesselPreset(static_cast<int>(i));
            }
            QJsonArray presets = water["vesselPresets"].toArray();
            for (const QJsonValue& v : presets) {
                QJsonObject p = v.toObject();
                settings->brew()->addWaterVesselPreset(p["name"].toString(), p["volume"].toInt(),
                                               p["mode"].toString("weight"), p["flowRate"].toInt(40));
            }
        }
    }

    // Flush settings
    if (json.contains("flush") && !excludeKeys.contains("flush")) {
        QJsonObject flush = json["flush"].toObject();
        if (flush.contains("flow")) settings->brew()->setFlushFlow(flush["flow"].toDouble());
        if (flush.contains("seconds")) settings->brew()->setFlushSeconds(flush["seconds"].toDouble());
        if (flush.contains("selectedPreset")) settings->brew()->setSelectedFlushPreset(flush["selectedPreset"].toInt());

        // Import flush presets
        if (flush.contains("presets")) {
            QVariantList existingPresets = settings->brew()->flushPresets();
            for (qsizetype i = existingPresets.size() - 1; i >= 0; --i) {
                settings->brew()->removeFlushPreset(static_cast<int>(i));
            }
            QJsonArray presets = flush["presets"].toArray();
            for (const QJsonValue& v : presets) {
                QJsonObject p = v.toObject();
                settings->brew()->addFlushPreset(p["name"].toString(), p["flow"].toDouble(), p["seconds"].toDouble());
            }
        }
    }

    // Bean presets
    if (json.contains("beans") && !excludeKeys.contains("beans")) {
        QJsonObject beans = json["beans"].toObject();
        if (beans.contains("selectedPreset")) settings->dye()->setSelectedBeanPreset(beans["selectedPreset"].toInt());

        if (beans.contains("presets")) {
            QVariantList existingPresets = settings->dye()->beanPresets();
            for (qsizetype i = existingPresets.size() - 1; i >= 0; --i) {
                settings->dye()->removeBeanPreset(static_cast<int>(i));
            }
            QJsonArray presets = beans["presets"].toArray();
            for (const QJsonValue& v : presets) {
                QJsonObject p = v.toObject();
                settings->dye()->addBeanPreset(
                    p["name"].toString(),
                    p["brand"].toString(),
                    p["type"].toString(),
                    p["roastDate"].toString(),
                    p["roastLevel"].toString(),
                    p["grinderBrand"].toString(),
                    p["grinderModel"].toString(),
                    p["grinderBurrs"].toString(),
                    p["grinderSetting"].toString(),
                    p["barista"].toString()
                );
            }
        }
    }

    // Profile favorites
    if (json.contains("profile") && !excludeKeys.contains("profile")) {
        QJsonObject profile = json["profile"].toObject();
        if (profile.contains("current")) settings->app()->setCurrentProfile(profile["current"].toString());
        if (profile.contains("selectedFavorite")) settings->app()->setSelectedFavoriteProfile(profile["selectedFavorite"].toInt());

        if (profile.contains("favorites")) {
            QJsonArray favorites = profile["favorites"].toArray();
            qWarning() << "SettingsSerializer: importFromJson replacing" << settings->app()->favoriteProfiles().size()
                       << "favorites with" << favorites.size() << "from import";
            // Remove existing favorites in reverse
            QVariantList existingFavs = settings->app()->favoriteProfiles();
            for (qsizetype i = existingFavs.size() - 1; i >= 0; --i) {
                settings->app()->removeFavoriteProfile(static_cast<int>(i));
            }
            for (const QJsonValue& v : favorites) {
                QJsonObject f = v.toObject();
                settings->app()->addFavoriteProfile(f["name"].toString(), f["filename"].toString());
            }
        }

        if (profile.contains("selectedBuiltIns")) {
            QStringList builtIns;
            QJsonArray arr = profile["selectedBuiltIns"].toArray();
            for (const QJsonValue& v : arr) {
                builtIns.append(v.toString());
            }
            settings->app()->setSelectedBuiltInProfiles(builtIns);
        }

        if (profile.contains("hiddenProfiles")) {
            QStringList hidden;
            QJsonArray arr = profile["hiddenProfiles"].toArray();
            for (const QJsonValue& v : arr) {
                hidden.append(v.toString());
            }
            settings->app()->setHiddenProfiles(hidden);
        }
    }

    // Shot history settings
    if (json.contains("shotHistory") && !excludeKeys.contains("shotHistory")) {
        QJsonObject shotHistory = json["shotHistory"].toObject();
        if (shotHistory.contains("savedSearches")) {
            QStringList searches;
            QJsonArray arr = shotHistory["savedSearches"].toArray();
            for (const QJsonValue& v : arr) {
                searches.append(v.toString());
            }
            settings->network()->setSavedSearches(searches);
        }
        if (shotHistory.contains("sortField"))
            settings->network()->setShotHistorySortField(shotHistory["sortField"].toString());
        if (shotHistory.contains("sortDirection"))
            settings->network()->setShotHistorySortDirection(shotHistory["sortDirection"].toString());
    }

    // UI settings
    if (json.contains("ui") && !excludeKeys.contains("ui")) {
        QJsonObject ui = json["ui"].toObject();
        if (ui.contains("skin")) settings->theme()->setSkin(ui["skin"].toString());
        // screenBrightness intentionally skipped on import — device-local runtime state (#495)
        if (ui.contains("waterLevelDisplayUnit")) settings->app()->setWaterLevelDisplayUnit(ui["waterLevelDisplayUnit"].toString());

        if (ui.contains("customFontSizes")) {
            QVariantMap sizes;
            QJsonObject fontSizes = ui["customFontSizes"].toObject();
            for (auto it = fontSizes.begin(); it != fontSizes.end(); ++it) {
                sizes[it.key()] = it.value().toDouble();
            }
            settings->theme()->setCustomFontSizes(sizes);
        }

        if (ui.contains("activeShader")) settings->theme()->setActiveShader(ui["activeShader"].toString());
        if (ui.contains("shaderParams")) {
            QJsonObject sp = ui["shaderParams"].toObject();
            for (auto it = sp.begin(); it != sp.end(); ++it) {
                settings->theme()->setShaderParam(it.key(), it.value().toDouble());
            }
        }
    }

    // Theme settings
    if (json.contains("theme") && !excludeKeys.contains("theme")) {
        QJsonObject theme = json["theme"].toObject();
        if (theme.contains("activeThemeName")) settings->theme()->setActiveThemeName(theme["activeThemeName"].toString());
        if (theme.contains("themeMode")) settings->theme()->setThemeMode(theme["themeMode"].toString());

        // Restore dual palettes if present (new format)
        if (theme.contains("customColorsDark")) {
            settings->setValue("theme/customColorsDark",
                QJsonDocument(theme["customColorsDark"].toObject()).toJson());
        }
        if (theme.contains("customColorsLight")) {
            settings->setValue("theme/customColorsLight",
                QJsonDocument(theme["customColorsLight"].toObject()).toJson());
        }
        // Backward compat: old single-palette backup (always dark — light mode didn't exist)
        if (theme.contains("customColors") && !theme.contains("customColorsDark")) {
            settings->setValue("theme/customColorsDark",
                QJsonDocument(theme["customColors"].toObject()).toJson());
        }

        if (theme.contains("colorGroups")) {
            QVariantList groups;
            QJsonArray arr = theme["colorGroups"].toArray();
            for (const QJsonValue& v : arr) {
                groups.append(v.toVariant());
            }
            settings->theme()->setColorGroups(groups);
        }
    }

    // Visualizer settings
    if (json.contains("visualizer") && !excludeKeys.contains("visualizer")) {
        QJsonObject visualizer = json["visualizer"].toObject();
        if (visualizer.contains("username")) settings->visualizer()->setVisualizerUsername(visualizer["username"].toString());
        if (visualizer.contains("password") && !excludeKeys.contains("visualizerPassword")) {
            settings->visualizer()->setVisualizerPassword(visualizer["password"].toString());
        }
        if (visualizer.contains("autoUpload")) settings->visualizer()->setVisualizerAutoUpload(visualizer["autoUpload"].toBool());
        if (visualizer.contains("minDuration")) settings->visualizer()->setVisualizerMinDuration(visualizer["minDuration"].toDouble());
        if (visualizer.contains("extendedMetadata")) settings->visualizer()->setVisualizerExtendedMetadata(visualizer["extendedMetadata"].toBool());
        if (visualizer.contains("showAfterShot")) settings->visualizer()->setVisualizerShowAfterShot(visualizer["showAfterShot"].toBool());
        if (visualizer.contains("clearNotesOnStart")) settings->visualizer()->setVisualizerClearNotesOnStart(visualizer["clearNotesOnStart"].toBool());
        if (visualizer.contains("defaultShotRating")) settings->visualizer()->setDefaultShotRating(visualizer["defaultShotRating"].toInt());
    }

    // AI settings
    if (json.contains("ai") && !excludeKeys.contains("ai")) {
        QJsonObject ai = json["ai"].toObject();
        auto* aiSettings = settings->ai();
        if (ai.contains("provider")) aiSettings->setAiProvider(ai["provider"].toString());
        if (ai.contains("openaiApiKey") && !excludeKeys.contains("openaiApiKey")) {
            aiSettings->setOpenaiApiKey(ai["openaiApiKey"].toString());
        }
        if (ai.contains("anthropicApiKey") && !excludeKeys.contains("anthropicApiKey")) {
            aiSettings->setAnthropicApiKey(ai["anthropicApiKey"].toString());
        }
        if (ai.contains("geminiApiKey") && !excludeKeys.contains("geminiApiKey")) {
            aiSettings->setGeminiApiKey(ai["geminiApiKey"].toString());
        }
        if (ai.contains("ollamaEndpoint")) aiSettings->setOllamaEndpoint(ai["ollamaEndpoint"].toString());
        if (ai.contains("ollamaModel")) aiSettings->setOllamaModel(ai["ollamaModel"].toString());
        if (ai.contains("openrouterApiKey") && !excludeKeys.contains("openrouterApiKey")) {
            aiSettings->setOpenrouterApiKey(ai["openrouterApiKey"].toString());
        }
        if (ai.contains("openrouterModel")) aiSettings->setOpenrouterModel(ai["openrouterModel"].toString());
    }

    // DYE metadata
    if (json.contains("dye") && !excludeKeys.contains("dye")) {
        QJsonObject dye = json["dye"].toObject();
        if (dye.contains("beanBrand")) settings->dye()->setDyeBeanBrand(dye["beanBrand"].toString());
        if (dye.contains("beanType")) settings->dye()->setDyeBeanType(dye["beanType"].toString());
        if (dye.contains("roastDate")) settings->dye()->setDyeRoastDate(dye["roastDate"].toString());
        if (dye.contains("roastLevel")) settings->dye()->setDyeRoastLevel(dye["roastLevel"].toString());
        if (dye.contains("grinderBrand")) settings->dye()->setDyeGrinderBrand(dye["grinderBrand"].toString());
        if (dye.contains("grinderModel")) settings->dye()->setDyeGrinderModel(dye["grinderModel"].toString());
        if (dye.contains("grinderBurrs")) settings->dye()->setDyeGrinderBurrs(dye["grinderBurrs"].toString());
        if (dye.contains("grinderSetting")) settings->dye()->setDyeGrinderSetting(dye["grinderSetting"].toString());
        if (dye.contains("beanWeight")) settings->dye()->setDyeBeanWeight(dye["beanWeight"].toDouble());
        if (dye.contains("drinkWeight")) settings->dye()->setDyeDrinkWeight(dye["drinkWeight"].toDouble());
        if (dye.contains("drinkTds")) settings->dye()->setDyeDrinkTds(dye["drinkTds"].toDouble());
        if (dye.contains("drinkEy")) settings->dye()->setDyeDrinkEy(dye["drinkEy"].toDouble());
        if (dye.contains("espressoEnjoyment")) settings->dye()->setDyeEspressoEnjoyment(dye["espressoEnjoyment"].toInt());
        // Shot notes: try new key first, fall back to old key
        if (dye.contains("shotNotes")) settings->dye()->setDyeShotNotes(dye["shotNotes"].toString());
        else if (dye.contains("espressoNotes")) settings->dye()->setDyeShotNotes(dye["espressoNotes"].toString());
        if (dye.contains("barista")) settings->dye()->setDyeBarista(dye["barista"].toString());
        if (dye.contains("shotDateTime")) settings->dye()->setDyeShotDateTime(dye["shotDateTime"].toString());
    }

    // Shot server settings
    if (json.contains("shotServer") && !excludeKeys.contains("shotServer")) {
        QJsonObject shotServer = json["shotServer"].toObject();
        if (shotServer.contains("enabled")) settings->network()->setShotServerEnabled(shotServer["enabled"].toBool());
        if (shotServer.contains("hostname")) settings->network()->setShotServerHostname(shotServer["hostname"].toString());
        if (shotServer.contains("port")) settings->network()->setShotServerPort(shotServer["port"].toInt());
        if (shotServer.contains("webSecurityEnabled")) settings->network()->setWebSecurityEnabled(shotServer["webSecurityEnabled"].toBool());
    }

    // Auto-update settings
    if (json.contains("updates") && !excludeKeys.contains("updates")) {
        QJsonObject updates = json["updates"].toObject();
        if (updates.contains("autoCheck")) settings->app()->setAutoCheckUpdates(updates["autoCheck"].toBool());
        if (updates.contains("betaUpdatesEnabled")) settings->app()->setBetaUpdatesEnabled(updates["betaUpdatesEnabled"].toBool());
    }

    // Developer settings
    if (json.contains("developer") && !excludeKeys.contains("developer")) {
        QJsonObject developer = json["developer"].toObject();
        if (developer.contains("translationUpload")) {
            settings->app()->setDeveloperTranslationUpload(developer["translationUpload"].toBool());
        }
    }

    // Auto-wake schedule
    if (json.contains("autoWake") && !excludeKeys.contains("autoWake")) {
        QJsonObject autoWake = json["autoWake"].toObject();
        auto* autoWakeSettings = settings->autoWake();
        if (autoWake.contains("enabled")) autoWakeSettings->setAutoWakeEnabled(autoWake["enabled"].toBool());
        if (autoWake.contains("schedule")) {
            QVariantList schedule;
            QJsonArray arr = autoWake["schedule"].toArray();
            for (const QJsonValue& v : arr) {
                schedule.append(v.toVariant());
            }
            autoWakeSettings->setAutoWakeSchedule(schedule);
        }
        if (autoWake.contains("stayAwakeEnabled")) autoWakeSettings->setAutoWakeStayAwakeEnabled(autoWake["stayAwakeEnabled"].toBool());
        if (autoWake.contains("stayAwakeMinutes")) autoWakeSettings->setAutoWakeStayAwakeMinutes(autoWake["stayAwakeMinutes"].toInt());
    }

    // Auto-favorites settings
    if (json.contains("autoFavorites") && !excludeKeys.contains("autoFavorites")) {
        QJsonObject af = json["autoFavorites"].toObject();
        if (af.contains("groupBy")) settings->network()->setAutoFavoritesGroupBy(af["groupBy"].toString());
        if (af.contains("maxItems")) settings->network()->setAutoFavoritesMaxItems(af["maxItems"].toInt());
        if (af.contains("openBrewSettings")) settings->network()->setAutoFavoritesOpenBrewSettings(af["openBrewSettings"].toBool());
        if (af.contains("hideUnrated")) settings->network()->setAutoFavoritesHideUnrated(af["hideUnrated"].toBool());
    }

    // MQTT settings
    if (json.contains("mqtt") && !excludeKeys.contains("mqtt")) {
        QJsonObject mqtt = json["mqtt"].toObject();
        auto* mqttSettings = settings->mqtt();
        if (mqtt.contains("enabled")) mqttSettings->setMqttEnabled(mqtt["enabled"].toBool());
        if (mqtt.contains("brokerHost")) mqttSettings->setMqttBrokerHost(mqtt["brokerHost"].toString());
        if (mqtt.contains("brokerPort")) mqttSettings->setMqttBrokerPort(mqtt["brokerPort"].toInt());
        if (mqtt.contains("username")) mqttSettings->setMqttUsername(mqtt["username"].toString());
        if (mqtt.contains("password") && !excludeKeys.contains("mqttPassword")) {
            mqttSettings->setMqttPassword(mqtt["password"].toString());
        }
        if (mqtt.contains("baseTopic")) mqttSettings->setMqttBaseTopic(mqtt["baseTopic"].toString());
        if (mqtt.contains("publishInterval")) mqttSettings->setMqttPublishInterval(mqtt["publishInterval"].toInt());
        if (mqtt.contains("retainMessages")) mqttSettings->setMqttRetainMessages(mqtt["retainMessages"].toBool());
        if (mqtt.contains("homeAssistantDiscovery")) mqttSettings->setMqttHomeAssistantDiscovery(mqtt["homeAssistantDiscovery"].toBool());
        if (mqtt.contains("clientId")) mqttSettings->setMqttClientId(mqtt["clientId"].toString());
    }

    // Layout configuration
    if (json.contains("layoutConfiguration") && !excludeKeys.contains("layoutConfiguration")) {
        settings->network()->setLayoutConfiguration(json["layoutConfiguration"].toString());
    }

    // Machine tuning
    if (json.contains("machineTuning") && !excludeKeys.contains("machineTuning")) {
        QJsonObject mt = json["machineTuning"].toObject();
        if (mt.contains("waterRefillPoint")) settings->app()->setWaterRefillPoint(mt["waterRefillPoint"].toInt());
        if (mt.contains("refillKitOverride")) settings->app()->setRefillKitOverride(mt["refillKitOverride"].toInt());
        {
            auto* hw = settings->hardware();
            if (mt.contains("heaterIdleTemp")) hw->setHeaterIdleTemp(mt["heaterIdleTemp"].toInt());
            if (mt.contains("heaterWarmupFlow")) hw->setHeaterWarmupFlow(mt["heaterWarmupFlow"].toInt());
            if (mt.contains("heaterTestFlow")) hw->setHeaterTestFlow(mt["heaterTestFlow"].toInt());
            if (mt.contains("heaterWarmupTimeout")) hw->setHeaterWarmupTimeout(mt["heaterWarmupTimeout"].toInt());
            if (mt.contains("hotWaterFlowRate")) hw->setHotWaterFlowRate(mt["hotWaterFlowRate"].toInt());
        }
        // Flow calibration is machine-specific — skip during cross-device migration
        // but restore from same-machine backups (caller passes excludeKeys to control this)
        if (mt.contains("flowCalibrationMultiplier") && !excludeKeys.contains("flowCalibration")) {
            settings->calibration()->setFlowCalibrationMultiplier(mt["flowCalibrationMultiplier"].toDouble());
        }
        if (mt.contains("autoFlowCalibration") && !excludeKeys.contains("flowCalibration")) {
            settings->calibration()->setAutoFlowCalibration(mt["autoFlowCalibration"].toBool());
        }
        if (mt.contains("ignoreVolumeWithScale")) {
            settings->brew()->setIgnoreVolumeWithScale(mt["ignoreVolumeWithScale"].toBool());
        }
        if (mt.contains("perProfileFlowCalibration") && !excludeKeys.contains("flowCalibration")) {
            QJsonObject perProfile = mt["perProfileFlowCalibration"].toObject();
            int imported = 0, rejected = 0;
            for (auto it = perProfile.begin(); it != perProfile.end(); ++it) {
                if (!it.value().isDouble()) {
                    qWarning() << "Settings import: flow calibration for" << it.key()
                               << "is not a number (type:" << it.value().type() << "), skipping";
                    rejected++;
                    continue;
                }
                double val = it.value().toDouble();
                if (val >= 0.5 && val <= 2.7) {
                    settings->calibration()->setProfileFlowCalibration(it.key(), val);
                    imported++;
                } else {
                    qWarning() << "Settings import: flow calibration out of bounds for"
                               << it.key() << ":" << val << "(expected [0.5, 2.7])";
                    rejected++;
                }
            }
            if (rejected > 0) {
                qWarning() << "Settings import: per-profile flow calibration -"
                           << imported << "imported," << rejected << "rejected";
            }
        }
    }

    // Daily backup hour
    if (json.contains("dailyBackupHour") && !excludeKeys.contains("dailyBackupHour")) {
        settings->app()->setDailyBackupHour(json["dailyBackupHour"].toInt());
    }

    // Sync to disk
    settings->sync();

    return true;
}

QJsonValue SettingsSerializer::variantToJson(const QVariant& value)
{
    return QJsonValue::fromVariant(value);
}

QVariant SettingsSerializer::jsonToVariant(const QJsonValue& value, const QString& key)
{
    Q_UNUSED(key)
    return value.toVariant();
}
