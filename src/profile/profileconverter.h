#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

/**
 * ProfileConverter - Batch convert DE1 app TCL profiles to our native JSON format
 *
 * Scans a source directory containing .tcl profile files (typically from de1app),
 * converts each to our native JSON format, and saves to a destination directory.
 *
 * Features:
 * - Properly parses all TCL profile fields including popup, weight, etc.
 * - Uses correct _advanced values for settings_2c profiles
 * - Reports progress for UI feedback
 * - Generates clean filenames from profile titles
 */
class ProfileConverter : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool isConverting READ isConverting NOTIFY isConvertingChanged)
    Q_PROPERTY(int totalFiles READ totalFiles NOTIFY progressChanged)
    Q_PROPERTY(int processedFiles READ processedFiles NOTIFY progressChanged)
    Q_PROPERTY(int successCount READ successCount NOTIFY progressChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY progressChanged)
    Q_PROPERTY(int skippedCount READ skippedCount NOTIFY progressChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY currentFileChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit ProfileConverter(QObject* parent = nullptr);

    bool isConverting() const { return m_converting; }
    int totalFiles() const { return m_totalFiles; }
    int processedFiles() const { return m_processedFiles; }
    int successCount() const { return m_successCount; }
    int errorCount() const { return m_errorCount; }
    int skippedCount() const { return m_skippedCount; }
    QString currentFile() const { return m_currentFile; }
    QString statusMessage() const { return m_statusMessage; }

    // Detect default DE1 app profiles path
    Q_INVOKABLE QString detectDE1AppProfilesPath() const;

    // Convert all .tcl files from source to destination directory
    // Returns true if conversion started, false if already running or paths invalid
    // If overwriteExisting is false, profiles that already exist will be skipped
    Q_INVOKABLE bool convertProfiles(const QString& sourceDir, const QString& destDir, bool overwriteExisting = false);

    // Get list of errors encountered during conversion
    Q_INVOKABLE QStringList errors() const { return m_errors; }

signals:
    void isConvertingChanged();
    void progressChanged();
    void currentFileChanged();
    void statusMessageChanged();
    void conversionComplete(int success, int errors);
    void conversionError(const QString& message);

private slots:
    void processNextFile();

private:
    void setStatus(const QString& message);
    QString generateFilename(const QString& title) const;
    void updateResourcesQrc();

    bool m_converting = false;
    int m_totalFiles = 0;
    int m_processedFiles = 0;
    int m_successCount = 0;
    int m_errorCount = 0;
    int m_skippedCount = 0;
    QString m_currentFile;
    QString m_statusMessage;
    QStringList m_errors;

    QStringList m_pendingFiles;
    QString m_destDir;
    bool m_overwriteExisting = false;
};
