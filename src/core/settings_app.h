#pragma once

#include <QObject>
#include "appsettings.h"
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

    // Launcher mode (Android only — registers app as the home screen launcher
    // via the LauncherAlias activity-alias)
    Q_PROPERTY(bool launcherMode READ launcherMode WRITE setLauncherMode NOTIFY launcherModeChanged)

    // Profile management
    Q_PROPERTY(QVariantList favoriteProfiles READ favoriteProfiles NOTIFY favoriteProfilesChanged)
    Q_PROPERTY(int selectedFavoriteProfile READ selectedFavoriteProfile WRITE setSelectedFavoriteProfile NOTIFY selectedFavoriteProfileChanged)
    Q_PROPERTY(QStringList selectedBuiltInProfiles READ selectedBuiltInProfiles WRITE setSelectedBuiltInProfiles NOTIFY selectedBuiltInProfilesChanged)
    Q_PROPERTY(QStringList hiddenProfiles READ hiddenProfiles WRITE setHiddenProfiles NOTIFY hiddenProfilesChanged)
    Q_PROPERTY(QString currentProfile READ currentProfile WRITE setCurrentProfile NOTIFY currentProfileChanged)
    Q_PROPERTY(QString autoLoadProfileFilename READ autoLoadProfileFilename WRITE setAutoLoadProfileFilename NOTIFY autoLoadProfileFilenameChanged)
    Q_PROPERTY(int autoLoadRevertMinutes READ autoLoadRevertMinutes WRITE setAutoLoadRevertMinutes NOTIFY autoLoadRevertMinutesChanged)

    // Auto-update
    Q_PROPERTY(bool autoCheckUpdates READ autoCheckUpdates WRITE setAutoCheckUpdates NOTIFY autoCheckUpdatesChanged)
    Q_PROPERTY(bool betaUpdatesEnabled READ betaUpdatesEnabled WRITE setBetaUpdatesEnabled NOTIFY betaUpdatesEnabledChanged)
    Q_PROPERTY(qint64 lastKnownApkSizeBytes READ lastKnownApkSizeBytes WRITE setLastKnownApkSizeBytes NOTIFY lastKnownApkSizeBytesChanged)

    // One-time "enable Appear on top to auto-reopen after updates" prompt
    // has been shown. Persists across app restarts; once true, the prompt
    // is never re-shown. Matches the GPS/storage permission-prompt model:
    // user is asked once at the teachable moment, no permanent in-app UI.
    // READ-only on the Q_PROPERTY so QML cannot accidentally re-arm the
    // prompt by writing false; UpdateChecker mutates via the C++ setter.
    Q_PROPERTY(bool autoRelaunchPromptShown READ autoRelaunchPromptShown NOTIFY autoRelaunchPromptShownChanged)

    // DE1 firmware update channel. When false (default), firmware comes
    // from fast.decentespresso.com/download/sync/de1plus; when true,
    // from .../de1nightly. Independent from betaUpdatesEnabled, which
    // controls the Decenza *app* update channel.
    Q_PROPERTY(bool firmwareNightlyChannel READ firmwareNightlyChannel WRITE setFirmwareNightlyChannel NOTIFY firmwareNightlyChannelChanged)

    // Daily backup
    Q_PROPERTY(int dailyBackupHour READ dailyBackupHour WRITE setDailyBackupHour NOTIFY dailyBackupHourChanged)

    // Water level / refill
    Q_PROPERTY(QString waterLevelDisplayUnit READ waterLevelDisplayUnit WRITE setWaterLevelDisplayUnit NOTIFY waterLevelDisplayUnitChanged)
    Q_PROPERTY(QString temperatureUnit READ temperatureUnit WRITE setTemperatureUnit NOTIFY temperatureUnitChanged)

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

    // During-steam live coaching cues (LiveSteamCoach). Two independent opt-ins,
    // both OFF by default: `steamCoachVisualEnabled` shows the on-screen banner on
    // the steam page, `steamCoachAudioEnabled` speaks the cues. Neither implies the
    // other, and the audio path is routed independently of the accessibility
    // master switch (AccessibilityManager::announceCoaching).
    Q_PROPERTY(bool steamCoachVisualEnabled READ steamCoachVisualEnabled WRITE setSteamCoachVisualEnabled NOTIFY steamCoachVisualEnabledChanged)
    Q_PROPERTY(bool steamCoachAudioEnabled READ steamCoachAudioEnabled WRITE setSteamCoachAudioEnabled NOTIFY steamCoachAudioEnabledChanged)

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

    // Auto-load profile
    QString autoLoadProfileFilename() const;
    void setAutoLoadProfileFilename(const QString& filename);
    int autoLoadRevertMinutes() const;
    void setAutoLoadRevertMinutes(int minutes);

    // Auto-update
    bool autoCheckUpdates() const;
    void setAutoCheckUpdates(bool enabled);
    bool betaUpdatesEnabled() const;
    void setBetaUpdatesEnabled(bool enabled);
    bool firmwareNightlyChannel() const;
    void setFirmwareNightlyChannel(bool enabled);
    qint64 lastKnownApkSizeBytes() const;
    void setLastKnownApkSizeBytes(qint64 size);

    // Android auto-relaunch one-time prompt sentinel
    bool autoRelaunchPromptShown() const;
    void setAutoRelaunchPromptShown(bool shown);

    // Daily backup
    int dailyBackupHour() const;
    void setDailyBackupHour(int hour);

    // Water level / refill
    QString waterLevelDisplayUnit() const;
    void setWaterLevelDisplayUnit(const QString& unit);

    // Temperature display unit ("celsius" or "fahrenheit"). Storage stays Celsius;
    // this only affects display/entry.
    QString temperatureUnit() const;
    void setTemperatureUnit(const QString& unit);
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

    // During-steam live coaching cues (independent visual + audio opt-ins)
    bool steamCoachVisualEnabled() const;
    void setSteamCoachVisualEnabled(bool enabled);
    bool steamCoachAudioEnabled() const;
    void setSteamCoachAudioEnabled(bool enabled);

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
    void autoLoadProfileFilenameChanged();
    void autoLoadRevertMinutesChanged();
    void autoCheckUpdatesChanged();
    void betaUpdatesEnabledChanged();
    void lastKnownApkSizeBytesChanged();
    void autoRelaunchPromptShownChanged();
    void firmwareNightlyChannelChanged();
    void dailyBackupHourChanged();
    void waterLevelDisplayUnitChanged();
    void temperatureUnitChanged();
    void waterRefillPointChanged();
    void refillKitOverrideChanged();
    void developerTranslationUploadChanged();
    void simulationModeChanged();
    void hideGhcSimulatorChanged();
    void simulatedScaleEnabledChanged();
    void screenCaptureEnabledChanged();
    void steamCoachVisualEnabledChanged();
    void steamCoachAudioEnabledChanged();

private:
    mutable AppSettings m_settings;
    bool m_use12HourTime = false;

    // Runtime-only flag — not persisted, resets to false on app restart
    bool m_developerTranslationUpload = false;
};
