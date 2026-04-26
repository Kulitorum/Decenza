#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// App-level settings: auto-update channel, backup schedule, developer/platform
// flags, water level/refill, profile management bookkeeping (favorites, hidden,
// selected built-ins, current profile), device identity, Pocket pairing.
//
// Split from Settings to keep settings.h's transitive-include footprint small.
class SettingsApp : public QObject {
    Q_OBJECT

    // Platform capabilities (compile-time)
    Q_PROPERTY(bool hasQuick3D READ hasQuick3D CONSTANT)
    Q_PROPERTY(bool use12HourTime READ use12HourTime CONSTANT)
    Q_PROPERTY(bool isDebugBuild READ isDebugBuild CONSTANT)

    // Launcher mode (Android only)
    Q_PROPERTY(bool launcherMode READ launcherMode WRITE setLauncherMode NOTIFY launcherModeChanged)

    // Profile management
    Q_PROPERTY(QVariantList favoriteProfiles READ favoriteProfiles NOTIFY favoriteProfilesChanged)
    Q_PROPERTY(int selectedFavoriteProfile READ selectedFavoriteProfile WRITE setSelectedFavoriteProfile NOTIFY selectedFavoriteProfileChanged)
    Q_PROPERTY(QStringList selectedBuiltInProfiles READ selectedBuiltInProfiles WRITE setSelectedBuiltInProfiles NOTIFY selectedBuiltInProfilesChanged)
    Q_PROPERTY(QStringList hiddenProfiles READ hiddenProfiles WRITE setHiddenProfiles NOTIFY hiddenProfilesChanged)
    Q_PROPERTY(QString currentProfile READ currentProfile WRITE setCurrentProfile NOTIFY currentProfileChanged)

    // Auto-update
    Q_PROPERTY(bool autoCheckUpdates READ autoCheckUpdates WRITE setAutoCheckUpdates NOTIFY autoCheckUpdatesChanged)
    Q_PROPERTY(bool betaUpdatesEnabled READ betaUpdatesEnabled WRITE setBetaUpdatesEnabled NOTIFY betaUpdatesEnabledChanged)
    Q_PROPERTY(bool firmwareNightlyChannel READ firmwareNightlyChannel WRITE setFirmwareNightlyChannel NOTIFY firmwareNightlyChannelChanged)

    // Daily backup
    Q_PROPERTY(int dailyBackupHour READ dailyBackupHour WRITE setDailyBackupHour NOTIFY dailyBackupHourChanged)

    // Water level / refill
    Q_PROPERTY(QString waterLevelDisplayUnit READ waterLevelDisplayUnit WRITE setWaterLevelDisplayUnit NOTIFY waterLevelDisplayUnitChanged)
    Q_PROPERTY(int waterRefillPoint READ waterRefillPoint WRITE setWaterRefillPoint NOTIFY waterRefillPointChanged)
    Q_PROPERTY(int refillKitOverride READ refillKitOverride WRITE setRefillKitOverride NOTIFY refillKitOverrideChanged)

    // Developer settings
    Q_PROPERTY(bool developerTranslationUpload READ developerTranslationUpload WRITE setDeveloperTranslationUpload NOTIFY developerTranslationUploadChanged)
    Q_PROPERTY(bool simulationMode READ simulationMode WRITE setSimulationMode NOTIFY simulationModeChanged)
    Q_PROPERTY(bool hideGhcSimulator READ hideGhcSimulator WRITE setHideGhcSimulator NOTIFY hideGhcSimulatorChanged)
    Q_PROPERTY(bool simulatedScaleEnabled READ simulatedScaleEnabled WRITE setSimulatedScaleEnabled NOTIFY simulatedScaleEnabledChanged)
    Q_PROPERTY(bool screenCaptureEnabled READ screenCaptureEnabled WRITE setScreenCaptureEnabled NOTIFY screenCaptureEnabledChanged)

public:
    explicit SettingsApp(QObject* parent = nullptr);

    // Platform capabilities
    bool hasQuick3D() const;
    bool use12HourTime() const { return m_use12HourTime; }
    bool isDebugBuild() const;

    // Launcher mode
    bool launcherMode() const;
    void setLauncherMode(bool enabled);

    // Profile favorites
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

    // Hidden profiles
    QStringList hiddenProfiles() const;
    void setHiddenProfiles(const QStringList& profiles);
    Q_INVOKABLE void addHiddenProfile(const QString& filename);
    Q_INVOKABLE void removeHiddenProfile(const QString& filename);
    Q_INVOKABLE bool isHiddenProfile(const QString& filename) const;

    // Current profile
    QString currentProfile() const;
    void setCurrentProfile(const QString& profile);

    // Auto-update
    bool autoCheckUpdates() const;
    void setAutoCheckUpdates(bool enabled);
    bool betaUpdatesEnabled() const;
    void setBetaUpdatesEnabled(bool enabled);
    bool firmwareNightlyChannel() const;
    void setFirmwareNightlyChannel(bool enabled);

    // Daily backup
    int dailyBackupHour() const;
    void setDailyBackupHour(int hour);

    // Water level / refill
    QString waterLevelDisplayUnit() const;
    void setWaterLevelDisplayUnit(const QString& unit);
    int waterRefillPoint() const;
    void setWaterRefillPoint(int mm);
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

    // Device identity (stable UUID for server communication)
    Q_INVOKABLE QString deviceId() const;

    // Pocket app pairing token
    Q_INVOKABLE QString pocketPairingToken() const;
    void setPocketPairingToken(const QString& token);

signals:
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
    void hideGhcSimulatorChanged();
    void simulatedScaleEnabledChanged();
    void screenCaptureEnabledChanged();

private:
    mutable QSettings m_settings;
    bool m_use12HourTime = false;

    // Runtime-only flag — not persisted, resets to false on app restart
    bool m_developerTranslationUpload = false;
};
