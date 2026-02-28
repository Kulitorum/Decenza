#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QDateTime>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>

class Settings;
class ProfileStorage;

// Media type for catalog items
enum class MediaType {
    Video,
    Image
};

// Represents a video category from the categories manifest
struct VideoCategory {
    QString id;
    QString name;

    bool isValid() const { return !id.isEmpty(); }
};

// Represents a single media item from the catalog (video or image)
struct VideoItem {
    int id = 0;
    MediaType type = MediaType::Video;
    QString path;           // Relative path (e.g., "1234567_Author_30s.mp4" or "7654321_Author.jpg")
    QString absoluteUrl;    // Full URL if provided directly
    int durationSeconds = 0;  // For videos; images use global setting
    QString author;
    QString authorUrl;
    QString sourceUrl;      // Pexels URL or other source
    QString sha256;
    qint64 bytes = 0;

    bool isValid() const { return !path.isEmpty() || !absoluteUrl.isEmpty(); }
    bool isImage() const { return type == MediaType::Image; }
    bool isVideo() const { return type == MediaType::Video; }
};

// Represents a cached video file
struct CachedVideo {
    QString localPath;
    QString sha256;
    qint64 bytes = 0;
    QDateTime lastAccessed;
    int catalogId = 0;
};

class ScreensaverVideoManager : public QObject {
    Q_OBJECT

    // Catalog state
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString catalogUrl READ catalogUrl WRITE setCatalogUrl NOTIFY catalogUrlChanged)
    Q_PROPERTY(bool isRefreshing READ isRefreshing NOTIFY isRefreshingChanged)
    Q_PROPERTY(QDateTime lastUpdatedUtc READ lastUpdatedUtc NOTIFY catalogUpdated)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY catalogUpdated)

    // Category state
    Q_PROPERTY(QVariantList categories READ categories NOTIFY categoriesChanged)
    Q_PROPERTY(QString selectedCategoryId READ selectedCategoryId WRITE setSelectedCategoryId NOTIFY selectedCategoryIdChanged)
    Q_PROPERTY(QString selectedCategoryName READ selectedCategoryName NOTIFY selectedCategoryIdChanged)
    Q_PROPERTY(bool isFetchingCategories READ isFetchingCategories NOTIFY isFetchingCategoriesChanged)

    // Cache state
    Q_PROPERTY(bool cacheEnabled READ cacheEnabled WRITE setCacheEnabled NOTIFY cacheEnabledChanged)
    Q_PROPERTY(bool streamingFallbackEnabled READ streamingFallbackEnabled WRITE setStreamingFallbackEnabled NOTIFY streamingFallbackEnabledChanged)
    Q_PROPERTY(qint64 cacheUsedBytes READ cacheUsedBytes NOTIFY cacheUsedBytesChanged)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadProgressChanged)
    Q_PROPERTY(bool isDownloading READ isDownloading NOTIFY isDownloadingChanged)

    // Current video info (for credits display)
    Q_PROPERTY(QString currentVideoAuthor READ currentVideoAuthor NOTIFY currentVideoChanged)
    Q_PROPERTY(QString currentVideoSourceUrl READ currentVideoSourceUrl NOTIFY currentVideoChanged)
    Q_PROPERTY(bool currentItemIsImage READ currentItemIsImage NOTIFY currentVideoChanged)
    Q_PROPERTY(QString currentMediaDate READ currentMediaDate NOTIFY currentVideoChanged)

    // Image display settings
    Q_PROPERTY(int imageDisplayDuration READ imageDisplayDuration WRITE setImageDisplayDuration NOTIFY imageDisplayDurationChanged)

    // Credits list for all videos
    Q_PROPERTY(QVariantList creditsList READ creditsList NOTIFY catalogUpdated)

    // Personal media state
    Q_PROPERTY(int personalMediaCount READ personalMediaCount NOTIFY personalMediaChanged)
    Q_PROPERTY(bool hasPersonalMedia READ hasPersonalMedia NOTIFY personalMediaChanged)
    Q_PROPERTY(bool isPersonalCategory READ isPersonalCategory NOTIFY selectedCategoryIdChanged)
    Q_PROPERTY(bool showDateOnPersonal READ showDateOnPersonal WRITE setShowDateOnPersonal NOTIFY showDateOnPersonalChanged)

    // Screensaver type (videos, pipes, etc.)
    Q_PROPERTY(QString screensaverType READ screensaverType WRITE setScreensaverType NOTIFY screensaverTypeChanged)
    Q_PROPERTY(QStringList availableScreensaverTypes READ availableScreensaverTypes CONSTANT)
    Q_PROPERTY(double pipesSpeed READ pipesSpeed WRITE setPipesSpeed NOTIFY pipesSpeedChanged)
    Q_PROPERTY(double pipesCameraSpeed READ pipesCameraSpeed WRITE setPipesCameraSpeed NOTIFY pipesCameraSpeedChanged)

    // Screen dimming (after delay)
    Q_PROPERTY(int dimPercent READ dimPercent WRITE setDimPercent NOTIFY dimPercentChanged)
    Q_PROPERTY(int dimDelayMinutes READ dimDelayMinutes WRITE setDimDelayMinutes NOTIFY dimDelayMinutesChanged)

    // Flip clock settings
    Q_PROPERTY(bool flipClockUse24Hour READ flipClockUse24Hour WRITE setFlipClockUse24Hour NOTIFY flipClockUse24HourChanged)
    Q_PROPERTY(bool flipClockUse3D READ flipClockUse3D WRITE setFlipClockUse3D NOTIFY flipClockUse3DChanged)

    // Shot map settings
    Q_PROPERTY(QString shotMapShape READ shotMapShape WRITE setShotMapShape NOTIFY shotMapShapeChanged)
    Q_PROPERTY(QString shotMapTexture READ shotMapTexture WRITE setShotMapTexture NOTIFY shotMapTextureChanged)
    Q_PROPERTY(bool shotMapShowClock READ shotMapShowClock WRITE setShotMapShowClock NOTIFY shotMapShowClockChanged)
    Q_PROPERTY(bool shotMapShowProfiles READ shotMapShowProfiles WRITE setShotMapShowProfiles NOTIFY shotMapShowProfilesChanged)

    // Show clock settings (per screensaver type)
    Q_PROPERTY(bool videosShowClock READ videosShowClock WRITE setVideosShowClock NOTIFY videosShowClockChanged)
    Q_PROPERTY(bool pipesShowClock READ pipesShowClock WRITE setPipesShowClock NOTIFY pipesShowClockChanged)
    Q_PROPERTY(bool attractorShowClock READ attractorShowClock WRITE setAttractorShowClock NOTIFY attractorShowClockChanged)

    // Rate limiting (after cache clear)
    Q_PROPERTY(bool isRateLimited READ isRateLimited NOTIFY rateLimitedChanged)
    Q_PROPERTY(int rateLimitMinutesRemaining READ rateLimitMinutesRemaining NOTIFY rateLimitedChanged)

public:
    explicit ScreensaverVideoManager(Settings* settings, ProfileStorage* profileStorage, QObject* parent = nullptr);
    ~ScreensaverVideoManager();

    // Property getters
    bool enabled() const { return m_enabled; }
    QString catalogUrl() const { return m_catalogUrl; }
    bool isRefreshing() const { return m_isRefreshing; }
    QDateTime lastUpdatedUtc() const { return m_lastUpdatedUtc; }
    int itemCount() const { return static_cast<int>(m_catalog.size()); }

    // Category getters
    QVariantList categories() const;
    QString selectedCategoryId() const { return m_selectedCategoryId; }
    QString selectedCategoryName() const;
    bool isFetchingCategories() const { return m_isFetchingCategories; }

    bool cacheEnabled() const { return m_cacheEnabled; }
    bool streamingFallbackEnabled() const { return m_streamingFallbackEnabled; }
    qint64 cacheUsedBytes() const { return m_cacheUsedBytes; }
    double downloadProgress() const { return m_downloadProgress; }
    bool isDownloading() const { return m_isDownloading; }

    QString currentVideoAuthor() const { return m_currentVideoAuthor; }
    QString currentVideoSourceUrl() const { return m_currentVideoSourceUrl; }
    bool currentItemIsImage() const { return m_currentItemIsImage; }
    QString currentMediaDate() const { return m_currentMediaDate; }
    int imageDisplayDuration() const { return m_imageDisplayDuration; }

    QVariantList creditsList() const;

    // Personal media getters
    int personalMediaCount() const { return static_cast<int>(m_personalCatalog.size()); }
    bool hasPersonalMedia() const { return !m_personalCatalog.isEmpty(); }
    QString personalMediaDirectory() const { return m_cacheDir + "/personal"; }
    bool isPersonalCategory() const { return m_selectedCategoryId == "personal"; }
    bool showDateOnPersonal() const { return m_showDateOnPersonal; }

    // Screensaver type
    QString screensaverType() const { return m_screensaverType; }
    QStringList availableScreensaverTypes() const { return {"disabled", "videos", "pipes", "flipclock", "attractor", "shotmap"}; }
    int dimPercent() const { return m_dimPercent; }
    int dimDelayMinutes() const { return m_dimDelayMinutes; }
    double pipesSpeed() const { return m_pipesSpeed; }
    double pipesCameraSpeed() const { return m_pipesCameraSpeed; }
    bool flipClockUse24Hour() const { return m_flipClockUse24Hour; }
    bool flipClockUse3D() const { return m_flipClockUse3D; }
    bool videosShowClock() const { return m_videosShowClock; }
    bool pipesShowClock() const { return m_pipesShowClock; }
    bool attractorShowClock() const { return m_attractorShowClock; }
    QString shotMapShape() const { return m_shotMapShape; }
    QString shotMapTexture() const { return m_shotMapTexture; }
    bool shotMapShowClock() const { return m_shotMapShowClock; }
    bool shotMapShowProfiles() const { return m_shotMapShowProfiles; }

    // Rate limiting
    bool isRateLimited() const;
    int rateLimitMinutesRemaining() const;

    // Property setters
    void setEnabled(bool enabled);
    void setCatalogUrl(const QString& url);
    void setCacheEnabled(bool enabled);
    void setStreamingFallbackEnabled(bool enabled);
    void setSelectedCategoryId(const QString& categoryId);
    void setImageDisplayDuration(int seconds);
    void setShowDateOnPersonal(bool show);
    void setDimPercent(int percent);
    void setDimDelayMinutes(int minutes);
    void setScreensaverType(const QString& type);
    void setPipesSpeed(double speed);
    void setPipesCameraSpeed(double speed);
    void setFlipClockUse24Hour(bool use24Hour);
    void setFlipClockUse3D(bool use3D);
    void setVideosShowClock(bool show);
    void setPipesShowClock(bool show);
    void setAttractorShowClock(bool show);
    void setShotMapShape(const QString& shape);
    void setShotMapTexture(const QString& texture);
    void setShotMapShowClock(bool show);
    void setShotMapShowProfiles(bool show);

public slots:
    // Screen wake lock (prevents screen from turning off)
    void setKeepScreenOn(bool on);

    // Restore screen brightness (after disabled mode wake)
    void restoreScreenBrightness();

    // Category management
    void refreshCategories();

    // Catalog management
    void refreshCatalog();

    // Get next video to play (returns local file URL if cached, or streaming URL)
    QString getNextVideoSource();

    // Mark current video as played (for LRU tracking)
    void markVideoPlayed(const QString& source);

    // Cache management
    void clearCache();
    void clearCacheWithRateLimit();  // Clears cache and enables rate limiting
    void startBackgroundDownload();
    void stopBackgroundDownload();

    // Personal media management (called from web upload and backup restore)
    void reloadPersonalMedia();
    bool addPersonalMedia(const QString& filePath, const QString& originalName = QString(), const QDateTime& mediaDate = QDateTime());
    bool hasPersonalMediaWithName(const QString& originalName) const;
    QVariantList getPersonalMediaList() const;
    bool deletePersonalMedia(int mediaId);
    void clearPersonalMedia();

signals:
    void enabledChanged();
    void catalogUrlChanged();
    void isRefreshingChanged();
    void catalogUpdated();
    void catalogError(const QString& message);

    void categoriesChanged();
    void selectedCategoryIdChanged();
    void isFetchingCategoriesChanged();
    void categoriesError(const QString& message);

    void cacheEnabledChanged();
    void streamingFallbackEnabledChanged();
    void cacheUsedBytesChanged();
    void downloadProgressChanged();
    void isDownloadingChanged();

    void currentVideoChanged();
    void videoReady(const QString& localPath);
    void downloadError(const QString& message);
    void imageDisplayDurationChanged();
    void personalMediaChanged();
    void showDateOnPersonalChanged();
    void dimPercentChanged();
    void dimDelayMinutesChanged();
    void screensaverTypeChanged();
    void pipesSpeedChanged();
    void pipesCameraSpeedChanged();
    void flipClockUse24HourChanged();
    void flipClockUse3DChanged();
    void videosShowClockChanged();
    void pipesShowClockChanged();
    void attractorShowClockChanged();
    void shotMapShapeChanged();
    void shotMapTextureChanged();
    void shotMapShowClockChanged();
    void shotMapShowProfilesChanged();
    void rateLimitedChanged();

private slots:
    void onCategoriesReplyFinished();
    void onCatalogReplyFinished();
    void onDownloadProgress(qint64 received, qint64 total);
    void onDownloadFinished();
    void processDownloadQueue();

private:
    // Category parsing
    void parseCategories(const QByteArray& data);
    QString buildCatalogUrlForCategory(const QString& categoryId) const;

    // Catalog parsing
    void parseCatalog(const QByteArray& data);
    VideoItem parseVideoItem(const QJsonObject& obj);
    QString derivePathFromLocalPath(const QString& localPath);

    // URL helpers
    QString getBaseUrl() const;
    QString buildVideoUrl(const VideoItem& item) const;

    // Cache management
    void loadCacheIndex();
    void saveCacheIndex();
    void updateCacheUsedBytes();
    QString getCachePath(const VideoItem& item) const;
    bool isVideoCached(const VideoItem& item) const;
    bool verifySha256(const QString& filePath, const QString& expectedHash) const;

    // Download management
    void downloadVideo(const VideoItem& item);
    void queueAllVideosForDownload();

    // Video selection
    int selectNextVideoIndex();

    // Storage management
    QString getExternalCachePath() const;
    QString getFallbackCachePath() const;
    void updateCacheDirectory();
    void migrateCacheToExternal();

    // Personal media management
    void loadPersonalCatalog();
    void savePersonalCatalog();
    int generatePersonalMediaId() const;

private:
    Settings* m_settings;
    ProfileStorage* m_profileStorage;
    QNetworkAccessManager* m_networkManager;

    // Category state
    QList<VideoCategory> m_categories;
    QString m_selectedCategoryId;
    bool m_isFetchingCategories = false;
    QNetworkReply* m_categoriesReply = nullptr;

    // Catalog state
    bool m_enabled = true;
    QString m_catalogUrl;
    QString m_lastETag;
    bool m_isRefreshing = false;
    QDateTime m_lastUpdatedUtc;
    QList<VideoItem> m_catalog;
    QNetworkReply* m_catalogReply = nullptr;

    // Cache state
    bool m_cacheEnabled = true;
    bool m_streamingFallbackEnabled = true;
    qint64 m_cacheUsedBytes = 0;
    QString m_cacheDir;
    QMap<QString, CachedVideo> m_cacheIndex;  // video path -> CachedVideo

    // Download state
    bool m_isDownloading = false;
    double m_downloadProgress = 0.0;
    QList<int> m_downloadQueue;  // List of catalog indices to download
    int m_currentDownloadIndex = -1;
    int m_totalToDownload = 0;   // Total videos to download this session
    int m_downloadedCount = 0;   // Videos downloaded so far this session
    QNetworkReply* m_downloadReply = nullptr;
    QFile* m_downloadFile = nullptr;

    // Playback state
    int m_lastPlayedIndex = -1;
    QString m_currentVideoAuthor;
    QString m_currentVideoSourceUrl;
    QString m_currentMediaDate;
    bool m_currentItemIsImage = false;
    int m_imageDisplayDuration = 10;  // seconds to display each image

    // Personal media state
    QList<VideoItem> m_personalCatalog;
    bool m_showDateOnPersonal = false;

    // Screen dimming
    int m_dimPercent = 0;        // 0 = disabled, 100 = fully black
    int m_dimDelayMinutes = 15;  // 0 = immediate

    // Screensaver type
    QString m_screensaverType = "videos";
    double m_pipesSpeed = 0.5;  // Default to slower speed
    double m_pipesCameraSpeed = 60.0;  // Seconds for full rotation (default 60s)
    bool m_flipClockUse24Hour = true;  // Default to 24-hour format
    bool m_flipClockUse3D = true;  // Default to 3D (perspective) mode
    bool m_videosShowClock = true;  // Default to showing clock on videos
    bool m_pipesShowClock = true;  // Default to showing clock on pipes
    bool m_attractorShowClock = false;
    QString m_shotMapShape = "flat";  // flat, globe
    QString m_shotMapTexture = "dark";  // dark, bright, satellite
    bool m_shotMapShowClock = true;
    bool m_shotMapShowProfiles = true;

    // Rate limiting (after cache clear to prevent S3 abuse)
    QDateTime m_rateLimitedUntil;  // Rate limit active until this time
    QDateTime m_lastDownloadTime;  // When the last video finished downloading
    QTimer* m_rateLimitTimer = nullptr;  // Timer for delayed downloads
    static constexpr int RATE_LIMIT_MINUTES = 3;  // Minutes between downloads when rate limited

    // Constants
    static const QString BASE_URL;
    static const QString CATEGORIES_URL;
    static const QString DEFAULT_CATALOG_URL;
    static const QString DEFAULT_CATEGORY_ID;
};
