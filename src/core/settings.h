#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariantList>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
// Domain sub-objects are forward-declared. The QML-facing Q_PROPERTYs return
// QObject* (a known type that QML can introspect) so this header doesn't need
// to include the seven sub-object headers — preserving the recompile-blast
// reduction this whole refactor is for.
//
// C++ callers use the typed accessor (e.g. `settings->mqtt()`) and include
// `settings_mqtt.h` themselves where they actually dereference.
class SettingsMqtt;
class SettingsAutoWake;
class SettingsHardware;
class SettingsAI;
class SettingsTheme;
class SettingsVisualizer;
class SettingsMcp;

class Settings : public QObject {
    Q_OBJECT

    // Domain sub-objects exposed to QML as QObject* so QML can resolve
    // `Settings.mqtt.mqttEnabled` via the runtime metaObject (SettingsMqtt's
    // Q_OBJECT supplies it). The typed `mqtt()` accessor below is what C++
    // callers use.
    //
    // Required prerequisite: each sub-object type must be registered with the
    // QML engine via qmlRegisterUncreatableType<SettingsXxx>(...) in main.cpp,
    // otherwise QML can't discover the concrete type and resolves the chained
    // property access (e.g. `.customThemeColors`) to `undefined` at runtime.
    Q_PROPERTY(QObject* mqtt READ mqttQObject CONSTANT)
    Q_PROPERTY(QObject* autoWake READ autoWakeQObject CONSTANT)
    Q_PROPERTY(QObject* hardware READ hardwareQObject CONSTANT)
    Q_PROPERTY(QObject* ai READ aiQObject CONSTANT)
    Q_PROPERTY(QObject* theme READ themeQObject CONSTANT)
    Q_PROPERTY(QObject* visualizer READ visualizerQObject CONSTANT)
    Q_PROPERTY(QObject* mcp READ mcpQObject CONSTANT)

    // Platform capabilities
    Q_PROPERTY(bool hasQuick3D READ hasQuick3D CONSTANT)
    Q_PROPERTY(bool use12HourTime READ use12HourTime CONSTANT)

    // Machine settings
    Q_PROPERTY(QString machineAddress READ machineAddress WRITE setMachineAddress NOTIFY machineAddressChanged)
    Q_PROPERTY(QString scaleAddress READ scaleAddress WRITE setScaleAddress NOTIFY scaleAddressChanged)
    Q_PROPERTY(QString scaleType READ scaleType WRITE setScaleType NOTIFY scaleTypeChanged)
    Q_PROPERTY(bool keepScaleOn READ keepScaleOn WRITE setKeepScaleOn NOTIFY keepScaleOnChanged)
    Q_PROPERTY(QString scaleName READ scaleName WRITE setScaleName NOTIFY scaleNameChanged)

    // Multi-scale management
    Q_PROPERTY(QVariantList knownScales READ knownScales NOTIFY knownScalesChanged)
    Q_PROPERTY(QString primaryScaleAddress READ primaryScaleAddress NOTIFY knownScalesChanged)

    // FlowScale (virtual scale from flow data)
    Q_PROPERTY(bool useFlowScale READ useFlowScale WRITE setUseFlowScale NOTIFY useFlowScaleChanged)

    // Allow user to disable modal scale connection alert dialogs
    Q_PROPERTY(bool showScaleDialogs READ showScaleDialogs WRITE setShowScaleDialogs NOTIFY showScaleDialogsChanged)

    // Refractometer (DiFluid R2)
    Q_PROPERTY(QString savedRefractometerAddress READ savedRefractometerAddress WRITE setSavedRefractometerAddress NOTIFY savedRefractometerChanged)
    Q_PROPERTY(QString savedRefractometerName READ savedRefractometerName WRITE setSavedRefractometerName NOTIFY savedRefractometerChanged)

    // Enable USB serial polling for DE1 connection. Off by default to save battery
    // (polling every 2 s). Only needed when connecting the DE1 via USB-C cable.
    Q_PROPERTY(bool usbSerialEnabled READ usbSerialEnabled WRITE setUsbSerialEnabled NOTIFY usbSerialEnabledChanged)

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

    // Launcher mode (Android only - register as home screen)
    Q_PROPERTY(bool launcherMode READ launcherMode WRITE setLauncherMode NOTIFY launcherModeChanged)

    // Profile favorites
    Q_PROPERTY(QVariantList favoriteProfiles READ favoriteProfiles NOTIFY favoriteProfilesChanged)
    Q_PROPERTY(int selectedFavoriteProfile READ selectedFavoriteProfile WRITE setSelectedFavoriteProfile NOTIFY selectedFavoriteProfileChanged)

    // Selected built-in profiles (shown in "Selected" view)
    Q_PROPERTY(QStringList selectedBuiltInProfiles READ selectedBuiltInProfiles WRITE setSelectedBuiltInProfiles NOTIFY selectedBuiltInProfilesChanged)

    // Hidden profiles (downloaded/user profiles removed from "Selected" view)
    Q_PROPERTY(QStringList hiddenProfiles READ hiddenProfiles WRITE setHiddenProfiles NOTIFY hiddenProfilesChanged)

    // Saved searches (shot history)
    Q_PROPERTY(QStringList savedSearches READ savedSearches WRITE setSavedSearches NOTIFY savedSearchesChanged)
    Q_PROPERTY(QString shotHistorySortField READ shotHistorySortField WRITE setShotHistorySortField NOTIFY shotHistorySortFieldChanged)
    Q_PROPERTY(QString shotHistorySortDirection READ shotHistorySortDirection WRITE setShotHistorySortDirection NOTIFY shotHistorySortDirectionChanged)

    // Hot water settings
    Q_PROPERTY(double waterTemperature READ waterTemperature WRITE setWaterTemperature NOTIFY waterTemperatureChanged)
    Q_PROPERTY(int waterVolume READ waterVolume WRITE setWaterVolume NOTIFY waterVolumeChanged)
    Q_PROPERTY(QString waterVolumeMode READ waterVolumeMode WRITE setWaterVolumeMode NOTIFY waterVolumeModeChanged)

    // Hot water SAW learning (learned stop offset from overshoot data)
    Q_PROPERTY(double hotWaterSawOffset READ hotWaterSawOffset WRITE setHotWaterSawOffset NOTIFY hotWaterSawOffsetChanged)

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
    // Filtered view of beanPresets: only rows where showOnIdle == true. Each row gains
    // an `originalIndex` field mapping back into beanPresets for selection/apply calls.
    Q_PROPERTY(QVariantList idleBeanPresets READ idleBeanPresets NOTIFY beanPresetsChanged)
    Q_PROPERTY(int selectedBeanPreset READ selectedBeanPreset WRITE setSelectedBeanPreset NOTIFY selectedBeanPresetChanged)
    // True when selectedBeanPreset >= 0 AND any DYE bean/grinder field diverges from the preset's stored value.
    // Mirrors the ProfileManager.profileModified concept so the beans pill can show an "unsaved" indicator.
    Q_PROPERTY(bool beansModified READ beansModified NOTIFY beansModifiedChanged)

    // UI settings
    Q_PROPERTY(QString currentProfile READ currentProfile WRITE setCurrentProfile NOTIFY currentProfileChanged)



    // Build info
    Q_PROPERTY(bool isDebugBuild READ isDebugBuild CONSTANT)

    // DYE (Describe Your Espresso) metadata - sticky fields
    Q_PROPERTY(QString dyeBeanBrand READ dyeBeanBrand WRITE setDyeBeanBrand NOTIFY dyeBeanBrandChanged)
    Q_PROPERTY(QString dyeBeanType READ dyeBeanType WRITE setDyeBeanType NOTIFY dyeBeanTypeChanged)
    Q_PROPERTY(QString dyeRoastDate READ dyeRoastDate WRITE setDyeRoastDate NOTIFY dyeRoastDateChanged)
    Q_PROPERTY(QString dyeRoastLevel READ dyeRoastLevel WRITE setDyeRoastLevel NOTIFY dyeRoastLevelChanged)
    Q_PROPERTY(QString dyeGrinderBrand READ dyeGrinderBrand WRITE setDyeGrinderBrand NOTIFY dyeGrinderBrandChanged)
    Q_PROPERTY(QString dyeGrinderModel READ dyeGrinderModel WRITE setDyeGrinderModel NOTIFY dyeGrinderModelChanged)
    Q_PROPERTY(QString dyeGrinderBurrs READ dyeGrinderBurrs WRITE setDyeGrinderBurrs NOTIFY dyeGrinderBurrsChanged)
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
    Q_PROPERTY(bool webSecurityEnabled READ webSecurityEnabled WRITE setWebSecurityEnabled NOTIFY webSecurityEnabledChanged)

    // Auto-favorites settings
    Q_PROPERTY(QString autoFavoritesGroupBy READ autoFavoritesGroupBy WRITE setAutoFavoritesGroupBy NOTIFY autoFavoritesGroupByChanged)
    Q_PROPERTY(int autoFavoritesMaxItems READ autoFavoritesMaxItems WRITE setAutoFavoritesMaxItems NOTIFY autoFavoritesMaxItemsChanged)
    Q_PROPERTY(bool autoFavoritesOpenBrewSettings READ autoFavoritesOpenBrewSettings WRITE setAutoFavoritesOpenBrewSettings NOTIFY autoFavoritesOpenBrewSettingsChanged)
    Q_PROPERTY(bool autoFavoritesHideUnrated READ autoFavoritesHideUnrated WRITE setAutoFavoritesHideUnrated NOTIFY autoFavoritesHideUnratedChanged)

    // Auto-update settings
    Q_PROPERTY(bool autoCheckUpdates READ autoCheckUpdates WRITE setAutoCheckUpdates NOTIFY autoCheckUpdatesChanged)
    Q_PROPERTY(bool betaUpdatesEnabled READ betaUpdatesEnabled WRITE setBetaUpdatesEnabled NOTIFY betaUpdatesEnabledChanged)

    // DE1 firmware update channel. When false (default), firmware comes
    // from fast.decentespresso.com/download/sync/de1plus; when true,
    // from .../de1nightly. Independent from betaUpdatesEnabled, which
    // controls the Decenza *app* update channel.
    Q_PROPERTY(bool firmwareNightlyChannel READ firmwareNightlyChannel WRITE setFirmwareNightlyChannel NOTIFY firmwareNightlyChannelChanged)

    // Daily backup settings
    Q_PROPERTY(int dailyBackupHour READ dailyBackupHour WRITE setDailyBackupHour NOTIFY dailyBackupHourChanged)

    // Shot export settings
    Q_PROPERTY(bool exportShotsToFile READ exportShotsToFile WRITE setExportShotsToFile NOTIFY exportShotsToFileChanged)

    // Water level display setting
    Q_PROPERTY(QString waterLevelDisplayUnit READ waterLevelDisplayUnit WRITE setWaterLevelDisplayUnit NOTIFY waterLevelDisplayUnitChanged)

    // Water refill level (mm threshold for refill warning, sent to machine)
    Q_PROPERTY(int waterRefillPoint READ waterRefillPoint WRITE setWaterRefillPoint NOTIFY waterRefillPointChanged)

    // Refill kit override (0=force off, 1=force on, 2=auto-detect)
    Q_PROPERTY(int refillKitOverride READ refillKitOverride WRITE setRefillKitOverride NOTIFY refillKitOverrideChanged)


    // Developer settings
    Q_PROPERTY(bool developerTranslationUpload READ developerTranslationUpload WRITE setDeveloperTranslationUpload NOTIFY developerTranslationUploadChanged)
    Q_PROPERTY(bool simulationMode READ simulationMode WRITE setSimulationMode NOTIFY simulationModeChanged)
    Q_PROPERTY(bool hideGhcSimulator READ hideGhcSimulator WRITE setHideGhcSimulator NOTIFY hideGhcSimulatorChanged)
    Q_PROPERTY(bool simulatedScaleEnabled READ simulatedScaleEnabled WRITE setSimulatedScaleEnabled NOTIFY simulatedScaleEnabledChanged)
    Q_PROPERTY(bool screenCaptureEnabled READ screenCaptureEnabled WRITE setScreenCaptureEnabled NOTIFY screenCaptureEnabledChanged)

    // Temperature override (persistent)
    Q_PROPERTY(double temperatureOverride READ temperatureOverride WRITE setTemperatureOverride NOTIFY temperatureOverrideChanged)
    Q_PROPERTY(bool hasTemperatureOverride READ hasTemperatureOverride NOTIFY temperatureOverrideChanged)

    // Brew parameter overrides (session-only, for next shot)
    Q_PROPERTY(double brewYieldOverride READ brewYieldOverride WRITE setBrewYieldOverride NOTIFY brewOverridesChanged)
    Q_PROPERTY(bool hasBrewYieldOverride READ hasBrewYieldOverride NOTIFY brewOverridesChanged)

    // Flow calibration
    Q_PROPERTY(double flowCalibrationMultiplier READ flowCalibrationMultiplier WRITE setFlowCalibrationMultiplier NOTIFY flowCalibrationMultiplierChanged)
    Q_PROPERTY(bool autoFlowCalibration READ autoFlowCalibration WRITE setAutoFlowCalibration NOTIFY autoFlowCalibrationChanged)
    Q_PROPERTY(int perProfileFlowCalVersion READ perProfileFlowCalVersion NOTIFY perProfileFlowCalibrationChanged)

    // Ignore stop-at-volume when a BLE scale is providing weight data
    Q_PROPERTY(bool ignoreVolumeWithScale READ ignoreVolumeWithScale WRITE setIgnoreVolumeWithScale NOTIFY ignoreVolumeWithScaleChanged)

    // SAW (Stop-at-Weight) learning
    Q_PROPERTY(double sawLearnedLag READ sawLearnedLag NOTIFY sawLearnedLagChanged)

    // Layout configuration (dynamic IdlePage layout)
    Q_PROPERTY(QString layoutConfiguration READ layoutConfiguration WRITE setLayoutConfiguration NOTIFY layoutConfigurationChanged)


    // Discuss Shot settings
    Q_PROPERTY(int discussShotApp READ discussShotApp WRITE setDiscussShotApp NOTIFY discussShotAppChanged)
    Q_PROPERTY(QString discussShotCustomUrl READ discussShotCustomUrl WRITE setDiscussShotCustomUrl NOTIFY discussShotCustomUrlChanged)
    Q_PROPERTY(QString claudeRcSessionUrl READ claudeRcSessionUrl WRITE setClaudeRcSessionUrl NOTIFY claudeRcSessionUrlChanged)
    Q_PROPERTY(int discussAppNone READ discussAppNone CONSTANT)
    Q_PROPERTY(int discussAppClaudeDesktop READ discussAppClaudeDesktop CONSTANT)

public:
    explicit Settings(QObject* parent = nullptr);

    // Domain sub-object accessors (typed, for C++ callers — header forward-declares
    // the types so callers must include the specific settings_<domain>.h to dereference).
    SettingsMqtt* mqtt() const { return m_mqtt; }
    SettingsAutoWake* autoWake() const { return m_autoWake; }
    SettingsHardware* hardware() const { return m_hardware; }
    SettingsAI* ai() const { return m_ai; }
    SettingsTheme* theme() const { return m_theme; }
    SettingsVisualizer* visualizer() const { return m_visualizer; }
    SettingsMcp* mcp() const { return m_mcp; }

    // QML-facing accessors — implemented out-of-line in settings.cpp where the
    // SettingsXxx -> QObject* upcast is visible. QML uses these via Q_PROPERTY.
    QObject* mqttQObject() const;
    QObject* autoWakeQObject() const;
    QObject* hardwareQObject() const;
    QObject* aiQObject() const;
    QObject* themeQObject() const;
    QObject* visualizerQObject() const;
    QObject* mcpQObject() const;

    int discussAppNone() const { return 6; }
    int discussAppClaudeDesktop() const { return 7; }

    // Platform capabilities (compile-time)
    bool hasQuick3D() const {
#ifdef HAVE_QUICK3D
        return true;
#else
        return false;
#endif
    }

    bool use12HourTime() const { return m_use12HourTime; }

    // Machine settings
    QString machineAddress() const;
    void setMachineAddress(const QString& address);

    QString scaleAddress() const;
    void setScaleAddress(const QString& address);

    bool keepScaleOn() const;
    void setKeepScaleOn(bool keep);

    QString scaleType() const;
    void setScaleType(const QString& type);

    QString scaleName() const;
    void setScaleName(const QString& name);

    // Multi-scale management
    Q_INVOKABLE QVariantList knownScales() const;
    Q_INVOKABLE void addKnownScale(const QString& address, const QString& type, const QString& name);
    Q_INVOKABLE void removeKnownScale(const QString& address);
    Q_INVOKABLE void setPrimaryScale(const QString& address);
    Q_INVOKABLE QString primaryScaleAddress() const;
    Q_INVOKABLE bool isKnownScale(const QString& address) const;

    // FlowScale
    bool useFlowScale() const;
    void setUseFlowScale(bool enabled);

    // Scale connection alert dialogs
    bool showScaleDialogs() const;
    void setShowScaleDialogs(bool enabled);

    // Refractometer
    QString savedRefractometerAddress() const;
    void setSavedRefractometerAddress(const QString& address);
    QString savedRefractometerName() const;
    void setSavedRefractometerName(const QString& name);

    // USB serial polling
    bool usbSerialEnabled() const;
    void setUsbSerialEnabled(bool enabled);

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
    Q_INVOKABLE void addSteamPitcherPresetDisabled(const QString& name);
    Q_INVOKABLE void updateSteamPitcherPreset(int index, const QString& name, int duration, int flow);
    Q_INVOKABLE void removeSteamPitcherPreset(int index);
    Q_INVOKABLE void moveSteamPitcherPreset(int from, int to);
    Q_INVOKABLE QVariantMap getSteamPitcherPreset(int index) const;
    Q_INVOKABLE void setSteamPitcherWeight(int index, double weightG);

    // Launcher mode (Android only)
    bool launcherMode() const;
    void setLauncherMode(bool enabled);

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

    // Saved searches (shot history)
    QStringList savedSearches() const;
    void setSavedSearches(const QStringList& searches);
    Q_INVOKABLE void addSavedSearch(const QString& search);
    Q_INVOKABLE void removeSavedSearch(const QString& search);

    // Shot history sort
    QString shotHistorySortField() const;
    void setShotHistorySortField(const QString& field);
    QString shotHistorySortDirection() const;
    void setShotHistorySortDirection(const QString& direction);

    // Hot water settings
    double waterTemperature() const;
    void setWaterTemperature(double temp);

    int waterVolume() const;
    void setWaterVolume(int volume);

    QString waterVolumeMode() const;  // "weight" or "volume"
    void setWaterVolumeMode(const QString& mode);

    // Hot water volume byte to send in DE1 ShotSettings. In "weight" mode the
    // app stops hot water via the scale and sends 0 so the DE1 flowmeter
    // auto-stop is disabled; in "volume" mode it returns the clamped
    // waterVolume(). All setShotSettings() call sites must use this to keep
    // payloads consistent — otherwise the ShotSettings drift detector trips
    // on BLE echo reordering.
    int effectiveHotWaterVolume() const;

    double hotWaterSawOffset() const;
    void setHotWaterSawOffset(double offset);
    int hotWaterSawSampleCount() const;
    void setHotWaterSawSampleCount(int count);

    // Hot water vessel presets
    QVariantList waterVesselPresets() const;
    int selectedWaterVessel() const;
    void setSelectedWaterCup(int index);

    Q_INVOKABLE void addWaterVesselPreset(const QString& name, int volume, const QString& mode = "weight", int flowRate = 40);
    Q_INVOKABLE void updateWaterVesselPreset(int index, const QString& name, int volume, const QString& mode = "weight", int flowRate = 40);
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
                                   const QString& grinderBrand, const QString& grinderModel,
                                   const QString& grinderBurrs, const QString& grinderSetting,
                                   const QString& barista);
    Q_INVOKABLE void updateBeanPreset(int index, const QString& name, const QString& brand,
                                      const QString& type, const QString& roastDate,
                                      const QString& roastLevel, const QString& grinderBrand,
                                      const QString& grinderModel, const QString& grinderBurrs,
                                      const QString& grinderSetting, const QString& barista);
    Q_INVOKABLE void removeBeanPreset(int index);
    Q_INVOKABLE void moveBeanPreset(int from, int to);
    Q_INVOKABLE void setBeanPresetShowOnIdle(int index, bool show);
    QVariantList idleBeanPresets() const;              // see Q_PROPERTY above
    Q_INVOKABLE QVariantMap getBeanPreset(int index) const;
    Q_INVOKABLE void applyBeanPreset(int index);       // Sets all DYE fields from preset
    Q_INVOKABLE void saveBeanPresetFromCurrent(const QString& name);  // Creates or updates preset from current DYE
    Q_INVOKABLE int findBeanPresetByContent(const QString& brand, const QString& type) const;  // Returns index or -1 (simple match)
    Q_INVOKABLE int findBeanPresetByName(const QString& name) const;  // Returns index or -1

    bool beansModified() const { return m_beansModified; }

    QString currentProfile() const;
    void setCurrentProfile(const QString& profile);


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

    QString dyeGrinderBrand() const;
    void setDyeGrinderBrand(const QString& value);

    QString dyeGrinderModel() const;
    void setDyeGrinderModel(const QString& value);

    QString dyeGrinderBurrs() const;
    void setDyeGrinderBurrs(const QString& value);

    QString dyeGrinderSetting() const;
    void setDyeGrinderSetting(const QString& value);

    Q_INVOKABLE QStringList suggestedBurrs(const QString& brand, const QString& model) const;
    Q_INVOKABLE bool isBurrSwappable(const QString& brand, const QString& model) const;
    Q_INVOKABLE QStringList knownGrinderBrands() const;
    Q_INVOKABLE QStringList knownGrinderModels(const QString& brand) const;

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
    bool webSecurityEnabled() const;
    void setWebSecurityEnabled(bool enabled);

    // Auto-favorites settings
    QString autoFavoritesGroupBy() const;
    void setAutoFavoritesGroupBy(const QString& groupBy);
    int autoFavoritesMaxItems() const;
    void setAutoFavoritesMaxItems(int maxItems);
    bool autoFavoritesOpenBrewSettings() const;
    void setAutoFavoritesOpenBrewSettings(bool open);
    bool autoFavoritesHideUnrated() const;
    void setAutoFavoritesHideUnrated(bool hide);

    // Auto-update settings
    bool autoCheckUpdates() const;
    void setAutoCheckUpdates(bool enabled);
    bool betaUpdatesEnabled() const;
    void setBetaUpdatesEnabled(bool enabled);
    bool firmwareNightlyChannel() const;
    void setFirmwareNightlyChannel(bool enabled);

    // Daily backup
    int dailyBackupHour() const;
    void setDailyBackupHour(int hour);

    // Shot export to file
    bool exportShotsToFile() const;
    void setExportShotsToFile(bool enabled);

    // Water level display
    QString waterLevelDisplayUnit() const;
    void setWaterLevelDisplayUnit(const QString& unit);

    // Water refill level
    int waterRefillPoint() const;
    void setWaterRefillPoint(int mm);

    // Refill kit override
    int refillKitOverride() const;
    void setRefillKitOverride(int value);


    // Developer settings
    bool developerTranslationUpload() const;
    void setDeveloperTranslationUpload(bool enabled);

    bool simulationMode() const;
    void setSimulationMode(bool enabled);

    bool hideGhcSimulator() const;
    void setHideGhcSimulator(bool hide);

    bool simulatedScaleEnabled() const;
    void setSimulatedScaleEnabled(bool enabled);

    bool screenCaptureEnabled() const;
    void setScreenCaptureEnabled(bool enabled);

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

    // MCP Server settings

    // Discuss Shot settings
    int discussShotApp() const;
    void setDiscussShotApp(int app);
    QString discussShotCustomUrl() const;
    void setDiscussShotCustomUrl(const QString& url);
    QString claudeRcSessionUrl() const;
    void setClaudeRcSessionUrl(const QString& url);
    Q_INVOKABLE QString discussShotUrl() const;
    Q_INVOKABLE void openDiscussUrl(const QString& url);
    Q_INVOKABLE void dismissDiscussOverlay();

    // Flow calibration
    double flowCalibrationMultiplier() const;
    void setFlowCalibrationMultiplier(double multiplier);
    bool autoFlowCalibration() const;
    void setAutoFlowCalibration(bool enabled);
    bool ignoreVolumeWithScale() const;
    void setIgnoreVolumeWithScale(bool enabled);
    double profileFlowCalibration(const QString& profileFilename) const;
    bool setProfileFlowCalibration(const QString& profileFilename, double multiplier);
    Q_INVOKABLE void clearProfileFlowCalibration(const QString& profileFilename);
    Q_INVOKABLE double effectiveFlowCalibration(const QString& profileFilename) const;
    Q_INVOKABLE bool hasProfileFlowCalibration(const QString& profileFilename) const;
    QJsonObject allProfileFlowCalibrations() const;
    int perProfileFlowCalVersion() const { return m_perProfileFlowCalVersion; }

    // Auto flow calibration batch accumulator: stores pending ideal values per profile
    // until a full batch (5 shots) is collected, then the median is used to update C.
    QVector<double> flowCalPendingIdeals(const QString& profileFilename) const;
    void appendFlowCalPendingIdeal(const QString& profileFilename, double ideal);
    void clearFlowCalPendingIdeals(const QString& profileFilename);

    // SAW (Stop-at-Weight) learning
    double sawLearnedLag() const;  // Average lag for display in QML (calculated from drip/flow)
    double getExpectedDrip(double currentFlowRate) const;  // Predicts drip based on flow and history
    // Per-(profile, scale) variant of sawLearnedLag — falls back to global bootstrap /
    // per-scale data when the pair has not yet graduated (< 3 committed batch-medians).
    // Pass empty profile for the legacy global-pool path. Returns the mean of drip/flow
    // over the last 5 committed batch-medians (same numeric units as sawLearnedLag).
    Q_INVOKABLE double sawLearnedLagFor(const QString& profileFilename, const QString& scaleType) const;
    double getExpectedDripFor(const QString& profileFilename, const QString& scaleType, double currentFlowRate) const;
    QList<QPair<double, double>> sawLearningEntriesFor(const QString& profileFilename, const QString& scaleType, int maxEntries) const;

    // Reports which model the read path uses for (profile, scale). Strings:
    // "perProfile" | "globalBootstrap" | "globalPool" | "scaleDefault". Used for logging.
    Q_INVOKABLE QString sawModelSource(const QString& profileFilename, const QString& scaleType) const;

    void addSawLearningPoint(double drip, double flowRate, const QString& scaleType, double overshoot,
                             const QString& profileFilename = QString());
    Q_INVOKABLE void resetSawLearning();
    Q_INVOKABLE void resetSawLearningForProfile(const QString& profileFilename, const QString& scaleType);

    // Per-pair committed history (storage helpers; mostly for tests + bootstrap recompute).
    // Each entry is a batch median {drip, flow, overshoot, ts}.
    QJsonArray perProfileSawHistory(const QString& profileFilename, const QString& scaleType) const;
    QJsonObject allPerProfileSawHistory() const;

    // Per-pair pending batch accumulator (5 entries before committing the batch median).
    QJsonArray sawPendingBatch(const QString& profileFilename, const QString& scaleType) const;

    // Global bootstrap lag for new (profile, scale) pairs without graduated history.
    // Returns 0.0 if no bootstrap exists (caller should fall through to global pool).
    double globalSawBootstrapLag(const QString& scaleType) const;
    void setGlobalSawBootstrapLag(const QString& scaleType, double lag);

    // Per-scale BLE sensor lag (seconds). Used as first-shot SAW default before learning kicks in.
    // Values empirically derived from de1app device_scale.tcl.
    static double sensorLag(const QString& scaleType);

    // Layout configuration (dynamic IdlePage layout)
    QString layoutConfiguration() const;
    void setLayoutConfiguration(const QString& json);
    Q_INVOKABLE QVariantList getZoneItems(const QString& zoneName) const;
    Q_INVOKABLE void moveItem(const QString& itemId, const QString& fromZone, const QString& toZone, int toIndex);
    Q_INVOKABLE void addItem(const QString& type, const QString& zone, int index = -1);
    Q_INVOKABLE void removeItem(const QString& itemId, const QString& zone);
    Q_INVOKABLE void reorderItem(const QString& zoneName, int fromIndex, int toIndex);
    Q_INVOKABLE void resetLayoutToDefault();
    Q_INVOKABLE void factoryReset();
    Q_INVOKABLE bool hasItemType(const QString& type) const;
    Q_INVOKABLE int getZoneYOffset(const QString& zoneName) const;
    Q_INVOKABLE void setZoneYOffset(const QString& zoneName, int offset);
    Q_INVOKABLE double getZoneScale(const QString& zoneName) const;
    Q_INVOKABLE void setZoneScale(const QString& zoneName, double scale);
    Q_INVOKABLE void setItemProperty(const QString& itemId, const QString& key, const QVariant& value);
    Q_INVOKABLE QVariantMap getItemProperties(const QString& itemId) const;

    // Device identity (stable UUID for server communication)
    Q_INVOKABLE QString deviceId() const;

    // Pocket app pairing token
    Q_INVOKABLE QString pocketPairingToken() const;
    void setPocketPairingToken(const QString& token);

    // Generic settings access (for extensibility)
    Q_INVOKABLE QVariant value(const QString& key, const QVariant& defaultValue = QVariant()) const;
    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);

    // Coerced boolean getter. QSettings' INI backend (used on Android/Linux/iOS)
    // round-trips booleans as the strings "true"/"false", which JavaScript then
    // treats as truthy — so `property bool foo: Settings.value("foo", true)`
    // returned the wrong value after the key had been written once. This helper
    // performs the coercion in C++ so QML callers don't have to.
    Q_INVOKABLE bool boolValue(const QString& key, bool defaultValue = false) const;

    // SAW convergence detection helper
    bool isSawConverged(const QString& scaleType) const;
    // Returns SAW learning entries filtered by scale type (most recent first).
    // Used by WeightProcessor to snapshot learning data at shot start.
    QList<QPair<double, double>> sawLearningEntries(const QString& scaleType, int maxEntries) const;

signals:
    void machineAddressChanged();
    void scaleAddressChanged();
    void scaleTypeChanged();
    void keepScaleOnChanged();
    void scaleNameChanged();
    void knownScalesChanged();
    void useFlowScaleChanged();
    void showScaleDialogsChanged();
    void savedRefractometerChanged();
    void usbSerialEnabledChanged();
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
    void launcherModeChanged();
    void favoriteProfilesChanged();
    void selectedFavoriteProfileChanged();
    void selectedBuiltInProfilesChanged();
    void hiddenProfilesChanged();
    void savedSearchesChanged();
    void shotHistorySortFieldChanged();
    void shotHistorySortDirectionChanged();
    void waterTemperatureChanged();
    void waterVolumeChanged();
    void waterVolumeModeChanged();
    void hotWaterSawOffsetChanged();
    void waterVesselPresetsChanged();
    void selectedWaterVesselChanged();
    void flushPresetsChanged();
    void selectedFlushPresetChanged();
    void flushFlowChanged();
    void flushSecondsChanged();
    void beanPresetsChanged();
    void selectedBeanPresetChanged();
    void beansModifiedChanged();
    void currentProfileChanged();
    void dyeBeanBrandChanged();
    void dyeBeanTypeChanged();
    void dyeRoastDateChanged();
    void dyeRoastLevelChanged();
    void dyeGrinderBrandChanged();
    void dyeGrinderModelChanged();
    void dyeGrinderBurrsChanged();
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
    void webSecurityEnabledChanged();
    void autoFavoritesGroupByChanged();
    void autoFavoritesMaxItemsChanged();
    void autoFavoritesOpenBrewSettingsChanged();
    void autoFavoritesHideUnratedChanged();
    void autoCheckUpdatesChanged();
    void betaUpdatesEnabledChanged();
    void firmwareNightlyChannelChanged();
    void dailyBackupHourChanged();
    void exportShotsToFileChanged();
    void waterLevelDisplayUnitChanged();
    void waterRefillPointChanged();
    void refillKitOverrideChanged();
    void developerTranslationUploadChanged();
    void simulationModeChanged();
    void screenCaptureEnabledChanged();
    void hideGhcSimulatorChanged();
    void simulatedScaleEnabledChanged();
    void temperatureOverrideChanged();
    void brewOverridesChanged();
    void discussShotAppChanged();
    void discussShotCustomUrlChanged();
    void claudeRcSessionUrlChanged();
    void flowCalibrationMultiplierChanged();
    void autoFlowCalibrationChanged();
    void ignoreVolumeWithScaleChanged();
    void perProfileFlowCalibrationChanged();
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

    void ensureSawCacheLoaded() const;
    void writeKnownScales(const QVariantList& scales);

    // Recompute m_beansModified from current DYE values vs the selected preset and emit
    // beansModifiedChanged() if the cached state flipped. Wired to the relevant NOTIFY
    // signals in the constructor.
    void recomputeBeansModified();
    bool m_beansModified = false;

    mutable QSettings m_settings;
    bool m_use12HourTime = false;

    // SAW learning history cache (avoids re-parsing JSON from QSettings on every weight sample)
    mutable QJsonArray m_sawHistoryCache;
    mutable bool m_sawHistoryCacheDirty = true;
    mutable int m_sawConvergedCache = -1;  // -1 = unknown, 0 = no, 1 = yes
    mutable QString m_sawConvergedScaleType;  // Scale type for cached convergence result
    mutable QJsonObject m_layoutCache;
    mutable QString m_layoutJsonCache;
    mutable bool m_layoutCacheValid = false;
    void invalidateLayoutCache();
    // Cached DYE values (avoid QSettings::value() → CFPreferences on every QML binding read)
    mutable QString m_dyeGrinderBrandCache;
    mutable QString m_dyeGrinderModelCache;
    mutable QString m_dyeGrinderBurrsCache;
    mutable QString m_dyeGrinderSettingCache;
    mutable double m_dyeBeanWeightCache = 18.0;
    mutable double m_dyeDrinkWeightCache = 36.0;
    mutable bool m_dyeCacheInitialized = false;
    void ensureDyeCacheLoaded() const;

    int m_perProfileFlowCalVersion = 0;  // Bumped on per-profile calibration changes to trigger QML rebind
    mutable QJsonObject m_perProfileFlowCalCache;  // Cached per-profile flow calibration map
    mutable bool m_perProfileFlowCalCacheValid = false;
    void savePerProfileFlowCalMap(const QJsonObject& map);

    // Per-(profile, scale) SAW history cache. INVARIANT: all writes route through
    // savePerProfileSawHistoryMap() / savePerProfileSawBatchMap() to keep the cache
    // and QSettings in sync (mirrors the perProfileFlowCal cache pattern).
    mutable QJsonObject m_perProfileSawHistoryCache;
    mutable bool m_perProfileSawHistoryCacheValid = false;
    mutable QJsonObject m_perProfileSawBatchCache;
    mutable bool m_perProfileSawBatchCacheValid = false;
    QJsonObject loadPerProfileSawHistoryMap() const;
    void savePerProfileSawHistoryMap(const QJsonObject& map);
    QJsonObject loadPerProfileSawBatchMap() const;
    void savePerProfileSawBatchMap(const QJsonObject& map);
    static QString sawPairKey(const QString& profileFilename, const QString& scaleType);
    void addSawPerPairEntry(double drip, double flowRate, const QString& scaleType,
                            double overshoot, const QString& profileFilename);
    void recomputeGlobalSawBootstrap(const QString& scaleType);
    bool m_steamDisabled = false;  // Session-only, not persisted (for descaling)
    double m_temperatureOverride = 0;  // Session-only, for next shot
    bool m_hasTemperatureOverride = false;  // Session-only
    bool m_developerTranslationUpload = false;  // Session-only, Easter egg unlock

    // Brew parameter overrides (session-only)
    double m_brewYieldOverride = 0;
    bool m_hasBrewYieldOverride = false;

    // Domain sub-objects (composition façade)
    SettingsMqtt* m_mqtt = nullptr;
    SettingsAutoWake* m_autoWake = nullptr;
    SettingsHardware* m_hardware = nullptr;
    SettingsAI* m_ai = nullptr;
    SettingsTheme* m_theme = nullptr;
    SettingsVisualizer* m_visualizer = nullptr;
    SettingsMcp* m_mcp = nullptr;
};
