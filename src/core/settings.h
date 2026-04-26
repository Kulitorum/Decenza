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
// to include the ten sub-object headers — preserving the recompile-blast
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
class SettingsBrew;
class SettingsDye;
class SettingsNetwork;

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
    Q_PROPERTY(QObject* brew READ brewQObject CONSTANT)
    Q_PROPERTY(QObject* dye READ dyeQObject CONSTANT)
    Q_PROPERTY(QObject* network READ networkQObject CONSTANT)

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

    // Launcher mode (Android only - register as home screen)
    Q_PROPERTY(bool launcherMode READ launcherMode WRITE setLauncherMode NOTIFY launcherModeChanged)

    // Profile favorites
    Q_PROPERTY(QVariantList favoriteProfiles READ favoriteProfiles NOTIFY favoriteProfilesChanged)
    Q_PROPERTY(int selectedFavoriteProfile READ selectedFavoriteProfile WRITE setSelectedFavoriteProfile NOTIFY selectedFavoriteProfileChanged)

    // Selected built-in profiles (shown in "Selected" view)
    Q_PROPERTY(QStringList selectedBuiltInProfiles READ selectedBuiltInProfiles WRITE setSelectedBuiltInProfiles NOTIFY selectedBuiltInProfilesChanged)

    // Hidden profiles (downloaded/user profiles removed from "Selected" view)
    Q_PROPERTY(QStringList hiddenProfiles READ hiddenProfiles WRITE setHiddenProfiles NOTIFY hiddenProfilesChanged)

    // UI settings
    Q_PROPERTY(QString currentProfile READ currentProfile WRITE setCurrentProfile NOTIFY currentProfileChanged)



    // Build info
    Q_PROPERTY(bool isDebugBuild READ isDebugBuild CONSTANT)

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

    // Flow calibration
    Q_PROPERTY(double flowCalibrationMultiplier READ flowCalibrationMultiplier WRITE setFlowCalibrationMultiplier NOTIFY flowCalibrationMultiplierChanged)
    Q_PROPERTY(bool autoFlowCalibration READ autoFlowCalibration WRITE setAutoFlowCalibration NOTIFY autoFlowCalibrationChanged)
    Q_PROPERTY(int perProfileFlowCalVersion READ perProfileFlowCalVersion NOTIFY perProfileFlowCalibrationChanged)

    // SAW (Stop-at-Weight) learning
    Q_PROPERTY(double sawLearnedLag READ sawLearnedLag NOTIFY sawLearnedLagChanged)

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
    SettingsBrew* brew() const { return m_brew; }
    SettingsDye* dye() const { return m_dye; }
    SettingsNetwork* network() const { return m_network; }

    // QML-facing accessors — implemented out-of-line in settings.cpp where the
    // SettingsXxx -> QObject* upcast is visible. QML uses these via Q_PROPERTY.
    QObject* mqttQObject() const;
    QObject* autoWakeQObject() const;
    QObject* hardwareQObject() const;
    QObject* aiQObject() const;
    QObject* themeQObject() const;
    QObject* visualizerQObject() const;
    QObject* mcpQObject() const;
    QObject* brewQObject() const;
    QObject* dyeQObject() const;
    QObject* networkQObject() const;

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
    QString currentProfile() const;
    void setCurrentProfile(const QString& profile);


    // Build info
    bool isDebugBuild() const;

    // Force sync to disk
    void sync() { m_settings.sync(); }

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

    // Flow calibration
    double flowCalibrationMultiplier() const;
    void setFlowCalibrationMultiplier(double multiplier);
    bool autoFlowCalibration() const;
    void setAutoFlowCalibration(bool enabled);
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

    Q_INVOKABLE void factoryReset();

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
    void launcherModeChanged();
    void favoriteProfilesChanged();
    void selectedFavoriteProfileChanged();
    void selectedBuiltInProfilesChanged();
    void hiddenProfilesChanged();
    void currentProfileChanged();
    void autoCheckUpdatesChanged();
    void betaUpdatesEnabledChanged();
    void firmwareNightlyChannelChanged();
    void dailyBackupHourChanged();
    void waterLevelDisplayUnitChanged();
    void waterRefillPointChanged();
    void refillKitOverrideChanged();
    void developerTranslationUploadChanged();
    void simulationModeChanged();
    void screenCaptureEnabledChanged();
    void hideGhcSimulatorChanged();
    void simulatedScaleEnabledChanged();
    void flowCalibrationMultiplierChanged();
    void autoFlowCalibrationChanged();
    void perProfileFlowCalibrationChanged();
    void sawLearnedLagChanged();
    void valueChanged(const QString& key);

private:
    void ensureSawCacheLoaded() const;
    void writeKnownScales(const QVariantList& scales);

    mutable QSettings m_settings;
    bool m_use12HourTime = false;

    // SAW learning history cache (avoids re-parsing JSON from QSettings on every weight sample)
    mutable QJsonArray m_sawHistoryCache;
    mutable bool m_sawHistoryCacheDirty = true;
    mutable int m_sawConvergedCache = -1;  // -1 = unknown, 0 = no, 1 = yes
    mutable QString m_sawConvergedScaleType;  // Scale type for cached convergence result
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
    bool m_developerTranslationUpload = false;  // Session-only, Easter egg unlock

    // Domain sub-objects (composition façade)
    SettingsMqtt* m_mqtt = nullptr;
    SettingsAutoWake* m_autoWake = nullptr;
    SettingsHardware* m_hardware = nullptr;
    SettingsAI* m_ai = nullptr;
    SettingsTheme* m_theme = nullptr;
    SettingsVisualizer* m_visualizer = nullptr;
    SettingsMcp* m_mcp = nullptr;
    SettingsBrew* m_brew = nullptr;
    SettingsDye* m_dye = nullptr;
    SettingsNetwork* m_network = nullptr;
};
