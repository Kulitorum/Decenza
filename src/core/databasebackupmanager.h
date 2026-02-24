#pragma once

#include <QObject>
#include <QTimer>
#include <QDateTime>

class Settings;
class ShotHistoryStorage;

/**
 * @brief Manages automatic daily backups of the shot history database.
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
    explicit DatabaseBackupManager(Settings* settings, ShotHistoryStorage* storage, QObject* parent = nullptr);

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
    Q_INVOKABLE void refreshBackupList();

    /// Restore a backup by filename
    /// @param filename The backup filename (e.g., "shots_backup_20260210.zip")
    /// @param merge If true, merge with existing shots; if false, replace all
    Q_INVOKABLE bool restoreBackup(const QString& filename, bool merge = true);

    /// Check if we should offer first-run restore (empty database + backups exist)
    Q_INVOKABLE bool shouldOfferFirstRunRestore() const;

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

    Settings* m_settings;
    ShotHistoryStorage* m_storage;
    QTimer* m_checkTimer;
    QDate m_lastBackupDate;  // Track when we last backed up
    bool m_backupInProgress = false;  // Prevent concurrent backups
    bool m_restoreInProgress = false;  // Prevent concurrent restores
    QStringList m_cachedBackups;       // Cached result of getAvailableBackups()
};
