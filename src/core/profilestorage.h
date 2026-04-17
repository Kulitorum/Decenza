#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// ProfileStorage handles profile persistence with external storage on Android
// (Documents/Decenza folder) to ensure profiles survive app reinstalls.
// On other platforms, uses standard file paths.
class ProfileStorage : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool isConfigured READ isConfigured NOTIFY configuredChanged)
    Q_PROPERTY(bool needsSetup READ needsSetup NOTIFY configuredChanged)

public:
    explicit ProfileStorage(QObject* parent = nullptr);

    // Check if storage is configured (permission granted on Android, always true on desktop)
    bool isConfigured() const;

    // Check if we need to show the setup dialog (Android 11+ only, first launch)
    bool needsSetup() const;

    // Request storage permission (opens Android settings)
    Q_INVOKABLE void selectFolder();

    // Skip setup (user chose not to grant permission)
    Q_INVOKABLE void skipSetup();

    // Check permission status and emit signals (call when app resumes)
    Q_INVOKABLE void checkPermissionAndNotify();

    // List all profile filenames (without .json extension)
    QStringList listProfiles() const;

    // Read profile JSON content
    QString readProfile(const QString& filename) const;

    // Write profile JSON content
    bool writeProfile(const QString& filename, const QString& content);

    // Delete a profile
    bool deleteProfile(const QString& filename);

    // Check if a profile exists
    bool profileExists(const QString& filename) const;

    // Get the fallback (app-internal) profiles path
    QString fallbackPath() const;

    // Get the external profiles path (Documents/Decenza on Android)
    QString externalProfilesPath() const;

    // Get the user profiles path (for user-created profiles)
    QString userProfilesPath() const;

    // Get the downloaded profiles path (for profiles imported from Visualizer)
    QString downloadedProfilesPath() const;

    // Get the user history path (sibling of the user profiles folder) for shot export
    QString userHistoryPath() const;

    // Migrate profiles from internal to external storage (call after permission granted)
    Q_INVOKABLE void migrateProfilesToExternal();

signals:
    void configuredChanged();
    void folderSelected(bool success);

private:
    bool m_setupSkipped = false;
};
