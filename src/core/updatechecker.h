#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QFile>
#include <QFileInfo>

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
    Q_PROPERTY(bool canDownloadUpdate READ canDownloadUpdate NOTIFY canDownloadUpdateChanged)
    Q_PROPERTY(bool canCheckForUpdates READ canCheckForUpdates CONSTANT)
    Q_PROPERTY(bool downloadReady READ isDownloadReady NOTIFY downloadReadyChanged)
    Q_PROPERTY(QString platformName READ platformName CONSTANT)
    Q_PROPERTY(QString releasePageUrl READ releasePageUrl NOTIFY latestVersionChanged)
    Q_PROPERTY(bool latestIsBeta READ latestIsBeta NOTIFY latestIsBetaChanged)
    Q_PROPERTY(bool installing READ isInstalling NOTIFY installingChanged)

    // Auto-relaunch after Android self-update. Always false / no-op on
    // non-Android platforms. See specs/android-update-relaunch/spec.md.
    Q_PROPERTY(bool autoRelaunchPermissionGranted READ autoRelaunchPermissionGranted
               NOTIFY autoRelaunchPermissionGrantedChanged)
    Q_PROPERTY(bool currentLaunchWasAutoRelaunch READ currentLaunchWasAutoRelaunch CONSTANT)
    Q_PROPERTY(bool autoRelaunchSupported READ autoRelaunchSupported CONSTANT)

public:
    explicit UpdateChecker(QNetworkAccessManager* networkManager, Settings* settings, QObject* parent = nullptr);
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
    bool isInstalling() const { return m_installInFlight; }

    // Always false on non-Android platforms.
    bool autoRelaunchPermissionGranted() const;
    bool currentLaunchWasAutoRelaunch() const { return m_currentLaunchWasAutoRelaunch; }
    bool autoRelaunchSupported() const;

    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void openReleasePage();
    Q_INVOKABLE void downloadAndInstall();
    Q_INVOKABLE void dismissUpdate();

    /// Opens Android Settings → "Display over other apps" → Decenza so the user
    /// can grant SYSTEM_ALERT_WINDOW. No-op on non-Android. The grant cannot be
    /// observed synchronously; after the user returns to the app, call
    /// refreshAutoRelaunchPermission().
    Q_INVOKABLE void requestAutoRelaunchPermission();

    /// Re-queries Settings.canDrawOverlays() and emits the change notification
    /// if it differs from the cached value. Cheap to call repeatedly.
    Q_INVOKABLE void refreshAutoRelaunchPermission();

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
    void installingChanged();
    void latestIsBetaChanged();
    void downloadReadyChanged();
    void canDownloadUpdateChanged();
    void autoRelaunchPermissionGrantedChanged();

    /// Emitted on the main thread immediately before installApk() invokes the
    /// Android PackageInstaller JNI dispatch. Listeners should synchronously
    /// shut down anything that owns a long-lived QSocketNotifier — Qt's UNIX
    /// event dispatcher SIGSEGVs in QSocketNotifier::setEnabled when Android
    /// reaps fds out from under us during the install handover (#865).
    void aboutToDispatchInstall();

public slots:
#ifdef Q_OS_ANDROID
    // Called (on the Qt main thread) from the static JNI bridge in
    // updatechecker.cpp when the Java PackageInstaller session reports a
    // terminal status or an internal create/write failure.
    void onInstallStatus(int status, const QString& message);
#endif

private slots:
    void onReleaseInfoReceived();
    void onDownloadProgress(qint64 received, qint64 total);
    void onDownloadFinished();
    void onPeriodicCheck();

private:
    void parseReleaseInfo(const QByteArray& data);
    void startDownload();
    bool installApk(const QString& apkPath);
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
    bool m_installInFlight = false;  // True between installApk() dispatch and terminal PackageInstaller status
    int m_contentLengthRetries = 0;  // Attempts so far waiting for a response with Content-Length
    bool m_contentLengthConfirmed = false;  // True once a response with Content-Length has been seen

    // Auto-relaunch state.
    // m_currentLaunchWasAutoRelaunch: set once in the constructor from the
    //   launching Activity's Intent extras, never mutated after that.
    // m_autoRelaunchPermissionGranted: cached result of Settings.canDrawOverlays();
    //   updated by refreshAutoRelaunchPermission().
    // Both default to false on non-Android platforms.
    bool m_currentLaunchWasAutoRelaunch = false;
    bool m_autoRelaunchPermissionGranted = false;

#ifdef Q_OS_ANDROID
    // Called from the constructor on Android. Reads the flag file written by
    // UpdateRelaunchReceiver (if present), updates SettingsApp diagnostic state,
    // then deletes the file. Reads the launching Activity's Intent extras to
    // determine whether THIS launch came through the auto-relaunch path and
    // sets m_currentLaunchWasAutoRelaunch accordingly.
    void readAutoRelaunchDiagnostic();
#endif
    // Generation counter lives at file scope in updatechecker.cpp so background
    // QFile::remove threads don't capture `this` (see s_downloadGeneration).

    static const QString GITHUB_API_URL;
    static const QString GITHUB_REPO;
};
