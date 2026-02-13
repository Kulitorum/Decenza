#include "databasebackupmanager.h"
#include "settings.h"
#include "../history/shothistorystorage.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#if !defined(Q_OS_IOS)
#include <QProcess>
#endif
#include <QDebug>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

DatabaseBackupManager::DatabaseBackupManager(Settings* settings, ShotHistoryStorage* storage, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_storage(storage)
    , m_checkTimer(new QTimer(this))
{
    connect(m_checkTimer, &QTimer::timeout, this, &DatabaseBackupManager::onTimerFired);
}

void DatabaseBackupManager::start()
{
    if (!m_settings || !m_storage) {
        qWarning() << "DatabaseBackupManager: Cannot start - missing settings or storage";
        return;
    }

    // Check immediately on startup (in case we missed a backup)
    onTimerFired();

    // Then check every hour
    scheduleNextCheck();
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
#ifdef Q_OS_ANDROID
    jboolean result = QJniObject::callStaticMethod<jboolean>(
        "io/github/kulitorum/decenza_de1/StorageHelper",
        "unzipToDirectory",
        "(Ljava/lang/String;Ljava/lang/String;)Z",
        QJniObject::fromString(zipPath).object<jstring>(),
        QJniObject::fromString(destDir).object<jstring>());
    return result;
#elif defined(Q_OS_IOS)
    // iOS doesn't support QProcess - zip extraction not available
    Q_UNUSED(zipPath);
    Q_UNUSED(destDir);
    qWarning() << "DatabaseBackupManager: ZIP extraction not supported on iOS";
    return false;
#else
    QProcess unzipProcess;
    unzipProcess.setWorkingDirectory(destDir);

#ifdef Q_OS_WIN
    QStringList args;
    args << "-NoProfile" << "-NonInteractive" << "-Command";
    QString escapedZipPath = QString(zipPath).replace("'", "''");
    QString escapedDestDir = QString(destDir).replace("'", "''");
    args << QString("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
            .arg(escapedZipPath)
            .arg(escapedDestDir);
    unzipProcess.start("powershell", args);
#else
    QStringList args;
    args << "-o" << zipPath << "-d" << destDir;
    unzipProcess.start("unzip", args);
#endif

    if (unzipProcess.waitForFinished(30000)) {
        return unzipProcess.exitCode() == 0;
    }
    return false;
#endif
}

bool DatabaseBackupManager::createBackup(bool force)
{
    // Prevent concurrent backups
    if (m_backupInProgress) {
        qWarning() << "DatabaseBackupManager: Backup already in progress";
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
    // Check storage permissions on Android
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
#ifdef Q_OS_IOS
    QString zipPath = backupDir + "/shots_backup_" + dateStr + ".db";
#else
    QString zipPath = backupDir + "/shots_backup_" + dateStr + ".zip";
#endif

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

    // Create staging directory in temp for assembling all backup data
    QString stagingDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/decenza_backup_staging";
    QDir(stagingDir).removeRecursively();
    QDir().mkpath(stagingDir);

    // Create the backup .db file into staging directory
    QString dbDestPath = stagingDir + "/shots_backup_" + dateStr + ".db";
    QString result = m_storage->createBackup(dbDestPath);

    if (result.isEmpty()) {
        QString error = "Failed to create backup";
        qWarning() << "DatabaseBackupManager:" << error;
        QDir(stagingDir).removeRecursively();
        m_backupInProgress = false;
        emit backupFailed(error);
        return false;
    }

    m_lastBackupDate = QDate::currentDate();

    // Verify DB file exists
    QFileInfo fileInfo(result);
    if (fileInfo.exists()) {
        qDebug() << "DatabaseBackupManager: DB file created:" << result;
        qDebug() << "DatabaseBackupManager: File size:" << fileInfo.size() << "bytes";
    } else {
        qWarning() << "DatabaseBackupManager: DB file not found at:" << result;
        QDir(stagingDir).removeRecursively();
        m_backupInProgress = false;
        emit backupFailed("Failed to create backup file");
        return false;
    }

    // Create ZIP from the staging directory contents
    QString finalPath = zipPath;
    bool zipSuccess = false;

#ifdef Q_OS_ANDROID
    // Android: Use Java to zip the directory
    QJniObject javaZipPath = QJniObject::callStaticObjectMethod(
        "io/github/kulitorum/decenza_de1/StorageHelper",
        "zipDirectory",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        QJniObject::fromString(stagingDir).object<jstring>(),
        QJniObject::fromString(zipPath).object<jstring>());

    if (javaZipPath.isValid() && !javaZipPath.toString().isEmpty()) {
        zipSuccess = true;
        qDebug() << "DatabaseBackupManager: ZIP created via Java:" << zipPath;
    }
#elif defined(Q_OS_IOS)
    // iOS: No QProcess available, save .db file directly (no zip compression)
    {
        QString dbFileName = QDir(stagingDir).entryList({"*.db"}, QDir::Files).value(0);
        if (!dbFileName.isEmpty()) {
            QString dbPath = backupDir + "/" + QFileInfo(dbFileName).fileName();
            if (QFile::copy(stagingDir + "/" + dbFileName, dbPath)) {
                zipSuccess = true;
                finalPath = dbPath;
                qDebug() << "DatabaseBackupManager: Saved backup as .db (no zip on iOS):" << dbPath;
            }
        }
    }
#else
    // Desktop: Use system zip command
    QProcess zipProcess;
    zipProcess.setWorkingDirectory(stagingDir);

#ifdef Q_OS_WIN
    // Windows: Use PowerShell Compress-Archive
    QStringList args;
    args << "-NoProfile" << "-NonInteractive" << "-Command";
    QString escapedStagingDir = QString(stagingDir).replace("'", "''");
    QString escapedZipPath = QString(zipPath).replace("'", "''");
    args << QString("Compress-Archive -Path '%1/*' -DestinationPath '%2' -Force -CompressionLevel Optimal")
            .arg(escapedStagingDir)
            .arg(escapedZipPath);
    zipProcess.start("powershell", args);
#else
    // macOS/Linux: Use zip command from within staging dir
    QStringList args;
    args << "-r" << zipPath << ".";
    zipProcess.start("zip", args);
#endif

    if (zipProcess.waitForFinished(30000)) { // 30 second timeout for larger backups
        if (zipProcess.exitCode() == 0) {
            zipSuccess = true;
            qDebug() << "DatabaseBackupManager: ZIP created via system command:" << zipPath;
        } else {
            qWarning() << "DatabaseBackupManager: zip command failed:" << zipProcess.readAllStandardError();
        }
    } else {
        qWarning() << "DatabaseBackupManager: zip command timeout";
    }
#endif

    // Clean up staging directory
    QDir(stagingDir).removeRecursively();

    if (zipSuccess) {
        // Verify ZIP file
        QFileInfo zipInfo(zipPath);
        qDebug() << "DatabaseBackupManager: ZIP size:" << zipInfo.size() << "bytes";

#ifdef Q_OS_ANDROID
        // Scan the ZIP file
        QJniObject::callStaticMethod<void>(
            "io/github/kulitorum/decenza_de1/StorageHelper",
            "scanFile",
            "(Ljava/lang/String;)V",
            QJniObject::fromString(zipPath).object<jstring>());
        qDebug() << "DatabaseBackupManager: Triggered media scan for ZIP";
#endif
    } else {
        qWarning() << "DatabaseBackupManager: Failed to create ZIP";
        finalPath = result;
    }

    m_backupInProgress = false;
    emit backupCreated(finalPath);

    // Clean up old backups after successful backup
    cleanOldBackups(backupDir);

    return true;
}

bool DatabaseBackupManager::shouldOfferFirstRunRestore() const
{
    if (!m_storage) {
        return false;
    }

    // Check if database is empty (first run or reinstall)
    // We'll consider it first run if there are 0 shots in history
    QVariantMap emptyFilter;
    int shotCount = m_storage->getFilteredShotCount(emptyFilter);
    bool isEmpty = (shotCount == 0);

    if (!isEmpty) {
        return false;  // Not first run
    }

#ifdef Q_OS_ANDROID
    // On Android, check storage permission first
    if (!hasStoragePermission()) {
        return false;  // Can't check for backups without permission
    }
#endif

    // Check if backups exist
    QStringList backups = getAvailableBackups();
    return !backups.isEmpty();
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

    // Get all backup files (ZIP on most platforms, .db on iOS)
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

bool DatabaseBackupManager::restoreBackup(const QString& filename, bool merge)
{
    // Prevent concurrent restores
    if (m_restoreInProgress) {
        qWarning() << "DatabaseBackupManager: Restore already in progress";
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

    // Get the .db file from the backup
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/decenza_restore_temp";
    QDir(tempDir).removeRecursively();
    QDir().mkpath(tempDir);

    QString tempDbPath;

    if (filename.endsWith(".db")) {
        // Raw .db backup (iOS) - use directly
        tempDbPath = zipPath;
        qDebug() << "DatabaseBackupManager: Using raw .db backup:" << tempDbPath;
    } else {
        // ZIP backup - extract first
        qDebug() << "DatabaseBackupManager: Extracting" << zipPath << "to" << tempDir;

        if (!extractZip(zipPath, tempDir)) {
            QString error = "Failed to extract backup file";
            qWarning() << "DatabaseBackupManager:" << error;
            QDir(tempDir).removeRecursively();
            m_restoreInProgress = false;
            emit restoreFailed(error);
            return false;
        }

        // Find the .db file in the extracted contents
        QDir tempDirObj(tempDir);
        QStringList dbFiles = tempDirObj.entryList({"*.db"}, QDir::Files, QDir::Time);
        if (!dbFiles.isEmpty()) {
            tempDbPath = tempDir + "/" + dbFiles.first();
            qDebug() << "DatabaseBackupManager: Found DB file:" << tempDbPath;
        } else {
            QString error = "No database file found in backup";
            qWarning() << "DatabaseBackupManager:" << error;
            QDir(tempDir).removeRecursively();
            m_restoreInProgress = false;
            emit restoreFailed(error);
            return false;
        }
    }

    // Validate extracted file exists and is a valid SQLite database
    QFileInfo extractedFileInfo(tempDbPath);
    if (!extractedFileInfo.exists()) {
        QString error = "Extracted file not found: " + tempDbPath;
        qWarning() << "DatabaseBackupManager:" << error;
        QDir(tempDir).removeRecursively();
        m_restoreInProgress = false;
        emit restoreFailed(error);
        return false;
    }

    if (extractedFileInfo.size() < 100) {
        QString error = "Extracted file is too small to be a valid database: " + QString::number(extractedFileInfo.size()) + " bytes";
        qWarning() << "DatabaseBackupManager:" << error;
        QDir(tempDir).removeRecursively();
        m_restoreInProgress = false;
        emit restoreFailed(error);
        return false;
    }

    // Verify SQLite magic header (first 16 bytes should be "SQLite format 3\0")
    QFile dbFile(tempDbPath);
    if (!dbFile.open(QIODevice::ReadOnly)) {
        QString error = "Cannot open extracted file for validation: " + dbFile.errorString();
        qWarning() << "DatabaseBackupManager:" << error;
        QDir(tempDir).removeRecursively();
        m_restoreInProgress = false;
        emit restoreFailed(error);
        return false;
    }

    QByteArray header = dbFile.read(16);
    dbFile.close();

    if (header.size() != 16 || !header.startsWith("SQLite format 3")) {
        QString error = "Extracted file is not a valid SQLite database (invalid magic header)";
        qWarning() << "DatabaseBackupManager:" << error;
        qWarning() << "DatabaseBackupManager: Header bytes:" << header.toHex();
        QDir(tempDir).removeRecursively();
        m_restoreInProgress = false;
        emit restoreFailed(error);
        return false;
    }

    qDebug() << "DatabaseBackupManager: Validated SQLite database (" << extractedFileInfo.size() << "bytes)";

    // Import the extracted database
    qDebug() << "DatabaseBackupManager: Importing database from" << tempDbPath
             << (merge ? "(merge mode)" : "(replace mode)");
    bool importSuccess = m_storage->importDatabase(tempDbPath, merge);

    if (importSuccess) {
        qDebug() << "DatabaseBackupManager: Restore completed successfully";
        QDir(tempDir).removeRecursively();
        m_restoreInProgress = false;
        emit restoreCompleted(filename);
        return true;
    } else {
        QString error = "Failed to import backup database";
        qWarning() << "DatabaseBackupManager:" << error;
        QDir(tempDir).removeRecursively();
        m_restoreInProgress = false;
        emit restoreFailed(error);
        return false;
    }
}
