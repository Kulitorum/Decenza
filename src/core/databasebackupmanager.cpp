#include "databasebackupmanager.h"
#include "settings.h"
#include "../history/shothistorystorage.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QProcess>
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

    // Verify directory exists (Java should have created it)
    QDir dir(backupDir);
    if (!dir.exists()) {
        qWarning() << "DatabaseBackupManager: Backup directory does not exist:" << backupDir;
        qWarning() << "DatabaseBackupManager: This may be due to missing storage permissions";
        return QString();
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
    QString destPath = backupDir + "/shots_backup_" + dateStr + ".db";
    QString zipPath = backupDir + "/shots_backup_" + dateStr + ".zip";

    // Check if ZIP backup already exists for today
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

    // Create the backup (temporary .db file)
    QString result = m_storage->createBackup(destPath);

    if (!result.isEmpty()) {
        m_lastBackupDate = QDate::currentDate();

        // Verify DB file exists
        QFileInfo fileInfo(result);
        if (fileInfo.exists()) {
            qDebug() << "DatabaseBackupManager: DB file created:" << result;
            qDebug() << "DatabaseBackupManager: File size:" << fileInfo.size() << "bytes";
        } else {
            qWarning() << "DatabaseBackupManager: DB file not found at:" << result;
            m_backupInProgress = false;
            emit backupFailed("Failed to create backup file");
            return false;
        }

        // Create ZIP file from the DB backup (all platforms)
        QString finalPath = zipPath;
        bool zipSuccess = false;

#ifdef Q_OS_ANDROID
        // Android: Use Java ZipOutputStream
        QJniObject javaZipPath = QJniObject::callStaticObjectMethod(
            "io/github/kulitorum/decenza_de1/StorageHelper",
            "zipFile",
            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
            QJniObject::fromString(result).object<jstring>(),
            QJniObject::fromString(zipPath).object<jstring>());

        if (javaZipPath.isValid() && !javaZipPath.toString().isEmpty()) {
            zipSuccess = true;
            qDebug() << "DatabaseBackupManager: ZIP created via Java:" << zipPath;
        }
#else
        // Desktop/iOS: Use system zip command
        QProcess zipProcess;
        zipProcess.setWorkingDirectory(backupDir);

        // Get just the filename without path
        QFileInfo dbFileInfo(result);
        QString dbFileName = dbFileInfo.fileName();
        QString zipFileName = QFileInfo(zipPath).fileName();

#ifdef Q_OS_WIN
        // Windows: Use PowerShell Compress-Archive with proper escaping
        QStringList args;
        args << "-NoProfile" << "-NonInteractive" << "-Command";
        // Use PowerShell's literal string syntax to avoid injection
        // Note: Compress-Archive always uses optimal compression, no level parameter needed
        args << QString("Compress-Archive -LiteralPath '%1' -DestinationPath '%2' -Force -CompressionLevel Optimal")
                .arg(dbFileName.replace("'", "''"))  // Escape single quotes
                .arg(zipFileName.replace("'", "''"));
        zipProcess.start("powershell", args);
#else
        // macOS/Linux: Use zip command
        QStringList args;
        args << "-9" << "-j" << zipFileName << dbFileName; // -9 = maximum compression, -j = junk directory paths
        zipProcess.start("zip", args);
#endif

        if (zipProcess.waitForFinished(10000)) { // 10 second timeout
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

        if (zipSuccess) {
            // Delete the temporary .db file
            if (QFile::remove(result)) {
                qDebug() << "DatabaseBackupManager: Removed temporary .db file";
            }

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
            qWarning() << "DatabaseBackupManager: Failed to create ZIP, keeping .db file";
            finalPath = result;
        }

        m_backupInProgress = false;
        emit backupCreated(finalPath);

        // Clean up old backups after successful backup
        cleanOldBackups(backupDir);

        return true;
    } else {
        QString error = "Failed to create backup";
        qWarning() << "DatabaseBackupManager:" << error;
        m_backupInProgress = false;
        emit backupFailed(error);
        return false;
    }
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

    // Get all backup ZIP files
    QStringList filters;
    filters << "shots_backup_*.zip";
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

    // Extract ZIP to temporary location
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString tempDbPath = tempDir + "/restore_temp.db";

    // Remove any existing temp file
    if (QFile::exists(tempDbPath) && !QFile::remove(tempDbPath)) {
        QString error = "Failed to remove existing temp file (may be locked): " + tempDbPath;
        qWarning() << "DatabaseBackupManager:" << error;
        m_restoreInProgress = false;
        emit restoreFailed(error);
        return false;
    }

    qDebug() << "DatabaseBackupManager: Extracting" << zipPath << "to" << tempDbPath;

    // Extract the ZIP file
    bool extractSuccess = false;

#ifdef Q_OS_ANDROID
    // Android: Use Java to unzip
    QJniObject javaResult = QJniObject::callStaticObjectMethod(
        "io/github/kulitorum/decenza_de1/StorageHelper",
        "unzipFile",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        QJniObject::fromString(zipPath).object<jstring>(),
        QJniObject::fromString(tempDbPath).object<jstring>());

    if (javaResult.isValid() && !javaResult.toString().isEmpty()) {
        extractSuccess = true;
        qDebug() << "DatabaseBackupManager: Extracted via Java";
    }
#else
    // Desktop/iOS: Use system unzip command
    QProcess unzipProcess;
    unzipProcess.setWorkingDirectory(tempDir);

#ifdef Q_OS_WIN
    // Windows: Use PowerShell Expand-Archive with proper escaping
    QStringList args;
    args << "-NoProfile" << "-NonInteractive" << "-Command";
    // Use PowerShell's literal string syntax to avoid injection
    args << QString("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
            .arg(zipPath.replace("'", "''"))  // Escape single quotes
            .arg(tempDir.replace("'", "''"));
    unzipProcess.start("powershell", args);
#else
    // macOS/Linux: Use unzip command
    QStringList args;
    args << "-o" << zipPath << "-d" << tempDir;  // -o = overwrite
    unzipProcess.start("unzip", args);
#endif

    if (unzipProcess.waitForFinished(10000)) {
        if (unzipProcess.exitCode() == 0) {
            extractSuccess = true;
            qDebug() << "DatabaseBackupManager: Extracted via system command";
        }
    }
#endif

    if (!extractSuccess) {
        QString error = "Failed to extract backup file";
        qWarning() << "DatabaseBackupManager:" << error;
        m_restoreInProgress = false;
        emit restoreFailed(error);
        return false;
    }

    // Validate extracted file exists and is a valid SQLite database
    QFileInfo extractedFileInfo(tempDbPath);
    if (!extractedFileInfo.exists()) {
        QString error = "Extracted file not found: " + tempDbPath;
        qWarning() << "DatabaseBackupManager:" << error;
        m_restoreInProgress = false;
        emit restoreFailed(error);
        return false;
    }

    if (extractedFileInfo.size() < 100) {
        QString error = "Extracted file is too small to be a valid database: " + QString::number(extractedFileInfo.size()) + " bytes";
        qWarning() << "DatabaseBackupManager:" << error;
        QFile::remove(tempDbPath);
        m_restoreInProgress = false;
        emit restoreFailed(error);
        return false;
    }

    // Verify SQLite magic header (first 16 bytes should be "SQLite format 3\0")
    QFile dbFile(tempDbPath);
    if (!dbFile.open(QIODevice::ReadOnly)) {
        QString error = "Cannot open extracted file for validation: " + dbFile.errorString();
        qWarning() << "DatabaseBackupManager:" << error;
        QFile::remove(tempDbPath);
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
        QFile::remove(tempDbPath);
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
        // Clean up temp file on success
        QFile::remove(tempDbPath);
        qDebug() << "DatabaseBackupManager: Restore completed successfully";
        m_restoreInProgress = false;
        emit restoreCompleted(filename);
        return true;
    } else {
        QString error = "Failed to import backup database. Temp file kept at: " + tempDbPath;
        qWarning() << "DatabaseBackupManager:" << error;
        m_restoreInProgress = false;
        emit restoreFailed(error);
        return false;
    }
}
