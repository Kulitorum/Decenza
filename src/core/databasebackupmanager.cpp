#include "core/diagnosticlogging.h"
#include "databasebackupmanager.h"
#include "appsettings.h"
#include "settings.h"
#include "translationmanager.h"
#include "settings_app.h"
#include "settingsserializer.h"
#include "profilestorage.h"
#include "../history/shothistorystorage.h"
#include "../ai/aiconversation.h"
#include "../profile/profile.h"
#include "../profile/profilesavehelper.h"
#include "../screensaver/screensavervideomanager.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QHash>
#include <QUuid>
#include <QRegularExpression>
#include <QSettings>
#include <QDebug>
#include <QThread>
#include <QTemporaryDir>
#include <QPointer>
#include <functional>
#include "ziparchive.h"

namespace {
struct PreparedBackupMedia {
    QString path;
    QString originalName;
    QDateTime date;
    int durationSeconds = -1;
};
}

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

DatabaseBackupManager::DatabaseBackupManager(Settings* settings, ShotHistoryStorage* storage,
                                             ProfileStorage* profileStorage,
                                             ScreensaverVideoManager* screensaverManager,
                                             QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_storage(storage)
    , m_profileStorage(profileStorage)
    , m_screensaverManager(screensaverManager)
    , m_checkTimer(new QTimer(this))
{
    connect(m_checkTimer, &QTimer::timeout, this, &DatabaseBackupManager::onTimerFired);
}

QString DatabaseBackupManager::tr_(const char* key, const char* fallback) const {
    return translateOrFallback(m_translationManager, key, fallback);
}

QString DatabaseBackupManager::joinErrors(const QVector<QPair<QString, QString>>& errs) const
{
    QStringList out;
    out.reserve(errs.size());
    for (const auto& e : errs) {
        // An EMPTY key means the second element is already-final text, not a
        // fallback to be translated — used for machine-generated detail (which
        // counts disagreed, which query failed) that has no fixed wording to
        // translate.
        //
        // TranslationManager::translateString already returns the fallback
        // unchanged for an empty key, so this branch does not change today's
        // output. It is here so the pass-through is guaranteed by this file
        // rather than inherited from a lookup path that has no reason to keep
        // treating an empty key as a special case.
        if (e.first.isEmpty())
            out << e.second;
        else
            out << tr_(e.first.toUtf8().constData(), e.second.toUtf8().constData());
    }
    return out.join(QStringLiteral("; "));
}

DatabaseBackupManager::~DatabaseBackupManager()
{
    m_checkTimer->stop();
    *m_destroyed = true;
    // Wait indefinitely for background threads — they captured `this` in
    // their lambdas, so we must not destruct until they finish.
    for (QThread* thread : m_activeThreads) {
        if (thread && thread->isRunning()) {
            thread->wait();
        }
    }
}

void DatabaseBackupManager::setAiConversationNote(const QString& note)
{
    if (m_aiConversationNote == note) return;
    m_aiConversationNote = note;
    emit aiConversationNoteChanged();
}

void DatabaseBackupManager::start()
{
    if (!m_settings || !m_storage) {
        DIAG_WARN(STORAGE, "DatabaseBackupManager") << "Cannot start - missing settings or storage";
        return;
    }

    // Populate the cached backup list so QML can bind immediately
    refreshBackupList();

    // Check immediately on startup (in case we missed a backup)
    onTimerFired();

    // Then check every hour
    scheduleNextCheck();
}

void DatabaseBackupManager::refreshBackupList()
{
    QStringList newList = getAvailableBackups();
    if (newList != m_cachedBackups) {
        m_cachedBackups = newList;
        emit availableBackupsChanged();
    }
}

void DatabaseBackupManager::stop()
{
    if (m_checkTimer->isActive()) {
        m_checkTimer->stop();
        DIAG_DEBUG(STORAGE, "DatabaseBackupManager") << "Stopped";
    }
}

void DatabaseBackupManager::scheduleNextCheck()
{
    // Check every 60 minutes
    m_checkTimer->start(3600000);  // 60 * 60 * 1000 ms
}

bool DatabaseBackupManager::shouldBackupNow() const
{
    int backupHour = m_settings->app()->dailyBackupHour();

    // Backups disabled
    if (backupHour < 0) {
        return false;
    }

    QDateTime now = QDateTime::currentDateTime();
    QDate today = now.date();
    int currentHour = now.time().hour();

    // Already backed up today
    if (m_lastBackupDate == today) {
        return false;
    }

    // Current time is past the backup hour
    if (currentHour >= backupHour) {
        return true;
    }

    return false;
}

QString DatabaseBackupManager::getBackupDirectory() const
{
#ifdef DECENZA_TESTING
    if (!m_backupDirectoryForTests.isEmpty())
        return m_backupDirectoryForTests;
#endif
    QString backupDir;

#ifdef Q_OS_ANDROID
    // On Android, use the Java StorageHelper to get the proper Documents directory path
    // This handles different devices and Android versions correctly
    QJniObject javaPath = QJniObject::callStaticObjectMethod(
        "io/github/kulitorum/decenza_de1/StorageHelper",
        "getBackupsPath",
        "()Ljava/lang/String;");

    if (javaPath.isValid()) {
        backupDir = javaPath.toString();
    } else {
        DIAG_WARN(STORAGE, "DatabaseBackupManager") << "Failed to get backup path from Java";
        return QString();
    }
#elif defined(Q_OS_IOS)
    // On iOS, use the app's Documents directory which is visible in Files app
    // This is automatically accessible via Files app under "On My iPhone/iPad"
    backupDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Decenza Backups";
#else
    // Desktop platforms (Windows, macOS, Linux) - use user's Documents folder
    backupDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Decenza Backups";
#endif

    if (backupDir.isEmpty()) {
        DIAG_WARN(STORAGE, "DatabaseBackupManager") << "Could not determine backup directory";
        return QString();
    }

    // Ensure directory exists
    QDir dir(backupDir);
    if (!dir.exists()) {
#ifdef Q_OS_ANDROID
        // On Android, Java's mkdirs() should have created it — missing means permission issue
        DIAG_WARN(STORAGE, "DatabaseBackupManager") << "Backup directory does not exist:" << backupDir;
        DIAG_WARN(STORAGE, "DatabaseBackupManager") << "This may be due to missing storage permissions";
        return QString();
#else
        // On desktop/iOS, create the directory
        if (!dir.mkpath(".")) {
            DIAG_WARN(STORAGE, "DatabaseBackupManager") << "Failed to create backup directory:" << backupDir;
            return QString();
        }
        DIAG_DEBUG(STORAGE, "DatabaseBackupManager") << "Created backup directory:" << backupDir;
#endif
    }

    return backupDir;
}

QString DatabaseBackupManager::backupPathForFilename(const QString& filename) const
{
    static const QRegularExpression pattern(QStringLiteral("^shots_backup_([0-9]{8})\\.zip$"));
    const auto match = pattern.match(filename);
    if (!match.hasMatch() || !QDate::fromString(match.captured(1), QStringLiteral("yyyyMMdd")).isValid()
        || QFileInfo(filename).fileName() != filename)
        return {};

    const QString suffix = QLatin1Char('|') + filename;
    bool cached = false;
    for (const QString& row : m_cachedBackups) {
        if (row.endsWith(suffix)) {
            cached = true;
            break;
        }
    }
    if (!cached) return {};

    const QString backupDir = getBackupDirectory();
    return backupDir.isEmpty() ? QString() : QDir(backupDir).filePath(filename);
}

void DatabaseBackupManager::cleanOldBackups(const QString& backupDir)
{
    QDir dir(backupDir);
    if (!dir.exists()) {
        return;
    }

    QDate cutoffDate = QDate::currentDate().addDays(-5);
    QStringList filters;
    filters << "shots_backup_*.db" << "shots_backup_*.zip";

    QFileInfoList backups = dir.entryInfoList(filters, QDir::Files, QDir::Time);

    for (const QFileInfo& fileInfo : backups) {
        // Extract date from filename: shots_backup_YYYYMMDD.{db,zip,txt}
        QString fileName = fileInfo.fileName();
        if (fileName.length() < 21) {
            continue;  // Invalid filename format
        }

        QString dateStr = fileName.mid(13, 8);  // Extract YYYYMMDD
        QDate backupDate = QDate::fromString(dateStr, "yyyyMMdd");

        if (backupDate.isValid() && backupDate < cutoffDate) {
            if (QFile::remove(fileInfo.absoluteFilePath())) {
                DIAG_DEBUG(STORAGE, "DatabaseBackupManager") << "Removed old backup" << fileName;
            } else {
                DIAG_WARN(STORAGE, "DatabaseBackupManager") << "Failed to remove old backup" << fileName;
            }
        }
    }
}

void DatabaseBackupManager::onTimerFired()
{
    if (shouldBackupNow()) {
        createBackup();
    }
}

bool DatabaseBackupManager::extractZip(const QString& zipPath, const QString& destDir) const
{
    return ZipArchive::extract(zipPath, destDir);
}

bool DatabaseBackupManager::createBackup(bool force)
{
    // Prevent concurrent backups or backup during restore
    if (m_backupInProgress) {
        DIAG_WARN(STORAGE, "DatabaseBackupManager") << "Backup already in progress";
        return false;
    }
    if (m_restoreInProgress) {
        DIAG_WARN(STORAGE, "DatabaseBackupManager") << "Cannot backup while restore is in progress";
        return false;
    }

    if (!m_storage) {
        QString error = tr_("backup.error.storageUnavailable", "Storage not available");
        DIAG_WARN(STORAGE, "DatabaseBackupManager") << error;
        emit backupFailed(error);
        return false;
    }

    m_backupInProgress = true;

#ifdef Q_OS_ANDROID
    // Check storage permissions on Android (must be on main thread)
    if (!hasStoragePermission()) {
        QString error = tr_("backup.error.storagePermission", "Storage permission not granted. Please enable storage access in Settings.");
        DIAG_WARN(STORAGE, "DatabaseBackupManager") << error;
        m_backupInProgress = false;
        emit backupFailed(error);
        emit storagePermissionNeeded();
        return false;
    }
#endif

    QString backupDir = getBackupDirectory();
    if (backupDir.isEmpty()) {
        QString error = tr_("backup.error.accessDirFailed", "Failed to access backup directory");
        DIAG_WARN(STORAGE, "DatabaseBackupManager") << error;
        m_backupInProgress = false;
        emit backupFailed(error);
        return false;
    }

    // Generate backup filename with date
    QString dateStr = QDate::currentDate().toString("yyyyMMdd");
    QString zipPath = backupDir + "/shots_backup_" + dateStr + ".zip";

    // Check if backup already exists for today
    QFileInfo existingZip(zipPath);
    if (!force && existingZip.exists() && existingZip.size() > 0) {
        // Automatic backup - skip if valid backup exists
        DIAG_DEBUG(STORAGE, "DatabaseBackupManager") << "Using existing valid backup for today:" << zipPath;
        m_lastBackupDate = QDate::currentDate();

#ifdef Q_OS_ANDROID
        // Notify media scanner in case it wasn't scanned before
        QJniObject::callStaticMethod<void>(
            "io/github/kulitorum/decenza_de1/StorageHelper",
            "scanFile",
            "(Ljava/lang/String;)V",
            QJniObject::fromString(zipPath).object<jstring>());
#endif

        m_backupInProgress = false;
        emit backupCreated(zipPath);
        refreshBackupList();
        return true;
    } else if (existingZip.exists()) {
        // File exists - delete it to create fresh backup
        if (!QFile::remove(zipPath)) {
            QString error = tr_("backup.error.removeExistingFailed", "Failed to remove existing backup: %1").arg(zipPath);
            DIAG_WARN(STORAGE, "DatabaseBackupManager") << error;
            m_backupInProgress = false;
            emit backupFailed(error);
            return false;
        }
        DIAG_DEBUG(STORAGE, "DatabaseBackupManager") << "Removed existing backup to create fresh one:" << zipPath;
    }

    // Snapshot settings and AI conversations on main thread (reads QSettings which
    // may use platform APIs that aren't thread-safe)
    QJsonObject settingsJson;
    if (m_settings) {
        settingsJson = SettingsSerializer::exportToJson(m_settings, /*includeSensitive=*/false);

        // Add AI conversations. Serialized by the importer's own producer, so
        // the archive cannot carry a different set of fields than a restore
        // reads back — see AIConversation::exportConversationsStatic.
        AppSettings qsettings;
        const QJsonArray conversations = AIConversation::exportConversationsStatic(qsettings);
        if (!conversations.isEmpty())
            settingsJson["ai_conversations"] = conversations;
    }

    // Collect paths on main thread (some accessors may use JNI/platform APIs)
    QString userProfilesPath = m_profileStorage ? m_profileStorage->userProfilesPath() : QString();
    QString downloadedProfilesPath = m_profileStorage ? m_profileStorage->downloadedProfilesPath() : QString();
    QString personalMediaDir = m_screensaverManager ? m_screensaverManager->personalMediaDirectory() : QString();
    QString dbPath = m_storage->databasePath();
    QString tempLocation = QStandardPaths::writableLocation(QStandardPaths::TempLocation);

    // Run heavy I/O on background thread
    QThread* thread = QThread::create(
        [this, dateStr, zipPath, backupDir, settingsJson, dbPath,
         userProfilesPath, downloadedProfilesPath, personalMediaDir, tempLocation,
         destroyed = m_destroyed]() {

        // Create staging directory
        QString stagingDir = tempLocation + "/decenza_backup_staging";
        QDir(stagingDir).removeRecursively();
        QDir().mkpath(stagingDir);

        // Create the backup .db file (uses separate connection, safe on background thread)
        QString dbDestPath = stagingDir + "/shots_backup_" + dateStr + ".db";
        QString dbResult = ShotHistoryStorage::createBackupStatic(dbPath, dbDestPath);

        if (dbResult.isEmpty()) {
            QDir(stagingDir).removeRecursively();
            QMetaObject::invokeMethod(this, [this, destroyed]() {
                if (*destroyed) return;
                m_backupInProgress = false;
                emit backupFailed(tr_("backup.error.createFailed", "Failed to create backup"));
            }, Qt::QueuedConnection);
            return;
        }

        QFileInfo dbInfo(dbResult);
        if (!dbInfo.exists()) {
            QDir(stagingDir).removeRecursively();
            QMetaObject::invokeMethod(this, [this, destroyed]() {
                if (*destroyed) return;
                m_backupInProgress = false;
                DIAG_WARN(STORAGE, "DatabaseBackupManager") << "Backup database file missing after creation";
                emit backupFailed(tr_("backup.error.createFileFailed", "Failed to create backup file"));
            }, Qt::QueuedConnection);
            return;
        }
        DIAG_DEBUG(STORAGE, "DatabaseBackupManager") << "DB file created:" << dbResult
                 << "size:" << dbInfo.size() << "bytes";

        // Write settings.json (from pre-captured snapshot)
        if (!settingsJson.isEmpty()) {
            QString settingsPath = stagingDir + "/settings.json";
            QFile file(settingsPath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(QJsonDocument(settingsJson).toJson(QJsonDocument::Indented));
                file.close();
                DIAG_DEBUG(STORAGE, "DatabaseBackupManager") << "Settings exported to" << settingsPath;
            } else {
                DIAG_WARN(STORAGE, "DatabaseBackupManager") << "Failed to write settings.json:" << file.errorString();
            }
        }

        // Copy profiles
        if (!userProfilesPath.isEmpty() && QDir(userProfilesPath).exists()) {
            if (copyDirectory(userProfilesPath, stagingDir + "/profiles/user")) {
                DIAG_DEBUG(STORAGE, "DatabaseBackupManager") << "User profiles backed up";
            }
        }
        if (!downloadedProfilesPath.isEmpty() && QDir(downloadedProfilesPath).exists()) {
            if (copyDirectory(downloadedProfilesPath, stagingDir + "/profiles/downloaded")) {
                DIAG_DEBUG(STORAGE, "DatabaseBackupManager") << "Downloaded profiles backed up";
            }
        }

        // Copy personal media
        if (!personalMediaDir.isEmpty() && QDir(personalMediaDir).exists()) {
            QDir mediaDir(personalMediaDir);
            QFileInfoList files = mediaDir.entryInfoList(QDir::Files);
            bool hasMedia = false;
            for (const QFileInfo& fi : files) {
                if (fi.fileName() != "catalog.json") { hasMedia = true; break; }
            }
            if (hasMedia) {
                if (copyDirectory(personalMediaDir, stagingDir + "/media")) {
                    DIAG_DEBUG(STORAGE, "DatabaseBackupManager") << "Personal media backed up";
                }
            }
        }

        // Stream the staged snapshot into the archive; never buffer a whole file.
        QuaZip writer(zipPath);
        bool zipSuccess = writer.open(QuaZip::mdCreate);
        if (zipSuccess) {
            QDirIterator files(stagingDir, QDir::Files, QDirIterator::Subdirectories);
            while (zipSuccess && files.hasNext()) {
                const QString path = files.next();
                QFile input(path);
                zipSuccess = input.open(QIODevice::ReadOnly)
                    && ZipArchive::addFile(writer, QDir(stagingDir).relativeFilePath(path), input);
            }
            writer.close();
            zipSuccess = zipSuccess && writer.getZipError() == UNZ_OK;
        }

        QDir(stagingDir).removeRecursively();

        if (!zipSuccess) {
            QFile::remove(zipPath);
            DIAG_WARN(STORAGE, "DatabaseBackupManager") << "Failed to create ZIP";
            QMetaObject::invokeMethod(this, [this, destroyed]() {
                if (*destroyed) return;
                m_backupInProgress = false;
                emit backupFailed(tr_("backup.error.createArchiveFailed", "Failed to create backup archive"));
            }, Qt::QueuedConnection);
            return;
        } else {
            QFileInfo zi(zipPath);
            DIAG_DEBUG(STORAGE, "DatabaseBackupManager") << "ZIP size:" << zi.size() << "bytes";
        }

        // Deliver result back to main thread
        QMetaObject::invokeMethod(this, [this, zipPath, backupDir, destroyed]() {
            if (*destroyed) return;
            m_lastBackupDate = QDate::currentDate();

#ifdef Q_OS_ANDROID
            QJniObject::callStaticMethod<void>(
                "io/github/kulitorum/decenza_de1/StorageHelper",
                "scanFile",
                "(Ljava/lang/String;)V",
                QJniObject::fromString(zipPath).object<jstring>());
            DIAG_DEBUG(STORAGE, "DatabaseBackupManager") << "Triggered media scan for ZIP";
#endif

            m_backupInProgress = false;
            DIAG_INFO(STORAGE, "DatabaseBackupManager") << "Backup created:" << zipPath;
            emit backupCreated(zipPath);
            refreshBackupList();
            cleanOldBackups(backupDir);
        }, Qt::QueuedConnection);
    });

    m_activeThreads.append(thread);
    connect(thread, &QThread::finished, this, [this, thread]() {
        m_activeThreads.removeOne(thread);
        // Safety net: if the thread ended without resetting the flag
        // (e.g., crash or unhandled exception), reset it here
        if (m_backupInProgress) {
            DIAG_WARN(STORAGE, "DatabaseBackupManager") << "Backup thread ended without result signal - resetting state";
            m_backupInProgress = false;
            emit backupFailed(tr_("backup.error.unexpected", "Backup failed unexpectedly"));
        }
        thread->deleteLater();
    });
    thread->start();
    return true;
}

void DatabaseBackupManager::checkFirstRunRestore()
{
    if (!m_storage) {
        emit firstRunRestoreResult(false);
        return;
    }

#ifdef Q_OS_ANDROID
    // On Android, check storage permission first (no DB involved)
    if (!hasStoragePermission()) {
        emit firstRunRestoreResult(false);
        return;
    }
#endif

    // Run DB query on background thread to avoid blocking the main thread
    QString dbPath = m_storage->databasePath();
    QThread* thread = QThread::create([this, dbPath, destroyed = m_destroyed]() {
        int shotCount = ShotHistoryStorage::getShotCountStatic(dbPath);

        QMetaObject::invokeMethod(this, [this, shotCount, destroyed]() {
            if (*destroyed) return;
            // If DB couldn't be opened (shotCount == -1), don't offer restore —
            // the database may actually have data but be temporarily locked
            if (shotCount != 0) {
                emit firstRunRestoreResult(false);
                return;
            }

            // Check if backups exist (filesystem only, safe on main thread)
            QStringList backups = getAvailableBackups();
            if (!backups.isEmpty()) {
                // Refresh the cached list so QML sees backups immediately
                // (the initial refreshBackupList() at startup ran before
                // storage permission was granted, so the cache is empty)
                refreshBackupList();
            }
            emit firstRunRestoreResult(!backups.isEmpty());
        }, Qt::QueuedConnection);
    });

    m_activeThreads.append(thread);
    connect(thread, &QThread::finished, this, [this, thread]() {
        m_activeThreads.removeOne(thread);
        thread->deleteLater();
    });
    thread->start();
}

bool DatabaseBackupManager::hasStoragePermission() const
{
#ifdef Q_OS_ANDROID
    return QJniObject::callStaticMethod<jboolean>(
        "io/github/kulitorum/decenza_de1/StorageHelper",
        "hasStoragePermission",
        "()Z");
#else
    return true; // Desktop/iOS always have access to Documents
#endif
}

void DatabaseBackupManager::requestStoragePermission()
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(
        "io/github/kulitorum/decenza_de1/StorageHelper",
        "requestStoragePermission",
        "()V");
    emit storagePermissionNeeded();
#endif
}

QStringList DatabaseBackupManager::getAvailableBackups() const
{
    QString backupDir = getBackupDirectory();
    if (backupDir.isEmpty()) {
        return QStringList();
    }

    QDir dir(backupDir);
    if (!dir.exists()) {
        return QStringList();
    }

    // Get all backup files, sorted newest first (QDir::Time default)
    QStringList filters;
    filters << "shots_backup_*.zip" << "shots_backup_*.db";
    QFileInfoList backups = dir.entryInfoList(filters, QDir::Files, QDir::Time);

    QStringList result;
    for (const QFileInfo& fileInfo : backups) {
        // Extract date from filename for display
        QString fileName = fileInfo.fileName();
        if (fileName.length() >= 21) {
            QString dateStr = fileName.mid(13, 8);  // Extract YYYYMMDD
            QDate backupDate = QDate::fromString(dateStr, "yyyyMMdd");
            if (backupDate.isValid()) {
                QString displayName = backupDate.toString("yyyy-MM-dd") +
                                     QString(" (%1 MB)").arg(fileInfo.size() / 1024.0 / 1024.0, 0, 'f', 2);
                result.append(displayName + "|" + fileName);  // displayName|actualFilename
            }
        }
    }

    return result;
}

bool DatabaseBackupManager::restoreBackup(const QString& filename, bool merge,
                                          bool restoreShots, bool restoreSettings,
                                          bool restoreProfiles, bool restoreMedia)
{
    QString backupDir = getBackupDirectory();
    if (backupDir.isEmpty()) {
        QString error = tr_("backup.error.accessDirFailed", "Failed to access backup directory");
        DIAG_WARN(STORAGE, "DatabaseBackupManager") << error;
        emit restoreFailed(error);
        return false;
    }
    static const QRegularExpression pattern(
        QStringLiteral("^shots_backup_([0-9]{8})\\.(zip|db)$"));
    const auto match = pattern.match(filename);
    if (!match.hasMatch() || !QDate::fromString(match.captured(1), QStringLiteral("yyyyMMdd")).isValid()
        || QFileInfo(filename).fileName() != filename) {
        emit restoreFailed(tr_("backup.error.fileNotFound", "Backup file not found: %1").arg(filename));
        return false;
    }
    bool cached = false;
    const QString suffix = QLatin1Char('|') + filename;
    for (const QString& row : m_cachedBackups) {
        if (row.endsWith(suffix)) { cached = true; break; }
    }
    if (!cached) {
        emit restoreFailed(tr_("backup.error.fileNotFound", "Backup file not found: %1").arg(filename));
        return false;
    }
    return restoreBackupFile(QDir(backupDir).filePath(filename), merge, restoreShots,
                             restoreSettings, restoreProfiles, restoreMedia);
}

bool DatabaseBackupManager::restoreBackupFile(const QString& path, bool merge,
                                              bool restoreShots, bool restoreSettings,
                                              bool restoreProfiles, bool restoreMedia,
                                              std::shared_ptr<QTemporaryDir> inputLease)
{
    if (m_restoreInProgress) {
        DIAG_WARN(STORAGE, "DatabaseBackupManager") << "Restore already in progress";
        return false;
    }
    if (m_backupInProgress) {
        DIAG_WARN(STORAGE, "DatabaseBackupManager") << "Cannot restore while backup is in progress";
        return false;
    }
    if (!m_storage) {
        const QString error = tr_("backup.error.storageUnavailable", "Storage not available");
        DIAG_WARN(STORAGE, "DatabaseBackupManager") << error;
        emit restoreFailed(error);
        return false;
    }
    const QFileInfo zipInfo(path);
    if (!zipInfo.isFile() || zipInfo.isSymLink()
        || (zipInfo.suffix().compare(QLatin1String("zip"), Qt::CaseInsensitive) != 0
            && zipInfo.suffix().compare(QLatin1String("db"), Qt::CaseInsensitive) != 0)) {
        const QString error = tr_("backup.error.fileNotFound", "Backup file not found: %1")
            .arg(zipInfo.fileName());
        DIAG_WARN(STORAGE, "DatabaseBackupManager") << error;
        emit restoreFailed(error);
        return false;
    }

    m_restoreInProgress = true;
    const QString filename = zipInfo.fileName();
    const QString zipPath = path;
    if (zipInfo.suffix().compare(QLatin1String("db"), Qt::CaseInsensitive) == 0) {
        // A raw shot database contains no other backup categories or sibling files.
        restoreSettings = restoreProfiles = restoreMedia = false;
    }

    // Collect restore target paths on main thread (some accessors may use JNI/platform APIs)
    QString userProfilesPath = m_profileStorage ? m_profileStorage->userProfilesPath() : QString();
    QString downloadedProfilesPath = m_profileStorage ? m_profileStorage->downloadedProfilesPath() : QString();
    QString personalMediaDir = m_screensaverManager ? m_screensaverManager->personalMediaDirectory() : QString();
    QString dbPath = m_storage->databasePath();
    QString tempLocation = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const bool hasScreensaverManager = m_screensaverManager != nullptr;
    QPointer<ScreensaverVideoManager> screensaverGuard(m_screensaverManager);
    QPointer<DatabaseBackupManager> managerGuard(this);
    QCoreApplication* callbackContext = QCoreApplication::instance();

    // Run heavy I/O on background thread. The optional lease is retained even
    // when the requester drops its transfer owner during the restore.
    QThread* thread = QThread::create(
        [this, filename, zipPath, merge, restoreShots, restoreSettings, restoreProfiles, restoreMedia,
         userProfilesPath, downloadedProfilesPath, personalMediaDir, dbPath, tempLocation,
         hasScreensaverManager, screensaverGuard, managerGuard, callbackContext,
         inputLease, destroyed = m_destroyed]() {
        Q_UNUSED(inputLease)
        // Create temp directory for extraction
        QString tempDir = tempLocation + "/decenza_restore_temp";
        QDir(tempDir).removeRecursively();
        QDir().mkpath(tempDir);

        // (translation key, English fallback) pairs — accumulated on this worker
        // thread: translated on main thread by joinErrors().
        QVector<QPair<QString, QString>> errors;
        bool shotsImported = false;
        ShotHistoryStorage::ImportResult shotImport;
        bool isRawDb = filename.endsWith(".db", Qt::CaseInsensitive);

        if (isRawDb) {
            if (!restoreShots) {
                QDir(tempDir).removeRecursively();
                QMetaObject::invokeMethod(callbackContext, [managerGuard, filename, destroyed]() {
                    if (!managerGuard || *destroyed) return;
                    managerGuard->m_restoreInProgress = false;
                    emit managerGuard->restoreCompleted(filename);
                }, Qt::QueuedConnection);
                return;
            }
        } else {
            DIAG_DEBUG(STORAGE, "DatabaseBackupManager") << "Extracting" << zipPath << "to" << tempDir;
            if (!extractZip(zipPath, tempDir)) {
                QDir(tempDir).removeRecursively();
                QMetaObject::invokeMethod(callbackContext, [managerGuard, destroyed]() {
                    if (!managerGuard || *destroyed) return;
                    managerGuard->m_restoreInProgress = false;
                    emit managerGuard->restoreFailed(managerGuard->tr_(
                        "backup.error.extractFailed", "Failed to extract backup file"));
                }, Qt::QueuedConnection);
                return;
            }
        }

        if (restoreShots) {
            QString tempDbPath;
            if (isRawDb) {
                tempDbPath = zipPath;
                DIAG_DEBUG(STORAGE, "DatabaseBackupManager") << "Using raw .db backup:" << tempDbPath;
            } else {
                QDir tempDirObj(tempDir);
                QStringList dbFiles = tempDirObj.entryList({"*.db"}, QDir::Files, QDir::Time);
                if (!dbFiles.isEmpty()) {
                    tempDbPath = tempDir + "/" + dbFiles.first();
                    DIAG_DEBUG(STORAGE, "DatabaseBackupManager") << "Found DB file:" << tempDbPath;
                } else {
                    DIAG_DEBUG(STORAGE, "DatabaseBackupManager") << "No DB file in backup, skipping shot restore";
                }
            }
            if (!tempDbPath.isEmpty()) {
                bool dbValid = true;
                QFileInfo extractedFileInfo(tempDbPath);
                if (!extractedFileInfo.exists() || extractedFileInfo.size() < 100) {
                    QPair<QString, QString> error = !extractedFileInfo.exists()
                        ? qMakePair(QStringLiteral("backup.error.extractedNotFound"),
                                    QStringLiteral("Extracted file not found"))
                        : qMakePair(QStringLiteral("backup.error.extractedTooSmall"),
                                    QStringLiteral("Extracted file is too small to be a valid database"));
                    DIAG_WARN(STORAGE, "DatabaseBackupManager") << error.second;
                    errors << error;
                    dbValid = false;
                }
                if (dbValid) {
                    QFile dbFile(tempDbPath);
                    if (dbFile.open(QIODevice::ReadOnly)) {
                        const QByteArray header = dbFile.read(16);
                        dbFile.close();
                        if (header.size() != 16 || !header.startsWith("SQLite format 3")) {
                            errors << qMakePair(QStringLiteral("backup.error.notSqlite"),
                                                QStringLiteral("Extracted file is not a valid SQLite database"));
                            dbValid = false;
                        }
                    } else {
                        errors << qMakePair(QStringLiteral("backup.error.cannotOpenExtracted"),
                                            QStringLiteral("Cannot open extracted file for validation"));
                        dbValid = false;
                    }
                }
                if (dbValid) {
                    const bool importSuccess = ShotHistoryStorage::importDatabaseStatic(
                        dbPath, tempDbPath, merge, &shotImport);
                    if (!importSuccess) {
                        if (!shotImport.integrityFailure.isEmpty()) {
                            errors << qMakePair(QStringLiteral("backup.error.importShotsRefused"),
                                                QStringLiteral("Shot history was not restored"));
                            errors << qMakePair(QString(), shotImport.integrityFailure);
                        } else {
                            errors << qMakePair(QStringLiteral("backup.error.importShotsFailed"),
                                                QStringLiteral("Failed to import shot history"));
                        }
                    } else {
                        shotsImported = true;
                    }
                }
            }
        }

        if (restoreShots && !merge && !shotsImported) {
            if (errors.isEmpty())
                errors << qMakePair(QStringLiteral("backup.error.missingShotDatabase"),
                                    QStringLiteral("Backup has no shot database to replace the current history"));
            QDir(tempDir).removeRecursively();
            QMetaObject::invokeMethod(callbackContext, [managerGuard, errors, destroyed]() {
                if (!managerGuard || *destroyed) return;
                managerGuard->m_restoreInProgress = false;
                emit managerGuard->restoreFailed(managerGuard->joinErrors(errors));
            }, Qt::QueuedConnection);
            return;
        }

        QString restoreDir = isRawDb ? QFileInfo(zipPath).absolutePath() : tempDir;
        bool profilesRestored = false;
        if (restoreProfiles) {
            if (!merge) {
                auto clearProfileDir = [](const QString& dirPath) {
                    QDir dir(dirPath);
                    if (!dir.exists()) return;
                    const QFileInfoList files = dir.entryInfoList(QDir::Files);
                    for (const QFileInfo& file : files) QFile::remove(file.absoluteFilePath());
                };
                if (!userProfilesPath.isEmpty()) clearProfileDir(userProfilesPath);
                if (!downloadedProfilesPath.isEmpty()) clearProfileDir(downloadedProfilesPath);
            }
            auto restoreProfileDir = [merge](const QString& srcPath, const QString& destPath) -> int {
                if (!QDir(srcPath).exists() || destPath.isEmpty()) return 0;
                QDir().mkpath(destPath);
                const QFileInfoList files = QDir(srcPath).entryInfoList(QDir::Files);
                int restored = 0;
                for (const QFileInfo& source : files) {
                    if (!source.fileName().endsWith(QLatin1String(".json"))) continue;
                    QString dest = destPath + "/" + source.fileName();
                    const QString builtinPath = QStringLiteral(":/profiles/") + source.fileName();
                    if (QFile::exists(builtinPath)) {
                        const Profile incoming = Profile::loadFromFile(source.absoluteFilePath());
                        const Profile builtIn = Profile::loadFromFile(builtinPath);
                        if (incoming.isValid() && builtIn.isValid()
                            && ProfileSaveHelper::compareProfiles(incoming, builtIn)) continue;
                    }
                    if (QFile::exists(dest)) {
                        if (merge) {
                            const Profile incoming = Profile::loadFromFile(source.absoluteFilePath());
                            const Profile existing = Profile::loadFromFile(dest);
                            if (incoming.isValid() && existing.isValid()
                                && ProfileSaveHelper::compareProfiles(incoming, existing)) continue;
                            const QString baseName = source.completeBaseName();
                            int counter = 1;
                            do {
                                dest = destPath + "/" + baseName + "_imported"
                                    + (counter > 1 ? QString::number(counter) : "") + ".json";
                                ++counter;
                            } while (QFile::exists(dest));
                        } else {
                            QFile::remove(dest);
                        }
                    }
                    if (QFile::copy(source.absoluteFilePath(), dest)) ++restored;
                }
                return restored;
            };
            const int userRestored = restoreProfileDir(restoreDir + "/profiles/user", userProfilesPath);
            const int downloadedRestored = restoreProfileDir(restoreDir + "/profiles/downloaded", downloadedProfilesPath);
            profilesRestored = userRestored + downloadedRestored > 0 || !merge;
        }

        bool mediaWasRestored = false;
        bool mediaStageReady = false;
        QList<PreparedBackupMedia> preparedMedia;
        std::shared_ptr<QTemporaryDir> mediaStage;
        if (restoreMedia) {
            const QString srcMedia = restoreDir + "/media";
            if (!personalMediaDir.isEmpty() && hasScreensaverManager) {
                QDir().mkpath(personalMediaDir);
                mediaStage = std::make_shared<QTemporaryDir>(
                    QDir(personalMediaDir).filePath(QStringLiteral(".restore-XXXXXX")));
                mediaStageReady = mediaStage->isValid();
                if (!mediaStageReady) {
                    errors << qMakePair(QStringLiteral("backup.error.mediaStageFailed"),
                                        QStringLiteral("Failed to stage restored media"));
                } else if (QDir(srcMedia).exists()) {
                    QHash<QString, QJsonObject> catalogByPath;
                    QFile catalogFile(QDir(srcMedia).filePath(QStringLiteral("catalog.json")));
                    if (catalogFile.open(QIODevice::ReadOnly)) {
                        const QJsonDocument catalog = QJsonDocument::fromJson(catalogFile.readAll());
                        if (catalog.isArray()) {
                            for (const QJsonValue& value : catalog.array()) {
                                const QJsonObject item = value.toObject();
                                const QString itemPath = item.value(QStringLiteral("path")).toString();
                                if (!itemPath.isEmpty())
                                    catalogByPath.insert(QFileInfo(itemPath).fileName(), item);
                            }
                        }
                    }
                    static const QSet<QString> supportedSuffixes = {
                        QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("mov"),
                        QStringLiteral("avi"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                        QStringLiteral("png"), QStringLiteral("gif"), QStringLiteral("webp")
                    };
                    const QFileInfoList files = QDir(srcMedia).entryInfoList(QDir::Files);
                    for (const QFileInfo& file : files) {
                        if (file.fileName() == QStringLiteral("catalog.json")
                            || !supportedSuffixes.contains(file.suffix().toLower())) continue;
                        const QJsonObject catalog = catalogByPath.value(file.fileName());
                        const QString originalName = catalog.value(QStringLiteral("original_name"))
                            .toString(file.fileName());
                        const QString stagedName = QUuid::createUuid().toString(QUuid::WithoutBraces)
                            + QLatin1Char('.') + file.suffix();
                        const QString stagedPath = mediaStage->filePath(stagedName);
                        if (!QFile::copy(file.absoluteFilePath(), stagedPath)) {
                            errors << qMakePair(QStringLiteral("backup.error.mediaStageFailed"),
                                                QStringLiteral("Failed to stage restored media"));
                            mediaStageReady = false;
                            preparedMedia.clear();
                            break;
                        }
                        PreparedBackupMedia item;
                        item.path = stagedPath;
                        item.originalName = originalName;
                        item.date = QDateTime::fromString(
                            catalog.value(QStringLiteral("date")).toString(), Qt::ISODate);
                        item.durationSeconds = catalog.value(QStringLiteral("duration_s")).toInt(-1);
                        preparedMedia.append(item);
                    }
                }
                mediaWasRestored = mediaStageReady && (!merge || !preparedMedia.isEmpty());
            }
        }

        QJsonObject settingsJson;
        if (restoreSettings) {
            const QString settingsPath = restoreDir + "/settings.json";
            if (QFile::exists(settingsPath)) {
                QFile file(settingsPath);
                if (!file.open(QIODevice::ReadOnly))
                    errors << qMakePair(QStringLiteral("backup.error.readSettingsFailed"),
                                        QStringLiteral("Failed to read settings from backup"));
                else {
                    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                    if (doc.isObject()) settingsJson = doc.object();
                    else errors << qMakePair(QStringLiteral("backup.error.settingsInvalidFormat"),
                                             QStringLiteral("settings.json has invalid format"));
                }
            }
        }
        QDir(tempDir).removeRecursively();
        QMetaObject::invokeMethod(callbackContext,
            [managerGuard, filename, settingsJson, shotsImported, profilesRestored, mediaWasRestored,
             errors, merge, shotImport, destroyed, mediaStageReady, preparedMedia,
             mediaStage, screensaverGuard, restoreMedia]() mutable {
                if (!managerGuard || *destroyed) return;
                Q_UNUSED(mediaStage)
                QPointer<DatabaseBackupManager> selfGuard = managerGuard;
                QVector<QPair<QString, QString>> restoreErrors = errors;
                if (shotsImported && managerGuard->m_storage)
                    managerGuard->m_storage->refreshTotalShots();
                if (!selfGuard) return;
                if (profilesRestored) emit managerGuard->profilesRestored();
                if (!selfGuard) return;
                if (restoreMedia && screensaverGuard && mediaStageReady) {
                    bool mediaUpdated = false;
                    if (!merge) {
                        screensaverGuard->clearPersonalMedia();
                        if (!selfGuard) return;
                        if (!screensaverGuard) return;
                        mediaUpdated = true;
                    }
                    for (const PreparedBackupMedia& item : preparedMedia) {
                        if (merge && screensaverGuard->hasPersonalMediaWithName(item.originalName)) continue;
                        if (!screensaverGuard->adoptPreparedPersonalMedia(
                                item.path, item.originalName, item.date, item.durationSeconds)) {
                            restoreErrors << qMakePair(QStringLiteral("backup.error.mediaAdoptFailed"),
                                                       QStringLiteral("Failed to add restored media"));
                        } else {
                            mediaUpdated = true;
                        }
                        if (!selfGuard || !screensaverGuard) return;
                    }
                    if (mediaUpdated) emit managerGuard->mediaRestored();
                } else if (mediaWasRestored) {
                    emit managerGuard->mediaRestored();
                }
                if (!selfGuard) return;
                if (!settingsJson.isEmpty() && managerGuard->m_settings) {
                    if (!SettingsSerializer::importFromJson(managerGuard->m_settings, settingsJson,
                                                              SettingsSerializer::sensitiveKeys()))
                        restoreErrors << qMakePair(QStringLiteral("backup.error.settingsImport"),
                                                   QStringLiteral("Some settings could not be restored"));
                }
                if (!selfGuard) return;
                QString conversationNote;
                if (settingsJson.contains(QStringLiteral("ai_conversations"))) {
                    const QJsonArray conversations = settingsJson.value(QStringLiteral("ai_conversations")).toArray();
                    if (!conversations.isEmpty()) {
                        AppSettings appSettings;
                        if (!merge) {
                            const QJsonDocument existing = QJsonDocument::fromJson(
                                appSettings.value(QStringLiteral("ai/conversations/index")).toByteArray());
                            if (existing.isArray()) {
                                for (const QJsonValue& value : existing.array()) {
                                    const QString key = value.toObject().value(QStringLiteral("key")).toString();
                                    if (!key.isEmpty()) appSettings.remove(QStringLiteral("ai/conversations/") + key);
                                }
                            }
                            appSettings.remove(QStringLiteral("ai/conversations/index"));
                        }
                        if (shotImport.refused())
                            conversationNote = AIConversation::importHeldBackNote(conversations.size(), managerGuard->m_translationManager);
                        else {
                            const AIConversation::ImportTally tally = AIConversation::importConversationsStatic(
                                appSettings, conversations,
                                shotsImported ? shotImport.idMapOrNull() : nullptr,
                                shotsImported ? shotImport.equipmentIdMapOrNull() : nullptr);
                            if (tally.conversationsImported > 0) appSettings.sync();
                            conversationNote = AIConversation::importRefusalNote(tally, managerGuard->m_translationManager);
                        }
                    }
                }
                if (!selfGuard) return;
                managerGuard->m_restoreInProgress = false;
                managerGuard->setAiConversationNote(conversationNote);
                if (!selfGuard) return;
                if (!restoreErrors.isEmpty()) emit managerGuard->restoreFailed(managerGuard->joinErrors(restoreErrors));
                else emit managerGuard->restoreCompleted(filename);
            }, Qt::QueuedConnection);
    });
    m_activeThreads.append(thread);
    connect(thread, &QThread::finished, this, [this, thread]() {
        m_activeThreads.removeOne(thread);
        if (m_restoreInProgress) {
            m_restoreInProgress = false;
            emit restoreFailed(tr_("backup.error.restoreUnexpected", "Restore failed unexpectedly"));
        }
        thread->deleteLater();
    });
    thread->start();
    return true;
}

// ============================================================================
// Utility
// ============================================================================

bool DatabaseBackupManager::copyDirectory(const QString& srcDir, const QString& destDir, bool overwrite)
{
    QDir src(srcDir);
    if (!src.exists()) return false;

    QDir().mkpath(destDir);

    bool success = true;
    QFileInfoList entries = src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : entries) {
        QString destPath = destDir + "/" + fi.fileName();
        if (fi.isDir()) {
            if (!copyDirectory(fi.absoluteFilePath(), destPath, overwrite)) {
                success = false;
            }
        } else {
            if (QFile::exists(destPath)) {
                if (!overwrite) continue;
                QFile::remove(destPath);
            }
            if (!QFile::copy(fi.absoluteFilePath(), destPath)) {
                DIAG_WARN(STORAGE, "DatabaseBackupManager") << "Failed to copy" << fi.absoluteFilePath() << "to" << destPath;
                success = false;
            }
        }
    }
    return success;
}
