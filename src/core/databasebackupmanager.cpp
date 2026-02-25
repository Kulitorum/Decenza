#include "databasebackupmanager.h"
#include "settings.h"
#include "settingsserializer.h"
#include "profilestorage.h"
#include "../history/shothistorystorage.h"
#include "../screensaver/screensavervideomanager.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QSettings>
#include <QDebug>
#include <QThread>
#include <functional>
#include <private/qzipwriter_p.h>
#include <private/qzipreader_p.h>

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

void DatabaseBackupManager::start()
{
    if (!m_settings || !m_storage) {
        qWarning() << "DatabaseBackupManager: Cannot start - missing settings or storage";
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
        qDebug() << "DatabaseBackupManager: Stopped";
    }
}

void DatabaseBackupManager::scheduleNextCheck()
{
    // Check every 60 minutes
    m_checkTimer->start(3600000);  // 60 * 60 * 1000 ms
}

bool DatabaseBackupManager::shouldBackupNow() const
{
    int backupHour = m_settings->dailyBackupHour();

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
        qDebug() << "DatabaseBackupManager: Got backup path from Java:" << backupDir;
    } else {
        qWarning() << "DatabaseBackupManager: Failed to get backup path from Java";
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
        qWarning() << "DatabaseBackupManager: Could not determine backup directory";
        return QString();
    }

    // Ensure directory exists
    QDir dir(backupDir);
    if (!dir.exists()) {
#ifdef Q_OS_ANDROID
        // On Android, Java's mkdirs() should have created it — missing means permission issue
        qWarning() << "DatabaseBackupManager: Backup directory does not exist:" << backupDir;
        qWarning() << "DatabaseBackupManager: This may be due to missing storage permissions";
        return QString();
#else
        // On desktop/iOS, create the directory
        if (!dir.mkpath(".")) {
            qWarning() << "DatabaseBackupManager: Failed to create backup directory:" << backupDir;
            return QString();
        }
        qDebug() << "DatabaseBackupManager: Created backup directory:" << backupDir;
#endif
    }

    qDebug() << "DatabaseBackupManager: Using backup directory:" << backupDir;
    return backupDir;
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
                qDebug() << "DatabaseBackupManager: Removed old backup" << fileName;
            } else {
                qWarning() << "DatabaseBackupManager: Failed to remove old backup" << fileName;
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
    QZipReader reader(zipPath);
    if (!reader.isReadable()) {
        qWarning() << "DatabaseBackupManager: Cannot read ZIP file:" << zipPath;
        return false;
    }

    // Manual extraction instead of extractAll() because QZipWriter stores
    // directory permissions from the staging temp dir (typically 0600 on
    // macOS/Linux due to umask). extractAll() applies those permissions,
    // which strips the execute bit from directories, making them
    // untraversable and causing subsequent file writes to fail.
    QDir baseDir(destDir);
    const QString basePrefix = baseDir.absolutePath() + "/";
    const auto entries = reader.fileInfoList();
    for (const QZipReader::FileInfo& entry : entries) {
        QString filePath = QDir::cleanPath(baseDir.absoluteFilePath(entry.filePath));

        // Guard against ZIP Slip path traversal
        if (!filePath.startsWith(basePrefix) && filePath != baseDir.absolutePath()) {
            qWarning() << "DatabaseBackupManager: Skipping ZIP entry with path traversal:" << entry.filePath;
            continue;
        }

        if (entry.isDir) {
            if (!baseDir.mkpath(entry.filePath)) {
                qWarning() << "DatabaseBackupManager: Failed to create directory:" << filePath;
                reader.close();
                return false;
            }
        } else {
            // Ensure parent directory exists
            QFileInfo fi(filePath);
            if (!baseDir.mkpath(fi.path().mid(baseDir.absolutePath().length() + 1))) {
                qWarning() << "DatabaseBackupManager: Failed to create parent dir for:" << filePath;
            }

            QFile outFile(filePath);
            if (!outFile.open(QIODevice::WriteOnly)) {
                qWarning() << "DatabaseBackupManager: Failed to create file:" << filePath << outFile.errorString();
                reader.close();
                return false;
            }
            // QZipReader has no streaming API, so fileData() loads the entire
            // uncompressed file into memory. Scope the QByteArray so it's freed
            // immediately after writing rather than accumulating across entries.
            {
                QByteArray data = reader.fileData(entry.filePath);
                if (data.isEmpty() && entry.size > 0) {
                    qWarning() << "DatabaseBackupManager: Failed to read ZIP entry:" << entry.filePath
                               << "(expected" << entry.size << "bytes)";
                    outFile.close();
                    reader.close();
                    return false;
                }
                qint64 written = outFile.write(data);
                if (written != data.size()) {
                    qWarning() << "DatabaseBackupManager: Incomplete write for" << filePath
                               << "- wrote" << written << "of" << data.size() << "bytes:" << outFile.errorString();
                    outFile.close();
                    reader.close();
                    return false;
                }
            }
            outFile.close();
        }
    }

    reader.close();
    qDebug() << "DatabaseBackupManager: Extracted" << entries.size() << "entries from ZIP";
    return true;
}

bool DatabaseBackupManager::createBackup(bool force)
{
    // Prevent concurrent backups or backup during restore
    if (m_backupInProgress) {
        qWarning() << "DatabaseBackupManager: Backup already in progress";
        return false;
    }
    if (m_restoreInProgress) {
        qWarning() << "DatabaseBackupManager: Cannot backup while restore is in progress";
        return false;
    }

    if (!m_storage) {
        QString error = "Storage not available";
        qWarning() << "DatabaseBackupManager:" << error;
        emit backupFailed(error);
        return false;
    }

    m_backupInProgress = true;

#ifdef Q_OS_ANDROID
    // Check storage permissions on Android (must be on main thread)
    if (!hasStoragePermission()) {
        QString error = "Storage permission not granted. Please enable storage access in Settings.";
        qWarning() << "DatabaseBackupManager:" << error;
        m_backupInProgress = false;
        emit backupFailed(error);
        emit storagePermissionNeeded();
        return false;
    }
#endif

    QString backupDir = getBackupDirectory();
    if (backupDir.isEmpty()) {
        QString error = "Failed to access backup directory";
        qWarning() << "DatabaseBackupManager:" << error;
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
        qDebug() << "DatabaseBackupManager: Valid backup already exists for today:" << zipPath;
        qDebug() << "DatabaseBackupManager: Existing backup size:" << existingZip.size() << "bytes";
        m_lastBackupDate = QDate::currentDate();

#ifdef Q_OS_ANDROID
        // Notify media scanner in case it wasn't scanned before
        QJniObject::callStaticMethod<void>(
            "io/github/kulitorum/decenza_de1/StorageHelper",
            "scanFile",
            "(Ljava/lang/String;)V",
            QJniObject::fromString(zipPath).object<jstring>());
        qDebug() << "DatabaseBackupManager: Triggered media scan for existing backup:" << zipPath;
#endif

        m_backupInProgress = false;
        emit backupCreated(zipPath);
        refreshBackupList();
        return true;
    } else if (existingZip.exists()) {
        // File exists - delete it to create fresh backup
        if (!QFile::remove(zipPath)) {
            QString error = "Failed to remove existing backup: " + zipPath;
            qWarning() << "DatabaseBackupManager:" << error;
            m_backupInProgress = false;
            emit backupFailed(error);
            return false;
        }
        qDebug() << "DatabaseBackupManager: Removed existing backup to create fresh one:" << zipPath;
    }

    // Snapshot settings and AI conversations on main thread (reads QSettings which
    // may use platform APIs that aren't thread-safe)
    QJsonObject settingsJson;
    if (m_settings) {
        settingsJson = SettingsSerializer::exportToJson(m_settings, /*includeSensitive=*/false);

        // Add AI conversations
        QSettings qsettings;
        QByteArray indexData = qsettings.value("ai/conversations/index").toByteArray();
        if (!indexData.isEmpty()) {
            QJsonDocument indexDoc = QJsonDocument::fromJson(indexData);
            if (indexDoc.isArray()) {
                QJsonArray conversations;
                QJsonArray indexArr = indexDoc.array();
                for (const QJsonValue& v : indexArr) {
                    QJsonObject entry = v.toObject();
                    QString key = entry["key"].toString();
                    if (key.isEmpty()) continue;

                    QString prefix = "ai/conversations/" + key + "/";
                    QJsonObject conv;
                    conv["key"] = key;
                    conv["beanBrand"] = entry["beanBrand"].toString();
                    conv["beanType"] = entry["beanType"].toString();
                    conv["profileName"] = entry["profileName"].toString();
                    conv["timestamp"] = qsettings.value(prefix + "timestamp").toString();
                    conv["systemPrompt"] = qsettings.value(prefix + "systemPrompt").toString();
                    conv["contextLabel"] = qsettings.value(prefix + "contextLabel").toString();
                    conv["indexTimestamp"] = entry["timestamp"].toVariant().toLongLong();

                    QByteArray messagesJson = qsettings.value(prefix + "messages").toByteArray();
                    if (!messagesJson.isEmpty()) {
                        QJsonDocument msgDoc = QJsonDocument::fromJson(messagesJson);
                        conv["messages"] = msgDoc.isArray() ? msgDoc.array() : QJsonArray();
                    } else {
                        conv["messages"] = QJsonArray();
                    }

                    conversations.append(conv);
                }
                settingsJson["ai_conversations"] = conversations;
            }
        }
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
                emit backupFailed("Failed to create backup");
            }, Qt::QueuedConnection);
            return;
        }

        QFileInfo dbInfo(dbResult);
        if (!dbInfo.exists()) {
            QDir(stagingDir).removeRecursively();
            QMetaObject::invokeMethod(this, [this, destroyed]() {
                if (*destroyed) return;
                m_backupInProgress = false;
                emit backupFailed("Failed to create backup file");
            }, Qt::QueuedConnection);
            return;
        }
        qDebug() << "DatabaseBackupManager: DB file created:" << dbResult
                 << "size:" << dbInfo.size() << "bytes";

        // Write settings.json (from pre-captured snapshot)
        if (!settingsJson.isEmpty()) {
            QString settingsPath = stagingDir + "/settings.json";
            QFile file(settingsPath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(QJsonDocument(settingsJson).toJson(QJsonDocument::Indented));
                file.close();
                qDebug() << "DatabaseBackupManager: Settings exported to" << settingsPath;
            } else {
                qWarning() << "DatabaseBackupManager: Failed to write settings.json:" << file.errorString();
            }
        }

        // Copy profiles
        if (!userProfilesPath.isEmpty() && QDir(userProfilesPath).exists()) {
            if (copyDirectory(userProfilesPath, stagingDir + "/profiles/user")) {
                qDebug() << "DatabaseBackupManager: User profiles backed up";
            }
        }
        if (!downloadedProfilesPath.isEmpty() && QDir(downloadedProfilesPath).exists()) {
            if (copyDirectory(downloadedProfilesPath, stagingDir + "/profiles/downloaded")) {
                qDebug() << "DatabaseBackupManager: Downloaded profiles backed up";
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
                    qDebug() << "DatabaseBackupManager: Personal media backed up";
                }
            }
        }

        // Create ZIP
        bool zipSuccess = false;

        {
            QZipWriter writer(zipPath);
            writer.setCompressionPolicy(QZipWriter::AutoCompress);

            std::function<void(const QString&, const QString&)> addDirToZip =
                [&writer, &addDirToZip](const QString& dirPath, const QString& zipPrefix) {
                QDir dir(dirPath);
                QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QFileInfo& fi : entries) {
                    QString entryPath = zipPrefix.isEmpty() ? fi.fileName() : zipPrefix + "/" + fi.fileName();
                    if (fi.isDir()) {
                        writer.addDirectory(entryPath);
                        addDirToZip(fi.absoluteFilePath(), entryPath);
                    } else {
                        QFile f(fi.absoluteFilePath());
                        if (f.open(QIODevice::ReadOnly)) {
                            writer.addFile(entryPath, &f);
                            f.close();
                        } else {
                            qWarning() << "DatabaseBackupManager: Failed to add to ZIP:" << fi.absoluteFilePath() << f.errorString();
                        }
                    }
                }
            };

            addDirToZip(stagingDir, QString());

            if (writer.status() == QZipWriter::NoError) {
                zipSuccess = true;
            } else {
                qWarning() << "DatabaseBackupManager: QZipWriter failed with status:" << writer.status();
            }
            writer.close();
            if (zipSuccess) {
                qDebug() << "DatabaseBackupManager: ZIP created:" << zipPath;
            }
        }

        QDir(stagingDir).removeRecursively();

        if (!zipSuccess) {
            QFile::remove(zipPath);
            qWarning() << "DatabaseBackupManager: Failed to create ZIP";
            QMetaObject::invokeMethod(this, [this, destroyed]() {
                if (*destroyed) return;
                m_backupInProgress = false;
                emit backupFailed("Failed to create backup archive");
            }, Qt::QueuedConnection);
            return;
        } else {
            QFileInfo zi(zipPath);
            qDebug() << "DatabaseBackupManager: ZIP size:" << zi.size() << "bytes";
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
            qDebug() << "DatabaseBackupManager: Triggered media scan for ZIP";
#endif

            m_backupInProgress = false;
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
            qWarning() << "DatabaseBackupManager: Backup thread ended without result signal - resetting state";
            m_backupInProgress = false;
            emit backupFailed("Backup failed unexpectedly");
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

    // Get all backup files (.zip on all platforms, .db for old backup compatibility)
    QStringList filters;
    filters << "shots_backup_*.zip" << "shots_backup_*.db";
    QFileInfoList backups = dir.entryInfoList(filters, QDir::Files, QDir::Time | QDir::Reversed);

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
    // Prevent concurrent restores or restore during backup
    if (m_restoreInProgress) {
        qWarning() << "DatabaseBackupManager: Restore already in progress";
        return false;
    }
    if (m_backupInProgress) {
        qWarning() << "DatabaseBackupManager: Cannot restore while backup is in progress";
        return false;
    }

    if (!m_storage) {
        QString error = "Storage not available";
        qWarning() << "DatabaseBackupManager:" << error;
        emit restoreFailed(error);
        return false;
    }

    m_restoreInProgress = true;

    QString backupDir = getBackupDirectory();
    if (backupDir.isEmpty()) {
        QString error = "Failed to access backup directory";
        qWarning() << "DatabaseBackupManager:" << error;
        m_restoreInProgress = false;
        emit restoreFailed(error);
        return false;
    }

    QString zipPath = backupDir + "/" + filename;
    QFileInfo zipInfo(zipPath);
    if (!zipInfo.exists()) {
        QString error = "Backup file not found: " + filename;
        qWarning() << "DatabaseBackupManager:" << error;
        m_restoreInProgress = false;
        emit restoreFailed(error);
        return false;
    }

    // Collect restore target paths on main thread (some accessors may use JNI/platform APIs)
    QString userProfilesPath = m_profileStorage ? m_profileStorage->userProfilesPath() : QString();
    QString downloadedProfilesPath = m_profileStorage ? m_profileStorage->downloadedProfilesPath() : QString();
    QString personalMediaDir = m_screensaverManager ? m_screensaverManager->personalMediaDirectory() : QString();
    QString dbPath = m_storage->databasePath();
    QString tempLocation = QStandardPaths::writableLocation(QStandardPaths::TempLocation);

    // Run heavy I/O on background thread
    QThread* thread = QThread::create(
        [this, filename, zipPath, merge, restoreShots, restoreSettings, restoreProfiles, restoreMedia,
         userProfilesPath, downloadedProfilesPath, personalMediaDir, dbPath, tempLocation,
         destroyed = m_destroyed]() {

        // Create temp directory for extraction
        QString tempDir = tempLocation + "/decenza_restore_temp";
        QDir(tempDir).removeRecursively();
        QDir().mkpath(tempDir);

        QStringList errors;
        bool shotsImported = false;
        bool isRawDb = filename.endsWith(".db");

        if (isRawDb) {
            // Raw .db backup (old format) — only shots can be restored
            if (!restoreShots) {
                QDir(tempDir).removeRecursively();
                QMetaObject::invokeMethod(this, [this, filename, destroyed]() {
                    if (*destroyed) return;
                    m_restoreInProgress = false;
                    emit restoreCompleted(filename);
                }, Qt::QueuedConnection);
                return;
            }
        } else {
            // ZIP backup - extract first
            qDebug() << "DatabaseBackupManager: Extracting" << zipPath << "to" << tempDir;

            if (!extractZip(zipPath, tempDir)) {
                QDir(tempDir).removeRecursively();
                QMetaObject::invokeMethod(this, [this, destroyed]() {
                    if (*destroyed) return;
                    m_restoreInProgress = false;
                    emit restoreFailed("Failed to extract backup file");
                }, Qt::QueuedConnection);
                return;
            }
        }

        // Restore shots (DB import)
        if (restoreShots) {
            QString tempDbPath;
            if (isRawDb) {
                tempDbPath = zipPath;
                qDebug() << "DatabaseBackupManager: Using raw .db backup:" << tempDbPath;
            } else {
                QDir tempDirObj(tempDir);
                QStringList dbFiles = tempDirObj.entryList({"*.db"}, QDir::Files, QDir::Time);
                if (!dbFiles.isEmpty()) {
                    tempDbPath = tempDir + "/" + dbFiles.first();
                    qDebug() << "DatabaseBackupManager: Found DB file:" << tempDbPath;
                } else {
                    qDebug() << "DatabaseBackupManager: No DB file in backup, skipping shot restore";
                }
            }

            if (!tempDbPath.isEmpty()) {
                // Validate SQLite database
                bool dbValid = true;
                QFileInfo extractedFileInfo(tempDbPath);
                if (!extractedFileInfo.exists() || extractedFileInfo.size() < 100) {
                    QString error = !extractedFileInfo.exists()
                        ? "Extracted file not found"
                        : "Extracted file is too small to be a valid database";
                    qWarning() << "DatabaseBackupManager:" << error;
                    errors << error;
                    dbValid = false;
                }

                if (dbValid) {
                    // Verify SQLite magic header
                    QFile dbFile(tempDbPath);
                    if (dbFile.open(QIODevice::ReadOnly)) {
                        QByteArray header = dbFile.read(16);
                        dbFile.close();
                        if (header.size() != 16 || !header.startsWith("SQLite format 3")) {
                            qWarning() << "DatabaseBackupManager: Invalid SQLite header:" << header.toHex();
                            errors << "Extracted file is not a valid SQLite database";
                            dbValid = false;
                        }
                    } else {
                        qWarning() << "DatabaseBackupManager: Cannot open extracted file for validation";
                        errors << "Cannot open extracted file for validation";
                        dbValid = false;
                    }
                }

                if (dbValid) {
                    qDebug() << "DatabaseBackupManager: Validated SQLite database (" << extractedFileInfo.size() << "bytes)";

                    // Import the database (uses separate connections, safe on background thread)
                    qDebug() << "DatabaseBackupManager: Importing database from" << tempDbPath
                             << (merge ? "(merge mode)" : "(replace mode)");
                    bool importSuccess = ShotHistoryStorage::importDatabaseStatic(dbPath, tempDbPath, merge);

                    if (!importSuccess) {
                        qWarning() << "DatabaseBackupManager: Shots import failed";
                        errors << "Failed to import shot history";
                        // In replace mode, abort — we haven't deleted profiles/settings yet,
                        // so returning now prevents data loss from a partial restore
                        if (!merge) {
                            QDir(tempDir).removeRecursively();
                            QMetaObject::invokeMethod(this, [this, errors, destroyed]() {
                                if (*destroyed) return;
                                m_restoreInProgress = false;
                                emit restoreFailed(errors.join("; "));
                            }, Qt::QueuedConnection);
                            return;
                        }
                    } else {
                        shotsImported = true;
                        qDebug() << "DatabaseBackupManager: Database restore completed successfully";
                    }
                }
            }
        }

        // Restore additional data if present (backwards-compatible)
        QString restoreDir = isRawDb ? QFileInfo(zipPath).absolutePath() : tempDir;

        // Restore profiles (pure file I/O, safe on background thread)
        bool profilesRestored = false;
        if (restoreProfiles) {
            // In replace mode, delete all existing profiles first (matching shots behavior)
            if (!merge) {
                auto clearProfileDir = [](const QString& dirPath) {
                    QDir dir(dirPath);
                    if (!dir.exists()) return;
                    QFileInfoList files = dir.entryInfoList(QDir::Files);
                    for (const QFileInfo& fi : files) {
                        QFile::remove(fi.absoluteFilePath());
                    }
                };
                if (!userProfilesPath.isEmpty()) clearProfileDir(userProfilesPath);
                if (!downloadedProfilesPath.isEmpty()) clearProfileDir(downloadedProfilesPath);
                qDebug() << "DatabaseBackupManager: Cleared existing profiles (replace mode)";
            }

            // Restore user profiles
            QString srcUser = restoreDir + "/profiles/user";
            if (QDir(srcUser).exists() && !userProfilesPath.isEmpty()) {
                QDir().mkpath(userProfilesPath);
                QDir srcDir(srcUser);
                QFileInfoList files = srcDir.entryInfoList(QDir::Files);
                int restored = 0;
                for (const QFileInfo& fi : files) {
                    QString destPath = userProfilesPath + "/" + fi.fileName();
                    if (merge && QFile::exists(destPath)) continue;
                    if (QFile::exists(destPath)) QFile::remove(destPath);
                    if (QFile::copy(fi.absoluteFilePath(), destPath)) restored++;
                }
                profilesRestored = (restored > 0);
                qDebug() << "DatabaseBackupManager: Restored" << restored << "user profiles";
            }

            // Restore downloaded profiles
            QString srcDownloaded = restoreDir + "/profiles/downloaded";
            if (QDir(srcDownloaded).exists() && !downloadedProfilesPath.isEmpty()) {
                QDir().mkpath(downloadedProfilesPath);
                QDir srcDir(srcDownloaded);
                QFileInfoList files = srcDir.entryInfoList(QDir::Files);
                int restored = 0;
                for (const QFileInfo& fi : files) {
                    QString destPath = downloadedProfilesPath + "/" + fi.fileName();
                    if (merge && QFile::exists(destPath)) continue;
                    if (QFile::exists(destPath)) QFile::remove(destPath);
                    if (QFile::copy(fi.absoluteFilePath(), destPath)) restored++;
                }
                if (restored > 0) profilesRestored = true;
                qDebug() << "DatabaseBackupManager: Restored" << restored << "downloaded profiles";
            }

            // In replace mode, profiles were restored even if backup had none (existing were cleared)
            if (!merge) profilesRestored = true;
        }

        // Restore personal media
        bool mediaWasRestored = false;
        if (restoreMedia) {
            // In replace mode, clear existing media files first
            if (!merge && !personalMediaDir.isEmpty()) {
                QDir mediaDir(personalMediaDir);
                if (mediaDir.exists()) {
                    const QFileInfoList existingFiles = mediaDir.entryInfoList(QDir::Files);
                    for (const QFileInfo& fi : existingFiles)
                        QFile::remove(fi.absoluteFilePath());
                    mediaWasRestored = true;
                }
            }

            QString srcMedia = restoreDir + "/media";
            if (QDir(srcMedia).exists() && !personalMediaDir.isEmpty()) {
                QDir().mkpath(personalMediaDir);
                QDir srcDir(srcMedia);
                QFileInfoList files = srcDir.entryInfoList(QDir::Files);
                int restored = 0;
                for (const QFileInfo& fi : files) {
                    QString destPath = personalMediaDir + "/" + fi.fileName();
                    if (merge && QFile::exists(destPath)) continue;
                    if (QFile::exists(destPath)) QFile::remove(destPath);
                    if (QFile::copy(fi.absoluteFilePath(), destPath)) restored++;
                }
                if (restored > 0) {
                    mediaWasRestored = true;
                    qDebug() << "DatabaseBackupManager: Restored" << restored << "personal media files";
                }
            }
        }

        // Read settings.json for settings + AI conversations restore (deliver to main thread)
        QJsonObject settingsJson;
        if (restoreSettings) {
            QString settingsPath = restoreDir + "/settings.json";
            if (QFile::exists(settingsPath)) {
                QFile file(settingsPath);
                if (!file.open(QIODevice::ReadOnly)) {
                    qWarning() << "DatabaseBackupManager: Failed to open settings.json:" << file.errorString();
                    errors << "Failed to read settings from backup";
                } else {
                    QByteArray data = file.readAll();
                    file.close();
                    QJsonParseError parseError;
                    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
                    if (doc.isNull()) {
                        qWarning() << "DatabaseBackupManager: Failed to parse settings.json:" << parseError.errorString();
                        errors << "Settings file in backup is corrupted";
                    } else if (!doc.isObject()) {
                        qWarning() << "DatabaseBackupManager: settings.json is not a JSON object";
                        errors << "Settings file in backup has invalid format";
                    } else {
                        settingsJson = doc.object();
                    }
                }
            }
        }

        QDir(tempDir).removeRecursively();

        // Deliver result to main thread — settings/AI restore touches QSettings
        QMetaObject::invokeMethod(this, [this, filename, settingsJson, shotsImported, profilesRestored, mediaWasRestored, errors, merge, destroyed]() {
            if (*destroyed) return;
            // Refresh storage state if shots were imported (via separate connection)
            if (shotsImported && m_storage) {
                m_storage->refreshTotalShots();
            }

            // Refresh profile list if profiles were restored
            if (profilesRestored) {
                emit this->profilesRestored();
            }

            // Refresh media catalog if media files were restored
            if (mediaWasRestored) {
                emit this->mediaRestored();
            }

            // Restore settings (must be on main thread — QSettings/platform APIs)
            if (!settingsJson.isEmpty() && m_settings) {
                QJsonObject json = settingsJson;
                QStringList excludeKeys = SettingsSerializer::sensitiveKeys();
                if (!SettingsSerializer::importFromJson(m_settings, json, excludeKeys)) {
                    qWarning() << "DatabaseBackupManager: Settings import returned failure";
                }
                qDebug() << "DatabaseBackupManager: Settings restored from backup";
            }

            // Restore AI conversations (writes to QSettings)
            if (settingsJson.contains("ai_conversations")) {
                QJsonArray conversations = settingsJson["ai_conversations"].toArray();
                if (!conversations.isEmpty()) {
                    QSettings qsettings;

                    // In replace mode, clear existing conversations first
                    if (!merge) {
                        QJsonDocument existingDoc = QJsonDocument::fromJson(
                            qsettings.value("ai/conversations/index").toByteArray());
                        QJsonArray existingIndex = existingDoc.isArray() ? existingDoc.array() : QJsonArray();
                        for (const QJsonValue& v : existingIndex) {
                            QString existingKey = v.toObject()["key"].toString();
                            if (!existingKey.isEmpty()) {
                                qsettings.remove("ai/conversations/" + existingKey);
                            }
                        }
                        qsettings.remove("ai/conversations/index");
                        qDebug() << "DatabaseBackupManager: Cleared" << existingIndex.size() << "existing AI conversations (replace mode)";
                    }

                    QJsonDocument existingDoc = QJsonDocument::fromJson(
                        qsettings.value("ai/conversations/index").toByteArray());
                    QJsonArray existingIndex = existingDoc.isArray() ? existingDoc.array() : QJsonArray();
                    QSet<QString> existingKeys;
                    for (const QJsonValue& v : existingIndex) {
                        existingKeys.insert(v.toObject()["key"].toString());
                    }

                    int imported = 0;
                    for (const QJsonValue& val : conversations) {
                        QJsonObject conv = val.toObject();
                        QString key = conv["key"].toString();
                        if (key.isEmpty() || existingKeys.contains(key)) continue;

                        QString prefix = "ai/conversations/" + key + "/";
                        qsettings.setValue(prefix + "systemPrompt", conv["systemPrompt"].toString());
                        qsettings.setValue(prefix + "contextLabel", conv["contextLabel"].toString());
                        qsettings.setValue(prefix + "timestamp", conv["timestamp"].toString());
                        QJsonArray messages = conv["messages"].toArray();
                        qsettings.setValue(prefix + "messages",
                            QJsonDocument(messages).toJson(QJsonDocument::Compact));

                        QJsonObject indexEntry;
                        indexEntry["key"] = key;
                        indexEntry["beanBrand"] = conv["beanBrand"].toString();
                        indexEntry["beanType"] = conv["beanType"].toString();
                        indexEntry["profileName"] = conv["profileName"].toString();
                        indexEntry["timestamp"] = conv["indexTimestamp"].toVariant().toLongLong();
                        existingIndex.append(indexEntry);
                        existingKeys.insert(key);
                        imported++;
                    }

                    if (imported > 0) {
                        qsettings.setValue("ai/conversations/index",
                            QJsonDocument(existingIndex).toJson(QJsonDocument::Compact));
                        qsettings.sync();
                        qDebug() << "DatabaseBackupManager: Imported" << imported << "AI conversations";
                    }
                }
            }

            m_restoreInProgress = false;
            if (!errors.isEmpty()) {
                emit restoreFailed(errors.join("; "));
            } else {
                emit restoreCompleted(filename);
            }
        }, Qt::QueuedConnection);
    });

    m_activeThreads.append(thread);
    connect(thread, &QThread::finished, this, [this, thread]() {
        m_activeThreads.removeOne(thread);
        // Safety net: if the thread ended without resetting the flag
        // (e.g., crash or unhandled exception), reset it here
        if (m_restoreInProgress) {
            qWarning() << "DatabaseBackupManager: Restore thread ended without result signal - resetting state";
            m_restoreInProgress = false;
            emit restoreFailed("Restore failed unexpectedly");
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
                qWarning() << "DatabaseBackupManager: Failed to copy" << fi.absoluteFilePath() << "to" << destPath;
                success = false;
            }
        }
    }
    return success;
}
