#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QFile>
#include <QFileInfo>

class Settings;
class TranslationManager;

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

    // True at the teachable moment: this startup followed a self-update where
    // the receiver fired but the activity launch was BAL-blocked because SAW
    // wasn't granted, AND we haven't shown the one-time prompt yet. QML binds
    // a Dialog to this. Either button on the Dialog calls
    // dismissAutoRelaunchPrompt() which clears this for good.
    Q_PROPERTY(bool shouldShowAutoRelaunchPrompt READ shouldShowAutoRelaunchPrompt
               NOTIFY shouldShowAutoRelaunchPromptChanged)

public:
    explicit UpdateChecker(QNetworkAccessManager* networkManager, Settings* settings, QObject* parent = nullptr);
    ~UpdateChecker();

    // Inject the TranslationManager so user-visible error messages localize
    // (mirrors VisualizerImporter/VisualizerUploader). Wired from
    // MainController::setTranslationManager. Until injected, tr_() returns the
    // English fallback.
    void setTranslationManager(TranslationManager* tm) { m_translationManager = tm; }

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
    bool shouldShowAutoRelaunchPrompt() const;

    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void openReleasePage();
    Q_INVOKABLE void downloadAndInstall();
    Q_INVOKABLE void dismissUpdate();

    /// Re-queries Settings.canDrawOverlays() and emits the change notification
    /// if it differs from the cached value. Cheap to call repeatedly; bound
    /// from QML on app-resume so shouldShowAutoRelaunchPrompt stays accurate
    /// after the user returns from Android system Settings.
    Q_INVOKABLE void refreshAutoRelaunchPermission();

    /// Opens Android Settings → "Display over other apps" (Samsung One UI:
    /// "Appear on top") deeplinked to the Decenza package. No-op on
    /// non-Android. Called by the one-time auto-relaunch prompt's "Open
    /// Settings" button.
    Q_INVOKABLE void requestAutoRelaunchPermission();

    /// Marks the one-time prompt as shown so shouldShowAutoRelaunchPrompt
    /// returns false from now on. Called by both the "Open Settings" and
    /// "Not now" buttons of the prompt — either way, the user has been
    /// asked, so we don't ask again.
    Q_INVOKABLE void dismissAutoRelaunchPrompt();

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
    void shouldShowAutoRelaunchPromptChanged();

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
    // The GitHub releases request, shared by the manual check and the hourly
    // poll so both carry the same headers and connection policy.
    QNetworkRequest releaseInfoRequest() const;

    void parseReleaseInfo(const QByteArray& data);
    void startDownload();
    bool installApk(const QString& apkPath);
    int extractBuildNumber(const QString& version) const;
    bool isNewerVersion(const QString& latest, const QString& current) const;

    // Translate a user-visible string via the injected TranslationManager,
    // falling back to the English source when none is set.
    QString tr_(const char* key, const char* fallback) const;

    Settings* m_settings = nullptr;
    TranslationManager* m_translationManager = nullptr;
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
    // m_receiverFiredOnThisStartup: set in readAutoRelaunchDiagnostic() if the
    //   diagnostic flag file was present this startup. Session-only signal —
    //   distinguishes "receiver fired moments ago" from "lastAutoRelaunchAt
    //   is a stale persisted timestamp from sessions past." Used by
    //   shouldShowAutoRelaunchPrompt() to scope the one-time prompt to the
    //   teachable moment.
    // All default to false on non-Android platforms.
    bool m_currentLaunchWasAutoRelaunch = false;
    bool m_autoRelaunchPermissionGranted = false;
    bool m_receiverFiredOnThisStartup = false;

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

#ifdef DECENZA_TESTING
    friend class tst_UpdateChecker;
#endif
};
