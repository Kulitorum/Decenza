#include "updatechecker.h"
#include "settings.h"
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
#include <QCoreApplication>
#include <unistd.h>  // fsync
#include <cerrno>    // errno, strerror
#endif

const QString UpdateChecker::GITHUB_API_URL = "https://api.github.com/repos/%1/releases?per_page=10";
const QString UpdateChecker::GITHUB_REPO = "Kulitorum/Decenza";

UpdateChecker::UpdateChecker(Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_network(new QNetworkAccessManager(this))
    , m_periodicTimer(new QTimer(this))
{
    // Check every hour
    m_periodicTimer->setInterval(60 * 60 * 1000);  // 1 hour
    connect(m_periodicTimer, &QTimer::timeout, this, &UpdateChecker::onPeriodicCheck);

    // Start periodic checks if enabled (not on iOS - App Store handles updates)
#if !defined(Q_OS_IOS)
    if (m_settings->autoCheckUpdates()) {
        m_periodicTimer->start();
        // Check shortly after startup (30 seconds delay)
        QTimer::singleShot(30000, this, &UpdateChecker::onPeriodicCheck);
    }
#endif

    connect(m_settings, &Settings::betaUpdatesEnabledChanged, this, [this]() {
        // Re-check when beta preference changes
        checkForUpdates();
    });

    connect(m_settings, &Settings::autoCheckUpdatesChanged, this, [this]() {
#if !defined(Q_OS_IOS)
        if (m_settings->autoCheckUpdates()) {
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
    request.setHeader(QNetworkRequest::UserAgentHeader, "Decenza-DE1");
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
        emit errorMessageChanged();
        return;
    }

    // Find the best release: if beta enabled, take the first (newest) release;
    // otherwise skip prereleases and take the first stable release.
    bool includeBeta = m_settings->betaUpdatesEnabled();
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
        // PackageInstaller may still be reading it via FileProvider. The cache
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
        // Fallback: extract from APK filename pattern (Decenza_DE1_X.Y.Z.apk where Z might be build)
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
    if (m_downloading || m_checking) return;
    if (m_downloadUrl.isEmpty()) {
        m_errorMessage = "No download available for this platform";
        qWarning() << "UpdateChecker:" << m_errorMessage;
        emit errorMessageChanged();
        return;
    }

    // If we already downloaded this version's APK and it's the right size, skip
    // straight to install. This handles the case where Android's "Install Unknown
    // Apps" permission flow consumed the first install intent.
    if (!m_downloadedApkPath.isEmpty() && QFileInfo::exists(m_downloadedApkPath)
        && m_expectedDownloadSize > 0
        && QFileInfo(m_downloadedApkPath).size() == m_expectedDownloadSize) {
        qDebug() << "UpdateChecker: APK already downloaded, installing directly:" << m_downloadedApkPath;
        m_errorMessage.clear();
        emit errorMessageChanged();
        emit installationStarted();
        installApk(m_downloadedApkPath);
        return;
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

    QString filename = QString("Decenza_DE1_%1.apk").arg(m_latestVersion);
    QString fullPath = savePath + "/" + filename;

    // Remove existing file (may still be held by PackageInstaller)
    if (QFile::exists(fullPath) && !QFile::remove(fullPath)) {
        qWarning() << "UpdateChecker: Could not remove existing file:" << fullPath;
    }

    m_downloadFile = new QFile(fullPath);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        m_errorMessage = "Failed to create download file: " + m_downloadFile->errorString();
        m_downloading = false;
        emit downloadingChanged();
        emit errorMessageChanged();
        delete m_downloadFile;
        m_downloadFile = nullptr;
        return;
    }

    qDebug() << "UpdateChecker: Downloading" << m_downloadUrl << "to" << fullPath;

    QNetworkRequest request(m_downloadUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Decenza-DE1");
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
        m_errorMessage = "Download failed unexpectedly";
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
            m_downloadFile->close();
            QFile::remove(m_downloadFile->fileName());
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

    // Flush Qt's userspace buffer, then call fsync to commit the kernel
    // page-cache to persistent storage before launching the install intent.
    // Without this, PackageInstaller can open a truncated APK.
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
    m_downloadFile->close();

    if (m_currentReply->error() != QNetworkReply::NoError) {
        // Preserve specific error set by readyRead write failure (otherwise
        // abort() produces a generic "Operation canceled" message)
        if (m_errorMessage.isEmpty()) {
            m_errorMessage = "Download failed: " + m_currentReply->errorString();
        }
        emit errorMessageChanged();
        QFile::remove(filePath);
        delete m_downloadFile;
        m_downloadFile = nullptr;
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
        m_downloading = false;
        emit downloadingChanged();
        return;
    }

    // Verify download is complete (not truncated by dropped connection)
    qint64 expectedSize = m_currentReply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    qint64 actualSize = QFileInfo(filePath).size();
    if (expectedSize > 0 && actualSize < expectedSize) {
        m_errorMessage = QString("Download incomplete: got %1 of %2 bytes")
                             .arg(actualSize).arg(expectedSize);
        qWarning() << "UpdateChecker:" << m_errorMessage;
        emit errorMessageChanged();
        QFile::remove(filePath);
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
        QFile::remove(filePath);
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

    // If flush or fsync failed, the file may not be fully committed to storage.
    // Cache it so the user can retry via the Install button (data may settle
    // after a brief delay), but don't auto-launch the intent now.
    if (!flushOk || !syncOk) {
        m_errorMessage = "Download completed but file sync failed. Please tap Install to retry.";
        emit errorMessageChanged();
        return;
    }

    // Install the APK
    emit installationStarted();
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

    // Delete the cached APK on dismiss. Unlike version-change invalidation
    // (which leaves the file for PackageInstaller), dismiss is an explicit
    // user action so the file is no longer needed.
    if (!m_downloadedApkPath.isEmpty()) {
        if (!QFile::remove(m_downloadedApkPath)) {
            qWarning() << "UpdateChecker: Failed to remove cached APK:" << m_downloadedApkPath;
        }
        m_downloadedApkPath.clear();
        m_expectedDownloadSize = 0;
        emit downloadReadyChanged();
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
    request.setHeader(QNetworkRequest::UserAgentHeader, "Decenza-DE1");
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

void UpdateChecker::installApk(const QString& apkPath)
{
#ifdef Q_OS_ANDROID
    qDebug() << "UpdateChecker: Installing APK:" << apkPath;

    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;");

    if (!activity.isValid()) {
        qWarning() << "Failed to get Android activity";
        m_errorMessage = "Failed to get Android activity";
        emit errorMessageChanged();
        return;
    }

    QJniObject context = activity.callObjectMethod(
        "getApplicationContext",
        "()Landroid/content/Context;");

    if (!context.isValid()) {
        qWarning() << "UpdateChecker: Failed to get application context";
        m_errorMessage = "Failed to prepare APK for installation (no app context)";
        emit errorMessageChanged();
        return;
    }

    // Create file URI using FileProvider for Android 7+
    QJniObject javaPath = QJniObject::fromString(apkPath);
    QJniObject file = QJniObject("java/io/File", "(Ljava/lang/String;)V",
                                  javaPath.object<jstring>());

    // Get package name for FileProvider authority
    QJniObject packageName = context.callObjectMethod(
        "getPackageName",
        "()Ljava/lang/String;");
    QString authority = packageName.toString() + ".fileprovider";

    QJniObject uri = QJniObject::callStaticObjectMethod(
        "androidx/core/content/FileProvider",
        "getUriForFile",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/io/File;)Landroid/net/Uri;",
        context.object(),
        QJniObject::fromString(authority).object<jstring>(),
        file.object());

    // Clear any JNI exception from getUriForFile (e.g. IllegalArgumentException
    // from FileProvider misconfiguration) before checking validity
    {
        QJniEnvironment env;
        if (env.checkAndClearExceptions()) {
            qWarning() << "UpdateChecker: FileProvider.getUriForFile threw a JNI exception";
        }
    }

    if (!uri.isValid()) {
        qWarning() << "Failed to create content URI for APK";
        m_errorMessage = "Failed to prepare APK for installation";
        emit errorMessageChanged();
        return;
    }

    // Create install intent
    QJniObject intent("android/content/Intent");
    QJniObject actionView = QJniObject::fromString("android.intent.action.VIEW");
    intent.callObjectMethod("setAction",
                            "(Ljava/lang/String;)Landroid/content/Intent;",
                            actionView.object<jstring>());

    QJniObject mimeType = QJniObject::fromString("application/vnd.android.package-archive");
    intent.callObjectMethod("setDataAndType",
                            "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;",
                            uri.object(),
                            mimeType.object<jstring>());

    // Add flags
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", 0x00000001); // FLAG_GRANT_READ_URI_PERMISSION
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", 0x10000000); // FLAG_ACTIVITY_NEW_TASK

    // Start activity — can throw ActivityNotFoundException (no handler),
    // SecurityException (missing REQUEST_INSTALL_PACKAGES), etc.
    activity.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());

    {
        QJniEnvironment env;
        if (env.checkAndClearExceptions()) {
            qWarning() << "UpdateChecker: startActivity threw a JNI exception";
            m_errorMessage = "Could not launch install dialog. Please enable "
                             "'Install Unknown Apps' for Decenza in Android Settings, then try again.";
            emit errorMessageChanged();
            return;
        }
    }

    qDebug() << "UpdateChecker: APK install intent launched";
#else
    qDebug() << "UpdateChecker: APK installation only supported on Android. File saved to:" << apkPath;
    m_errorMessage = "APK installation only supported on Android";
    emit errorMessageChanged();
#endif
}

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
