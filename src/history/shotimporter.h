#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QFuture>
#include <QTemporaryDir>

class ShotHistoryStorage;

/**
 * Imports DE1 app shot history files (.shot format)
 *
 * Supports importing from:
 * - Single .shot files
 * - Directories containing .shot files
 * - ZIP archives containing .shot files
 */
class ShotImporter : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool isImporting READ isImporting NOTIFY isImportingChanged)
    Q_PROPERTY(bool isExtracting READ isExtracting NOTIFY isExtractingChanged)
    Q_PROPERTY(int totalFiles READ totalFiles NOTIFY progressChanged)
    Q_PROPERTY(int processedFiles READ processedFiles NOTIFY progressChanged)
    Q_PROPERTY(int importedFiles READ importedFiles NOTIFY progressChanged)
    Q_PROPERTY(int skippedFiles READ skippedFiles NOTIFY progressChanged)
    Q_PROPERTY(int failedFiles READ failedFiles NOTIFY progressChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY currentFileChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit ShotImporter(ShotHistoryStorage* storage, QObject* parent = nullptr);
    ~ShotImporter();

    bool isImporting() const { return m_importing; }
    bool isExtracting() const { return m_extracting; }
    int totalFiles() const { return m_totalFiles; }
    int processedFiles() const { return m_processedFiles; }
    int importedFiles() const { return m_importedFiles; }
    int skippedFiles() const { return m_skippedFiles; }
    int failedFiles() const { return m_failedFiles; }
    QString currentFile() const { return m_currentFile; }
    QString statusMessage() const { return m_statusMessage; }

    // Import methods - all run asynchronously
    // If overwriteExisting is true, duplicate shots will be replaced instead of skipped
    Q_INVOKABLE void importFromZip(const QString& zipPath, bool overwriteExisting = false);
    Q_INVOKABLE void importFromDirectory(const QString& dirPath, bool overwriteExisting = false);
    Q_INVOKABLE void importSingleFile(const QString& filePath, bool overwriteExisting = false);

    // Auto-detect DE1 app history folder
    Q_INVOKABLE QString detectDE1AppHistoryPath();
    Q_INVOKABLE void importFromDE1App(bool overwriteExisting = false);

    // Cancel ongoing import
    Q_INVOKABLE void cancel();

signals:
    void isImportingChanged();
    void isExtractingChanged();
    void progressChanged();
    void currentFileChanged();
    void statusMessageChanged();
    void importComplete(int imported, int skipped, int failed);
    void importError(const QString& message);

private slots:
    void processNextFile();
    void performZipExtraction();

private:
    bool extractZip(const QString& zipPath, const QString& destDir);
#ifdef Q_OS_ANDROID
    bool extractZipFromContentUri(const QString& contentUri, const QString& destDir);
#endif
    QStringList findShotFiles(const QString& dirPath);
    void startImport(const QStringList& files, bool overwriteExisting);
    void setStatus(const QString& message);

    QString m_pendingZipPath;

    ShotHistoryStorage* m_storage;
    QTemporaryDir* m_tempDir = nullptr;

    bool m_importing = false;
    bool m_extracting = false;
    bool m_cancelled = false;
    bool m_overwriteExisting = false;
    int m_totalFiles = 0;
    int m_processedFiles = 0;
    int m_importedFiles = 0;
    int m_skippedFiles = 0;
    int m_failedFiles = 0;
    QString m_currentFile;
    QString m_statusMessage;

    QStringList m_pendingFiles;
};
