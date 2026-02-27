#include "screensavervideomanager.h"
#include "core/settings.h"
#include "core/profilestorage.h"

#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QRandomGenerator>
#include <QFileInfo>
#include <QLocale>
#include <utility>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
#endif

// New unified S3 bucket structure
const QString ScreensaverVideoManager::BASE_URL =
    "https://decent-de1-media.s3.eu-north-1.amazonaws.com";

const QString ScreensaverVideoManager::CATEGORIES_URL =
    "https://decent-de1-media.s3.eu-north-1.amazonaws.com/categories.json";

const QString ScreensaverVideoManager::DEFAULT_CATALOG_URL =
    "https://decent-de1-media.s3.eu-north-1.amazonaws.com/catalogs/espresso.json";

const QString ScreensaverVideoManager::DEFAULT_CATEGORY_ID = "espresso";

ScreensaverVideoManager::ScreensaverVideoManager(Settings* settings, ProfileStorage* profileStorage, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_profileStorage(profileStorage)
    , m_networkManager(new QNetworkAccessManager(this))
{
    // Initialize cache directory - prefer external storage (Documents/Decenza) if configured
    updateCacheDirectory();

    // Listen for storage permission changes to migrate cache
    if (m_profileStorage) {
        connect(m_profileStorage, &ProfileStorage::configuredChanged, this, [this]() {
            if (m_profileStorage->isConfigured()) {
//                qDebug() << "[Screensaver] Storage configured, migrating cache to external";
                migrateCacheToExternal();
            }
        });
    }

    QDir().mkpath(m_cacheDir);

    // Load settings
    m_enabled = m_settings->value("screensaver/enabled", true).toBool();
    m_catalogUrl = m_settings->value("screensaver/catalogUrl", DEFAULT_CATALOG_URL).toString();
    m_cacheEnabled = m_settings->value("screensaver/cacheEnabled", true).toBool();
    m_streamingFallbackEnabled = m_settings->value("screensaver/streamingFallback", true).toBool();
    m_lastETag = m_settings->value("screensaver/lastETag", "").toString();
    m_selectedCategoryId = m_settings->value("screensaver/categoryId", DEFAULT_CATEGORY_ID).toString();
    m_imageDisplayDuration = m_settings->value("screensaver/imageDisplayDuration", 10).toInt();
    m_showDateOnPersonal = m_settings->value("screensaver/showDateOnPersonal", false).toBool();
    m_screensaverType = m_settings->value("screensaver/type", "videos").toString();
    m_pipesSpeed = m_settings->value("screensaver/pipesSpeed", 0.5).toDouble();
    m_pipesCameraSpeed = m_settings->value("screensaver/pipesCameraSpeed", 60.0).toDouble();
    // Default to locale: 24h unless system locale uses AM/PM
    bool localeDefault = !QLocale::system().timeFormat(QLocale::ShortFormat).contains("AP", Qt::CaseInsensitive);
    m_flipClockUse24Hour = m_settings->value("screensaver/flipClockUse24Hour", localeDefault).toBool();
    m_flipClockUse3D = m_settings->value("screensaver/flipClockUse3D", true).toBool();
    m_videosShowClock = m_settings->value("screensaver/videosShowClock", true).toBool();
    m_pipesShowClock = m_settings->value("screensaver/pipesShowClock", true).toBool();
    m_attractorShowClock = m_settings->value("screensaver/attractorShowClock", false).toBool();
    m_shotMapShape = m_settings->value("screensaver/shotMapShape", "flat").toString();
    m_shotMapTexture = m_settings->value("screensaver/shotMapTexture", "dark").toString();
    m_shotMapShowClock = m_settings->value("screensaver/shotMapShowClock", true).toBool();
    m_shotMapShowProfiles = m_settings->value("screensaver/shotMapShowProfiles", true).toBool();
    m_dimPercent = qBound(0, m_settings->value("screensaver/dimPercent", 0).toInt(), 100);
    m_dimDelayMinutes = qBound(0, m_settings->value("screensaver/dimDelayMinutes", 15).toInt(), 45);

    // Load rate limit state
    QString rateLimitStr = m_settings->value("screensaver/rateLimitedUntil", "").toString();
    if (!rateLimitStr.isEmpty()) {
        m_rateLimitedUntil = QDateTime::fromString(rateLimitStr, Qt::ISODate);
    }
    QString lastDownloadStr = m_settings->value("screensaver/lastDownloadTime", "").toString();
    if (!lastDownloadStr.isEmpty()) {
        m_lastDownloadTime = QDateTime::fromString(lastDownloadStr, Qt::ISODate);
    }

    // Timer for rate-limited downloads
    m_rateLimitTimer = new QTimer(this);
    m_rateLimitTimer->setSingleShot(true);
    connect(m_rateLimitTimer, &QTimer::timeout, this, &ScreensaverVideoManager::processDownloadQueue);

    // Load cache index and personal catalog
    loadCacheIndex();
    loadPersonalCatalog();
    updateCacheUsedBytes();

//    qDebug() << "[Screensaver] Initialized. Cache dir:" << m_cacheDir
//             << "Personal media:" << m_personalCatalog.size()
//             << "Cache used:" << (m_cacheUsedBytes / 1024 / 1024) << "MB"
//             << "Enabled:" << m_enabled
//             << "Category:" << m_selectedCategoryId;

    // Fetch categories and catalog on startup if enabled
    if (m_enabled) {
        QTimer::singleShot(0, this, &ScreensaverVideoManager::refreshCategories);
    }
}

ScreensaverVideoManager::~ScreensaverVideoManager()
{
    stopBackgroundDownload();
    saveCacheIndex();
}

void ScreensaverVideoManager::setKeepScreenOn(bool on)
{
#ifdef Q_OS_ANDROID
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([on]() {
        // FLAG_KEEP_SCREEN_ON = 128 (WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        constexpr int FLAG_KEEP_SCREEN_ON = 128;

        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (!activity.isValid()) {
//            qWarning() << "[Screensaver] Failed to get Android activity";
            return;
        }

        QJniObject window = activity.callObjectMethod(
            "getWindow", "()Landroid/view/Window;");
        if (!window.isValid()) {
//            qWarning() << "[Screensaver] Failed to get window";
            return;
        }

        if (on) {
            window.callMethod<void>("addFlags", "(I)V", FLAG_KEEP_SCREEN_ON);
//            qDebug() << "[Screensaver] Keep screen on: enabled";
        } else {
            window.callMethod<void>("clearFlags", "(I)V", FLAG_KEEP_SCREEN_ON);
//            qDebug() << "[Screensaver] Keep screen on: disabled";
        }
    });
#else
    Q_UNUSED(on)
//    qDebug() << "[Screensaver] Keep screen on not available on this platform";
#endif
}

void ScreensaverVideoManager::turnScreenOff()
{
#ifdef Q_OS_ANDROID
    // Clear the keep screen on flag to allow Android's system screen timeout
    // to turn off the screen naturally. We don't set brightness to 0 because
    // that keeps the screen technically "on" (just dark) and prevents the
    // system from actually putting the device to sleep.
    setKeepScreenOn(false);
//    qDebug() << "[Screensaver] Cleared keep-screen-on flag, allowing system timeout";
#else
//    qDebug() << "[Screensaver] Turn screen off not available on this platform";
#endif
}

void ScreensaverVideoManager::restoreScreenBrightness()
{
#ifdef Q_OS_ANDROID
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (!activity.isValid()) {
//            qWarning() << "[Screensaver] Failed to get Android activity for brightness restore";
            return;
        }

        QJniObject window = activity.callObjectMethod(
            "getWindow", "()Landroid/view/Window;");
        if (window.isValid()) {
            QJniObject layoutParams = window.callObjectMethod(
                "getAttributes", "()Landroid/view/WindowManager$LayoutParams;");
            if (layoutParams.isValid()) {
                // Set screen brightness to -1.0f (use system default)
                layoutParams.setField<jfloat>("screenBrightness", -1.0f);
                window.callMethod<void>("setAttributes",
                    "(Landroid/view/WindowManager$LayoutParams;)V",
                    layoutParams.object());
//                qDebug() << "[Screensaver] Screen brightness restored to system default";
            }
        }
    });
#else
//    qDebug() << "[Screensaver] Restore screen brightness not available on this platform";
#endif
}

// Property setters
void ScreensaverVideoManager::setEnabled(bool enabled)
{
    if (m_enabled != enabled) {
        m_enabled = enabled;
        m_settings->setValue("screensaver/enabled", enabled);
        emit enabledChanged();

        if (enabled && m_catalog.isEmpty()) {
            refreshCatalog();
        }
    }
}

void ScreensaverVideoManager::setCatalogUrl(const QString& url)
{
    if (m_catalogUrl != url) {
        m_catalogUrl = url;
        m_settings->setValue("screensaver/catalogUrl", url);
        m_lastETag.clear();  // Reset ETag when URL changes
        emit catalogUrlChanged();
    }
}

void ScreensaverVideoManager::setCacheEnabled(bool enabled)
{
    if (m_cacheEnabled != enabled) {
        m_cacheEnabled = enabled;
        m_settings->setValue("screensaver/cacheEnabled", enabled);
        emit cacheEnabledChanged();

        if (enabled && !m_catalog.isEmpty()) {
            startBackgroundDownload();
        } else {
            stopBackgroundDownload();
        }
    }
}

void ScreensaverVideoManager::setStreamingFallbackEnabled(bool enabled)
{
    if (m_streamingFallbackEnabled != enabled) {
        m_streamingFallbackEnabled = enabled;
        m_settings->setValue("screensaver/streamingFallback", enabled);
        emit streamingFallbackEnabledChanged();
    }
}

void ScreensaverVideoManager::setSelectedCategoryId(const QString& categoryId)
{
    if (m_selectedCategoryId != categoryId) {
        m_selectedCategoryId = categoryId;
        m_settings->setValue("screensaver/categoryId", categoryId);
        emit selectedCategoryIdChanged();

        // Stop any in-progress download from the old category
        if (m_isDownloading) {
//            qDebug() << "[Screensaver] Stopping download from previous category";
            stopBackgroundDownload();
        }

        // When switching categories while rate limited, allow one immediate download
        // so there's always something to show in the new category
        bool allowImmediateDownload = false;
        if (isRateLimited()) {
            m_lastDownloadTime = QDateTime();
            m_settings->setValue("screensaver/lastDownloadTime", "");
//            qDebug() << "[Screensaver] Category changed - allowing one immediate download";
            emit rateLimitedChanged();
            allowImmediateDownload = true;
        }

        // Handle personal category - use local catalog, no network fetch
        if (categoryId == "personal") {
//            qDebug() << "[Screensaver] Switched to Personal category with"
//                     << m_personalCatalog.size() << "items";
            // Copy personal catalog to active catalog for playback
            m_catalog = m_personalCatalog;
            emit catalogUpdated();
            return;
        }

        // Update catalog URL based on new category
        QString newCatalogUrl = buildCatalogUrlForCategory(categoryId);
        if (!newCatalogUrl.isEmpty() && newCatalogUrl != m_catalogUrl) {
            m_catalogUrl = newCatalogUrl;
            m_lastETag.clear();  // Reset ETag when category changes
            m_settings->setValue("screensaver/catalogUrl", m_catalogUrl);
            m_settings->setValue("screensaver/lastETag", "");
            emit catalogUrlChanged();

//            qDebug() << "[Screensaver] Category changed to:" << categoryId
//                     << "New catalog URL:" << m_catalogUrl;

            // Refresh catalog for new category (keep cache - media identified by sha256)
            // startBackgroundDownload() will be called after catalog loads
            refreshCatalog();
        } else if (allowImmediateDownload && m_cacheEnabled && !m_catalog.isEmpty()) {
            // Catalog URL unchanged (rare) but we need to trigger download for the immediate allowance
            startBackgroundDownload();
        }
    }
}

void ScreensaverVideoManager::setImageDisplayDuration(int seconds)
{
    if (seconds < 1) seconds = 1;
    if (seconds > 300) seconds = 300;  // Max 5 minutes

    if (m_imageDisplayDuration != seconds) {
        m_imageDisplayDuration = seconds;
        m_settings->setValue("screensaver/imageDisplayDuration", seconds);
        emit imageDisplayDurationChanged();
    }
}

void ScreensaverVideoManager::setShowDateOnPersonal(bool show)
{
    if (m_showDateOnPersonal != show) {
        m_showDateOnPersonal = show;
        m_settings->setValue("screensaver/showDateOnPersonal", show);
        emit showDateOnPersonalChanged();
    }
}

void ScreensaverVideoManager::setScreensaverType(const QString& type)
{
    if (m_screensaverType != type && availableScreensaverTypes().contains(type)) {
        m_screensaverType = type;
        m_settings->setValue("screensaver/type", type);
        emit screensaverTypeChanged();
    }
}

void ScreensaverVideoManager::setPipesSpeed(double speed)
{
    speed = qBound(0.1, speed, 2.0);  // Clamp between 0.1 and 2.0
    if (!qFuzzyCompare(m_pipesSpeed, speed)) {
        m_pipesSpeed = speed;
        m_settings->setValue("screensaver/pipesSpeed", speed);
        emit pipesSpeedChanged();
    }
}

void ScreensaverVideoManager::setPipesCameraSpeed(double speed)
{
    speed = qBound(10.0, speed, 300.0);  // Clamp between 10 and 300 seconds
    if (!qFuzzyCompare(m_pipesCameraSpeed, speed)) {
        m_pipesCameraSpeed = speed;
        m_settings->setValue("screensaver/pipesCameraSpeed", speed);
        emit pipesCameraSpeedChanged();
    }
}

void ScreensaverVideoManager::setFlipClockUse24Hour(bool use24Hour)
{
    if (m_flipClockUse24Hour != use24Hour) {
        m_flipClockUse24Hour = use24Hour;
        m_settings->setValue("screensaver/flipClockUse24Hour", use24Hour);
        emit flipClockUse24HourChanged();
    }
}

void ScreensaverVideoManager::setFlipClockUse3D(bool use3D)
{
    if (m_flipClockUse3D != use3D) {
        m_flipClockUse3D = use3D;
        m_settings->setValue("screensaver/flipClockUse3D", use3D);
        emit flipClockUse3DChanged();
    }
}

void ScreensaverVideoManager::setVideosShowClock(bool show)
{
    if (m_videosShowClock != show) {
        m_videosShowClock = show;
        m_settings->setValue("screensaver/videosShowClock", show);
        emit videosShowClockChanged();
    }
}

void ScreensaverVideoManager::setPipesShowClock(bool show)
{
    if (m_pipesShowClock != show) {
        m_pipesShowClock = show;
        m_settings->setValue("screensaver/pipesShowClock", show);
        emit pipesShowClockChanged();
    }
}

void ScreensaverVideoManager::setAttractorShowClock(bool show)
{
    if (m_attractorShowClock != show) {
        m_attractorShowClock = show;
        m_settings->setValue("screensaver/attractorShowClock", show);
        emit attractorShowClockChanged();
    }
}
void ScreensaverVideoManager::setShotMapShape(const QString& shape)
{
    if (m_shotMapShape != shape) {
        m_shotMapShape = shape;
        m_settings->setValue("screensaver/shotMapShape", shape);
        emit shotMapShapeChanged();
    }
}

void ScreensaverVideoManager::setShotMapTexture(const QString& texture)
{
    if (m_shotMapTexture != texture) {
        m_shotMapTexture = texture;
        m_settings->setValue("screensaver/shotMapTexture", texture);
        emit shotMapTextureChanged();
    }
}

void ScreensaverVideoManager::setShotMapShowClock(bool show)
{
    if (m_shotMapShowClock != show) {
        m_shotMapShowClock = show;
        m_settings->setValue("screensaver/shotMapShowClock", show);
        emit shotMapShowClockChanged();
    }
}

void ScreensaverVideoManager::setShotMapShowProfiles(bool show)
{
    if (m_shotMapShowProfiles != show) {
        m_shotMapShowProfiles = show;
        m_settings->setValue("screensaver/shotMapShowProfiles", show);
        emit shotMapShowProfilesChanged();
    }
}

void ScreensaverVideoManager::setDimPercent(int percent)
{
    percent = qBound(0, percent, 100);
    if (m_dimPercent != percent) {
        m_dimPercent = percent;
        m_settings->setValue("screensaver/dimPercent", percent);
        emit dimPercentChanged();
    }
}

void ScreensaverVideoManager::setDimDelayMinutes(int minutes)
{
    minutes = qBound(0, minutes, 45);
    if (m_dimDelayMinutes != minutes) {
        m_dimDelayMinutes = minutes;
        m_settings->setValue("screensaver/dimDelayMinutes", minutes);
        emit dimDelayMinutesChanged();
    }
}

QVariantList ScreensaverVideoManager::categories() const
{
    QVariantList result;

    // Add "Personal" category at the top if there are personal media files
    if (!m_personalCatalog.isEmpty()) {
        QVariantMap personal;
        personal["id"] = "personal";
        personal["name"] = QString("Personal (%1)").arg(m_personalCatalog.size());
        result.append(personal);
//        qDebug() << "[Screensaver] Category for QML: personal" << personal["name"].toString();
    }

    for (const VideoCategory& cat : std::as_const(m_categories)) {
        QVariantMap item;
        item["id"] = cat.id;
        item["name"] = cat.name;
        result.append(item);
//        qDebug() << "[Screensaver] Category for QML:" << cat.id << cat.name;
    }
//    qDebug() << "[Screensaver] Returning" << result.size() << "categories to QML";
    return result;
}

QString ScreensaverVideoManager::selectedCategoryName() const
{
    // Handle personal category
    if (m_selectedCategoryId == "personal") {
        return QString("Personal (%1)").arg(m_personalCatalog.size());
    }

    for (const VideoCategory& cat : std::as_const(m_categories)) {
        if (cat.id == m_selectedCategoryId) {
            return cat.name;
        }
    }
    return m_selectedCategoryId;  // Fallback to ID if name not found
}

QString ScreensaverVideoManager::buildCatalogUrlForCategory(const QString& categoryId) const
{
    // New unified bucket structure: catalogs/{categoryId}.json
    if (categoryId.isEmpty()) {
        return DEFAULT_CATALOG_URL;
    }
    return QString("%1/catalogs/%2.json").arg(BASE_URL, categoryId);
}

// Category management
void ScreensaverVideoManager::refreshCategories()
{
    if (m_isFetchingCategories) {
//        qDebug() << "[Screensaver] Categories fetch already in progress";
        return;
    }

//    qDebug() << "[Screensaver] Fetching categories from:" << CATEGORIES_URL;

    m_isFetchingCategories = true;
    emit isFetchingCategoriesChanged();

    QNetworkRequest request{QUrl(CATEGORIES_URL)};
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Cache-Control", "no-cache");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);

    m_categoriesReply = m_networkManager->get(request);
    connect(m_categoriesReply, &QNetworkReply::finished,
            this, &ScreensaverVideoManager::onCategoriesReplyFinished);
}

void ScreensaverVideoManager::onCategoriesReplyFinished()
{
    m_isFetchingCategories = false;
    emit isFetchingCategoriesChanged();

    if (!m_categoriesReply) return;

    if (m_categoriesReply->error() != QNetworkReply::NoError) {
        QString errorMsg = m_categoriesReply->errorString();
//        qWarning() << "[Screensaver] Categories fetch error:" << errorMsg;
        emit categoriesError(errorMsg);
        m_categoriesReply->deleteLater();
        m_categoriesReply = nullptr;

        // Still try to refresh catalog with existing URL
        refreshCatalog();
        return;
    }

    QByteArray data = m_categoriesReply->readAll();
    m_categoriesReply->deleteLater();
    m_categoriesReply = nullptr;

    parseCategories(data);
}

void ScreensaverVideoManager::parseCategories(const QByteArray& data)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        QString errorMsg = "Categories JSON parse error: " + parseError.errorString();
//        qWarning() << "[Screensaver]" << errorMsg;
        emit categoriesError(errorMsg);
        refreshCatalog();  // Try catalog anyway
        return;
    }

    const QJsonArray array = doc.array();
    QList<VideoCategory> newCategories;

    for (const QJsonValue& val : array) {
        if (!val.isObject()) continue;

        QJsonObject obj = val.toObject();
        VideoCategory cat;
        cat.id = obj["id"].toString();
        cat.name = obj["name"].toString();

        if (cat.isValid()) {
            newCategories.append(cat);
        }
    }

    m_categories = newCategories;
//    qDebug() << "[Screensaver] Loaded" << m_categories.size() << "categories";
    emit categoriesChanged();

    // Update catalog URL based on selected category
    QString newCatalogUrl = buildCatalogUrlForCategory(m_selectedCategoryId);
    if (m_catalogUrl != newCatalogUrl) {
        m_catalogUrl = newCatalogUrl;
        m_lastETag.clear();
        m_settings->setValue("screensaver/catalogUrl", m_catalogUrl);
        m_settings->setValue("screensaver/lastETag", "");
        emit catalogUrlChanged();
    }

    // Validate selected category exists, fallback to first if not
    // "personal" is a virtual category that exists if there's personal media
    bool categoryExists = (m_selectedCategoryId == "personal" && !m_personalCatalog.isEmpty());
    if (!categoryExists) {
        for (const VideoCategory& cat : std::as_const(m_categories)) {
            if (cat.id == m_selectedCategoryId) {
                categoryExists = true;
                break;
            }
        }
    }

    if (!categoryExists && !m_categories.isEmpty()) {
        // Prefer "espresso" category, fallback to first if not found
        QString fallbackId = m_categories.first().id;
        for (const VideoCategory& cat : std::as_const(m_categories)) {
            if (cat.id == "espresso") {
                fallbackId = cat.id;
                break;
            }
        }
        m_selectedCategoryId = fallbackId;
        m_settings->setValue("screensaver/categoryId", m_selectedCategoryId);
        emit selectedCategoryIdChanged();

        m_catalogUrl = buildCatalogUrlForCategory(m_selectedCategoryId);
        m_lastETag.clear();
        m_settings->setValue("screensaver/catalogUrl", m_catalogUrl);
        m_settings->setValue("screensaver/lastETag", "");
        emit catalogUrlChanged();
    }

    // Skip catalog refresh for personal category - media is local, not fetched from network
    if (m_selectedCategoryId == "personal") {
//        qDebug() << "[Screensaver] Personal category selected - using local catalog";
        m_catalog = m_personalCatalog;
        emit catalogUpdated();
        return;
    }

    // Now refresh the catalog
    refreshCatalog();
}

// Catalog management
void ScreensaverVideoManager::refreshCatalog()
{
    if (m_isRefreshing) {
//        qDebug() << "[Screensaver] Catalog refresh already in progress";
        return;
    }

//    qDebug() << "[Screensaver] Refreshing catalog from:" << m_catalogUrl;

    m_isRefreshing = true;
    emit isRefreshingChanged();

    QNetworkRequest request{QUrl(m_catalogUrl)};
    request.setRawHeader("Accept", "application/json");

    // Use ETag for conditional request
    if (!m_lastETag.isEmpty()) {
        request.setRawHeader("If-None-Match", m_lastETag.toUtf8());
//        qDebug() << "[Screensaver] Using ETag:" << m_lastETag;
    }

    m_catalogReply = m_networkManager->get(request);
    connect(m_catalogReply, &QNetworkReply::finished,
            this, &ScreensaverVideoManager::onCatalogReplyFinished);
}

void ScreensaverVideoManager::onCatalogReplyFinished()
{
    m_isRefreshing = false;
    emit isRefreshingChanged();

    if (!m_catalogReply) return;

    int statusCode = m_catalogReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
//    qDebug() << "[Screensaver] Catalog response status:" << statusCode;

    if (m_catalogReply->error() != QNetworkReply::NoError) {
        QString errorMsg = m_catalogReply->errorString();
//        qWarning() << "[Screensaver] Catalog fetch error:" << errorMsg;
        emit catalogError(errorMsg);
        m_catalogReply->deleteLater();
        m_catalogReply = nullptr;
        return;
    }

    if (statusCode == 304) {
        // Not modified - but only valid if we already have catalog loaded
        if (!m_catalog.isEmpty()) {
//            qDebug() << "[Screensaver] Catalog not modified (304), using cached" << m_catalog.size() << "videos";
            m_catalogReply->deleteLater();
            m_catalogReply = nullptr;
            return;
        }
        // Catalog is empty despite 304 - clear ETag and refetch
//        qDebug() << "[Screensaver] Got 304 but catalog is empty, refetching...";
        m_lastETag.clear();
        m_settings->setValue("screensaver/lastETag", "");
        m_catalogReply->deleteLater();
        m_catalogReply = nullptr;
        refreshCatalog();  // Retry without ETag
        return;
    }

    // Store new ETag
    QString newETag = QString::fromUtf8(m_catalogReply->rawHeader("ETag"));
    if (!newETag.isEmpty()) {
        m_lastETag = newETag;
        m_settings->setValue("screensaver/lastETag", newETag);
//        qDebug() << "[Screensaver] New ETag:" << newETag;
    }

    // Parse catalog
    QByteArray data = m_catalogReply->readAll();
    m_catalogReply->deleteLater();
    m_catalogReply = nullptr;

    parseCatalog(data);
}

void ScreensaverVideoManager::parseCatalog(const QByteArray& data)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        QString errorMsg = "JSON parse error: " + parseError.errorString();
//        qWarning() << "[Screensaver]" << errorMsg;
        emit catalogError(errorMsg);
        return;
    }

    const QJsonArray array = doc.array();
    QList<VideoItem> newCatalog;

    for (const QJsonValue& val : array) {
        if (!val.isObject()) continue;

        VideoItem item = parseVideoItem(val.toObject());
        if (item.isValid()) {
            newCatalog.append(item);
        }
    }

    m_catalog = newCatalog;
    m_lastUpdatedUtc = QDateTime::currentDateTimeUtc();

//    qDebug() << "[Screensaver] Catalog loaded:" << m_catalog.size() << "videos";
    emit catalogUpdated();

    // Start background download if caching is enabled
    if (m_cacheEnabled && !m_catalog.isEmpty()) {
        startBackgroundDownload();
    }
}

VideoItem ScreensaverVideoManager::parseVideoItem(const QJsonObject& obj)
{
    VideoItem item;

    item.id = obj["id"].toInt();
    item.durationSeconds = obj["duration_s"].toInt();
    item.author = obj["author"].toString();
    item.authorUrl = obj["author_url"].toString();
    item.sha256 = obj["sha256"].toString();
    item.bytes = obj["bytes"].toVariant().toLongLong();

    // Parse media type (default to video for backwards compatibility)
    QString typeStr = obj["type"].toString().toLower();
    if (typeStr == "image") {
        item.type = MediaType::Image;
    } else {
        item.type = MediaType::Video;
    }

    // Try different URL/path fields
    if (obj.contains("path")) {
        item.path = obj["path"].toString();
    } else if (obj.contains("url")) {
        item.absoluteUrl = obj["url"].toString();
    } else if (obj.contains("local_path")) {
        // Extract filename from local_path as fallback
        item.path = derivePathFromLocalPath(obj["local_path"].toString());
    } else if (obj.contains("filename")) {
        item.path = obj["filename"].toString();
    }

    // Source URL (pexels or generic)
    if (obj.contains("pexels_url")) {
        item.sourceUrl = obj["pexels_url"].toString();
    } else if (obj.contains("source_url")) {
        item.sourceUrl = obj["source_url"].toString();
    }

    if (!item.isValid()) {
//        qWarning() << "[Screensaver] Skipping invalid catalog item, id:" << item.id
//                   << "- no path or url found";
    }

    return item;
}

QString ScreensaverVideoManager::derivePathFromLocalPath(const QString& localPath)
{
    // Extract filename from a local path like "C:\...\pexels_videos\filename.mp4"
    // Note: QFileInfo doesn't understand Windows paths on Linux/Android, so manually extract
    QString filename = localPath;

    // Find last separator (either / or \)
    qsizetype lastSlash = localPath.lastIndexOf('/');
    qsizetype lastBackslash = localPath.lastIndexOf('\\');
    qsizetype lastSep = qMax(lastSlash, lastBackslash);

    if (lastSep >= 0) {
        filename = localPath.mid(lastSep + 1);
    }

    // URL-encode spaces and special chars (spaces become %20)
    return QString::fromUtf8(QUrl::toPercentEncoding(filename));
}

QString ScreensaverVideoManager::getBaseUrl() const
{
    // New unified bucket structure uses BASE_URL/media/ for all media files
    return BASE_URL + "/media/";
}

QString ScreensaverVideoManager::buildVideoUrl(const VideoItem& item) const
{
    if (!item.absoluteUrl.isEmpty()) {
        return item.absoluteUrl;
    }

    // The path should already be URL-encoded from the catalog
    return getBaseUrl() + item.path;
}

// Cache management
void ScreensaverVideoManager::loadCacheIndex()
{
    QString indexPath = m_cacheDir + "/cache_index.json";
    QFile file(indexPath);

    if (!file.open(QIODevice::ReadOnly)) {
//        qDebug() << "[Screensaver] No cache index found, starting fresh";
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return;

    QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        QString cacheKey = it.key();  // video URL
        QJsonObject cached = it.value().toObject();

        CachedVideo cv;
        cv.localPath = cached["localPath"].toString();
        cv.sha256 = cached["sha256"].toString();
        cv.bytes = cached["bytes"].toVariant().toLongLong();
        cv.lastAccessed = QDateTime::fromString(cached["lastAccessed"].toString(), Qt::ISODate);
        cv.catalogId = cached["catalogId"].toInt();

        // Verify file still exists
        if (QFile::exists(cv.localPath)) {
            m_cacheIndex[cacheKey] = cv;
        }
    }

//    qDebug() << "[Screensaver] Loaded cache index with" << m_cacheIndex.size() << "entries";
}

void ScreensaverVideoManager::saveCacheIndex()
{
    QString indexPath = m_cacheDir + "/cache_index.json";
    QFile file(indexPath);

    if (!file.open(QIODevice::WriteOnly)) {
//        qWarning() << "[Screensaver] Failed to save cache index";
        return;
    }

    QJsonObject root;
    for (auto it = m_cacheIndex.begin(); it != m_cacheIndex.end(); ++it) {
        QJsonObject cached;
        cached["localPath"] = it.value().localPath;
        cached["sha256"] = it.value().sha256;
        cached["bytes"] = it.value().bytes;
        cached["lastAccessed"] = it.value().lastAccessed.toString(Qt::ISODate);
        cached["catalogId"] = it.value().catalogId;
        root[it.key()] = cached;  // key is video URL
    }

    file.write(QJsonDocument(root).toJson());
    file.close();
}

void ScreensaverVideoManager::updateCacheUsedBytes()
{
    qint64 total = 0;
    for (const CachedVideo& cv : std::as_const(m_cacheIndex)) {
        total += cv.bytes;
    }
    if (m_cacheUsedBytes != total) {
        m_cacheUsedBytes = total;
        emit cacheUsedBytesChanged();
    }
}

QString ScreensaverVideoManager::getCachePath(const VideoItem& item) const
{
    // Use hash of full URL for unique filename across categories
    QString mediaUrl = buildVideoUrl(item);
    QString urlHash = QString(QCryptographicHash::hash(mediaUrl.toUtf8(), QCryptographicHash::Md5).toHex()).left(12);

    // Determine extension based on media type and original path
    QString extension;
    if (item.isImage()) {
        // Preserve original image extension
        QString pathLower = item.path.toLower();
        if (pathLower.endsWith(".png")) {
            extension = ".png";
        } else if (pathLower.endsWith(".jpeg")) {
            extension = ".jpeg";
        } else {
            extension = ".jpg";  // Default for images
        }
    } else {
        extension = ".mp4";
    }

    return m_cacheDir + "/" + QString::number(item.id) + "_" + urlHash + extension;
}

bool ScreensaverVideoManager::isVideoCached(const VideoItem& item) const
{
    // Use full video URL as cache key (unique across categories)
    QString cacheKey = buildVideoUrl(item);

    if (!m_cacheIndex.contains(cacheKey)) {
        return false;
    }

    const CachedVideo& cv = m_cacheIndex[cacheKey];

    // Verify file exists
    if (!QFile::exists(cv.localPath)) {
        return false;
    }

    return true;
}

bool ScreensaverVideoManager::verifySha256(const QString& filePath, const QString& expectedHash) const
{
    if (expectedHash.isEmpty()) {
        return true;  // No hash to verify
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    file.close();

    QString actualHash = hash.result().toHex();
    bool match = (actualHash.toLower() == expectedHash.toLower());

    if (!match) {
//        qWarning() << "[Screensaver] SHA256 mismatch for" << filePath
//                   << "expected:" << expectedHash << "actual:" << actualHash;
    }

    return match;
}

void ScreensaverVideoManager::clearCache()
{
//    qDebug() << "[Screensaver] Clearing cache";

    stopBackgroundDownload();

    // Delete all cached files
    for (const CachedVideo& cv : std::as_const(m_cacheIndex)) {
        QFile::remove(cv.localPath);
    }

    m_cacheIndex.clear();
    m_cacheUsedBytes = 0;
    saveCacheIndex();

    emit cacheUsedBytesChanged();
}

void ScreensaverVideoManager::clearCacheWithRateLimit()
{
//    qDebug() << "[Screensaver] Clearing cache with rate limiting enabled";

    clearCache();

    // Enable rate limiting for 24 hours (enough time to download all videos slowly)
    m_rateLimitedUntil = QDateTime::currentDateTimeUtc().addDays(1);
    m_settings->setValue("screensaver/rateLimitedUntil", m_rateLimitedUntil.toString(Qt::ISODate));

    // Clear last download time so first download can happen immediately
    m_lastDownloadTime = QDateTime();
    m_settings->setValue("screensaver/lastDownloadTime", "");

    emit rateLimitedChanged();

//    qDebug() << "[Screensaver] Rate limiting enabled until" << m_rateLimitedUntil.toString();

    // Start downloading immediately so there's something to show
    if (m_cacheEnabled && !m_catalog.isEmpty()) {
        startBackgroundDownload();
    }
}

bool ScreensaverVideoManager::isRateLimited() const
{
    if (!m_rateLimitedUntil.isValid()) {
        return false;
    }
    return QDateTime::currentDateTimeUtc() < m_rateLimitedUntil;
}

int ScreensaverVideoManager::rateLimitMinutesRemaining() const
{
    if (!isRateLimited()) {
        return 0;
    }

    // If we have a last download time, calculate time until next download is allowed
    if (m_lastDownloadTime.isValid()) {
        QDateTime nextAllowed = m_lastDownloadTime.addSecs(RATE_LIMIT_MINUTES * 60);
        qint64 secsRemaining = QDateTime::currentDateTimeUtc().secsTo(nextAllowed);
        if (secsRemaining > 0) {
            return static_cast<int>((secsRemaining + 59) / 60);  // Round up to minutes
        }
    }

    return 0;  // Next download can happen now
}

// Download management
void ScreensaverVideoManager::startBackgroundDownload()
{
//    qDebug() << "[Screensaver] startBackgroundDownload called, isDownloading:" << m_isDownloading
//             << "cacheEnabled:" << m_cacheEnabled << "catalogSize:" << m_catalog.size();

    if (m_isDownloading) {
//        qDebug() << "[Screensaver] Already downloading, skipping";
        return;
    }
    if (!m_cacheEnabled) {
//        qDebug() << "[Screensaver] Cache disabled, skipping download";
        return;
    }

    queueAllVideosForDownload();

//    qDebug() << "[Screensaver] Download queue size:" << m_downloadQueue.size();

    if (!m_downloadQueue.isEmpty()) {
        m_totalToDownload = static_cast<int>(m_downloadQueue.size());
        m_downloadedCount = 0;
        m_downloadProgress = 0.0;
//        qDebug() << "[Screensaver] Starting background download of" << m_totalToDownload << "videos";
        processDownloadQueue();
    } else {
//        qDebug() << "[Screensaver] All videos already cached or queue empty";
    }
}

void ScreensaverVideoManager::stopBackgroundDownload()
{
    m_downloadQueue.clear();

    // Stop rate limit timer
    if (m_rateLimitTimer) {
        m_rateLimitTimer->stop();
    }

    if (m_downloadReply) {
        // Disconnect signals before aborting to prevent onDownloadFinished from being called
        m_downloadReply->disconnect(this);
        m_downloadReply->abort();
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }

    if (m_downloadFile) {
        m_downloadFile->close();
        m_downloadFile->remove();
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }

    if (m_isDownloading) {
        m_isDownloading = false;
        emit isDownloadingChanged();
    }
}

void ScreensaverVideoManager::queueAllVideosForDownload()
{
    m_downloadQueue.clear();

    for (int i = 0; i < m_catalog.size(); ++i) {
        const VideoItem& item = m_catalog[i];

        // Skip if already cached
        if (isVideoCached(item)) {
            continue;
        }

        m_downloadQueue.append(i);
    }
}

void ScreensaverVideoManager::processDownloadQueue()
{
    if (m_downloadQueue.isEmpty()) {
//        qDebug() << "[Screensaver] Download queue complete";
        m_isDownloading = false;
        m_downloadProgress = 1.0;
        emit isDownloadingChanged();
        emit downloadProgressChanged();
        saveCacheIndex();
        return;
    }

    m_currentDownloadIndex = m_downloadQueue.takeFirst();

    // Validate index is still valid (catalog may have been refreshed during download)
    if (m_currentDownloadIndex < 0 || m_currentDownloadIndex >= m_catalog.size()) {
//        qDebug() << "[Screensaver] Skipping invalid index" << m_currentDownloadIndex
//                 << "(catalog size:" << m_catalog.size() << ")";
        QTimer::singleShot(0, this, &ScreensaverVideoManager::processDownloadQueue);
        return;
    }

    const VideoItem& item = m_catalog[m_currentDownloadIndex];

    // Rate limiting only applies to videos, not images (images are small)
    if (item.isVideo() && isRateLimited() && m_lastDownloadTime.isValid()) {
        QDateTime nextAllowed = m_lastDownloadTime.addSecs(RATE_LIMIT_MINUTES * 60);
        qint64 msRemaining = QDateTime::currentDateTimeUtc().msecsTo(nextAllowed);

        if (msRemaining > 0) {
//            qDebug() << "[Screensaver] Rate limited for video, waiting" << (msRemaining / 1000 / 60) << "minutes";
            // Put the item back at the front of the queue
            m_downloadQueue.prepend(m_currentDownloadIndex);
            m_rateLimitTimer->start(static_cast<int>(qMin(msRemaining, (qint64)60000)));  // Check at least every minute
            emit rateLimitedChanged();
            return;
        }
    }

//    qDebug() << "[Screensaver] Downloading" << (item.isImage() ? "image" : "video") << item.id << ":" << item.author;

    QString url = buildVideoUrl(item);
    QString cachePath = getCachePath(item);

    // Create temp file for download
    m_downloadFile = new QFile(cachePath + ".tmp");
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
//        qWarning() << "[Screensaver] Failed to create download file:" << cachePath;
        delete m_downloadFile;
        m_downloadFile = nullptr;
        // Try next
        QTimer::singleShot(100, this, &ScreensaverVideoManager::processDownloadQueue);
        return;
    }

    m_isDownloading = true;
    emit isDownloadingChanged();

    QNetworkRequest request{QUrl(url)};
    m_downloadReply = m_networkManager->get(request);

    connect(m_downloadReply, &QNetworkReply::downloadProgress,
            this, &ScreensaverVideoManager::onDownloadProgress);
    connect(m_downloadReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_downloadFile && m_downloadReply) {
            m_downloadFile->write(m_downloadReply->readAll());
        }
    });
    connect(m_downloadReply, &QNetworkReply::finished,
            this, &ScreensaverVideoManager::onDownloadFinished);
}

void ScreensaverVideoManager::onDownloadProgress(qint64 received, qint64 total)
{
    if (total > 0 && m_totalToDownload > 0) {
        // Calculate overall progress for this download session
        double videoProgress = static_cast<double>(received) / total;
        m_downloadProgress = (m_downloadedCount + videoProgress) / m_totalToDownload;
        emit downloadProgressChanged();
    }
}

void ScreensaverVideoManager::onDownloadFinished()
{
    if (!m_downloadReply || !m_downloadFile) {
        return;
    }

    bool success = (m_downloadReply->error() == QNetworkReply::NoError);
    QString errorString = m_downloadReply->errorString();

    m_downloadFile->close();
    m_downloadReply->deleteLater();
    m_downloadReply = nullptr;

    if (!success) {
//        qWarning() << "[Screensaver] Download failed:" << errorString;
        m_downloadFile->remove();
        delete m_downloadFile;
        m_downloadFile = nullptr;
        emit downloadError(errorString);

        // Try next video
        QTimer::singleShot(1000, this, &ScreensaverVideoManager::processDownloadQueue);
        return;
    }

    // Validate index is still valid (catalog may have been refreshed during async download)
    if (m_currentDownloadIndex < 0 || m_currentDownloadIndex >= m_catalog.size()) {
//        qWarning() << "[Screensaver] Download completed but index" << m_currentDownloadIndex
//                   << "is now invalid (catalog size:" << m_catalog.size() << "), discarding";
        m_downloadFile->remove();
        delete m_downloadFile;
        m_downloadFile = nullptr;
        QTimer::singleShot(100, this, &ScreensaverVideoManager::processDownloadQueue);
        return;
    }

    const VideoItem& item = m_catalog[m_currentDownloadIndex];
    QString cachePath = getCachePath(item);
    QString tempPath = cachePath + ".tmp";

    // Verify SHA256 if available
    if (!item.sha256.isEmpty()) {
        if (!verifySha256(tempPath, item.sha256)) {
//            qWarning() << "[Screensaver] SHA256 verification failed, deleting file";
            m_downloadFile->remove();
            delete m_downloadFile;
            m_downloadFile = nullptr;

            // Try next video
            QTimer::singleShot(1000, this, &ScreensaverVideoManager::processDownloadQueue);
            return;
        }
    }

    // Rename temp file to final
    QFile::remove(cachePath);  // Remove any existing file
    if (!QFile::rename(tempPath, cachePath)) {
//        qWarning() << "[Screensaver] Failed to rename temp file to:" << cachePath;
        QFile::remove(tempPath);
        delete m_downloadFile;
        m_downloadFile = nullptr;

        QTimer::singleShot(1000, this, &ScreensaverVideoManager::processDownloadQueue);
        return;
    }

    delete m_downloadFile;
    m_downloadFile = nullptr;

    // Update cache index (keyed by video URL)
    QString cacheKey = buildVideoUrl(item);
    CachedVideo cv;
    cv.localPath = cachePath;
    cv.sha256 = item.sha256;
    cv.bytes = QFileInfo(cachePath).size();
    cv.lastAccessed = QDateTime::currentDateTimeUtc();
    cv.catalogId = item.id;

    m_cacheIndex[cacheKey] = cv;
    m_cacheUsedBytes += cv.bytes;
    m_downloadedCount++;

//    qDebug() << "[Screensaver] Downloaded and cached:" << cachePath
//             << "(" << (cv.bytes / 1024 / 1024) << "MB)"
//             << "[" << m_downloadedCount << "/" << m_totalToDownload << "]";

    emit cacheUsedBytesChanged();
    emit videoReady(cachePath);

    // Update rate limit tracking (only for videos, not images)
    if (item.isVideo() && isRateLimited()) {
        m_lastDownloadTime = QDateTime::currentDateTimeUtc();
        m_settings->setValue("screensaver/lastDownloadTime", m_lastDownloadTime.toString(Qt::ISODate));
        emit rateLimitedChanged();
    }

    // Save cache index after each download so progress isn't lost when app is killed
    saveCacheIndex();

    // Continue with queue
    QTimer::singleShot(100, this, &ScreensaverVideoManager::processDownloadQueue);
}

// Video selection and playback
int ScreensaverVideoManager::selectNextVideoIndex()
{
    if (m_catalog.isEmpty()) {
        return -1;
    }

    // For personal media, all items are available (already local)
    if (m_selectedCategoryId == "personal") {
        QList<int> indices;
        for (int i = 0; i < m_catalog.size(); ++i) {
            if (i == m_lastPlayedIndex && m_catalog.size() > 1) continue;  // Avoid immediate repeat
            indices.append(i);
        }
        if (indices.isEmpty()) {
            return -1;
        }
        int randIndex = QRandomGenerator::global()->bounded(static_cast<int>(indices.size()));
        return indices[randIndex];
    }

    // Only play cached videos - no streaming
    // First, collect all cached video indices
    QList<int> allCachedIndices;
    for (int i = 0; i < m_catalog.size(); ++i) {
        if (isVideoCached(m_catalog[i])) {
            allCachedIndices.append(i);
        }
    }

    if (allCachedIndices.isEmpty()) {
        // No cached videos yet - return -1 to show fallback
        return -1;
    }

    // If only one video cached, play it (even if it's a repeat)
    if (allCachedIndices.size() == 1) {
        return allCachedIndices.first();
    }

    // Multiple videos cached - avoid immediate repeat by excluding last played
    QList<int> availableIndices;
    for (int idx : allCachedIndices) {
        if (idx != m_lastPlayedIndex) {
            availableIndices.append(idx);
        }
    }

    // Random selection from available videos
    int randIndex = QRandomGenerator::global()->bounded(static_cast<int>(availableIndices.size()));
    return availableIndices[randIndex];
}

QString ScreensaverVideoManager::getNextVideoSource()
{
    int index = selectNextVideoIndex();
    if (index < 0) {
//        qDebug() << "[Screensaver] No cached media available yet";
        return QString();
    }

    const VideoItem& item = m_catalog[index];
    m_lastPlayedIndex = index;

    // Update current media info for credits display and type
    m_currentVideoAuthor = item.author;
    m_currentItemIsImage = item.isImage();

    // Handle personal media - files are stored directly in personal folder
    if (m_selectedCategoryId == "personal") {
        // For personal media, sourceUrl contains the upload date in ISO format
        QDateTime uploadDate = QDateTime::fromString(item.sourceUrl, Qt::ISODate);
        if (uploadDate.isValid()) {
            m_currentMediaDate = uploadDate.toString("MMMM d, yyyy");
        } else {
            m_currentMediaDate.clear();
        }
        m_currentVideoSourceUrl.clear();  // No external source for personal media
        emit currentVideoChanged();
        QString personalDir = m_cacheDir + "/personal";
        QString localPath = personalDir + "/" + item.path;
//        qDebug() << "[Screensaver] Playing personal" << (item.isImage() ? "image:" : "video:") << localPath;
        return QUrl::fromLocalFile(localPath).toString();
    }

    // For catalog media, use regular source URL and clear date
    m_currentVideoSourceUrl = item.sourceUrl.isEmpty() ? item.authorUrl : item.sourceUrl;
    m_currentMediaDate.clear();
    emit currentVideoChanged();

    // Return cached media path (keyed by media URL)
    QString cacheKey = buildVideoUrl(item);
    QString localPath = m_cacheIndex[cacheKey].localPath;
//    qDebug() << "[Screensaver] Playing cached" << (item.isImage() ? "image:" : "video:") << localPath;
    return QUrl::fromLocalFile(localPath).toString();
}

void ScreensaverVideoManager::markVideoPlayed(const QString& source)
{
    // Update last accessed time for LRU tracking
    for (auto it = m_cacheIndex.begin(); it != m_cacheIndex.end(); ++it) {
        if (source.contains(it.value().localPath) ||
            QUrl::fromLocalFile(it.value().localPath).toString() == source) {
            it.value().lastAccessed = QDateTime::currentDateTimeUtc();
            saveCacheIndex();
            break;
        }
    }
}

QVariantList ScreensaverVideoManager::creditsList() const
{
    QVariantList credits;

    for (const VideoItem& item : std::as_const(m_catalog)) {
        QVariantMap credit;
        credit["author"] = item.author;
        credit["authorUrl"] = item.authorUrl;
        credit["sourceUrl"] = item.sourceUrl;
        credit["duration"] = item.durationSeconds;
        credits.append(credit);
    }

    return credits;
}

QString ScreensaverVideoManager::getExternalCachePath() const
{
#if defined(Q_OS_ANDROID)
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        QString extPath = m_profileStorage->externalProfilesPath();
        if (!extPath.isEmpty()) {
            // Use Documents/Decenza/screensaver instead of Documents/Decenza/screensaver_videos
            // to keep path shorter and cleaner
            return extPath + "/screensaver";
        }
    }
#endif
    return QString();
}

QString ScreensaverVideoManager::getFallbackCachePath() const
{
#if defined(Q_OS_ANDROID)
    const QStringList appDataPaths = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    QString externalPath;
    for (const QString& path : appDataPaths) {
        if (path.contains("/Android/data/")) {
            externalPath = path;
            break;
        }
    }
    if (externalPath.isEmpty() && !appDataPaths.isEmpty()) {
        externalPath = appDataPaths.first();
    }
    return externalPath + "/screensaver_videos";
#elif defined(Q_OS_IOS)
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return dataPath + "/screensaver_videos";
#else
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dataPath + "/screensaver_videos";
#endif
}

void ScreensaverVideoManager::updateCacheDirectory()
{
    QString externalPath = getExternalCachePath();
    if (!externalPath.isEmpty()) {
        m_cacheDir = externalPath;
//        qDebug() << "[Screensaver] Using external cache dir:" << m_cacheDir;
    } else {
        m_cacheDir = getFallbackCachePath();
//        qDebug() << "[Screensaver] Using fallback cache dir:" << m_cacheDir;
    }
}

void ScreensaverVideoManager::migrateCacheToExternal()
{
    QString externalPath = getExternalCachePath();
    if (externalPath.isEmpty()) {
//        qDebug() << "[Screensaver] Cannot migrate - no external path available";
        return;
    }

    QString fallbackPath = getFallbackCachePath();
    if (fallbackPath == externalPath) {
//        qDebug() << "[Screensaver] No migration needed - already using external path";
        return;
    }

    QDir fallbackDir(fallbackPath);
    if (!fallbackDir.exists()) {
//        qDebug() << "[Screensaver] No fallback cache to migrate";
        updateCacheDirectory();
        return;
    }

    // Create external directory
    QDir externalDir(externalPath);
    if (!externalDir.exists()) {
        externalDir.mkpath(".");
    }

    // Migrate cache index and files
    const QStringList files = fallbackDir.entryList(QDir::Files);
    int migrated = 0;

    for (const QString& file : files) {
        QString srcPath = fallbackDir.filePath(file);
        QString destPath = externalDir.filePath(file);

        // Skip if already exists in destination
        if (QFile::exists(destPath)) {
//            qDebug() << "[Screensaver] File already in external, removing from fallback:" << file;
            QFile::remove(srcPath);
            continue;
        }

        // Move file to external storage
        if (QFile::rename(srcPath, destPath)) {
            migrated++;
        } else {
            // If rename fails (cross-filesystem), try copy+delete
            if (QFile::copy(srcPath, destPath)) {
                QFile::remove(srcPath);
                migrated++;
            } else {
//                qWarning() << "[Screensaver] Failed to migrate:" << file;
            }
        }
    }

//    qDebug() << "[Screensaver] Migration complete. Migrated" << migrated << "files";

    // Update cache index paths
    for (auto it = m_cacheIndex.begin(); it != m_cacheIndex.end(); ++it) {
        QString oldPath = it.value().localPath;
        if (oldPath.startsWith(fallbackPath)) {
            QString filename = QFileInfo(oldPath).fileName();
            it.value().localPath = externalDir.filePath(filename);
        }
    }

    // Update cache directory and save index
    m_cacheDir = externalPath;
    saveCacheIndex();

    // Try to remove old fallback directory if empty
    fallbackDir.rmdir(".");
}

// ============================================================================
// Personal Media Management
// ============================================================================

void ScreensaverVideoManager::reloadPersonalMedia()
{
    loadPersonalCatalog();
    emit personalMediaChanged();
}

void ScreensaverVideoManager::loadPersonalCatalog()
{
    QString catalogPath = m_cacheDir + "/personal/catalog.json";
    QFile file(catalogPath);

    m_personalCatalog.clear();

    if (!file.open(QIODevice::ReadOnly)) {
//        qDebug() << "[Screensaver] No personal catalog found";
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isArray()) return;

    const QJsonArray items = doc.array();
    for (const QJsonValue& val : items) {
        QJsonObject obj = val.toObject();
        VideoItem item;
        item.id = obj["id"].toInt();
        item.type = obj["type"].toString() == "image" ? MediaType::Image : MediaType::Video;
        item.path = obj["path"].toString();
        item.durationSeconds = obj["duration_s"].toInt(30);
        item.author = obj["author"].toString();
        item.sourceUrl = obj["date"].toString();  // Load upload date from sourceUrl field
        item.bytes = obj["bytes"].toVariant().toLongLong();

        // Verify file exists
        QString fullPath = m_cacheDir + "/personal/" + item.path;
        if (QFile::exists(fullPath)) {
            m_personalCatalog.append(item);
        }
    }

//    qDebug() << "[Screensaver] Loaded personal catalog with" << m_personalCatalog.size() << "items";
}

void ScreensaverVideoManager::savePersonalCatalog()
{
    QString personalDir = m_cacheDir + "/personal";
    QDir().mkpath(personalDir);

    QString catalogPath = personalDir + "/catalog.json";
    QFile file(catalogPath);

    if (!file.open(QIODevice::WriteOnly)) {
//        qWarning() << "[Screensaver] Failed to save personal catalog";
        return;
    }

    QJsonArray items;
    for (const VideoItem& item : std::as_const(m_personalCatalog)) {
        QJsonObject obj;
        obj["id"] = item.id;
        obj["type"] = item.isImage() ? "image" : "video";
        obj["path"] = item.path;
        obj["duration_s"] = item.durationSeconds;
        obj["author"] = item.author;
        obj["date"] = item.sourceUrl;  // Store upload date in sourceUrl field
        obj["bytes"] = item.bytes;
        items.append(obj);
    }

    file.write(QJsonDocument(items).toJson());
    file.close();

//    qDebug() << "[Screensaver] Saved personal catalog with" << m_personalCatalog.size() << "items";
}

int ScreensaverVideoManager::generatePersonalMediaId() const
{
    int maxId = 0;
    for (const VideoItem& item : std::as_const(m_personalCatalog)) {
        if (item.id > maxId) {
            maxId = item.id;
        }
    }
    return maxId + 1;
}

bool ScreensaverVideoManager::addPersonalMedia(const QString& filePath, const QString& originalName, const QDateTime& mediaDate)
{
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
//        qWarning() << "[Screensaver] Personal media file not found:" << filePath;
        return false;
    }

    // Determine media type from extension
    QString ext = fileInfo.suffix().toLower();
    MediaType type;
    if (ext == "mp4" || ext == "webm" || ext == "mov" || ext == "avi") {
        type = MediaType::Video;
    } else if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "gif" || ext == "webp") {
        type = MediaType::Image;
    } else {
//        qWarning() << "[Screensaver] Unsupported media type:" << ext;
        return false;
    }

    // Check for duplicate by original filename (the part after the ID prefix)
    QString checkName = originalName.isEmpty() ? fileInfo.fileName() : originalName;
    for (const VideoItem& existing : std::as_const(m_personalCatalog)) {
        // Extract original name from stored path (format: "ID_originalname.ext")
        qsizetype underscorePos = existing.path.indexOf('_');
        QString existingOriginal = underscorePos >= 0 ? existing.path.mid(underscorePos + 1) : existing.path;
        if (existingOriginal.compare(checkName, Qt::CaseInsensitive) == 0) {
//            qDebug() << "[Screensaver] Skipping duplicate:" << checkName;
            QFile::remove(filePath);  // Clean up temp file
            return false;  // Already exists
        }
    }

    // Create personal media directory
    QString personalDir = m_cacheDir + "/personal";
    QDir().mkpath(personalDir);

    // Generate unique ID and filename
    int newId = generatePersonalMediaId();
    QString displayName = originalName.isEmpty() ? fileInfo.fileName() : originalName;
    QString targetFilename = QString("%1_%2").arg(newId).arg(fileInfo.fileName());
    QString targetPath = personalDir + "/" + targetFilename;

    // Move file to personal directory (or copy if on different filesystem)
    if (filePath != targetPath) {
        if (QFile::exists(targetPath)) {
            QFile::remove(targetPath);
        }
        if (!QFile::rename(filePath, targetPath)) {
            // Try copy+delete
            if (!QFile::copy(filePath, targetPath)) {
//                qWarning() << "[Screensaver] Failed to move media to personal directory";
                return false;
            }
            QFile::remove(filePath);
        }
    }

    // Create catalog entry
    VideoItem item;
    item.id = newId;
    item.type = type;
    item.path = targetFilename;
    item.durationSeconds = (type == MediaType::Video) ? 30 : m_imageDisplayDuration;
    item.author = "Personal";
    // Use provided media date (from EXIF/metadata) or fall back to current time
    QDateTime dateToStore = mediaDate.isValid() ? mediaDate : QDateTime::currentDateTime();
    item.sourceUrl = dateToStore.toString(Qt::ISODate);
    item.bytes = QFileInfo(targetPath).size();

    m_personalCatalog.append(item);
    savePersonalCatalog();

    // Personal media doesn't count against streaming cache limit
    emit personalMediaChanged();

//    qDebug() << "[Screensaver] Added personal media:" << targetFilename
//             << "type:" << (type == MediaType::Video ? "video" : "image")
//             << "size:" << (item.bytes / 1024) << "KB";

    return true;
}

bool ScreensaverVideoManager::hasPersonalMediaWithName(const QString& originalName) const
{
    for (const VideoItem& existing : std::as_const(m_personalCatalog)) {
        // Extract original name from stored path (format: "ID_originalname.ext")
        qsizetype underscorePos = existing.path.indexOf('_');
        QString existingOriginal = underscorePos >= 0 ? existing.path.mid(underscorePos + 1) : existing.path;
        if (existingOriginal.compare(originalName, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QVariantList ScreensaverVideoManager::getPersonalMediaList() const
{
    QVariantList list;
    QString personalDir = m_cacheDir + "/personal";

    for (const VideoItem& item : std::as_const(m_personalCatalog)) {
        QVariantMap map;
        map["id"] = item.id;
        map["type"] = item.isImage() ? "image" : "video";
        map["filename"] = item.path;
        map["path"] = personalDir + "/" + item.path;
        map["bytes"] = item.bytes;
        map["author"] = item.author;
        list.append(map);
    }
    return list;
}

bool ScreensaverVideoManager::deletePersonalMedia(int mediaId)
{
    QString personalDir = m_cacheDir + "/personal";

    for (int i = 0; i < m_personalCatalog.size(); ++i) {
        if (m_personalCatalog[i].id == mediaId) {
            VideoItem item = m_personalCatalog[i];
            QString filePath = personalDir + "/" + item.path;

            // Delete file
            if (QFile::exists(filePath)) {
                QFile::remove(filePath);
            }

            // Remove from catalog
            m_personalCatalog.removeAt(i);
            savePersonalCatalog();

            emit personalMediaChanged();

//            qDebug() << "[Screensaver] Deleted personal media:" << item.path;
            return true;
        }
    }

//    qWarning() << "[Screensaver] Personal media not found with id:" << mediaId;
    return false;
}

void ScreensaverVideoManager::clearPersonalMedia()
{
    QString personalDir = m_cacheDir + "/personal";

    qint64 freedBytes = 0;
    for (const VideoItem& item : std::as_const(m_personalCatalog)) {
        QString filePath = personalDir + "/" + item.path;
        if (QFile::exists(filePath)) {
            QFile::remove(filePath);
        }
        freedBytes += item.bytes;
    }

    m_personalCatalog.clear();
    savePersonalCatalog();

    // If personal category was selected, switch to first available category
    if (m_selectedCategoryId == "personal") {
        if (!m_categories.isEmpty()) {
            setSelectedCategoryId(m_categories.first().id);
        } else {
            // No categories available, set to empty
            m_selectedCategoryId.clear();
            emit selectedCategoryIdChanged();
        }
    }

    emit personalMediaChanged();
    emit categoriesChanged();

//    qDebug() << "[Screensaver] Cleared all personal media, freed" << (freedBytes / 1024 / 1024) << "MB";
}
