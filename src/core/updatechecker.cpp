#include "updatechecker.h"
#include "settings.h"
#include "version.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QRegularExpression>
#include <QDesktopServices>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
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
    return VERSION_CODE;
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

    // Check if update is available using display version comparison
    // On iOS/macOS, update can be available even without a direct download URL
    bool wasAvailable = m_updateAvailable;
#if defined(Q_OS_IOS)
    // iOS: just check version, user updates via App Store
    m_updateAvailable = isNewerVersion(m_latestVersion, currentVersion());
#elif defined(Q_OS_ANDROID)
    // Android: need APK to be available
    m_updateAvailable = isNewerVersion(m_latestVersion, currentVersion()) && !m_downloadUrl.isEmpty();
#else
    // macOS/other: update available even if no direct download (user can visit release page)
    m_updateAvailable = isNewerVersion(m_latestVersion, currentVersion());
#endif

    qDebug() << "UpdateChecker: Current version:" << currentVersion()
             << "Latest version:" << m_latestVersion
             << "Beta:" << m_latestIsBeta
             << "Update available:" << m_updateAvailable;

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
    if (m_downloading || m_downloadUrl.isEmpty()) return;

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
    QDir().mkpath(savePath);

    QString filename = QString("Decenza_DE1_%1.apk").arg(m_latestVersion);
    QString fullPath = savePath + "/" + filename;

    // Remove existing file
    QFile::remove(fullPath);

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
            m_downloadFile->write(m_currentReply->readAll());
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
    m_downloading = false;
    emit downloadingChanged();

    if (!m_currentReply || !m_downloadFile) return;

    QString filePath = m_downloadFile->fileName();
    m_downloadFile->close();

    if (m_currentReply->error() != QNetworkReply::NoError) {
        m_errorMessage = "Download failed: " + m_currentReply->errorString();
        emit errorMessageChanged();
        QFile::remove(filePath);
        delete m_downloadFile;
        m_downloadFile = nullptr;
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
        return;
    }

    qDebug() << "UpdateChecker: Download complete:" << filePath;

    delete m_downloadFile;
    m_downloadFile = nullptr;
    m_currentReply->deleteLater();
    m_currentReply = nullptr;

    // Install the APK
    emit installationStarted();
    installApk(filePath);
}

void UpdateChecker::dismissUpdate()
{
    m_updateAvailable = false;
    emit updateAvailableChanged();
}

void UpdateChecker::onPeriodicCheck()
{
    if (m_checking || m_downloading) return;

    qDebug() << "UpdateChecker: Periodic update check";

    // Check for updates silently
    QUrl url(GITHUB_API_URL.arg(GITHUB_REPO));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Decenza-DE1");
    request.setRawHeader("Accept", "application/vnd.github.v3+json");

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            parseReleaseInfo(data);

            // If update found, emit signal for popup
            if (m_updateAvailable) {
                emit updatePromptRequested();
            }
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

    if (!uri.isValid()) {
        qWarning() << "Failed to create content URI for APK";
        m_errorMessage = "Failed to create content URI";
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

    // Start activity
    activity.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());

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
