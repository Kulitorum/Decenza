#pragma once

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QThread>
#include <QVector>
#include <memory>

class Settings;
class ShotHistoryStorage;
class ProfileStorage;
class ScreensaverVideoManager;

/**
 * @brief Manages automatic daily comprehensive backups (shots, settings, profiles, media).
 *
 * Uses hourly checks to detect when backup time has passed:
 * - Checks every hour (3600000ms)
 * - If current time >= target time AND we haven't backed up today, create backup
 * - Tracks last backup date to avoid duplicates
 * - Cleans up backups older than 5 days after successful backup
 */
class DatabaseBackupManager : public QObject {
    Q_OBJECT

    Q_PROPERTY(QStringList availableBackups READ availableBackups NOTIFY availableBackupsChanged)

public:
    explicit DatabaseBackupManager(Settings* settings, ShotHistoryStorage* storage,
                                   ProfileStorage* profileStorage = nullptr,
                                   ScreensaverVideoManager* screensaverManager = nullptr,
                                   QObject* parent = nullptr);
    ~DatabaseBackupManager();

    /// Start the backup scheduler (call after app initialization)
    void start();

    /// Stop the backup scheduler
    void stop();

    /// Manually trigger a backup (for testing or user-initiated backup)
    /// @param force If true, overwrites existing backup for today
    Q_INVOKABLE bool createBackup(bool force = false);

    /// Get cached list of available backups (returns list of filenames)
    QStringList availableBackups() const { return m_cachedBackups; }

    /// Get list of available backups (returns list of filenames)
    Q_INVOKABLE QStringList getAvailableBackups() const;

    /// Refresh the cached backup list (called on start and after backup creation)
    void refreshBackupList();

    /// Restore a backup by filename (selectively restoring chosen data types)
    /// @param filename The backup filename (e.g., "shots_backup_20260210.zip")
    /// @param merge If true, merge all data with existing; if false, replace all existing data
    /// @param restoreShots If true, import the shot history database
    /// @param restoreSettings If true, import settings and AI conversations
    /// @param restoreProfiles If true, import user and downloaded profiles
    /// @param restoreMedia If true, import screensaver media files
    Q_INVOKABLE bool restoreBackup(const QString& filename, bool merge = true,
                                    bool restoreShots = true, bool restoreSettings = true,
                                    bool restoreProfiles = true, bool restoreMedia = true);

    /// Check if we should offer first-run restore (empty database + backups exist).
    /// Runs the DB query on a background thread; emits firstRunRestoreResult() when done.
    Q_INVOKABLE void checkFirstRunRestore();

    /// Check if storage permissions are granted (Android only)
    Q_INVOKABLE bool hasStoragePermission() const;

    /// Request storage permissions (Android only)
    Q_INVOKABLE void requestStoragePermission();

signals:
    /// Emitted when backup succeeds
    void backupCreated(const QString& path);

    /// Emitted when backup fails
    void backupFailed(const QString& error);

    /// Emitted when restore succeeds
    void restoreCompleted(const QString& filename);

    /// Emitted when restore fails
    void restoreFailed(const QString& error);

    /// Emitted with the result of checkFirstRunRestore()
    void firstRunRestoreResult(bool shouldOffer);

    /// Emitted when profiles were restored and the profile list needs refreshing
    void profilesRestored();

    /// Emitted when media files were restored and the catalog needs reloading
    void mediaRestored();

    /// Emitted when storage permission is needed (Android only)
    void storagePermissionNeeded();

    /// Emitted when the cached backup list changes
    void availableBackupsChanged();

private slots:
    void onTimerFired();

private:
    void scheduleNextCheck();
    bool shouldBackupNow() const;
    QString getBackupDirectory() const;
    void cleanOldBackups(const QString& backupDir);
    bool extractZip(const QString& zipPath, const QString& destDir) const;

    // Utility to copy a directory recursively
    static bool copyDirectory(const QString& srcDir, const QString& destDir, bool overwrite = false);

    Settings* m_settings;
    ShotHistoryStorage* m_storage;
    ProfileStorage* m_profileStorage;
    ScreensaverVideoManager* m_screensaverManager;
    QTimer* m_checkTimer;
    QDate m_lastBackupDate;  // Track when we last backed up
    bool m_backupInProgress = false;  // Prevent concurrent backups
    bool m_restoreInProgress = false;  // Prevent concurrent restores
    QStringList m_cachedBackups;       // Cached result of getAvailableBackups()
    QVector<QThread*> m_activeThreads; // Track background threads for cleanup

    // Shared flag: set to true in destructor so background-thread lambdas
    // that captured `this` can detect the object is gone before invoking
    // methods on it via QueuedConnection.
    std::shared_ptr<bool> m_destroyed = std::make_shared<bool>(false);
};
