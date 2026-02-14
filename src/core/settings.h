#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariantList>
#include <QJsonArray>
#include <QJsonObject>

class Settings : public QObject {
    Q_OBJECT

    // Platform capabilities
    Q_PROPERTY(bool hasQuick3D READ hasQuick3D CONSTANT)

    // Machine settings
    Q_PROPERTY(QString machineAddress READ machineAddress WRITE setMachineAddress NOTIFY machineAddressChanged)
    Q_PROPERTY(QString scaleAddress READ scaleAddress WRITE setScaleAddress NOTIFY scaleAddressChanged)
    Q_PROPERTY(QString scaleType READ scaleType WRITE setScaleType NOTIFY scaleTypeChanged)
    Q_PROPERTY(QString scaleName READ scaleName WRITE setScaleName NOTIFY scaleNameChanged)

    // FlowScale (virtual scale from flow data)
    Q_PROPERTY(bool useFlowScale READ useFlowScale WRITE setUseFlowScale NOTIFY useFlowScaleChanged)

    // Espresso settings
    Q_PROPERTY(double espressoTemperature READ espressoTemperature WRITE setEspressoTemperature NOTIFY espressoTemperatureChanged)
    Q_PROPERTY(double targetWeight READ targetWeight WRITE setTargetWeight NOTIFY targetWeightChanged)
    Q_PROPERTY(double lastUsedRatio READ lastUsedRatio WRITE setLastUsedRatio NOTIFY lastUsedRatioChanged)

    // Steam settings
    Q_PROPERTY(double steamTemperature READ steamTemperature WRITE setSteamTemperature NOTIFY steamTemperatureChanged)
    Q_PROPERTY(int steamTimeout READ steamTimeout WRITE setSteamTimeout NOTIFY steamTimeoutChanged)
    Q_PROPERTY(int steamFlow READ steamFlow WRITE setSteamFlow NOTIFY steamFlowChanged)
    Q_PROPERTY(bool steamDisabled READ steamDisabled WRITE setSteamDisabled NOTIFY steamDisabledChanged)
    Q_PROPERTY(bool keepSteamHeaterOn READ keepSteamHeaterOn WRITE setKeepSteamHeaterOn NOTIFY keepSteamHeaterOnChanged)
    Q_PROPERTY(int steamAutoFlushSeconds READ steamAutoFlushSeconds WRITE setSteamAutoFlushSeconds NOTIFY steamAutoFlushSecondsChanged)

    // Steam pitcher presets
    Q_PROPERTY(QVariantList steamPitcherPresets READ steamPitcherPresets NOTIFY steamPitcherPresetsChanged)
    Q_PROPERTY(int selectedSteamPitcher READ selectedSteamPitcher WRITE setSelectedSteamCup NOTIFY selectedSteamPitcherChanged)

    // Headless machine settings
    Q_PROPERTY(bool headlessSkipPurgeConfirm READ headlessSkipPurgeConfirm WRITE setHeadlessSkipPurgeConfirm NOTIFY headlessSkipPurgeConfirmChanged)

    // Profile favorites
    Q_PROPERTY(QVariantList favoriteProfiles READ favoriteProfiles NOTIFY favoriteProfilesChanged)
    Q_PROPERTY(int selectedFavoriteProfile READ selectedFavoriteProfile WRITE setSelectedFavoriteProfile NOTIFY selectedFavoriteProfileChanged)

    // Selected built-in profiles (shown in "Selected" view)
    Q_PROPERTY(QStringList selectedBuiltInProfiles READ selectedBuiltInProfiles WRITE setSelectedBuiltInProfiles NOTIFY selectedBuiltInProfilesChanged)

    // Hidden profiles (downloaded/user profiles removed from "Selected" view)
    Q_PROPERTY(QStringList hiddenProfiles READ hiddenProfiles WRITE setHiddenProfiles NOTIFY hiddenProfilesChanged)

    // Hot water settings
    Q_PROPERTY(double waterTemperature READ waterTemperature WRITE setWaterTemperature NOTIFY waterTemperatureChanged)
    Q_PROPERTY(int waterVolume READ waterVolume WRITE setWaterVolume NOTIFY waterVolumeChanged)
    Q_PROPERTY(QString waterVolumeMode READ waterVolumeMode WRITE setWaterVolumeMode NOTIFY waterVolumeModeChanged)

    // Hot water vessel presets
    Q_PROPERTY(QVariantList waterVesselPresets READ waterVesselPresets NOTIFY waterVesselPresetsChanged)
    Q_PROPERTY(int selectedWaterVessel READ selectedWaterVessel WRITE setSelectedWaterCup NOTIFY selectedWaterVesselChanged)

    // Flush presets
    Q_PROPERTY(QVariantList flushPresets READ flushPresets NOTIFY flushPresetsChanged)
    Q_PROPERTY(int selectedFlushPreset READ selectedFlushPreset WRITE setSelectedFlushPreset NOTIFY selectedFlushPresetChanged)
    Q_PROPERTY(double flushFlow READ flushFlow WRITE setFlushFlow NOTIFY flushFlowChanged)
    Q_PROPERTY(double flushSeconds READ flushSeconds WRITE setFlushSeconds NOTIFY flushSecondsChanged)

    // Bean presets
    Q_PROPERTY(QVariantList beanPresets READ beanPresets NOTIFY beanPresetsChanged)
    Q_PROPERTY(int selectedBeanPreset READ selectedBeanPreset WRITE setSelectedBeanPreset NOTIFY selectedBeanPresetChanged)

    // UI settings
    Q_PROPERTY(QString skin READ skin WRITE setSkin NOTIFY skinChanged)
    Q_PROPERTY(QString skinPath READ skinPath NOTIFY skinChanged)
    Q_PROPERTY(QString currentProfile READ currentProfile WRITE setCurrentProfile NOTIFY currentProfileChanged)

    // Theme settings
    Q_PROPERTY(QVariantMap customThemeColors READ customThemeColors WRITE setCustomThemeColors NOTIFY customThemeColorsChanged)
    Q_PROPERTY(QVariantList colorGroups READ colorGroups WRITE setColorGroups NOTIFY colorGroupsChanged)
    Q_PROPERTY(QString activeThemeName READ activeThemeName WRITE setActiveThemeName NOTIFY activeThemeNameChanged)
    Q_PROPERTY(double screenBrightness READ screenBrightness WRITE setScreenBrightness NOTIFY screenBrightnessChanged)

    // Visualizer settings
    Q_PROPERTY(QString visualizerUsername READ visualizerUsername WRITE setVisualizerUsername NOTIFY visualizerUsernameChanged)
    Q_PROPERTY(QString visualizerPassword READ visualizerPassword WRITE setVisualizerPassword NOTIFY visualizerPasswordChanged)
    Q_PROPERTY(bool visualizerAutoUpload READ visualizerAutoUpload WRITE setVisualizerAutoUpload NOTIFY visualizerAutoUploadChanged)
    Q_PROPERTY(double visualizerMinDuration READ visualizerMinDuration WRITE setVisualizerMinDuration NOTIFY visualizerMinDurationChanged)
    Q_PROPERTY(bool visualizerExtendedMetadata READ visualizerExtendedMetadata WRITE setVisualizerExtendedMetadata NOTIFY visualizerExtendedMetadataChanged)
    Q_PROPERTY(bool visualizerShowAfterShot READ visualizerShowAfterShot WRITE setVisualizerShowAfterShot NOTIFY visualizerShowAfterShotChanged)
    Q_PROPERTY(bool visualizerClearNotesOnStart READ visualizerClearNotesOnStart WRITE setVisualizerClearNotesOnStart NOTIFY visualizerClearNotesOnStartChanged)
    Q_PROPERTY(int defaultShotRating READ defaultShotRating WRITE setDefaultShotRating NOTIFY defaultShotRatingChanged)

    // AI Dialing Assistant settings
    Q_PROPERTY(QString aiProvider READ aiProvider WRITE setAiProvider NOTIFY aiProviderChanged)
    Q_PROPERTY(QString openaiApiKey READ openaiApiKey WRITE setOpenaiApiKey NOTIFY openaiApiKeyChanged)
    Q_PROPERTY(QString anthropicApiKey READ anthropicApiKey WRITE setAnthropicApiKey NOTIFY anthropicApiKeyChanged)
    Q_PROPERTY(QString geminiApiKey READ geminiApiKey WRITE setGeminiApiKey NOTIFY geminiApiKeyChanged)
    Q_PROPERTY(QString ollamaEndpoint READ ollamaEndpoint WRITE setOllamaEndpoint NOTIFY ollamaEndpointChanged)
    Q_PROPERTY(QString ollamaModel READ ollamaModel WRITE setOllamaModel NOTIFY ollamaModelChanged)
    Q_PROPERTY(QString openrouterApiKey READ openrouterApiKey WRITE setOpenrouterApiKey NOTIFY openrouterApiKeyChanged)
    Q_PROPERTY(QString openrouterModel READ openrouterModel WRITE setOpenrouterModel NOTIFY openrouterModelChanged)

    // Build info
    Q_PROPERTY(bool isDebugBuild READ isDebugBuild CONSTANT)

    // DYE (Describe Your Espresso) metadata - sticky fields
    Q_PROPERTY(QString dyeBeanBrand READ dyeBeanBrand WRITE setDyeBeanBrand NOTIFY dyeBeanBrandChanged)
    Q_PROPERTY(QString dyeBeanType READ dyeBeanType WRITE setDyeBeanType NOTIFY dyeBeanTypeChanged)
    Q_PROPERTY(QString dyeRoastDate READ dyeRoastDate WRITE setDyeRoastDate NOTIFY dyeRoastDateChanged)
    Q_PROPERTY(QString dyeRoastLevel READ dyeRoastLevel WRITE setDyeRoastLevel NOTIFY dyeRoastLevelChanged)
    Q_PROPERTY(QString dyeGrinderModel READ dyeGrinderModel WRITE setDyeGrinderModel NOTIFY dyeGrinderModelChanged)
    Q_PROPERTY(QString dyeGrinderSetting READ dyeGrinderSetting WRITE setDyeGrinderSetting NOTIFY dyeGrinderSettingChanged)
    Q_PROPERTY(double dyeBeanWeight READ dyeBeanWeight WRITE setDyeBeanWeight NOTIFY dyeBeanWeightChanged)
    Q_PROPERTY(double dyeDrinkWeight READ dyeDrinkWeight WRITE setDyeDrinkWeight NOTIFY dyeDrinkWeightChanged)
    Q_PROPERTY(double dyeDrinkTds READ dyeDrinkTds WRITE setDyeDrinkTds NOTIFY dyeDrinkTdsChanged)
    Q_PROPERTY(double dyeDrinkEy READ dyeDrinkEy WRITE setDyeDrinkEy NOTIFY dyeDrinkEyChanged)
    Q_PROPERTY(int dyeEspressoEnjoyment READ dyeEspressoEnjoyment WRITE setDyeEspressoEnjoyment NOTIFY dyeEspressoEnjoymentChanged)
    Q_PROPERTY(QString dyeShotNotes READ dyeShotNotes WRITE setDyeShotNotes NOTIFY dyeShotNotesChanged)
    Q_PROPERTY(QString dyeBarista READ dyeBarista WRITE setDyeBarista NOTIFY dyeBaristaChanged)
    Q_PROPERTY(QString dyeShotDateTime READ dyeShotDateTime WRITE setDyeShotDateTime NOTIFY dyeShotDateTimeChanged)

    // Shot server settings (HTTP API)
    Q_PROPERTY(bool shotServerEnabled READ shotServerEnabled WRITE setShotServerEnabled NOTIFY shotServerEnabledChanged)
    Q_PROPERTY(QString shotServerHostname READ shotServerHostname WRITE setShotServerHostname NOTIFY shotServerHostnameChanged)
    Q_PROPERTY(int shotServerPort READ shotServerPort WRITE setShotServerPort NOTIFY shotServerPortChanged)

    // Auto-favorites settings
    Q_PROPERTY(QString autoFavoritesGroupBy READ autoFavoritesGroupBy WRITE setAutoFavoritesGroupBy NOTIFY autoFavoritesGroupByChanged)
    Q_PROPERTY(int autoFavoritesMaxItems READ autoFavoritesMaxItems WRITE setAutoFavoritesMaxItems NOTIFY autoFavoritesMaxItemsChanged)

    // Auto-update settings
    Q_PROPERTY(bool autoCheckUpdates READ autoCheckUpdates WRITE setAutoCheckUpdates NOTIFY autoCheckUpdatesChanged)
    Q_PROPERTY(bool betaUpdatesEnabled READ betaUpdatesEnabled WRITE setBetaUpdatesEnabled NOTIFY betaUpdatesEnabledChanged)

    // Daily backup settings
    Q_PROPERTY(int dailyBackupHour READ dailyBackupHour WRITE setDailyBackupHour NOTIFY dailyBackupHourChanged)

    // Water level display setting
    Q_PROPERTY(QString waterLevelDisplayUnit READ waterLevelDisplayUnit WRITE setWaterLevelDisplayUnit NOTIFY waterLevelDisplayUnitChanged)

    // Water refill level (mm threshold for refill warning, sent to machine)
    Q_PROPERTY(int waterRefillPoint READ waterRefillPoint WRITE setWaterRefillPoint NOTIFY waterRefillPointChanged)

    // Refill kit override (0=force off, 1=force on, 2=auto-detect)
    Q_PROPERTY(int refillKitOverride READ refillKitOverride WRITE setRefillKitOverride NOTIFY refillKitOverrideChanged)

    // Heater calibration (de1app's set_heater_tweaks — values in raw firmware units)
    Q_PROPERTY(int heaterIdleTemp READ heaterIdleTemp WRITE setHeaterIdleTemp NOTIFY heaterIdleTempChanged)
    Q_PROPERTY(int heaterWarmupFlow READ heaterWarmupFlow WRITE setHeaterWarmupFlow NOTIFY heaterWarmupFlowChanged)
    Q_PROPERTY(int heaterTestFlow READ heaterTestFlow WRITE setHeaterTestFlow NOTIFY heaterTestFlowChanged)
    Q_PROPERTY(int heaterWarmupTimeout READ heaterWarmupTimeout WRITE setHeaterWarmupTimeout NOTIFY heaterWarmupTimeoutChanged)
    Q_PROPERTY(int hotWaterFlowRate READ hotWaterFlowRate WRITE setHotWaterFlowRate NOTIFY hotWaterFlowRateChanged)
    Q_PROPERTY(bool steamTwoTapStop READ steamTwoTapStop WRITE setSteamTwoTapStop NOTIFY steamTwoTapStopChanged)

    // Developer settings
    Q_PROPERTY(bool developerTranslationUpload READ developerTranslationUpload WRITE setDeveloperTranslationUpload NOTIFY developerTranslationUploadChanged)
    Q_PROPERTY(bool simulationMode READ simulationMode WRITE setSimulationMode NOTIFY simulationModeChanged)

    // Temperature override (persistent)
    Q_PROPERTY(double temperatureOverride READ temperatureOverride WRITE setTemperatureOverride NOTIFY temperatureOverrideChanged)
    Q_PROPERTY(bool hasTemperatureOverride READ hasTemperatureOverride NOTIFY temperatureOverrideChanged)

    // Brew parameter overrides (session-only, for next shot)
    Q_PROPERTY(double brewYieldOverride READ brewYieldOverride WRITE setBrewYieldOverride NOTIFY brewOverridesChanged)
    Q_PROPERTY(bool hasBrewYieldOverride READ hasBrewYieldOverride NOTIFY brewOverridesChanged)

    // Auto-wake schedule
    Q_PROPERTY(bool autoWakeEnabled READ autoWakeEnabled WRITE setAutoWakeEnabled NOTIFY autoWakeEnabledChanged)
    Q_PROPERTY(QVariantList autoWakeSchedule READ autoWakeSchedule WRITE setAutoWakeSchedule NOTIFY autoWakeScheduleChanged)
    Q_PROPERTY(bool autoWakeStayAwakeEnabled READ autoWakeStayAwakeEnabled WRITE setAutoWakeStayAwakeEnabled NOTIFY autoWakeStayAwakeEnabledChanged)
    Q_PROPERTY(int autoWakeStayAwakeMinutes READ autoWakeStayAwakeMinutes WRITE setAutoWakeStayAwakeMinutes NOTIFY autoWakeStayAwakeMinutesChanged)

    // Flow calibration
    Q_PROPERTY(double flowCalibrationMultiplier READ flowCalibrationMultiplier WRITE setFlowCalibrationMultiplier NOTIFY flowCalibrationMultiplierChanged)

    // SAW (Stop-at-Weight) learning
    Q_PROPERTY(double sawLearnedLag READ sawLearnedLag NOTIFY sawLearnedLagChanged)

    // Layout configuration (dynamic IdlePage layout)
    Q_PROPERTY(QString layoutConfiguration READ layoutConfiguration WRITE setLayoutConfiguration NOTIFY layoutConfigurationChanged)

    // MQTT settings (Home Automation)
    Q_PROPERTY(bool mqttEnabled READ mqttEnabled WRITE setMqttEnabled NOTIFY mqttEnabledChanged)
    Q_PROPERTY(QString mqttBrokerHost READ mqttBrokerHost WRITE setMqttBrokerHost NOTIFY mqttBrokerHostChanged)
    Q_PROPERTY(int mqttBrokerPort READ mqttBrokerPort WRITE setMqttBrokerPort NOTIFY mqttBrokerPortChanged)
    Q_PROPERTY(QString mqttUsername READ mqttUsername WRITE setMqttUsername NOTIFY mqttUsernameChanged)
    Q_PROPERTY(QString mqttPassword READ mqttPassword WRITE setMqttPassword NOTIFY mqttPasswordChanged)
    Q_PROPERTY(QString mqttBaseTopic READ mqttBaseTopic WRITE setMqttBaseTopic NOTIFY mqttBaseTopicChanged)
    Q_PROPERTY(int mqttPublishInterval READ mqttPublishInterval WRITE setMqttPublishInterval NOTIFY mqttPublishIntervalChanged)
    Q_PROPERTY(bool mqttRetainMessages READ mqttRetainMessages WRITE setMqttRetainMessages NOTIFY mqttRetainMessagesChanged)
    Q_PROPERTY(bool mqttHomeAssistantDiscovery READ mqttHomeAssistantDiscovery WRITE setMqttHomeAssistantDiscovery NOTIFY mqttHomeAssistantDiscoveryChanged)
    Q_PROPERTY(QString mqttClientId READ mqttClientId WRITE setMqttClientId NOTIFY mqttClientIdChanged)

public:
    explicit Settings(QObject* parent = nullptr);

    // Platform capabilities (compile-time)
    bool hasQuick3D() const {
#ifdef HAVE_QUICK3D
        return true;
#else
        return false;
#endif
    }

    // Machine settings
    QString machineAddress() const;
    void setMachineAddress(const QString& address);

    QString scaleAddress() const;
    void setScaleAddress(const QString& address);

    QString scaleType() const;
    void setScaleType(const QString& type);

    QString scaleName() const;
    void setScaleName(const QString& name);

    // FlowScale
    bool useFlowScale() const;
    void setUseFlowScale(bool enabled);

    // Espresso settings
    double espressoTemperature() const;
    void setEspressoTemperature(double temp);

    double targetWeight() const;
    void setTargetWeight(double weight);

    double lastUsedRatio() const;
    void setLastUsedRatio(double ratio);

    // Steam settings
    double steamTemperature() const;
    void setSteamTemperature(double temp);

    int steamTimeout() const;
    void setSteamTimeout(int timeout);

    int steamFlow() const;
    void setSteamFlow(int flow);

    bool steamDisabled() const;
    void setSteamDisabled(bool disabled);
    bool keepSteamHeaterOn() const;
    void setKeepSteamHeaterOn(bool keep);
    int steamAutoFlushSeconds() const;
    void setSteamAutoFlushSeconds(int seconds);

    // Steam pitcher presets
    QVariantList steamPitcherPresets() const;
    int selectedSteamPitcher() const;
    void setSelectedSteamCup(int index);

    Q_INVOKABLE void addSteamPitcherPreset(const QString& name, int duration, int flow);
    Q_INVOKABLE void updateSteamPitcherPreset(int index, const QString& name, int duration, int flow);
    Q_INVOKABLE void removeSteamPitcherPreset(int index);
    Q_INVOKABLE void moveSteamPitcherPreset(int from, int to);
    Q_INVOKABLE QVariantMap getSteamPitcherPreset(int index) const;

    // Headless machine settings
    bool headlessSkipPurgeConfirm() const;
    void setHeadlessSkipPurgeConfirm(bool skip);

    // Profile favorites (max 5)
    QVariantList favoriteProfiles() const;
    int selectedFavoriteProfile() const;
    void setSelectedFavoriteProfile(int index);

    Q_INVOKABLE void addFavoriteProfile(const QString& name, const QString& filename);
    Q_INVOKABLE void removeFavoriteProfile(int index);
    Q_INVOKABLE void moveFavoriteProfile(int from, int to);
    Q_INVOKABLE QVariantMap getFavoriteProfile(int index) const;
    Q_INVOKABLE bool isFavoriteProfile(const QString& filename) const;
    Q_INVOKABLE bool updateFavoriteProfile(const QString& oldFilename, const QString& newFilename, const QString& newTitle);
    Q_INVOKABLE int findFavoriteIndexByFilename(const QString& filename) const;

    // Selected built-in profiles
    QStringList selectedBuiltInProfiles() const;
    void setSelectedBuiltInProfiles(const QStringList& profiles);
    Q_INVOKABLE void addSelectedBuiltInProfile(const QString& filename);
    Q_INVOKABLE void removeSelectedBuiltInProfile(const QString& filename);
    Q_INVOKABLE bool isSelectedBuiltInProfile(const QString& filename) const;

    // Hidden profiles (downloaded/user profiles removed from "Selected" view)
    QStringList hiddenProfiles() const;
    void setHiddenProfiles(const QStringList& profiles);
    Q_INVOKABLE void addHiddenProfile(const QString& filename);
    Q_INVOKABLE void removeHiddenProfile(const QString& filename);
    Q_INVOKABLE bool isHiddenProfile(const QString& filename) const;

    // Hot water settings
    double waterTemperature() const;
    void setWaterTemperature(double temp);

    int waterVolume() const;
    void setWaterVolume(int volume);

    QString waterVolumeMode() const;  // "weight" or "volume"
    void setWaterVolumeMode(const QString& mode);

    // Hot water vessel presets
    QVariantList waterVesselPresets() const;
    int selectedWaterVessel() const;
    void setSelectedWaterCup(int index);

    Q_INVOKABLE void addWaterVesselPreset(const QString& name, int volume, const QString& mode = "weight");
    Q_INVOKABLE void updateWaterVesselPreset(int index, const QString& name, int volume, const QString& mode = "weight");
    Q_INVOKABLE void removeWaterVesselPreset(int index);
    Q_INVOKABLE void moveWaterVesselPreset(int from, int to);
    Q_INVOKABLE QVariantMap getWaterVesselPreset(int index) const;

    // Flush presets
    QVariantList flushPresets() const;
    int selectedFlushPreset() const;
    void setSelectedFlushPreset(int index);

    double flushFlow() const;
    void setFlushFlow(double flow);

    double flushSeconds() const;
    void setFlushSeconds(double seconds);

    Q_INVOKABLE void addFlushPreset(const QString& name, double flow, double seconds);
    Q_INVOKABLE void updateFlushPreset(int index, const QString& name, double flow, double seconds);
    Q_INVOKABLE void removeFlushPreset(int index);
    Q_INVOKABLE void moveFlushPreset(int from, int to);
    Q_INVOKABLE QVariantMap getFlushPreset(int index) const;

    // Bean presets
    QVariantList beanPresets() const;
    int selectedBeanPreset() const;
    void setSelectedBeanPreset(int index);

    Q_INVOKABLE void addBeanPreset(const QString& name, const QString& brand, const QString& type,
                                   const QString& roastDate, const QString& roastLevel,
                                   const QString& grinderModel, const QString& grinderSetting);
    Q_INVOKABLE void updateBeanPreset(int index, const QString& name, const QString& brand,
                                      const QString& type, const QString& roastDate,
                                      const QString& roastLevel, const QString& grinderModel,
                                      const QString& grinderSetting);
    Q_INVOKABLE void removeBeanPreset(int index);
    Q_INVOKABLE void moveBeanPreset(int from, int to);
    Q_INVOKABLE QVariantMap getBeanPreset(int index) const;
    Q_INVOKABLE void applyBeanPreset(int index);       // Sets all DYE fields from preset
    Q_INVOKABLE void saveBeanPresetFromCurrent(const QString& name);  // Creates or updates preset from current DYE
    Q_INVOKABLE int findBeanPresetByContent(const QString& brand, const QString& type) const;  // Returns index or -1 (simple match)
    Q_INVOKABLE int findBeanPresetByName(const QString& name) const;  // Returns index or -1

    // UI settings
    QString skin() const;
    void setSkin(const QString& skin);
    QString skinPath() const;

    QString currentProfile() const;
    void setCurrentProfile(const QString& profile);

    // Theme settings
    QVariantMap customThemeColors() const;
    void setCustomThemeColors(const QVariantMap& colors);

    QVariantList colorGroups() const;
    void setColorGroups(const QVariantList& groups);

    QString activeThemeName() const;
    void setActiveThemeName(const QString& name);

    Q_INVOKABLE void setThemeColor(const QString& colorName, const QString& colorValue);
    Q_INVOKABLE QString getThemeColor(const QString& colorName) const;
    Q_INVOKABLE void resetThemeToDefault();
    Q_INVOKABLE QVariantList getPresetThemes() const;
    Q_INVOKABLE void applyPresetTheme(const QString& name);
    Q_INVOKABLE void saveCurrentTheme(const QString& name);
    Q_INVOKABLE void deleteUserTheme(const QString& name);
    Q_INVOKABLE bool saveThemeToFile(const QString& filePath);
    Q_INVOKABLE bool loadThemeFromFile(const QString& filePath);
    Q_INVOKABLE QVariantMap generatePalette(double hue, double saturation, double lightness) const;

    double screenBrightness() const;
    void setScreenBrightness(double brightness);

    // Visualizer settings
    QString visualizerUsername() const;
    void setVisualizerUsername(const QString& username);

    QString visualizerPassword() const;
    void setVisualizerPassword(const QString& password);

    bool visualizerAutoUpload() const;
    void setVisualizerAutoUpload(bool enabled);

    double visualizerMinDuration() const;
    void setVisualizerMinDuration(double seconds);

    bool visualizerExtendedMetadata() const;
    void setVisualizerExtendedMetadata(bool enabled);

    bool visualizerShowAfterShot() const;
    void setVisualizerShowAfterShot(bool enabled);

    bool visualizerClearNotesOnStart() const;
    void setVisualizerClearNotesOnStart(bool enabled);

    int defaultShotRating() const;
    void setDefaultShotRating(int rating);

    // AI Dialing Assistant settings
    QString aiProvider() const;
    void setAiProvider(const QString& provider);

    QString openaiApiKey() const;
    void setOpenaiApiKey(const QString& key);

    QString anthropicApiKey() const;
    void setAnthropicApiKey(const QString& key);

    QString geminiApiKey() const;
    void setGeminiApiKey(const QString& key);

    QString ollamaEndpoint() const;
    void setOllamaEndpoint(const QString& endpoint);

    QString ollamaModel() const;
    void setOllamaModel(const QString& model);

    QString openrouterApiKey() const;
    void setOpenrouterApiKey(const QString& key);

    QString openrouterModel() const;
    void setOpenrouterModel(const QString& model);

    // Build info
    bool isDebugBuild() const;

    // DYE metadata
    QString dyeBeanBrand() const;
    void setDyeBeanBrand(const QString& value);

    QString dyeBeanType() const;
    void setDyeBeanType(const QString& value);

    QString dyeRoastDate() const;
    void setDyeRoastDate(const QString& value);

    QString dyeRoastLevel() const;
    void setDyeRoastLevel(const QString& value);

    QString dyeGrinderModel() const;
    void setDyeGrinderModel(const QString& value);

    QString dyeGrinderSetting() const;
    void setDyeGrinderSetting(const QString& value);

    double dyeBeanWeight() const;
    void setDyeBeanWeight(double value);

    double dyeDrinkWeight() const;
    void setDyeDrinkWeight(double value);

    double dyeDrinkTds() const;
    void setDyeDrinkTds(double value);

    double dyeDrinkEy() const;
    void setDyeDrinkEy(double value);

    int dyeEspressoEnjoyment() const;
    void setDyeEspressoEnjoyment(int value);

    QString dyeShotNotes() const;
    void setDyeShotNotes(const QString& value);

    QString dyeBarista() const;
    void setDyeBarista(const QString& value);

    QString dyeShotDateTime() const;
    void setDyeShotDateTime(const QString& value);

    // Force sync to disk
    void sync() { m_settings.sync(); }

    // Shot server settings (HTTP API)
    bool shotServerEnabled() const;
    void setShotServerEnabled(bool enabled);
    QString shotServerHostname() const;
    void setShotServerHostname(const QString& hostname);
    int shotServerPort() const;
    void setShotServerPort(int port);

    // Auto-favorites settings
    QString autoFavoritesGroupBy() const;
    void setAutoFavoritesGroupBy(const QString& groupBy);
    int autoFavoritesMaxItems() const;
    void setAutoFavoritesMaxItems(int maxItems);

    // Auto-update settings
    bool autoCheckUpdates() const;
    void setAutoCheckUpdates(bool enabled);
    bool betaUpdatesEnabled() const;
    void setBetaUpdatesEnabled(bool enabled);

    // Daily backup
    int dailyBackupHour() const;
    void setDailyBackupHour(int hour);

    // Water level display
    QString waterLevelDisplayUnit() const;
    void setWaterLevelDisplayUnit(const QString& unit);

    // Water refill level
    int waterRefillPoint() const;
    void setWaterRefillPoint(int mm);

    // Refill kit override
    int refillKitOverride() const;
    void setRefillKitOverride(int value);

    // Heater calibration
    int heaterIdleTemp() const;
    void setHeaterIdleTemp(int value);
    int heaterWarmupFlow() const;
    void setHeaterWarmupFlow(int value);
    int heaterTestFlow() const;
    void setHeaterTestFlow(int value);
    int heaterWarmupTimeout() const;
    void setHeaterWarmupTimeout(int value);
    int hotWaterFlowRate() const;
    void setHotWaterFlowRate(int value);
    bool steamTwoTapStop() const;
    void setSteamTwoTapStop(bool value);

    // Developer settings
    bool developerTranslationUpload() const;
    void setDeveloperTranslationUpload(bool enabled);

    bool simulationMode() const;
    void setSimulationMode(bool enabled);

    // Temperature override (persistent)
    double temperatureOverride() const;
    void setTemperatureOverride(double temp);
    bool hasTemperatureOverride() const;
    Q_INVOKABLE void clearTemperatureOverride();

    // Brew parameter overrides (persistent)
    double brewYieldOverride() const;
    void setBrewYieldOverride(double yield);
    bool hasBrewYieldOverride() const;
    Q_INVOKABLE void clearAllBrewOverrides();

    // Auto-wake schedule
    bool autoWakeEnabled() const;
    void setAutoWakeEnabled(bool enabled);
    QVariantList autoWakeSchedule() const;
    void setAutoWakeSchedule(const QVariantList& schedule);
    Q_INVOKABLE void setAutoWakeDayEnabled(int dayIndex, bool enabled);
    Q_INVOKABLE void setAutoWakeDayTime(int dayIndex, int hour, int minute);
    bool autoWakeStayAwakeEnabled() const;
    void setAutoWakeStayAwakeEnabled(bool enabled);
    int autoWakeStayAwakeMinutes() const;
    void setAutoWakeStayAwakeMinutes(int minutes);

    // MQTT settings (Home Automation)
    bool mqttEnabled() const;
    void setMqttEnabled(bool enabled);
    QString mqttBrokerHost() const;
    void setMqttBrokerHost(const QString& host);
    int mqttBrokerPort() const;
    void setMqttBrokerPort(int port);
    QString mqttUsername() const;
    void setMqttUsername(const QString& username);
    QString mqttPassword() const;
    void setMqttPassword(const QString& password);
    QString mqttBaseTopic() const;
    void setMqttBaseTopic(const QString& topic);
    int mqttPublishInterval() const;
    void setMqttPublishInterval(int interval);
    bool mqttRetainMessages() const;
    void setMqttRetainMessages(bool retain);
    bool mqttHomeAssistantDiscovery() const;
    void setMqttHomeAssistantDiscovery(bool enabled);
    QString mqttClientId() const;
    void setMqttClientId(const QString& clientId);

    // Flow calibration
    double flowCalibrationMultiplier() const;
    void setFlowCalibrationMultiplier(double multiplier);

    // SAW (Stop-at-Weight) learning
    double sawLearnedLag() const;  // Average lag for display in QML (calculated from drip/flow)
    double getExpectedDrip(double currentFlowRate) const;  // Predicts drip based on flow and history
    void addSawLearningPoint(double drip, double flowRate, const QString& scaleType);
    Q_INVOKABLE void resetSawLearning();

    // Layout configuration (dynamic IdlePage layout)
    QString layoutConfiguration() const;
    void setLayoutConfiguration(const QString& json);
    Q_INVOKABLE QVariantList getZoneItems(const QString& zoneName) const;
    Q_INVOKABLE void moveItem(const QString& itemId, const QString& fromZone, const QString& toZone, int toIndex);
    Q_INVOKABLE void addItem(const QString& type, const QString& zone, int index = -1);
    Q_INVOKABLE void removeItem(const QString& itemId, const QString& zone);
    Q_INVOKABLE void reorderItem(const QString& zoneName, int fromIndex, int toIndex);
    Q_INVOKABLE void resetLayoutToDefault();
    Q_INVOKABLE bool hasItemType(const QString& type) const;
    Q_INVOKABLE int getZoneYOffset(const QString& zoneName) const;
    Q_INVOKABLE void setZoneYOffset(const QString& zoneName, int offset);
    Q_INVOKABLE double getZoneScale(const QString& zoneName) const;
    Q_INVOKABLE void setZoneScale(const QString& zoneName, double scale);
    Q_INVOKABLE void setItemProperty(const QString& itemId, const QString& key, const QVariant& value);
    Q_INVOKABLE QVariantMap getItemProperties(const QString& itemId) const;

    // Device identity (stable UUID for server communication)
    Q_INVOKABLE QString deviceId() const;

    // Generic settings access (for extensibility)
    Q_INVOKABLE QVariant value(const QString& key, const QVariant& defaultValue = QVariant()) const;
    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);

signals:
    void machineAddressChanged();
    void scaleAddressChanged();
    void scaleTypeChanged();
    void scaleNameChanged();
    void useFlowScaleChanged();
    void espressoTemperatureChanged();
    void targetWeightChanged();
    void lastUsedRatioChanged();
    void steamTemperatureChanged();
    void steamTimeoutChanged();
    void steamFlowChanged();
    void steamDisabledChanged();
    void keepSteamHeaterOnChanged();
    void steamAutoFlushSecondsChanged();
    void steamPitcherPresetsChanged();
    void selectedSteamPitcherChanged();
    void headlessSkipPurgeConfirmChanged();
    void favoriteProfilesChanged();
    void selectedFavoriteProfileChanged();
    void selectedBuiltInProfilesChanged();
    void hiddenProfilesChanged();
    void waterTemperatureChanged();
    void waterVolumeChanged();
    void waterVolumeModeChanged();
    void waterVesselPresetsChanged();
    void selectedWaterVesselChanged();
    void flushPresetsChanged();
    void selectedFlushPresetChanged();
    void flushFlowChanged();
    void flushSecondsChanged();
    void beanPresetsChanged();
    void selectedBeanPresetChanged();
    void skinChanged();
    void currentProfileChanged();
    void customThemeColorsChanged();
    void colorGroupsChanged();
    void activeThemeNameChanged();
    void screenBrightnessChanged();
    void visualizerUsernameChanged();
    void visualizerPasswordChanged();
    void visualizerAutoUploadChanged();
    void visualizerMinDurationChanged();
    void visualizerExtendedMetadataChanged();
    void visualizerShowAfterShotChanged();
    void visualizerClearNotesOnStartChanged();
    void defaultShotRatingChanged();
    void aiProviderChanged();
    void openaiApiKeyChanged();
    void anthropicApiKeyChanged();
    void geminiApiKeyChanged();
    void ollamaEndpointChanged();
    void ollamaModelChanged();
    void openrouterApiKeyChanged();
    void openrouterModelChanged();
    void dyeBeanBrandChanged();
    void dyeBeanTypeChanged();
    void dyeRoastDateChanged();
    void dyeRoastLevelChanged();
    void dyeGrinderModelChanged();
    void dyeGrinderSettingChanged();
    void dyeBeanWeightChanged();
    void dyeDrinkWeightChanged();
    void dyeDrinkTdsChanged();
    void dyeDrinkEyChanged();
    void dyeEspressoEnjoymentChanged();
    void dyeShotNotesChanged();
    void dyeBaristaChanged();
    void dyeShotDateTimeChanged();
    void shotServerEnabledChanged();
    void shotServerHostnameChanged();
    void shotServerPortChanged();
    void autoFavoritesGroupByChanged();
    void autoFavoritesMaxItemsChanged();
    void autoCheckUpdatesChanged();
    void betaUpdatesEnabledChanged();
    void dailyBackupHourChanged();
    void waterLevelDisplayUnitChanged();
    void waterRefillPointChanged();
    void refillKitOverrideChanged();
    void heaterIdleTempChanged();
    void heaterWarmupFlowChanged();
    void heaterTestFlowChanged();
    void heaterWarmupTimeoutChanged();
    void hotWaterFlowRateChanged();
    void steamTwoTapStopChanged();
    void developerTranslationUploadChanged();
    void simulationModeChanged();
    void temperatureOverrideChanged();
    void brewOverridesChanged();
    void autoWakeEnabledChanged();
    void autoWakeScheduleChanged();
    void autoWakeStayAwakeEnabledChanged();
    void autoWakeStayAwakeMinutesChanged();
    void mqttEnabledChanged();
    void mqttBrokerHostChanged();
    void mqttBrokerPortChanged();
    void mqttUsernameChanged();
    void mqttPasswordChanged();
    void mqttBaseTopicChanged();
    void mqttPublishIntervalChanged();
    void mqttRetainMessagesChanged();
    void mqttHomeAssistantDiscoveryChanged();
    void mqttClientIdChanged();
    void flowCalibrationMultiplierChanged();
    void sawLearnedLagChanged();
    void layoutConfigurationChanged();
    void valueChanged(const QString& key);

private:
    // Helper method to get bean presets as QJsonArray for internal manipulation
    QJsonArray getBeanPresetsArray() const;

    // Layout configuration helpers
    QString defaultLayoutJson() const;
    QJsonObject getLayoutObject() const;
    void saveLayoutObject(const QJsonObject& layout);
    QString generateItemId(const QString& type) const;

    QSettings m_settings;
    mutable QJsonObject m_layoutCache;
    mutable QString m_layoutJsonCache;
    mutable bool m_layoutCacheValid = false;
    void invalidateLayoutCache();
    bool m_steamDisabled = false;  // Session-only, not persisted (for descaling)
    double m_temperatureOverride = 0;  // Session-only, for next shot
    bool m_hasTemperatureOverride = false;  // Session-only
    bool m_developerTranslationUpload = false;  // Session-only, Easter egg unlock

    // Brew parameter overrides (session-only)
    double m_brewYieldOverride = 0;
    bool m_hasBrewYieldOverride = false;
};
