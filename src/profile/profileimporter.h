#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include "profile.h"

class MainController;
class Settings;

/**
 * ProfileImporter - Import profiles directly from DE1 tablet
 *
 * Scans de1plus/profiles (TCL) and de1plus/profiles_v2 (JSON) folders
 * on the device and allows importing profiles without going through Visualizer.
 *
 * Features:
 * - Auto-detects DE1 app profile folders
 * - Supports both TCL (legacy) and JSON (v2) profile formats
 * - Duplicate detection with frame-by-frame comparison
 * - Batch import with overwrite/skip options
 */
class ProfileImporter : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool isScanning READ isScanning NOTIFY isScanningChanged)
    Q_PROPERTY(bool isImporting READ isImporting NOTIFY isImportingChanged)
    Q_PROPERTY(QVariantList availableProfiles READ availableProfiles NOTIFY availableProfilesChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString detectedPath READ detectedPath NOTIFY detectedPathChanged)
    Q_PROPERTY(int totalProfiles READ totalProfiles NOTIFY progressChanged)
    Q_PROPERTY(int processedProfiles READ processedProfiles NOTIFY progressChanged)

public:
    explicit ProfileImporter(MainController* controller, Settings* settings, QObject* parent = nullptr);

    bool isScanning() const { return m_scanning; }
    bool isImporting() const { return m_importing; }
    QVariantList availableProfiles() const { return m_availableProfiles; }
    QString statusMessage() const { return m_statusMessage; }
    QString detectedPath() const { return m_detectedPath; }
    int totalProfiles() const { return m_totalProfiles; }
    int processedProfiles() const { return m_processedProfiles; }

    // Auto-detect de1plus folder paths
    Q_INVOKABLE QString detectDE1AppPath() const;

    // Scan for available profiles (both TCL and JSON)
    Q_INVOKABLE void scanProfiles();
    Q_INVOKABLE void scanProfilesFromPath(const QString& path);

    // Import a single profile
    Q_INVOKABLE void importProfile(const QString& sourcePath);
    Q_INVOKABLE void importProfileFromUrl(const QUrl& fileUrl);
    Q_INVOKABLE void importProfileWithName(const QString& sourcePath, const QString& newName);
    Q_INVOKABLE void forceImportProfile(const QString& sourcePath);  // Overwrite without asking

    // Import all new profiles (skip existing unless overwrite is true)
    Q_INVOKABLE void importAllNew();
    Q_INVOKABLE void importAll(bool overwriteExisting);
    Q_INVOKABLE void updateAllDifferent();  // Update all profiles marked as "different"

    // Duplicate resolution actions (after duplicateFound signal)
    Q_INVOKABLE void saveOverwrite();
    Q_INVOKABLE void saveAsNew();
    Q_INVOKABLE void saveWithNewName(const QString& newName);
    Q_INVOKABLE void cancelImport();

    // Refresh status of a single profile after import
    Q_INVOKABLE void refreshProfileStatus(int index);

signals:
    void isScanningChanged();
    void isImportingChanged();
    void availableProfilesChanged();
    void statusMessageChanged();
    void detectedPathChanged();
    void progressChanged();

    void scanComplete(int found);
    void importSuccess(const QString& profileTitle);
    void importFailed(const QString& error);
    void duplicateFound(const QString& profileTitle, const QString& existingPath);
    void batchImportComplete(int imported, int skipped, int failed);

private slots:
    void processNextScan();
    void processNextImport();

private:
    void setStatus(const QString& message);
    QVariantMap checkProfileStatus(const QString& profileTitle, const Profile* incomingProfile);
    bool compareProfileFrames(const Profile& a, const Profile& b) const;
    Profile loadLocalProfile(const QString& filename) const;
    QString generateFilename(const QString& title) const;
    int saveProfile(const Profile& profile, const QString& filename);
    QString downloadedProfilesPath() const;

    MainController* m_controller = nullptr;
    Settings* m_settings = nullptr;

    bool m_scanning = false;
    bool m_importing = false;
    QString m_statusMessage;
    QString m_detectedPath;

    // Scanning state
    QStringList m_pendingFiles;
    QVariantList m_availableProfiles;
    int m_totalProfiles = 0;
    int m_processedProfiles = 0;

    // Import state
    QStringList m_importQueue;
    bool m_batchOverwrite = false;
    int m_batchImported = 0;
    int m_batchSkipped = 0;
    int m_batchFailed = 0;

    // Pending profile for duplicate resolution
    Profile m_pendingProfile;
    QString m_pendingSourcePath;
};
