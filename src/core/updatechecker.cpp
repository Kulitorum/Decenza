#include "updatechecker.h"
#include "crashhandler.h"
#include "settings.h"
#include "settings_app.h"
#include "version.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QGuiApplication>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#include <QPointer>
#include <jni.h>
#include <unistd.h>  // fsync
#include <cerrno>    // errno, strerror
#endif

#include <QAtomicInt>
#include <QThread>

const QString UpdateChecker::GITHUB_API_URL = "https://api.github.com/repos/%1/releases?per_page=10";
const QString UpdateChecker::GITHUB_REPO = "Kulitorum/Decenza";

// File-scope so background QFile::remove threads can read it without capturing
// `this`. Bumped by startDownload() after a successful QFile::open() — any
// pending cleanup lambda for the now-superseded file sees an advanced generation
// and skips the delete. Every background QFile::remove in this file follows the
// pattern: capture the current generation on the main thread before spawning
// the thread, then compare against the current value inside the lambda.
static QAtomicInt s_downloadGeneration{0};

#ifdef Q_OS_ANDROID
namespace {
// Weak pointer to the active UpdateChecker so the JNI callback can route
// async install status back to it. Only one UpdateChecker exists at a time.
QPointer<UpdateChecker> s_activeChecker;

// JNI bridge invoked from ApkInstaller's worker thread / BroadcastReceiver.
// Must hop to the Qt main thread because it touches QObject state + emits
// signals that drive QML property updates.
void installerNativeOnInstallStatus(JNIEnv* env, jclass, jint status, jstring messageJ)
{
    QString message;
    if (messageJ) {
        const char* chars = env->GetStringUTFChars(messageJ, nullptr);
        if (chars) {
            message = QString::fromUtf8(chars);
            env->ReleaseStringUTFChars(messageJ, chars);
        }
    }
    const int s = static_cast<int>(status);
    QMetaObject::invokeMethod(qApp, [s, message]() {
        if (auto* checker = s_activeChecker.data()) {
            checker->onInstallStatus(s, message);
        }
    }, Qt::QueuedConnection);
}

bool s_nativeRegistrationFailed = false;

void registerInstallerNativeMethods()
{
    static bool registered = false;
    if (registered) return;
    QJniEnvironment env;
    const JNINativeMethod methods[] = {
        {"nativeOnInstallStatus",
         "(ILjava/lang/String;)V",
         reinterpret_cast<void*>(installerNativeOnInstallStatus)},
    };
    if (!env.registerNativeMethods(
            "io/github/kulitorum/decenza_de1/ApkInstaller", methods, 1)) {
        qWarning() << "UpdateChecker: failed to register native methods on ApkInstaller";
        s_nativeRegistrationFailed = true;
        registered = true;
        return;
    }
    registered = true;
    // Mark native as registered so ShotServer can query isNativeRegistered() via JNI.
    QJniObject::callStaticMethod<void>(
        "io/github/kulitorum/decenza_de1/ApkInstaller", "onNativeRegistered", "()V");
}
}  // namespace
#endif

UpdateChecker::UpdateChecker(QNetworkAccessManager* networkManager, Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_network(networkManager)
    , m_periodicTimer(new QTimer(this))
{
    Q_ASSERT(networkManager);
#ifdef Q_OS_ANDROID
    registerInstallerNativeMethods();
    s_activeChecker = this;
#endif
    // Check every hour
    m_periodicTimer->setInterval(60 * 60 * 1000);  // 1 hour
    connect(m_periodicTimer, &QTimer::timeout, this, &UpdateChecker::onPeriodicCheck);

    // Start periodic checks if enabled (not on iOS - App Store handles updates)
#if !defined(Q_OS_IOS)
    if (m_settings->app()->autoCheckUpdates()) {
        m_periodicTimer->start();
        // Check shortly after startup (30 seconds delay)
        QTimer::singleShot(30000, this, &UpdateChecker::onPeriodicCheck);
    }
#endif

    connect(m_settings->app(), &SettingsApp::betaUpdatesEnabledChanged, this, [this]() {
        // Re-check when beta preference changes
        checkForUpdates();
    });

    connect(m_settings->app(), &SettingsApp::autoCheckUpdatesChanged, this, [this]() {
#if !defined(Q_OS_IOS)
        if (m_settings->app()->autoCheckUpdates()) {
            m_periodicTimer->start();
        } else {
            m_periodicTimer->stop();
        }
#endif
    });
}

UpdateChecker::~UpdateChecker()
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
    }
    if (m_downloadFile) {
        m_downloadFile->close();
        delete m_downloadFile;
    }
}

QString UpdateChecker::currentVersion() const
{
    return VERSION_STRING;
}

int UpdateChecker::currentVersionCode() const
{
    return versionCode();
}

void UpdateChecker::checkForUpdates()
{
#if defined(Q_OS_IOS)
    // iOS updates come from App Store only
    m_errorMessage = "Updates are handled by the App Store";
    emit errorMessageChanged();
    return;
#endif

    if (m_checking || m_downloading) return;

    m_checking = true;
    m_errorMessage.clear();
    emit checkingChanged();
    emit errorMessageChanged();

    QUrl url(GITHUB_API_URL.arg(GITHUB_REPO));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Decenza");
    request.setRawHeader("Accept", "application/vnd.github.v3+json");

    m_currentReply = m_network->get(request);
    connect(m_currentReply, &QNetworkReply::finished, this, &UpdateChecker::onReleaseInfoReceived);
}

void UpdateChecker::onReleaseInfoReceived()
{
    m_checking = false;
    emit checkingChanged();

    if (!m_currentReply) return;

    if (m_currentReply->error() != QNetworkReply::NoError) {
        m_errorMessage = "Failed to check for updates: " + m_currentReply->errorString();
        emit errorMessageChanged();
        qWarning() << "UpdateChecker:" << m_errorMessage;
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
        return;
    }

    QByteArray data = m_currentReply->readAll();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;

    parseReleaseInfo(data);
}

void UpdateChecker::parseReleaseInfo(const QByteArray& data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        m_errorMessage = "Invalid response from GitHub";
        qWarning() << "UpdateChecker:" << m_errorMessage << "- response:" << data.left(200);
        emit errorMessageChanged();
        return;
    }

    // Find the best release: if beta enabled, take the first (newest) release;
    // otherwise skip prereleases and take the first stable release.
    bool includeBeta = m_settings->app()->betaUpdatesEnabled();
    QJsonArray releases = doc.array();
    QJsonObject release;
    bool found = false;

    for (const QJsonValue& val : releases) {
        QJsonObject rel = val.toObject();
        if (rel["draft"].toBool()) continue;
        if (!includeBeta && rel["prerelease"].toBool()) continue;
        release = rel;
        found = true;
        break;
    }

    if (!found) {
        m_errorMessage = "No releases found";
        emit errorMessageChanged();
        return;
    }

    QString tagName = release["tag_name"].toString();
    QString body = release["body"].toString();
    bool wasBeta = m_latestIsBeta;
    m_latestIsBeta = release["prerelease"].toBool();

    // Reset prompt flag when a new release is discovered so the user gets
    // notified once for each new version (but not repeatedly for the same one)
    if (m_releaseTag != tagName) {
        m_updatePromptShown = false;
        // Invalidate cached APK from previous version. Don't delete the file —
        // a PackageInstaller session may still be streaming from it. The cache
        // directory is cleaned up by the OS.
        if (!m_downloadedApkPath.isEmpty()) {
            m_downloadedApkPath.clear();
            m_expectedDownloadSize = 0;
            emit downloadReadyChanged();
        }
    }
    m_releaseTag = tagName;
    m_latestVersion = tagName.startsWith("v") ? tagName.mid(1) : tagName;
    m_releaseNotes = body;

    // Extract build number from release notes (look for "Build: XXXX" or "Build XXXX")
    QRegularExpression buildRe(R"(Build[:\s]+(\d+))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch buildMatch = buildRe.match(body);
    if (buildMatch.hasMatch()) {
        m_latestBuildNumber = buildMatch.captured(1).toInt();
    } else {
        // Fallback: extract from APK filename pattern (Decenza_X.Y.Z.apk where Z might be build)
        m_latestBuildNumber = extractBuildNumber(tagName);
    }

    emit latestVersionChanged();
    emit latestVersionCodeChanged();
    emit releaseNotesChanged();
    if (m_latestIsBeta != wasBeta) {
        emit latestIsBetaChanged();
    }

    // Find platform-appropriate asset
    QJsonArray assets = release["assets"].toArray();
    const QString previousDownloadUrl = m_downloadUrl;
    m_downloadUrl.clear();
    for (const QJsonValue& assetVal : assets) {
        QJsonObject asset = assetVal.toObject();
        QString name = asset["name"].toString();
#if defined(Q_OS_ANDROID)
        if (name.endsWith(".apk", Qt::CaseInsensitive)) {
            m_downloadUrl = asset["browser_download_url"].toString();
            break;
        }
#elif defined(Q_OS_MACOS)
        if (name.endsWith(".dmg", Qt::CaseInsensitive) ||
            (name.endsWith(".zip", Qt::CaseInsensitive) && name.contains("macos", Qt::CaseInsensitive))) {
            m_downloadUrl = asset["browser_download_url"].toString();
            break;
        }
#endif
        // iOS doesn't download from GitHub - updates come from App Store
    }

#if defined(Q_OS_ANDROID) || defined(Q_OS_MACOS)
    if (m_downloadUrl.isEmpty()) {
        qWarning() << "UpdateChecker: release" << m_releaseTag
                   << "found but no platform asset available";
    }
#endif

    // Check if update is available using display version comparison,
    // falling back to build number if versions are equal
    bool newer = isNewerVersion(m_latestVersion, currentVersion());
    if (!newer && !isNewerVersion(currentVersion(), m_latestVersion) && m_latestBuildNumber > 0) {
        // Same display version — compare build numbers (strictly greater)
        newer = m_latestBuildNumber > currentVersionCode();
    }

    qDebug() << "UpdateChecker: current=" << currentVersion() << "build=" << currentVersionCode()
             << "latest=" << m_latestVersion << "latestBuild=" << m_latestBuildNumber
             << "newer=" << newer << "tag=" << m_releaseTag;

    bool wasAvailable = m_updateAvailable;
#if defined(Q_OS_IOS)
    m_updateAvailable = newer;
#elif defined(Q_OS_ANDROID)
    m_updateAvailable = newer && !m_downloadUrl.isEmpty();
#else
    m_updateAvailable = newer;
#endif

    if (m_updateAvailable != wasAvailable) {
        emit updateAvailableChanged();
    }

    // Clear any cached APK whenever no update is available and no operation is
    // in flight, so a stale path can't be installed accidentally. Runs
    // unconditionally (not only on transition) to cover the case where the path
    // was set before a re-check that finds the running version already current.
    // The file is left on disk for OS cleanup — a PackageInstaller session may
    // still hold it open. (dismissUpdate() also clears the path but additionally
    // removes the file; onInstallStatus() mirrors that behaviour. All three sites
    // clear m_downloadedApkPath and emit downloadReadyChanged — keep in sync.)
    if (!m_updateAvailable && !m_downloading && !m_installInFlight && !m_downloadedApkPath.isEmpty()) {
        m_downloadedApkPath.clear();
        m_expectedDownloadSize = 0;
        emit downloadReadyChanged();
    }
    // canDownloadUpdate is derived from m_downloadUrl on desktop; fire when it
    // changes so QML re-evaluates the download-button visibility binding. On
    // Android/iOS the return value is platform-constant (always true / always
    // false), so the signal fires but QML bindings see no effective change.
    if (m_downloadUrl != previousDownloadUrl) {
        emit canDownloadUpdateChanged();
    }

    // When no update is available and the user is running a different version
    // than the selected release (e.g., on a beta with beta updates disabled),
    // find and show the current version's release notes instead.
    if (!m_updateAvailable && m_latestVersion != currentVersion()) {
        QString currentTag = "v" + currentVersion();
        for (const QJsonValue& val : releases) {
            QJsonObject rel = val.toObject();
            if (rel["draft"].toBool()) continue;
            if (rel["tag_name"].toString() == currentTag) {
                m_releaseNotes = rel["body"].toString();
                emit releaseNotesChanged();
                break;
            }
        }
    }
}

int UpdateChecker::extractBuildNumber(const QString& version) const
{
    // Match patterns like "v1.0.123" or "1.0.123"
    QRegularExpression re(R"((\d+)\.(\d+)\.(\d+))");
    QRegularExpressionMatch match = re.match(version);
    if (match.hasMatch()) {
        return match.captured(3).toInt();
    }
    return 0;
}

bool UpdateChecker::isNewerVersion(const QString& latest, const QString& current) const
{
    // Compare versions like "1.1.1" vs "1.0.1054"
    QRegularExpression re(R"((\d+)\.(\d+)\.(\d+))");
    QRegularExpressionMatch latestMatch = re.match(latest);
    QRegularExpressionMatch currentMatch = re.match(current);

    if (!latestMatch.hasMatch() || !currentMatch.hasMatch()) {
        return false;
    }

    int latestMajor = latestMatch.captured(1).toInt();
    int latestMinor = latestMatch.captured(2).toInt();
    int latestBuild = latestMatch.captured(3).toInt();

    int currentMajor = currentMatch.captured(1).toInt();
    int currentMinor = currentMatch.captured(2).toInt();
    int currentBuild = currentMatch.captured(3).toInt();

    // Compare major.minor.build
    if (latestMajor != currentMajor) return latestMajor > currentMajor;
    if (latestMinor != currentMinor) return latestMinor > currentMinor;
    return latestBuild > currentBuild;
}

void UpdateChecker::downloadAndInstall()
{
    if (!m_updateAvailable) {
        qWarning() << "UpdateChecker: downloadAndInstall called but no update available (current:"
                   << currentVersion() << "latest:" << m_latestVersion
                   << "downloadUrl:" << (m_downloadUrl.isEmpty() ? "<empty>" : m_downloadUrl) << ")";
        m_errorMessage = "No update available";
        emit errorMessageChanged();
        return;
    }
    if (m_downloading || m_checking) return;
    if (m_installInFlight) {
        m_errorMessage = "Install already in progress. If no confirmation dialog is visible, "
                         "restart the app and try again.";
        emit errorMessageChanged();
        return;
    }
#ifdef Q_OS_ANDROID
    // ShotServer can initiate installs that we don't track in m_installInFlight;
    // check the Java-level flag so we don't waste a ~146 MB download only to be
    // rejected by ApkInstaller.install() at the end.
    if (QJniObject::callStaticMethod<jboolean>(
            "io/github/kulitorum/decenza_de1/ApkInstaller", "isInFlight", "()Z") == JNI_TRUE) {
        m_errorMessage = "Install already in progress. If no confirmation dialog is visible, "
                         "restart the app and try again.";
        emit errorMessageChanged();
        return;
    }
#endif
    if (m_downloadUrl.isEmpty()) {
        m_errorMessage = "No download available for this platform";
        qWarning() << "UpdateChecker:" << m_errorMessage;
        emit errorMessageChanged();
        return;
    }

    // If a prior run of this version's APK download finished, skip straight to
    // install. m_expectedDownloadSize > 0 is a sentinel indicating a completed
    // download (not a size comparison — we no longer stat the file from the
    // main thread). Handles the case where a prior attempt didn't reach the
    // install step (e.g., Android's "Install Unknown Apps" permission redirect).
    if (!m_downloadedApkPath.isEmpty() && m_expectedDownloadSize > 0) {
        qDebug() << "UpdateChecker: APK already downloaded, installing directly:" << m_downloadedApkPath;
        m_errorMessage.clear();
        emit errorMessageChanged();
        if (installApk(m_downloadedApkPath))
            return;
        // installApk() failed — Java detected a missing/invalid APK (typically
        // cache eviction) and returned false without a status callback. Clear
        // the stale error set by installApk() and fall through to re-download.
        qWarning() << "UpdateChecker: cached APK install failed, forcing re-download";
        m_errorMessage.clear();
        emit errorMessageChanged();
    }

    m_downloadedApkPath.clear();
    m_expectedDownloadSize = 0;
    emit downloadReadyChanged();

    m_downloading = true;
    m_downloadProgress = 0;
    m_errorMessage.clear();
    emit downloadingChanged();
    emit downloadProgressChanged();
    emit errorMessageChanged();

    startDownload();
}

void UpdateChecker::startDownload()
{
    // Prepare download path
    QString savePath;
#ifdef Q_OS_ANDROID
    savePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
#else
    savePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
#endif
    if (!QDir().mkpath(savePath)) {
        m_errorMessage = "Failed to create download directory: " + savePath;
        qWarning() << "UpdateChecker:" << m_errorMessage;
        m_downloading = false;
        emit downloadingChanged();
        emit errorMessageChanged();
        return;
    }

    QString filename = QString("Decenza_%1.apk").arg(m_latestVersion);
    QString fullPath = savePath + "/" + filename;

    m_downloadFile = new QFile(fullPath);
    if (!m_downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_errorMessage = "Failed to create download file: " + m_downloadFile->errorString();
        m_downloading = false;
        emit downloadingChanged();
        emit errorMessageChanged();
        delete m_downloadFile;
        m_downloadFile = nullptr;
        return;
    }
    // Bump after successful open so any pending background remove for the old
    // file skips deletion. Bumping before open() would "phantom bump" on open
    // failure, causing in-flight dismiss cleanups from a prior download to skip.
    s_downloadGeneration.fetchAndAddOrdered(1);

    qDebug() << "UpdateChecker: Downloading" << m_downloadUrl << "to" << fullPath;

    QNetworkRequest request(m_downloadUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Decenza");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_currentReply = m_network->get(request);
    connect(m_currentReply, &QNetworkReply::downloadProgress, this, &UpdateChecker::onDownloadProgress);
    connect(m_currentReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_downloadFile && m_currentReply) {
            QByteArray chunk = m_currentReply->readAll();
            if (m_downloadFile->write(chunk) != chunk.size()) {
                qWarning() << "UpdateChecker: Write failed during download:" << m_downloadFile->errorString();
                // Set the real error before aborting — abort() triggers onDownloadFinished
                // with OperationCanceledError, which would show a misleading message
                m_errorMessage = "Download failed: could not write file (" + m_downloadFile->errorString() + ")";
                m_currentReply->abort();
            }
        }
    });
    connect(m_currentReply, &QNetworkReply::finished, this, &UpdateChecker::onDownloadFinished);
}

void UpdateChecker::onDownloadProgress(qint64 received, qint64 total)
{
    if (total > 0) {
        m_downloadProgress = static_cast<int>((received * 100) / total);
        emit downloadProgressChanged();
    }
}

void UpdateChecker::onDownloadFinished()
{
    if (!m_currentReply || !m_downloadFile) {
        qWarning() << "UpdateChecker: onDownloadFinished called with null reply or file"
                    << "reply=" << m_currentReply << "file=" << m_downloadFile;
        m_errorMessage = "Download failed unexpectedly. Please try again.";
        emit errorMessageChanged();
        m_downloading = false;
        emit downloadingChanged();
        return;
    }

    // Flush any remaining buffered data not delivered via readyRead
    QByteArray remaining = m_currentReply->readAll();
    if (!remaining.isEmpty()) {
        if (m_downloadFile->write(remaining) != remaining.size()) {
            qWarning() << "UpdateChecker: Final write failed:" << m_downloadFile->errorString();
            m_errorMessage = "Download failed: could not write file (" + m_downloadFile->errorString() + ")";
            emit errorMessageChanged();
            const QString fileToRemove = m_downloadFile->fileName();
            const int capturedGen = s_downloadGeneration.loadAcquire();
            m_downloadFile->close();
            QThread* t = QThread::create([fileToRemove, capturedGen]() {
                if (s_downloadGeneration.loadAcquire() == capturedGen)
                    QFile::remove(fileToRemove);
            });
            connect(t, &QThread::finished, t, &QThread::deleteLater);
            t->start();
            delete m_downloadFile;
            m_downloadFile = nullptr;
            m_currentReply->deleteLater();
            m_currentReply = nullptr;
            m_downloading = false;
            emit downloadingChanged();
            return;
        }
    }

    QString filePath = m_downloadFile->fileName();

    // Best-effort flush: push Qt's userspace buffer to the kernel, then fsync
    // to nudge the kernel toward persistent storage before the PackageInstaller
    // session opens the file for reading. Failures are non-fatal — if the file
    // really is incomplete, PackageInstaller's verification will reject it.
    bool flushOk = m_downloadFile->flush();
    if (!flushOk) {
        qWarning() << "UpdateChecker: flush() failed:" << m_downloadFile->errorString();
    }
    bool syncOk = true;
#ifdef Q_OS_ANDROID
    int fd = m_downloadFile->handle();
    if (fd != -1) {
        if (::fsync(fd) != 0) {
            qWarning() << "UpdateChecker: fsync() failed:" << strerror(errno);
            syncOk = false;
        }
    } else {
        qWarning() << "UpdateChecker: file handle invalid, skipping fsync";
        syncOk = false;
    }
#endif
    // Capture the write position before close so we can validate download size
    // without calling QFileInfo(path).size() (a stat() syscall on the main
    // thread — CLAUDE.md prohibits disk I/O on the main thread).
    const qint64 actualSize = m_downloadFile->pos();
    m_downloadFile->close();

    if (m_currentReply->error() != QNetworkReply::NoError) {
        // Preserve specific error set by readyRead write failure (otherwise
        // abort() produces a generic "Operation canceled" message)
        if (m_errorMessage.isEmpty()) {
            m_errorMessage = "Download failed: " + m_currentReply->errorString();
        }
        emit errorMessageChanged();
        {
            const int capturedGen = s_downloadGeneration.loadAcquire();
            QThread* t = QThread::create([filePath, capturedGen]() {
                if (s_downloadGeneration.loadAcquire() == capturedGen)
                    QFile::remove(filePath);
            });
            connect(t, &QThread::finished, t, &QThread::deleteLater);
            t->start();
        }
        delete m_downloadFile;
        m_downloadFile = nullptr;
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
        m_downloading = false;
        emit downloadingChanged();
        return;
    }

    // Verify download is complete (not truncated by dropped connection).
    // actualSize was captured from m_downloadFile->pos() before close() above.
    qint64 expectedSize = m_currentReply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    if (expectedSize > 0 && actualSize < expectedSize) {
        m_errorMessage = QString("Download incomplete: got %1 of %2 bytes")
                             .arg(actualSize).arg(expectedSize);
        qWarning() << "UpdateChecker:" << m_errorMessage;
        emit errorMessageChanged();
        {
            const int capturedGen = s_downloadGeneration.loadAcquire();
            QThread* t = QThread::create([filePath, capturedGen]() {
                if (s_downloadGeneration.loadAcquire() == capturedGen)
                    QFile::remove(filePath);
            });
            connect(t, &QThread::finished, t, &QThread::deleteLater);
            t->start();
        }
        delete m_downloadFile;
        m_downloadFile = nullptr;
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
        m_downloading = false;
        emit downloadingChanged();
        return;
    }

    // APKs are always at least 1 MB; a smaller file likely indicates an error
    // page from a failed redirect or severe truncation
    const qint64 MIN_APK_SIZE = 1024 * 1024;
    if (actualSize < MIN_APK_SIZE) {
        m_errorMessage = QString("Downloaded file too small (%1 bytes) — download may have failed")
                             .arg(actualSize);
        qWarning() << "UpdateChecker:" << m_errorMessage;
        emit errorMessageChanged();
        {
            const int capturedGen = s_downloadGeneration.loadAcquire();
            QThread* t = QThread::create([filePath, capturedGen]() {
                if (s_downloadGeneration.loadAcquire() == capturedGen)
                    QFile::remove(filePath);
            });
            connect(t, &QThread::finished, t, &QThread::deleteLater);
            t->start();
        }
        delete m_downloadFile;
        m_downloadFile = nullptr;
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
        m_downloading = false;
        emit downloadingChanged();
        return;
    }

    qDebug() << "UpdateChecker: Download complete:" << filePath
             << "(" << actualSize << "bytes)";

    // Remember the downloaded APK and its expected size so we can retry install
    // without re-downloading (e.g. if Android's "Install Unknown Apps" permission
    // flow interrupts the first install attempt)
    m_downloadedApkPath = filePath;
    m_expectedDownloadSize = actualSize;

    delete m_downloadFile;
    m_downloadFile = nullptr;
    m_currentReply->deleteLater();
    m_currentReply = nullptr;

    // Emit downloadReady before downloadingChanged so that when the UI unhides
    // the button row, the button text is already correct ("Install" not "Download & Install")
    emit downloadReadyChanged();
    m_downloading = false;
    emit downloadingChanged();

    if (!flushOk || !syncOk) {
        qWarning() << "UpdateChecker: flush/fsync reported failure; proceeding with install anyway";
    }

    // Install the APK
    installApk(filePath);
}

void UpdateChecker::dismissUpdate()
{
    // m_updatePromptShown intentionally NOT reset here — user dismissed this
    // version, so don't re-prompt until a new version is discovered.
    // Contrast with screensaver hiding: goToScreensaver() re-queues the popup
    // via the QML pendingPopups queue, which bypasses this C++ flag entirely.
    m_updateAvailable = false;
    emit updateAvailableChanged();

    // Cancel any in-flight download so it does not proceed to install. Without
    // this, the QNetworkReply continues, onDownloadFinished() fires, and the
    // system install dialog appears despite the user explicitly dismissing.
    // Disconnect finished() first so the slot does not run during abort().
    if (m_downloading && m_currentReply) {
        QString partialPath;
        if (m_downloadFile) partialPath = m_downloadFile->fileName();
        disconnect(m_currentReply, &QNetworkReply::finished, this, &UpdateChecker::onDownloadFinished);
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
        if (m_downloadFile) {
            m_downloadFile->close();
            delete m_downloadFile;
            m_downloadFile = nullptr;
        }
        m_downloading = false;
        emit downloadingChanged();
        if (!partialPath.isEmpty()) {
            // Generation guard: if a new startDownload() (programmatic or via a
            // subsequent user action) reuses the same filename before this thread
            // runs, we must not delete the new download's freshly-opened file.
            const int capturedGen = s_downloadGeneration.loadAcquire();
            QThread* t = QThread::create([partialPath, capturedGen]() {
                if (s_downloadGeneration.loadAcquire() == capturedGen)
                    QFile::remove(partialPath);
            });
            connect(t, &QThread::finished, t, &QThread::deleteLater);
            t->start();
        }
    }

    // Dismiss is an explicit user action: clean up the cached APK unconditionally.
    // Because we also clear m_installInFlight below (to hide the spinner on OEM
    // ROMs that skip STATUS_FAILURE_ABORTED), any later terminal status callback
    // will be dropped by the early-return guard in onInstallStatus(). This is
    // therefore our only chance to clean up, so we can't defer to that path.
    //
    // Safety: on Android, unlinking an APK that a Java worker has open via
    // FileInputStream is safe — POSIX semantics keep the fd (and the file data)
    // valid until the last descriptor closes, so an in-flight session-write
    // completes even though the path has been removed from the cache directory.
    if (!m_downloadedApkPath.isEmpty()) {
        QString path = m_downloadedApkPath;
        const int capturedGen = s_downloadGeneration.loadAcquire();
        QThread* t = QThread::create([path, capturedGen]() {
            if (s_downloadGeneration.loadAcquire() == capturedGen && !QFile::remove(path))
                qWarning() << "UpdateChecker: Failed to remove cached APK:" << path;
        });
        connect(t, &QThread::finished, t, &QThread::deleteLater);
        t->start();
        m_downloadedApkPath.clear();
        m_expectedDownloadSize = 0;
        emit downloadReadyChanged();
    }

    // Always clear the installing flag on explicit dismiss. Some OEM ROMs skip
    // STATUS_FAILURE_ABORTED when the user back-dismisses the confirmation dialog,
    // leaving m_installInFlight stuck and the spinner permanently visible.
    if (m_installInFlight) {
        m_installInFlight = false;
        emit installingChanged();
        // Match: app keeps running, so resume normal crash reporting.
        CrashHandler::setSuppressCrashLog(false);
    }
}

void UpdateChecker::onPeriodicCheck()
{
    if (m_checking || m_downloading) return;

    // Don't check while app is suspended — attempting to show a popup while
    // the EGL surface is destroyed causes a deadlock between the accessibility
    // thread and the render thread on Android (see issue #178)
    if (QGuiApplication::applicationState() != Qt::ApplicationActive) return;

    m_checking = true;
    emit checkingChanged();

    qDebug() << "UpdateChecker: Periodic update check";

    // Check for updates silently
    QUrl url(GITHUB_API_URL.arg(GITHUB_REPO));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Decenza");
    request.setRawHeader("Accept", "application/vnd.github.v3+json");

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_checking = false;
        emit checkingChanged();

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            parseReleaseInfo(data);

            // If update found, emit signal for popup (once per new version)
            if (m_updateAvailable && !m_updatePromptShown) {
                m_updatePromptShown = true;
                emit updatePromptRequested();
            }
        } else {
            qDebug() << "UpdateChecker: Periodic check failed:" << reply->errorString();
        }
        reply->deleteLater();
    });
}

bool UpdateChecker::installApk(const QString& apkPath)
{
#ifdef Q_OS_ANDROID
    if (s_nativeRegistrationFailed) {
        m_errorMessage = "Install bridge failed to initialize. Please restart the app and try again.";
        emit errorMessageChanged();
        return false;
    }

    qDebug() << "UpdateChecker: Installing APK via PackageInstaller session:" << apkPath;

    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;");

    if (!activity.isValid()) {
        qWarning() << "UpdateChecker: Failed to get Android activity";
        m_errorMessage = "Failed to get Android activity";
        emit errorMessageChanged();
        return false;
    }

    QJniObject javaPath = QJniObject::fromString(apkPath);
    jboolean ok = QJniObject::callStaticMethod<jboolean>(
        "io/github/kulitorum/decenza_de1/ApkInstaller",
        "install",
        "(Landroid/app/Activity;Ljava/lang/String;)Z",
        activity.object(),
        javaPath.object<jstring>());

    {
        QJniEnvironment env;
        if (env.checkAndClearExceptions()) {
            qWarning() << "UpdateChecker: ApkInstaller.install threw a JNI exception";
            ok = JNI_FALSE;
        }
    }

    if (!ok) {
        // Distinguish: in-flight (stuck OEM flag or ShotServer session) vs. genuine failure.
        jboolean inFlight = QJniObject::callStaticMethod<jboolean>(
            "io/github/kulitorum/decenza_de1/ApkInstaller", "isInFlight", "()Z");
        if (inFlight == JNI_TRUE) {
            m_errorMessage = "Install already in progress. If no confirmation dialog is visible, "
                             "restart the app and try again.";
        } else {
            m_errorMessage = "Could not start install. If this is a first install, enable "
                             "'Install Unknown Apps' for Decenza in Android Settings, then try again.";
        }
        emit errorMessageChanged();
        return false;
    }

    m_installInFlight = true;
    emit installingChanged();
    // Suppress crash reporting from here on: Android's package-install handover
    // races against Qt's UNIX event dispatcher (it reaps fds out from under
    // QSocketNotifier and we SIGSEGV in QSocketNotifier::setEnabled). Cleared
    // again in onInstallStatus on any non-success terminal status. See #865.
    CrashHandler::setSuppressCrashLog(true);
    qDebug() << "UpdateChecker: PackageInstaller install dispatched (session write runs on worker thread)";
    return true;
#else
    qDebug() << "UpdateChecker: APK installation only supported on Android. File saved to:" << apkPath;
    m_errorMessage = "APK installation only supported on Android";
    emit errorMessageChanged();
    return false;
#endif
}

#ifdef Q_OS_ANDROID
void UpdateChecker::onInstallStatus(int status, const QString& message)
{
    // Mirror the Java sentinels in ApkInstaller.java — kept outside the
    // range of codes that can actually arrive here: STATUS_SUCCESS=0,
    // STATUS_FAILURE=1..STATUS_FAILURE_INCOMPATIBLE=7, STATUS_FAILURE_TIMEOUT=8 [API 34+].
    // STATUS_PENDING_USER_ACTION=-1 is handled entirely in Java (startActivity)
    // and is never forwarded here.
    constexpr int INTERNAL_STATUS_CREATE_FAILED    = -100;
    constexpr int INTERNAL_STATUS_WRITE_FAILED     = -101;
    constexpr int INTERNAL_STATUS_NO_CONFIRM_INTENT = -102;

    if (!m_installInFlight) {
        // Status from a ShotServer-triggered install or a stale session — not ours.
        qWarning() << "UpdateChecker: ignoring install status=" << status << "msg=" << message << "(no active install — originated from ShotServer or stale session)";
        return;
    }

    qDebug() << "UpdateChecker: install status=" << status << "message=" << message;

    // PackageInstaller codes: STATUS_SUCCESS=0, STATUS_FAILURE=1,
    // STATUS_FAILURE_BLOCKED=2, STATUS_FAILURE_ABORTED=3, STATUS_FAILURE_INVALID=4,
    // STATUS_FAILURE_CONFLICT=5, STATUS_FAILURE_STORAGE=6,
    // STATUS_FAILURE_INCOMPATIBLE=7, STATUS_FAILURE_TIMEOUT=8.
    QString userMessage;
    switch (status) {
        case 0:  // STATUS_SUCCESS — app is typically being killed for the upgrade.
            m_installInFlight = false;
            emit installingChanged();
            if (!m_downloadedApkPath.isEmpty()) {
                QString path = m_downloadedApkPath;
                m_downloadedApkPath.clear();
                m_expectedDownloadSize = 0;
                emit downloadReadyChanged();
                const int capturedGen = s_downloadGeneration.loadAcquire();
                QThread* t = QThread::create([path, capturedGen]() {
                    if (s_downloadGeneration.loadAcquire() == capturedGen)
                        QFile::remove(path);
                });
                connect(t, &QThread::finished, t, &QThread::deleteLater);
                t->start();
            }
            return;
        case 3:  // STATUS_FAILURE_ABORTED — user cancelled the confirmation.
            m_installInFlight = false;
            emit installingChanged();
            // App keeps running; resume normal crash reporting (see installApk).
            CrashHandler::setSuppressCrashLog(false);
            // Clear any stale error from a prior failed attempt so it doesn't
            // linger when the user merely cancelled the current attempt.
            if (!m_errorMessage.isEmpty()) {
                m_errorMessage.clear();
                emit errorMessageChanged();
            }
            // Safety net. After dismissUpdate() this block is unreachable:
            // dismissUpdate clears m_installInFlight (so we early-returned at
            // the top) and also clears m_downloadedApkPath (so the condition
            // below is false). The block is still reachable today via
            // parseReleaseInfo (called from onReleaseInfoReceived or onPeriodicCheck) setting m_updateAvailable
            // back to false when a later check finds no update, while the
            // previous version's m_downloadedApkPath is still set.
            if (!m_updateAvailable && !m_downloadedApkPath.isEmpty()) {
                QString path = m_downloadedApkPath;
                const int capturedGen = s_downloadGeneration.loadAcquire();
                QThread* t = QThread::create([path, capturedGen]() {
                    if (s_downloadGeneration.loadAcquire() == capturedGen)
                        QFile::remove(path);
                });
                connect(t, &QThread::finished, t, &QThread::deleteLater);
                t->start();
                m_downloadedApkPath.clear();
                m_expectedDownloadSize = 0;
                emit downloadReadyChanged();
            }
            return;
        case 2:  // STATUS_FAILURE_BLOCKED
            userMessage = "Install blocked by device policy.";
            break;
        case 4:  // STATUS_FAILURE_INVALID
            userMessage = "The downloaded APK is invalid. Please try again.";
            break;
        case 5:  // STATUS_FAILURE_CONFLICT
            userMessage = "Install conflicts with an existing app. Please uninstall and retry.";
            break;
        case 6:  // STATUS_FAILURE_STORAGE
            userMessage = "Not enough storage to install the update.";
            break;
        case 7:  // STATUS_FAILURE_INCOMPATIBLE
            userMessage = "Update is incompatible with this device.";
            break;
        case 8:  // STATUS_FAILURE_TIMEOUT
            userMessage = "Install timed out. Please try again.";
            break;
        case INTERNAL_STATUS_CREATE_FAILED:
            userMessage = "Could not start install session. Check that 'Install Unknown Apps' "
                          "is enabled for Decenza in Android Settings, then try again.";
            break;
        case INTERNAL_STATUS_WRITE_FAILED:
            userMessage = "Failed to write the update package. Please try again.";
            break;
        case INTERNAL_STATUS_NO_CONFIRM_INTENT:
            userMessage = "Install dialog could not be launched. Please try again.";
            break;
        default:  // STATUS_FAILURE (1) and anything unexpected.
            userMessage = "Install failed.";
            if (!message.isEmpty()) {
                userMessage += " (" + message + ")";
            }
            break;
    }

    m_installInFlight = false;
    emit installingChanged();
    // App keeps running on every non-success path; resume normal crash reporting.
    CrashHandler::setSuppressCrashLog(false);
    // Safety net. After dismissUpdate() this block is unreachable (dismiss
    // clears both m_installInFlight and m_downloadedApkPath). It can still
    // be reached today via parseReleaseInfo (called from onReleaseInfoReceived
    // or onPeriodicCheck) setting m_updateAvailable back to false when a later
    // check finds no update, while the previous version's m_downloadedApkPath
    // is still set.
    if (!m_updateAvailable && !m_downloadedApkPath.isEmpty()) {
        QString path = m_downloadedApkPath;
        const int capturedGen = s_downloadGeneration.loadAcquire();
        QThread* t = QThread::create([path, capturedGen]() {
            if (s_downloadGeneration.loadAcquire() == capturedGen)
                QFile::remove(path);
        });
        connect(t, &QThread::finished, t, &QThread::deleteLater);
        t->start();
        m_downloadedApkPath.clear();
        m_expectedDownloadSize = 0;
        emit downloadReadyChanged();
    }
    m_errorMessage = userMessage;
    emit errorMessageChanged();
}
#endif

bool UpdateChecker::canDownloadUpdate() const
{
#if defined(Q_OS_ANDROID)
    return true;  // Android can download and install APKs
#elif defined(Q_OS_IOS)
    return false;  // iOS updates via App Store only
#else
    return !m_downloadUrl.isEmpty();  // macOS can download if asset exists
#endif
}

bool UpdateChecker::canCheckForUpdates() const
{
#if defined(Q_OS_IOS)
    return false;  // iOS updates via App Store only
#else
    return true;
#endif
}

QString UpdateChecker::platformName() const
{
#if defined(Q_OS_ANDROID)
    return "Android";
#elif defined(Q_OS_IOS)
    return "iOS";
#elif defined(Q_OS_MACOS)
    return "macOS";
#elif defined(Q_OS_WIN)
    return "Windows";
#elif defined(Q_OS_LINUX)
    return "Linux";
#else
    return "Unknown";
#endif
}

QString UpdateChecker::releasePageUrl() const
{
    if (m_releaseTag.isEmpty()) {
        return QString("https://github.com/%1/releases/latest").arg(GITHUB_REPO);
    }
    return QString("https://github.com/%1/releases/tag/%2").arg(GITHUB_REPO, m_releaseTag);
}

void UpdateChecker::openReleasePage()
{
    QDesktopServices::openUrl(QUrl(releasePageUrl()));
}
