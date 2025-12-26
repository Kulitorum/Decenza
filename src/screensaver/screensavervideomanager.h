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
    Q_PROPERTY(qint64 maxCacheBytes READ maxCacheBytes WRITE setMaxCacheBytes NOTIFY maxCacheBytesChanged)
    Q_PROPERTY(qint64 cacheUsedBytes READ cacheUsedBytes NOTIFY cacheUsedBytesChanged)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadProgressChanged)
    Q_PROPERTY(bool isDownloading READ isDownloading NOTIFY isDownloadingChanged)

    // Current video info (for credits display)
    Q_PROPERTY(QString currentVideoAuthor READ currentVideoAuthor NOTIFY currentVideoChanged)
    Q_PROPERTY(QString currentVideoSourceUrl READ currentVideoSourceUrl NOTIFY currentVideoChanged)
    Q_PROPERTY(bool currentItemIsImage READ currentItemIsImage NOTIFY currentVideoChanged)

    // Image display settings
    Q_PROPERTY(int imageDisplayDuration READ imageDisplayDuration WRITE setImageDisplayDuration NOTIFY imageDisplayDurationChanged)

    // Credits list for all videos
    Q_PROPERTY(QVariantList creditsList READ creditsList NOTIFY catalogUpdated)

public:
    explicit ScreensaverVideoManager(Settings* settings, QObject* parent = nullptr);
    ~ScreensaverVideoManager();

    // Property getters
    bool enabled() const { return m_enabled; }
    QString catalogUrl() const { return m_catalogUrl; }
    bool isRefreshing() const { return m_isRefreshing; }
    QDateTime lastUpdatedUtc() const { return m_lastUpdatedUtc; }
    int itemCount() const { return m_catalog.size(); }

    // Category getters
    QVariantList categories() const;
    QString selectedCategoryId() const { return m_selectedCategoryId; }
    QString selectedCategoryName() const;
    bool isFetchingCategories() const { return m_isFetchingCategories; }

    bool cacheEnabled() const { return m_cacheEnabled; }
    bool streamingFallbackEnabled() const { return m_streamingFallbackEnabled; }
    qint64 maxCacheBytes() const { return m_maxCacheBytes; }
    qint64 cacheUsedBytes() const { return m_cacheUsedBytes; }
    double downloadProgress() const { return m_downloadProgress; }
    bool isDownloading() const { return m_isDownloading; }

    QString currentVideoAuthor() const { return m_currentVideoAuthor; }
    QString currentVideoSourceUrl() const { return m_currentVideoSourceUrl; }
    bool currentItemIsImage() const { return m_currentItemIsImage; }
    int imageDisplayDuration() const { return m_imageDisplayDuration; }

    QVariantList creditsList() const;

    // Property setters
    void setEnabled(bool enabled);
    void setCatalogUrl(const QString& url);
    void setCacheEnabled(bool enabled);
    void setStreamingFallbackEnabled(bool enabled);
    void setMaxCacheBytes(qint64 bytes);
    void setSelectedCategoryId(const QString& categoryId);
    void setImageDisplayDuration(int seconds);

public slots:
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
    void startBackgroundDownload();
    void stopBackgroundDownload();

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
    void maxCacheBytesChanged();
    void cacheUsedBytesChanged();
    void downloadProgressChanged();
    void isDownloadingChanged();

    void currentVideoChanged();
    void videoReady(const QString& localPath);
    void downloadError(const QString& message);
    void imageDisplayDurationChanged();

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
    void evictLruIfNeeded(qint64 neededBytes);
    QString getCachePath(const VideoItem& item) const;
    bool isVideoCached(const VideoItem& item) const;
    bool verifySha256(const QString& filePath, const QString& expectedHash) const;

    // Download management
    void downloadVideo(const VideoItem& item);
    void queueAllVideosForDownload();

    // Video selection
    int selectNextVideoIndex();

    // Migration
    void migrateToNewBucketStructure();

private:
    Settings* m_settings;
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
    qint64 m_maxCacheBytes = 2LL * 1024 * 1024 * 1024;  // 2 GB default
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
    bool m_currentItemIsImage = false;
    int m_imageDisplayDuration = 10;  // seconds to display each image

    // Constants
    static const QString BASE_URL;
    static const QString CATEGORIES_URL;
    static const QString DEFAULT_CATALOG_URL;
    static const QString DEFAULT_CATEGORY_ID;
};
