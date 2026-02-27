#include "settingsserializer.h"
#include "settings.h"
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

    // Espresso settings
    QJsonObject espresso;
    espresso["temperature"] = settings->espressoTemperature();
    espresso["targetWeight"] = settings->targetWeight();
    espresso["lastUsedRatio"] = settings->lastUsedRatio();
    root["espresso"] = espresso;

    // Steam settings
    QJsonObject steam;
    steam["temperature"] = settings->steamTemperature();
    steam["timeout"] = settings->steamTimeout();
    steam["flow"] = settings->steamFlow();
    steam["keepHeaterOn"] = settings->keepSteamHeaterOn();
    steam["disabled"] = settings->steamDisabled();
    steam["autoFlushSeconds"] = settings->steamAutoFlushSeconds();
    steam["twoTapStop"] = settings->steamTwoTapStop();
    steam["selectedPitcher"] = settings->selectedSteamPitcher();

    // Steam pitcher presets
    QJsonArray pitcherPresets;
    for (const QVariant& preset : settings->steamPitcherPresets()) {
        QJsonObject p;
        QVariantMap m = preset.toMap();
        p["name"] = m["name"].toString();
        p["duration"] = m["duration"].toInt();
        p["flow"] = m["flow"].toInt();
        pitcherPresets.append(p);
    }
    steam["pitcherPresets"] = pitcherPresets;
    root["steam"] = steam;

    // Headless settings
    QJsonObject headless;
    headless["skipPurgeConfirm"] = settings->headlessSkipPurgeConfirm();
    root["headless"] = headless;

    // Launcher mode
    root["launcherMode"] = settings->launcherMode();

    // Hot water settings
    QJsonObject water;
    water["temperature"] = settings->waterTemperature();
    water["volume"] = settings->waterVolume();
    water["volumeMode"] = settings->waterVolumeMode();
    water["selectedVessel"] = settings->selectedWaterVessel();

    // Water vessel presets
    QJsonArray vesselPresets;
    for (const QVariant& preset : settings->waterVesselPresets()) {
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
    flush["flow"] = settings->flushFlow();
    flush["seconds"] = settings->flushSeconds();
    flush["selectedPreset"] = settings->selectedFlushPreset();

    // Flush presets
    QJsonArray flushPresets;
    for (const QVariant& preset : settings->flushPresets()) {
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
    for (const QVariant& preset : settings->beanPresets()) {
        QJsonObject p;
        QVariantMap m = preset.toMap();
        p["name"] = m["name"].toString();
        p["brand"] = m["brand"].toString();
        p["type"] = m["type"].toString();
        p["roastDate"] = m["roastDate"].toString();
        p["roastLevel"] = m["roastLevel"].toString();
        p["grinderModel"] = m["grinderModel"].toString();
        p["grinderSetting"] = m["grinderSetting"].toString();
        beanPresets.append(p);
    }
    QJsonObject beans;
    beans["presets"] = beanPresets;
    beans["selectedPreset"] = settings->selectedBeanPreset();
    root["beans"] = beans;

    // Profile favorites
    QJsonObject profile;
    profile["current"] = settings->currentProfile();
    profile["selectedFavorite"] = settings->selectedFavoriteProfile();

    QJsonArray favorites;
    for (const QVariant& fav : settings->favoriteProfiles()) {
        QJsonObject f;
        QVariantMap m = fav.toMap();
        f["name"] = m["name"].toString();
        f["filename"] = m["filename"].toString();
        favorites.append(f);
    }
    profile["favorites"] = favorites;

    QJsonArray selectedBuiltIns;
    for (const QString& s : settings->selectedBuiltInProfiles()) {
        selectedBuiltIns.append(s);
    }
    profile["selectedBuiltIns"] = selectedBuiltIns;

    QJsonArray hiddenProfiles;
    for (const QString& s : settings->hiddenProfiles()) {
        hiddenProfiles.append(s);
    }
    profile["hiddenProfiles"] = hiddenProfiles;
    root["profile"] = profile;

    // Shot history settings
    QJsonObject shotHistory;
    QJsonArray savedSearchesArr;
    for (const QString& s : settings->savedSearches()) {
        savedSearchesArr.append(s);
    }
    shotHistory["savedSearches"] = savedSearchesArr;
    shotHistory["sortField"] = settings->shotHistorySortField();
    shotHistory["sortDirection"] = settings->shotHistorySortDirection();
    root["shotHistory"] = shotHistory;

    // UI settings
    QJsonObject ui;
    ui["skin"] = settings->skin();
    ui["screenBrightness"] = settings->screenBrightness();
    ui["waterLevelDisplayUnit"] = settings->waterLevelDisplayUnit();

    // Custom font sizes
    QJsonObject fontSizes;
    QVariantMap sizes = settings->customFontSizes();
    for (auto it = sizes.begin(); it != sizes.end(); ++it) {
        fontSizes[it.key()] = it.value().toDouble();
    }
    ui["customFontSizes"] = fontSizes;

    // Shader settings
    ui["activeShader"] = settings->activeShader();
    QJsonObject shaderParams;
    QVariantMap params = settings->shaderParams();
    for (auto it = params.begin(); it != params.end(); ++it) {
        shaderParams[it.key()] = QJsonValue::fromVariant(it.value());
    }
    ui["shaderParams"] = shaderParams;
    root["ui"] = ui;

    // Theme settings
    QJsonObject theme;
    theme["activeThemeName"] = settings->activeThemeName();

    // Custom theme colors
    QJsonObject customColors;
    QVariantMap colors = settings->customThemeColors();
    for (auto it = colors.begin(); it != colors.end(); ++it) {
        customColors[it.key()] = it.value().toString();
    }
    theme["customColors"] = customColors;

    // Color groups
    QJsonArray colorGroups;
    for (const QVariant& group : settings->colorGroups()) {
        colorGroups.append(QJsonValue::fromVariant(group));
    }
    theme["colorGroups"] = colorGroups;
    root["theme"] = theme;

    // Visualizer settings
    QJsonObject visualizer;
    visualizer["username"] = settings->visualizerUsername();
    if (includeSensitive) {
        visualizer["password"] = settings->visualizerPassword();
    }
    visualizer["autoUpload"] = settings->visualizerAutoUpload();
    visualizer["minDuration"] = settings->visualizerMinDuration();
    visualizer["extendedMetadata"] = settings->visualizerExtendedMetadata();
    visualizer["showAfterShot"] = settings->visualizerShowAfterShot();
    visualizer["clearNotesOnStart"] = settings->visualizerClearNotesOnStart();
    visualizer["defaultShotRating"] = settings->defaultShotRating();
    root["visualizer"] = visualizer;

    // AI settings
    QJsonObject ai;
    ai["provider"] = settings->aiProvider();
    if (includeSensitive) {
        ai["openaiApiKey"] = settings->openaiApiKey();
        ai["anthropicApiKey"] = settings->anthropicApiKey();
        ai["geminiApiKey"] = settings->geminiApiKey();
    }
    ai["ollamaEndpoint"] = settings->ollamaEndpoint();
    ai["ollamaModel"] = settings->ollamaModel();
    if (includeSensitive) {
        ai["openrouterApiKey"] = settings->openrouterApiKey();
    }
    ai["openrouterModel"] = settings->openrouterModel();
    root["ai"] = ai;

    // DYE (Describe Your Espresso) metadata
    QJsonObject dye;
    dye["beanBrand"] = settings->dyeBeanBrand();
    dye["beanType"] = settings->dyeBeanType();
    dye["roastDate"] = settings->dyeRoastDate();
    dye["roastLevel"] = settings->dyeRoastLevel();
    dye["grinderModel"] = settings->dyeGrinderModel();
    dye["grinderSetting"] = settings->dyeGrinderSetting();
    dye["beanWeight"] = settings->dyeBeanWeight();
    dye["drinkWeight"] = settings->dyeDrinkWeight();
    dye["drinkTds"] = settings->dyeDrinkTds();
    dye["drinkEy"] = settings->dyeDrinkEy();
    dye["espressoEnjoyment"] = settings->dyeEspressoEnjoyment();
    dye["shotNotes"] = settings->dyeShotNotes();
    dye["barista"] = settings->dyeBarista();
    dye["shotDateTime"] = settings->dyeShotDateTime();
    root["dye"] = dye;

    // Shot server settings
    QJsonObject shotServer;
    shotServer["enabled"] = settings->shotServerEnabled();
    shotServer["hostname"] = settings->shotServerHostname();
    shotServer["port"] = settings->shotServerPort();
    shotServer["webSecurityEnabled"] = settings->webSecurityEnabled();
    root["shotServer"] = shotServer;

    // Auto-update settings
    QJsonObject updates;
    updates["autoCheck"] = settings->autoCheckUpdates();
    updates["betaUpdatesEnabled"] = settings->betaUpdatesEnabled();
    root["updates"] = updates;

    // Developer settings - intentionally not exported (session-only Easter eggs)

    // Auto-wake schedule
    QJsonObject autoWake;
    autoWake["enabled"] = settings->autoWakeEnabled();
    QJsonArray schedule;
    for (const QVariant& day : settings->autoWakeSchedule()) {
        schedule.append(QJsonValue::fromVariant(day));
    }
    autoWake["schedule"] = schedule;
    autoWake["stayAwakeEnabled"] = settings->autoWakeStayAwakeEnabled();
    autoWake["stayAwakeMinutes"] = settings->autoWakeStayAwakeMinutes();
    root["autoWake"] = autoWake;

    // Auto-favorites settings
    QJsonObject autoFavorites;
    autoFavorites["groupBy"] = settings->autoFavoritesGroupBy();
    autoFavorites["maxItems"] = settings->autoFavoritesMaxItems();
    autoFavorites["openBrewSettings"] = settings->autoFavoritesOpenBrewSettings();
    autoFavorites["hideUnrated"] = settings->autoFavoritesHideUnrated();
    root["autoFavorites"] = autoFavorites;

    // MQTT settings (Home Automation)
    QJsonObject mqtt;
    mqtt["enabled"] = settings->mqttEnabled();
    mqtt["brokerHost"] = settings->mqttBrokerHost();
    mqtt["brokerPort"] = settings->mqttBrokerPort();
    mqtt["username"] = settings->mqttUsername();
    if (includeSensitive) {
        mqtt["password"] = settings->mqttPassword();
    }
    mqtt["baseTopic"] = settings->mqttBaseTopic();
    mqtt["publishInterval"] = settings->mqttPublishInterval();
    mqtt["retainMessages"] = settings->mqttRetainMessages();
    mqtt["homeAssistantDiscovery"] = settings->mqttHomeAssistantDiscovery();
    mqtt["clientId"] = settings->mqttClientId();
    root["mqtt"] = mqtt;

    // Layout configuration
    root["layoutConfiguration"] = settings->layoutConfiguration();

    // Machine tuning
    QJsonObject machineTuning;
    machineTuning["waterRefillPoint"] = settings->waterRefillPoint();
    machineTuning["refillKitOverride"] = settings->refillKitOverride();
    machineTuning["heaterIdleTemp"] = settings->heaterIdleTemp();
    machineTuning["heaterWarmupFlow"] = settings->heaterWarmupFlow();
    machineTuning["heaterTestFlow"] = settings->heaterTestFlow();
    machineTuning["heaterWarmupTimeout"] = settings->heaterWarmupTimeout();
    machineTuning["hotWaterFlowRate"] = settings->hotWaterFlowRate();
    machineTuning["flowCalibrationMultiplier"] = settings->flowCalibrationMultiplier();
    machineTuning["autoFlowCalibration"] = settings->autoFlowCalibration();
    QJsonObject perProfileMap = settings->allProfileFlowCalibrations();
    if (!perProfileMap.isEmpty()) {
        machineTuning["perProfileFlowCalibration"] = perProfileMap;
    }
    root["machineTuning"] = machineTuning;

    // BLE health refresh
    root["bleHealthRefreshEnabled"] = settings->bleHealthRefreshEnabled();

    // Daily backup hour
    root["dailyBackupHour"] = settings->dailyBackupHour();

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

    // Espresso settings
    if (json.contains("espresso") && !excludeKeys.contains("espresso")) {
        QJsonObject espresso = json["espresso"].toObject();
        if (espresso.contains("temperature")) settings->setEspressoTemperature(espresso["temperature"].toDouble());
        if (espresso.contains("targetWeight")) settings->setTargetWeight(espresso["targetWeight"].toDouble());
        if (espresso.contains("lastUsedRatio")) settings->setLastUsedRatio(espresso["lastUsedRatio"].toDouble());
    }

    // Steam settings
    if (json.contains("steam") && !excludeKeys.contains("steam")) {
        QJsonObject steam = json["steam"].toObject();
        if (steam.contains("temperature")) settings->setSteamTemperature(steam["temperature"].toDouble());
        if (steam.contains("timeout")) settings->setSteamTimeout(steam["timeout"].toInt());
        if (steam.contains("flow")) settings->setSteamFlow(steam["flow"].toInt());
        if (steam.contains("keepHeaterOn")) settings->setKeepSteamHeaterOn(steam["keepHeaterOn"].toBool());
        if (steam.contains("disabled")) settings->setSteamDisabled(steam["disabled"].toBool());
        if (steam.contains("autoFlushSeconds")) settings->setSteamAutoFlushSeconds(steam["autoFlushSeconds"].toInt());
        if (steam.contains("twoTapStop")) settings->setSteamTwoTapStop(steam["twoTapStop"].toBool());
        if (steam.contains("selectedPitcher")) settings->setSelectedSteamCup(steam["selectedPitcher"].toInt());

        // Import pitcher presets
        if (steam.contains("pitcherPresets")) {
            // Clear existing presets first by removing them in reverse order
            QVariantList existingPresets = settings->steamPitcherPresets();
            for (int i = existingPresets.size() - 1; i >= 0; --i) {
                settings->removeSteamPitcherPreset(i);
            }
            // Add imported presets
            QJsonArray presets = steam["pitcherPresets"].toArray();
            for (const QJsonValue& v : presets) {
                QJsonObject p = v.toObject();
                settings->addSteamPitcherPreset(p["name"].toString(), p["duration"].toInt(), p["flow"].toInt());
            }
        }
    }

    // Headless settings
    if (json.contains("headless") && !excludeKeys.contains("headless")) {
        QJsonObject headless = json["headless"].toObject();
        if (headless.contains("skipPurgeConfirm")) {
            settings->setHeadlessSkipPurgeConfirm(headless["skipPurgeConfirm"].toBool());
        }
    }

    // Launcher mode
    if (json.contains("launcherMode") && !excludeKeys.contains("launcherMode")) {
        settings->setLauncherMode(json["launcherMode"].toBool());
    }

    // Hot water settings
    if (json.contains("water") && !excludeKeys.contains("water")) {
        QJsonObject water = json["water"].toObject();
        if (water.contains("temperature")) settings->setWaterTemperature(water["temperature"].toDouble());
        if (water.contains("volume")) settings->setWaterVolume(water["volume"].toInt());
        if (water.contains("volumeMode")) settings->setWaterVolumeMode(water["volumeMode"].toString());
        if (water.contains("selectedVessel")) settings->setSelectedWaterCup(water["selectedVessel"].toInt());

        // Import vessel presets
        if (water.contains("vesselPresets")) {
            QVariantList existingPresets = settings->waterVesselPresets();
            for (int i = existingPresets.size() - 1; i >= 0; --i) {
                settings->removeWaterVesselPreset(i);
            }
            QJsonArray presets = water["vesselPresets"].toArray();
            for (const QJsonValue& v : presets) {
                QJsonObject p = v.toObject();
                settings->addWaterVesselPreset(p["name"].toString(), p["volume"].toInt(),
                                               p["mode"].toString("weight"), p["flowRate"].toInt(40));
            }
        }
    }

    // Flush settings
    if (json.contains("flush") && !excludeKeys.contains("flush")) {
        QJsonObject flush = json["flush"].toObject();
        if (flush.contains("flow")) settings->setFlushFlow(flush["flow"].toDouble());
        if (flush.contains("seconds")) settings->setFlushSeconds(flush["seconds"].toDouble());
        if (flush.contains("selectedPreset")) settings->setSelectedFlushPreset(flush["selectedPreset"].toInt());

        // Import flush presets
        if (flush.contains("presets")) {
            QVariantList existingPresets = settings->flushPresets();
            for (int i = existingPresets.size() - 1; i >= 0; --i) {
                settings->removeFlushPreset(i);
            }
            QJsonArray presets = flush["presets"].toArray();
            for (const QJsonValue& v : presets) {
                QJsonObject p = v.toObject();
                settings->addFlushPreset(p["name"].toString(), p["flow"].toDouble(), p["seconds"].toDouble());
            }
        }
    }

    // Bean presets
    if (json.contains("beans") && !excludeKeys.contains("beans")) {
        QJsonObject beans = json["beans"].toObject();
        if (beans.contains("selectedPreset")) settings->setSelectedBeanPreset(beans["selectedPreset"].toInt());

        if (beans.contains("presets")) {
            QVariantList existingPresets = settings->beanPresets();
            for (int i = existingPresets.size() - 1; i >= 0; --i) {
                settings->removeBeanPreset(i);
            }
            QJsonArray presets = beans["presets"].toArray();
            for (const QJsonValue& v : presets) {
                QJsonObject p = v.toObject();
                settings->addBeanPreset(
                    p["name"].toString(),
                    p["brand"].toString(),
                    p["type"].toString(),
                    p["roastDate"].toString(),
                    p["roastLevel"].toString(),
                    p["grinderModel"].toString(),
                    p["grinderSetting"].toString()
                );
            }
        }
    }

    // Profile favorites
    if (json.contains("profile") && !excludeKeys.contains("profile")) {
        QJsonObject profile = json["profile"].toObject();
        if (profile.contains("current")) settings->setCurrentProfile(profile["current"].toString());
        if (profile.contains("selectedFavorite")) settings->setSelectedFavoriteProfile(profile["selectedFavorite"].toInt());

        if (profile.contains("favorites")) {
            // Remove existing favorites in reverse
            QVariantList existingFavs = settings->favoriteProfiles();
            for (int i = existingFavs.size() - 1; i >= 0; --i) {
                settings->removeFavoriteProfile(i);
            }
            QJsonArray favorites = profile["favorites"].toArray();
            for (const QJsonValue& v : favorites) {
                QJsonObject f = v.toObject();
                settings->addFavoriteProfile(f["name"].toString(), f["filename"].toString());
            }
        }

        if (profile.contains("selectedBuiltIns")) {
            QStringList builtIns;
            QJsonArray arr = profile["selectedBuiltIns"].toArray();
            for (const QJsonValue& v : arr) {
                builtIns.append(v.toString());
            }
            settings->setSelectedBuiltInProfiles(builtIns);
        }

        if (profile.contains("hiddenProfiles")) {
            QStringList hidden;
            QJsonArray arr = profile["hiddenProfiles"].toArray();
            for (const QJsonValue& v : arr) {
                hidden.append(v.toString());
            }
            settings->setHiddenProfiles(hidden);
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
            settings->setSavedSearches(searches);
        }
        if (shotHistory.contains("sortField"))
            settings->setShotHistorySortField(shotHistory["sortField"].toString());
        if (shotHistory.contains("sortDirection"))
            settings->setShotHistorySortDirection(shotHistory["sortDirection"].toString());
    }

    // UI settings
    if (json.contains("ui") && !excludeKeys.contains("ui")) {
        QJsonObject ui = json["ui"].toObject();
        if (ui.contains("skin")) settings->setSkin(ui["skin"].toString());
        if (ui.contains("screenBrightness")) settings->setScreenBrightness(ui["screenBrightness"].toDouble());
        if (ui.contains("waterLevelDisplayUnit")) settings->setWaterLevelDisplayUnit(ui["waterLevelDisplayUnit"].toString());

        if (ui.contains("customFontSizes")) {
            QVariantMap sizes;
            QJsonObject fontSizes = ui["customFontSizes"].toObject();
            for (auto it = fontSizes.begin(); it != fontSizes.end(); ++it) {
                sizes[it.key()] = it.value().toDouble();
            }
            settings->setCustomFontSizes(sizes);
        }

        if (ui.contains("activeShader")) settings->setActiveShader(ui["activeShader"].toString());
        if (ui.contains("shaderParams")) {
            QJsonObject sp = ui["shaderParams"].toObject();
            for (auto it = sp.begin(); it != sp.end(); ++it) {
                settings->setShaderParam(it.key(), it.value().toDouble());
            }
        }
    }

    // Theme settings
    if (json.contains("theme") && !excludeKeys.contains("theme")) {
        QJsonObject theme = json["theme"].toObject();
        if (theme.contains("activeThemeName")) settings->setActiveThemeName(theme["activeThemeName"].toString());

        if (theme.contains("customColors")) {
            QVariantMap colors;
            QJsonObject customColors = theme["customColors"].toObject();
            for (auto it = customColors.begin(); it != customColors.end(); ++it) {
                colors[it.key()] = it.value().toString();
            }
            settings->setCustomThemeColors(colors);
        }

        if (theme.contains("colorGroups")) {
            QVariantList groups;
            QJsonArray arr = theme["colorGroups"].toArray();
            for (const QJsonValue& v : arr) {
                groups.append(v.toVariant());
            }
            settings->setColorGroups(groups);
        }
    }

    // Visualizer settings
    if (json.contains("visualizer") && !excludeKeys.contains("visualizer")) {
        QJsonObject visualizer = json["visualizer"].toObject();
        if (visualizer.contains("username")) settings->setVisualizerUsername(visualizer["username"].toString());
        if (visualizer.contains("password") && !excludeKeys.contains("visualizerPassword")) {
            settings->setVisualizerPassword(visualizer["password"].toString());
        }
        if (visualizer.contains("autoUpload")) settings->setVisualizerAutoUpload(visualizer["autoUpload"].toBool());
        if (visualizer.contains("minDuration")) settings->setVisualizerMinDuration(visualizer["minDuration"].toDouble());
        if (visualizer.contains("extendedMetadata")) settings->setVisualizerExtendedMetadata(visualizer["extendedMetadata"].toBool());
        if (visualizer.contains("showAfterShot")) settings->setVisualizerShowAfterShot(visualizer["showAfterShot"].toBool());
        if (visualizer.contains("clearNotesOnStart")) settings->setVisualizerClearNotesOnStart(visualizer["clearNotesOnStart"].toBool());
        if (visualizer.contains("defaultShotRating")) settings->setDefaultShotRating(visualizer["defaultShotRating"].toInt());
    }

    // AI settings
    if (json.contains("ai") && !excludeKeys.contains("ai")) {
        QJsonObject ai = json["ai"].toObject();
        if (ai.contains("provider")) settings->setAiProvider(ai["provider"].toString());
        if (ai.contains("openaiApiKey") && !excludeKeys.contains("openaiApiKey")) {
            settings->setOpenaiApiKey(ai["openaiApiKey"].toString());
        }
        if (ai.contains("anthropicApiKey") && !excludeKeys.contains("anthropicApiKey")) {
            settings->setAnthropicApiKey(ai["anthropicApiKey"].toString());
        }
        if (ai.contains("geminiApiKey") && !excludeKeys.contains("geminiApiKey")) {
            settings->setGeminiApiKey(ai["geminiApiKey"].toString());
        }
        if (ai.contains("ollamaEndpoint")) settings->setOllamaEndpoint(ai["ollamaEndpoint"].toString());
        if (ai.contains("ollamaModel")) settings->setOllamaModel(ai["ollamaModel"].toString());
        if (ai.contains("openrouterApiKey") && !excludeKeys.contains("openrouterApiKey")) {
            settings->setOpenrouterApiKey(ai["openrouterApiKey"].toString());
        }
        if (ai.contains("openrouterModel")) settings->setOpenrouterModel(ai["openrouterModel"].toString());
    }

    // DYE metadata
    if (json.contains("dye") && !excludeKeys.contains("dye")) {
        QJsonObject dye = json["dye"].toObject();
        if (dye.contains("beanBrand")) settings->setDyeBeanBrand(dye["beanBrand"].toString());
        if (dye.contains("beanType")) settings->setDyeBeanType(dye["beanType"].toString());
        if (dye.contains("roastDate")) settings->setDyeRoastDate(dye["roastDate"].toString());
        if (dye.contains("roastLevel")) settings->setDyeRoastLevel(dye["roastLevel"].toString());
        if (dye.contains("grinderModel")) settings->setDyeGrinderModel(dye["grinderModel"].toString());
        if (dye.contains("grinderSetting")) settings->setDyeGrinderSetting(dye["grinderSetting"].toString());
        if (dye.contains("beanWeight")) settings->setDyeBeanWeight(dye["beanWeight"].toDouble());
        if (dye.contains("drinkWeight")) settings->setDyeDrinkWeight(dye["drinkWeight"].toDouble());
        if (dye.contains("drinkTds")) settings->setDyeDrinkTds(dye["drinkTds"].toDouble());
        if (dye.contains("drinkEy")) settings->setDyeDrinkEy(dye["drinkEy"].toDouble());
        if (dye.contains("espressoEnjoyment")) settings->setDyeEspressoEnjoyment(dye["espressoEnjoyment"].toInt());
        // Shot notes: try new key first, fall back to old key
        if (dye.contains("shotNotes")) settings->setDyeShotNotes(dye["shotNotes"].toString());
        else if (dye.contains("espressoNotes")) settings->setDyeShotNotes(dye["espressoNotes"].toString());
        if (dye.contains("barista")) settings->setDyeBarista(dye["barista"].toString());
        if (dye.contains("shotDateTime")) settings->setDyeShotDateTime(dye["shotDateTime"].toString());
    }

    // Shot server settings
    if (json.contains("shotServer") && !excludeKeys.contains("shotServer")) {
        QJsonObject shotServer = json["shotServer"].toObject();
        if (shotServer.contains("enabled")) settings->setShotServerEnabled(shotServer["enabled"].toBool());
        if (shotServer.contains("hostname")) settings->setShotServerHostname(shotServer["hostname"].toString());
        if (shotServer.contains("port")) settings->setShotServerPort(shotServer["port"].toInt());
        if (shotServer.contains("webSecurityEnabled")) settings->setWebSecurityEnabled(shotServer["webSecurityEnabled"].toBool());
    }

    // Auto-update settings
    if (json.contains("updates") && !excludeKeys.contains("updates")) {
        QJsonObject updates = json["updates"].toObject();
        if (updates.contains("autoCheck")) settings->setAutoCheckUpdates(updates["autoCheck"].toBool());
        if (updates.contains("betaUpdatesEnabled")) settings->setBetaUpdatesEnabled(updates["betaUpdatesEnabled"].toBool());
    }

    // Developer settings
    if (json.contains("developer") && !excludeKeys.contains("developer")) {
        QJsonObject developer = json["developer"].toObject();
        if (developer.contains("translationUpload")) {
            settings->setDeveloperTranslationUpload(developer["translationUpload"].toBool());
        }
    }

    // Auto-wake schedule
    if (json.contains("autoWake") && !excludeKeys.contains("autoWake")) {
        QJsonObject autoWake = json["autoWake"].toObject();
        if (autoWake.contains("enabled")) settings->setAutoWakeEnabled(autoWake["enabled"].toBool());
        if (autoWake.contains("schedule")) {
            QVariantList schedule;
            QJsonArray arr = autoWake["schedule"].toArray();
            for (const QJsonValue& v : arr) {
                schedule.append(v.toVariant());
            }
            settings->setAutoWakeSchedule(schedule);
        }
        if (autoWake.contains("stayAwakeEnabled")) settings->setAutoWakeStayAwakeEnabled(autoWake["stayAwakeEnabled"].toBool());
        if (autoWake.contains("stayAwakeMinutes")) settings->setAutoWakeStayAwakeMinutes(autoWake["stayAwakeMinutes"].toInt());
    }

    // Auto-favorites settings
    if (json.contains("autoFavorites") && !excludeKeys.contains("autoFavorites")) {
        QJsonObject af = json["autoFavorites"].toObject();
        if (af.contains("groupBy")) settings->setAutoFavoritesGroupBy(af["groupBy"].toString());
        if (af.contains("maxItems")) settings->setAutoFavoritesMaxItems(af["maxItems"].toInt());
        if (af.contains("openBrewSettings")) settings->setAutoFavoritesOpenBrewSettings(af["openBrewSettings"].toBool());
        if (af.contains("hideUnrated")) settings->setAutoFavoritesHideUnrated(af["hideUnrated"].toBool());
    }

    // MQTT settings
    if (json.contains("mqtt") && !excludeKeys.contains("mqtt")) {
        QJsonObject mqtt = json["mqtt"].toObject();
        if (mqtt.contains("enabled")) settings->setMqttEnabled(mqtt["enabled"].toBool());
        if (mqtt.contains("brokerHost")) settings->setMqttBrokerHost(mqtt["brokerHost"].toString());
        if (mqtt.contains("brokerPort")) settings->setMqttBrokerPort(mqtt["brokerPort"].toInt());
        if (mqtt.contains("username")) settings->setMqttUsername(mqtt["username"].toString());
        if (mqtt.contains("password") && !excludeKeys.contains("mqttPassword")) {
            settings->setMqttPassword(mqtt["password"].toString());
        }
        if (mqtt.contains("baseTopic")) settings->setMqttBaseTopic(mqtt["baseTopic"].toString());
        if (mqtt.contains("publishInterval")) settings->setMqttPublishInterval(mqtt["publishInterval"].toInt());
        if (mqtt.contains("retainMessages")) settings->setMqttRetainMessages(mqtt["retainMessages"].toBool());
        if (mqtt.contains("homeAssistantDiscovery")) settings->setMqttHomeAssistantDiscovery(mqtt["homeAssistantDiscovery"].toBool());
        if (mqtt.contains("clientId")) settings->setMqttClientId(mqtt["clientId"].toString());
    }

    // Layout configuration
    if (json.contains("layoutConfiguration") && !excludeKeys.contains("layoutConfiguration")) {
        settings->setLayoutConfiguration(json["layoutConfiguration"].toString());
    }

    // Machine tuning
    if (json.contains("machineTuning") && !excludeKeys.contains("machineTuning")) {
        QJsonObject mt = json["machineTuning"].toObject();
        if (mt.contains("waterRefillPoint")) settings->setWaterRefillPoint(mt["waterRefillPoint"].toInt());
        if (mt.contains("refillKitOverride")) settings->setRefillKitOverride(mt["refillKitOverride"].toInt());
        if (mt.contains("heaterIdleTemp")) settings->setHeaterIdleTemp(mt["heaterIdleTemp"].toInt());
        if (mt.contains("heaterWarmupFlow")) settings->setHeaterWarmupFlow(mt["heaterWarmupFlow"].toInt());
        if (mt.contains("heaterTestFlow")) settings->setHeaterTestFlow(mt["heaterTestFlow"].toInt());
        if (mt.contains("heaterWarmupTimeout")) settings->setHeaterWarmupTimeout(mt["heaterWarmupTimeout"].toInt());
        if (mt.contains("hotWaterFlowRate")) settings->setHotWaterFlowRate(mt["hotWaterFlowRate"].toInt());
        // Flow calibration is machine-specific — skip during cross-device migration
        // but restore from same-machine backups (caller passes excludeKeys to control this)
        if (mt.contains("flowCalibrationMultiplier") && !excludeKeys.contains("flowCalibration")) {
            settings->setFlowCalibrationMultiplier(mt["flowCalibrationMultiplier"].toDouble());
        }
        if (mt.contains("autoFlowCalibration") && !excludeKeys.contains("flowCalibration")) {
            settings->setAutoFlowCalibration(mt["autoFlowCalibration"].toBool());
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
                if (val >= 0.5 && val <= 1.8) {
                    settings->setProfileFlowCalibration(it.key(), val);
                    imported++;
                } else {
                    qWarning() << "Settings import: flow calibration out of bounds for"
                               << it.key() << ":" << val << "(expected [0.5, 1.8])";
                    rejected++;
                }
            }
            if (rejected > 0) {
                qWarning() << "Settings import: per-profile flow calibration -"
                           << imported << "imported," << rejected << "rejected";
            }
        }
    }

    // BLE health refresh
    if (json.contains("bleHealthRefreshEnabled") && !excludeKeys.contains("bleHealthRefreshEnabled")) {
        settings->setBleHealthRefreshEnabled(json["bleHealthRefreshEnabled"].toBool());
    }

    // Daily backup hour
    if (json.contains("dailyBackupHour") && !excludeKeys.contains("dailyBackupHour")) {
        settings->setDailyBackupHour(json["dailyBackupHour"].toInt());
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
