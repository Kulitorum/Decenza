#include "shotimporter.h"
#include "shothistorystorage.h"
#include "shotfileparser.h"
#include <QDir>
#include <QDirIterator>
#include <QProcess>
#include <QTimer>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#endif

ShotImporter::ShotImporter(ShotHistoryStorage* storage, QObject* parent)
    : QObject(parent)
    , m_storage(storage)
{
}

ShotImporter::~ShotImporter()
{
    delete m_tempDir;
}

void ShotImporter::importFromZip(const QString& zipPath, bool overwriteExisting)
{
    if (m_importing) {
        emit importError("Import already in progress");
        return;
    }

    // Clean up any previous temp dir
    delete m_tempDir;
    m_tempDir = new QTemporaryDir();

    if (!m_tempDir->isValid()) {
        emit importError("Failed to create temporary directory");
        return;
    }

    // Store parameters for deferred extraction
    m_pendingZipPath = zipPath;
    m_overwriteExisting = overwriteExisting;

    setStatus("Extracting archive...");
    m_importing = true;
    m_extracting = true;
    m_cancelled = false;
    emit isImportingChanged();
    emit isExtractingChanged();

    // Defer extraction to allow UI to render the popup first
    QMetaObject::invokeMethod(this, &ShotImporter::performZipExtraction, Qt::QueuedConnection);
}

void ShotImporter::performZipExtraction()
{
    // Extract zip to temp directory
    bool extractSuccess = extractZip(m_pendingZipPath, m_tempDir->path());

    m_extracting = false;
    emit isExtractingChanged();

    if (!extractSuccess) {
        m_importing = false;
        emit isImportingChanged();
        emit importError("Failed to extract ZIP archive. Make sure 'unzip' is available or extract manually.");
        return;
    }

    // Find all .shot files
    QStringList shotFiles = findShotFiles(m_tempDir->path());

    if (shotFiles.isEmpty()) {
        m_importing = false;
        emit isImportingChanged();
        emit importError("No .shot files found in archive");
        return;
    }

    startImport(shotFiles, m_overwriteExisting);
}

void ShotImporter::importFromDirectory(const QString& dirPath, bool overwriteExisting)
{
    if (m_importing) {
        emit importError("Import already in progress");
        return;
    }

    QStringList shotFiles = findShotFiles(dirPath);

    if (shotFiles.isEmpty()) {
        emit importError("No .shot files found in directory");
        return;
    }

    m_importing = true;
    m_cancelled = false;
    emit isImportingChanged();

    startImport(shotFiles, overwriteExisting);
}

void ShotImporter::importSingleFile(const QString& filePath, bool overwriteExisting)
{
    if (m_importing) {
        emit importError("Import already in progress");
        return;
    }

    if (!filePath.endsWith(".shot", Qt::CaseInsensitive)) {
        emit importError("File must be a .shot file");
        return;
    }

    m_importing = true;
    m_cancelled = false;
    emit isImportingChanged();

    startImport(QStringList() << filePath, overwriteExisting);
}

QString ShotImporter::detectDE1AppHistoryPath()
{
    // Common locations for DE1 app history folder
    QStringList possiblePaths = {
#ifdef Q_OS_ANDROID
        "/sdcard/de1plus/history",
        "/storage/emulated/0/de1plus/history",
        "/sdcard/Android/data/tk.tcl.wish/files/de1plus/history",
#endif
        // Desktop paths (for testing or if user copied the folder)
        QDir::homePath() + "/de1plus/history",
        QDir::homePath() + "/Documents/de1plus/history",
    };

    for (const QString& path : possiblePaths) {
        QDir dir(path);
        if (dir.exists()) {
            // Check if it actually contains .shot files
            QStringList shots = dir.entryList(QStringList() << "*.shot", QDir::Files);
            if (!shots.isEmpty()) {
                qDebug() << "ShotImporter: Found DE1 app history at" << path << "with" << shots.size() << "shots";
                return path;
            }
        }
    }

    return QString();  // Not found
}

void ShotImporter::importFromDE1App(bool overwriteExisting)
{
    QString historyPath = detectDE1AppHistoryPath();
    if (historyPath.isEmpty()) {
        emit importError("DE1 app history folder not found. Make sure the DE1 tablet app has been used on this device.");
        return;
    }

    importFromDirectory(historyPath, overwriteExisting);
}

void ShotImporter::cancel()
{
    m_cancelled = true;
    setStatus("Cancelling...");
}

bool ShotImporter::extractZip(const QString& zipPath, const QString& destDir)
{
#ifdef Q_OS_ANDROID
    // Use Java's ZipInputStream on Android
    QJniEnvironment env;

    // Convert file:// URL to path if needed
    QString path = zipPath;
    if (path.startsWith("file://")) {
        path = QUrl(path).toLocalFile();
    }
    // Handle content:// URIs - need to copy to temp file first
    if (path.startsWith("content://")) {
        return extractZipFromContentUri(zipPath, destDir);
    }

    qDebug() << "ShotImporter: Extracting" << path << "to" << destDir;

    QJniObject jZipPath = QJniObject::fromString(path);
    QJniObject jDestDir = QJniObject::fromString(destDir);

    // FileInputStream fis = new FileInputStream(zipPath);
    QJniObject fis("java/io/FileInputStream", "(Ljava/lang/String;)V",
                   jZipPath.object<jstring>());
    if (!fis.isValid() || env.checkAndClearExceptions()) {
        qWarning() << "ShotImporter: Failed to open zip file";
        return false;
    }

    // ZipInputStream zis = new ZipInputStream(fis);
    QJniObject zis("java/util/zip/ZipInputStream", "(Ljava/io/InputStream;)V",
                   fis.object<jobject>());
    if (!zis.isValid() || env.checkAndClearExceptions()) {
        qWarning() << "ShotImporter: Failed to create ZipInputStream";
        fis.callMethod<void>("close");
        return false;
    }

    // Extract each entry
    int extractedCount = 0;
    QJniObject entry;
    while (true) {
        entry = zis.callObjectMethod("getNextEntry", "()Ljava/util/zip/ZipEntry;");
        if (env.checkAndClearExceptions() || !entry.isValid()) {
            break;
        }

        QString entryName = entry.callObjectMethod("getName", "()Ljava/lang/String;").toString();
        bool isDirectory = entry.callMethod<jboolean>("isDirectory");

        QString outPath = destDir + "/" + entryName;

        if (isDirectory) {
            QDir().mkpath(outPath);
        } else {
            // Ensure parent directory exists
            QFileInfo fi(outPath);
            QDir().mkpath(fi.absolutePath());

            // Read and write file
            QFile outFile(outPath);
            if (outFile.open(QIODevice::WriteOnly)) {
                QByteArray buffer(8192, 0);
                jbyteArray jBuffer = env->NewByteArray(8192);

                while (true) {
                    jint bytesRead = zis.callMethod<jint>("read", "([B)I", jBuffer);
                    if (env.checkAndClearExceptions() || bytesRead <= 0) {
                        break;
                    }
                    env->GetByteArrayRegion(jBuffer, 0, bytesRead, reinterpret_cast<jbyte*>(buffer.data()));
                    outFile.write(buffer.constData(), bytesRead);
                }

                env->DeleteLocalRef(jBuffer);
                outFile.close();
                extractedCount++;
            }
        }

        zis.callMethod<void>("closeEntry");
        env.checkAndClearExceptions();
    }

    zis.callMethod<void>("close");
    fis.callMethod<void>("close");
    env.checkAndClearExceptions();

    qDebug() << "ShotImporter: Extracted" << extractedCount << "files";
    return extractedCount > 0;

#elif defined(Q_OS_IOS)
    // QProcess is not available on iOS - zip extraction not supported
    Q_UNUSED(zipPath)
    Q_UNUSED(destDir)
    qWarning() << "ShotImporter: Zip extraction not supported on iOS (no QProcess)";
    return false;

#elif defined(Q_OS_WIN)
    QProcess process;
    process.setWorkingDirectory(destDir);

    // Convert file:// URL to path if needed
    QString path = zipPath;
    if (path.startsWith("file:///")) {
        path = QUrl(path).toLocalFile();
    }

    // Try PowerShell first (always available on Windows)
    QString psPath = path;
    QString psDestDir = destDir;
    process.start("powershell", QStringList()
        << "-NoProfile"
        << "-Command"
        << QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
            .arg(psPath.replace("'", "''"))
            .arg(psDestDir.replace("'", "''")));

    if (!process.waitForFinished(300000)) {  // 5 minute timeout for large archives
        qWarning() << "PowerShell extraction timeout";

        // Try unzip as fallback (available in Git Bash)
        process.start("unzip", QStringList() << "-o" << "-q" << path << "-d" << destDir);
        if (!process.waitForFinished(300000)) {
            qWarning() << "unzip extraction timeout";
            return false;
        }
    }

    if (process.exitCode() != 0) {
        qWarning() << "Extraction failed:" << process.readAllStandardError();
        return false;
    }

    return true;
#else
    QProcess process;
    process.setWorkingDirectory(destDir);

    // Convert file:// URL to path if needed
    QString path = zipPath;
    if (path.startsWith("file://")) {
        path = QUrl(path).toLocalFile();
    }

    // Unix-like systems: use unzip
    process.start("unzip", QStringList() << "-o" << "-q" << path << "-d" << destDir);
    if (!process.waitForFinished(300000)) {
        qWarning() << "unzip extraction timeout";
        return false;
    }

    if (process.exitCode() != 0) {
        qWarning() << "Extraction failed:" << process.readAllStandardError();
        return false;
    }

    return true;
#endif
}

#ifdef Q_OS_ANDROID
bool ShotImporter::extractZipFromContentUri(const QString& contentUri, const QString& destDir)
{
    QJniEnvironment env;

    qDebug() << "ShotImporter: Extracting from content URI:" << contentUri;

    // Get ContentResolver from context
    QJniObject context = QJniObject(QNativeInterface::QAndroidApplication::context());
    QJniObject contentResolver = context.callObjectMethod(
        "getContentResolver", "()Landroid/content/ContentResolver;");

    if (!contentResolver.isValid()) {
        qWarning() << "ShotImporter: Failed to get ContentResolver";
        return false;
    }

    // Parse the URI
    QJniObject jUriString = QJniObject::fromString(contentUri);
    QJniObject uri = QJniObject::callStaticObjectMethod(
        "android/net/Uri", "parse",
        "(Ljava/lang/String;)Landroid/net/Uri;",
        jUriString.object<jstring>());

    if (!uri.isValid()) {
        qWarning() << "ShotImporter: Failed to parse URI";
        return false;
    }

    // Open InputStream from content resolver
    QJniObject inputStream = contentResolver.callObjectMethod(
        "openInputStream",
        "(Landroid/net/Uri;)Ljava/io/InputStream;",
        uri.object<jobject>());

    if (!inputStream.isValid() || env.checkAndClearExceptions()) {
        qWarning() << "ShotImporter: Failed to open content URI";
        return false;
    }

    // Create ZipInputStream from the content InputStream
    QJniObject zis("java/util/zip/ZipInputStream", "(Ljava/io/InputStream;)V",
                   inputStream.object<jobject>());
    if (!zis.isValid() || env.checkAndClearExceptions()) {
        qWarning() << "ShotImporter: Failed to create ZipInputStream";
        inputStream.callMethod<void>("close");
        return false;
    }

    // Extract each entry
    int extractedCount = 0;
    QJniObject entry;
    while (true) {
        entry = zis.callObjectMethod("getNextEntry", "()Ljava/util/zip/ZipEntry;");
        if (env.checkAndClearExceptions() || !entry.isValid()) {
            break;
        }

        QString entryName = entry.callObjectMethod("getName", "()Ljava/lang/String;").toString();
        bool isDirectory = entry.callMethod<jboolean>("isDirectory");

        QString outPath = destDir + "/" + entryName;

        if (isDirectory) {
            QDir().mkpath(outPath);
        } else {
            // Ensure parent directory exists
            QFileInfo fi(outPath);
            QDir().mkpath(fi.absolutePath());

            // Read and write file
            QFile outFile(outPath);
            if (outFile.open(QIODevice::WriteOnly)) {
                QByteArray buffer(8192, 0);
                jbyteArray jBuffer = env->NewByteArray(8192);

                while (true) {
                    jint bytesRead = zis.callMethod<jint>("read", "([B)I", jBuffer);
                    if (env.checkAndClearExceptions() || bytesRead <= 0) {
                        break;
                    }
                    env->GetByteArrayRegion(jBuffer, 0, bytesRead, reinterpret_cast<jbyte*>(buffer.data()));
                    outFile.write(buffer.constData(), bytesRead);
                }

                env->DeleteLocalRef(jBuffer);
                outFile.close();
                extractedCount++;
            }
        }

        zis.callMethod<void>("closeEntry");
        env.checkAndClearExceptions();
    }

    zis.callMethod<void>("close");
    inputStream.callMethod<void>("close");
    env.checkAndClearExceptions();

    qDebug() << "ShotImporter: Extracted" << extractedCount << "files from content URI";
    return extractedCount > 0;
}
#endif

QStringList ShotImporter::findShotFiles(const QString& dirPath)
{
    QStringList files;

    QDirIterator it(dirPath, QStringList() << "*.shot",
                    QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        files.append(it.next());
    }

    // Sort by filename (which contains timestamp) for chronological import
    files.sort();

    return files;
}

void ShotImporter::startImport(const QStringList& files, bool overwriteExisting)
{
    m_pendingFiles = files;
    m_overwriteExisting = overwriteExisting;
    m_totalFiles = files.size();
    m_processedFiles = 0;
    m_importedFiles = 0;
    m_skippedFiles = 0;
    m_failedFiles = 0;

    setStatus(QString("Importing %1 shots...").arg(m_totalFiles));
    emit progressChanged();

    // Process files in batches using timer to keep UI responsive
    QTimer::singleShot(0, this, &ShotImporter::processNextFile);
}

void ShotImporter::processNextFile()
{
    if (m_cancelled || m_pendingFiles.isEmpty()) {
        // Import complete or cancelled
        m_importing = false;
        emit isImportingChanged();

        // Refresh the total shots count
        if (m_storage) {
            m_storage->refreshTotalShots();
        }

        if (m_cancelled) {
            setStatus("Import cancelled");
        } else {
            setStatus(QString("Complete: %1 imported, %2 skipped, %3 failed")
                .arg(m_importedFiles).arg(m_skippedFiles).arg(m_failedFiles));
        }

        emit importComplete(m_importedFiles, m_skippedFiles, m_failedFiles);

        // Clean up temp dir
        delete m_tempDir;
        m_tempDir = nullptr;

        return;
    }

    // Process a batch of files
    int batchSize = 10;  // Process 10 files per timer tick
    for (int i = 0; i < batchSize && !m_pendingFiles.isEmpty() && !m_cancelled; ++i) {
        QString filePath = m_pendingFiles.takeFirst();
        QString filename = QFileInfo(filePath).fileName();

        m_currentFile = filename;
        emit currentFileChanged();

        // Parse the file
        ShotFileParser::ParseResult result = ShotFileParser::parseFile(filePath);

        if (!result.success) {
            qWarning() << "Failed to parse" << filename << ":" << result.errorMessage;
            m_failedFiles++;
        } else {
            // Try to import into database
            qint64 shotId = m_storage->importShotRecord(result.record, m_overwriteExisting);

            if (shotId > 0) {
                m_importedFiles++;
            } else if (shotId == 0) {
                m_skippedFiles++;  // Duplicate
            } else {
                m_failedFiles++;  // Database error
            }
        }

        m_processedFiles++;
        emit progressChanged();

        // Update status periodically
        if (m_processedFiles % 50 == 0) {
            setStatus(QString("Importing... %1/%2").arg(m_processedFiles).arg(m_totalFiles));
        }
    }

    // Schedule next batch
    QTimer::singleShot(0, this, &ShotImporter::processNextFile);
}

void ShotImporter::setStatus(const QString& message)
{
    if (m_statusMessage != message) {
        m_statusMessage = message;
        emit statusMessageChanged();
    }
}
