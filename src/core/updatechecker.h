#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QFile>

class Settings;

class UpdateChecker : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool checking READ isChecking NOTIFY checkingChanged)
    Q_PROPERTY(bool downloading READ isDownloading NOTIFY downloadingChanged)
    Q_PROPERTY(int downloadProgress READ downloadProgress NOTIFY downloadProgressChanged)
    Q_PROPERTY(bool updateAvailable READ isUpdateAvailable NOTIFY updateAvailableChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY latestVersionChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(int currentVersionCode READ currentVersionCode CONSTANT)
    Q_PROPERTY(int latestVersionCode READ latestVersionCode NOTIFY latestVersionCodeChanged)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY releaseNotesChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(bool canDownloadUpdate READ canDownloadUpdate CONSTANT)
    Q_PROPERTY(bool canCheckForUpdates READ canCheckForUpdates CONSTANT)
    Q_PROPERTY(bool downloadReady READ isDownloadReady NOTIFY downloadReadyChanged)
    Q_PROPERTY(QString platformName READ platformName CONSTANT)
    Q_PROPERTY(QString releasePageUrl READ releasePageUrl NOTIFY latestVersionChanged)
    Q_PROPERTY(bool latestIsBeta READ latestIsBeta NOTIFY latestIsBetaChanged)

public:
    explicit UpdateChecker(Settings* settings, QObject* parent = nullptr);
    ~UpdateChecker();

    bool isChecking() const { return m_checking; }
    bool isDownloading() const { return m_downloading; }
    int downloadProgress() const { return m_downloadProgress; }
    bool isUpdateAvailable() const { return m_updateAvailable; }
    QString latestVersion() const { return m_latestVersion; }
    QString currentVersion() const;
    int currentVersionCode() const;
    int latestVersionCode() const { return m_latestBuildNumber; }
    QString releaseNotes() const { return m_releaseNotes; }
    QString errorMessage() const { return m_errorMessage; }
    bool canDownloadUpdate() const;
    bool canCheckForUpdates() const;
    bool isDownloadReady() const { return !m_downloadedApkPath.isEmpty(); }
    QString platformName() const;
    QString releasePageUrl() const;
    bool latestIsBeta() const { return m_latestIsBeta; }

    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void openReleasePage();
    Q_INVOKABLE void downloadAndInstall();
    Q_INVOKABLE void dismissUpdate();

signals:
    void checkingChanged();
    void downloadingChanged();
    void downloadProgressChanged();
    void updateAvailableChanged();
    void latestVersionChanged();
    void latestVersionCodeChanged();
    void releaseNotesChanged();
    void errorMessageChanged();
    void updatePromptRequested();  // Emitted when auto-check finds update
    void installationStarted();
    void latestIsBetaChanged();
    void downloadReadyChanged();

private slots:
    void onReleaseInfoReceived();
    void onDownloadProgress(qint64 received, qint64 total);
    void onDownloadFinished();
    void onPeriodicCheck();

private:
    void parseReleaseInfo(const QByteArray& data);
    void startDownload();
    void installApk(const QString& apkPath);
    int extractBuildNumber(const QString& version) const;
    bool isNewerVersion(const QString& latest, const QString& current) const;

    Settings* m_settings = nullptr;
    QNetworkAccessManager* m_network = nullptr;
    QNetworkReply* m_currentReply = nullptr;
    QFile* m_downloadFile = nullptr;
    QTimer* m_periodicTimer = nullptr;

    bool m_checking = false;
    bool m_downloading = false;
    int m_downloadProgress = 0;
    bool m_updateAvailable = false;
    bool m_updatePromptShown = false;  // Only emit updatePromptRequested once per update
    QString m_latestVersion;
    QString m_releaseNotes;
    QString m_downloadUrl;
    QString m_errorMessage;
    QString m_releaseTag;
    int m_latestBuildNumber = 0;
    bool m_latestIsBeta = false;
    QString m_downloadedApkPath;
    qint64 m_expectedDownloadSize = 0;

    static const QString GITHUB_API_URL;
    static const QString GITHUB_REPO;
};
