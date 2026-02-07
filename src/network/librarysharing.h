#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>
#include <QImage>
#include <QDateTime>

class Settings;
class WidgetLibrary;

/**
 * @brief Server communication for the widget library sharing system.
 *
 * Handles uploading library entries to api.decenza.coffee, browsing the
 * community library, downloading entries, and managing user uploads.
 *
 * Auth is anonymous via X-Device-Id header (stable UUID from Settings).
 *
 * Usage from QML:
 *   LibrarySharing.uploadEntry(entryId)
 *   LibrarySharing.browseCommunity("item", "%TEMP%", "", "", "newest", 1)
 *   LibrarySharing.downloadEntry(serverId)
 */
class LibrarySharing : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool uploading READ isUploading NOTIFY uploadingChanged)
    Q_PROPERTY(bool browsing READ isBrowsing NOTIFY browsingChanged)
    Q_PROPERTY(bool downloading READ isDownloading NOTIFY downloadingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QVariantList communityEntries READ communityEntries NOTIFY communityEntriesChanged)
    Q_PROPERTY(QVariantList featuredEntries READ featuredEntries NOTIFY featuredEntriesChanged)
    Q_PROPERTY(int totalCommunityResults READ totalCommunityResults NOTIFY totalCommunityResultsChanged)

public:
    explicit LibrarySharing(Settings* settings, WidgetLibrary* library, QObject* parent = nullptr);

    bool isUploading() const { return m_uploading; }
    bool isBrowsing() const { return m_browsing; }
    bool isDownloading() const { return m_downloading; }
    QString lastError() const { return m_lastError; }
    QVariantList communityEntries() const { return m_communityEntries; }
    QVariantList featuredEntries() const { return m_featuredEntries; }
    int totalCommunityResults() const { return m_totalCommunityResults; }

    /// Upload a local library entry to the server (multipart: JSON + thumbnail)
    Q_INVOKABLE void uploadEntry(const QString& entryId);

    /// Upload with pre-captured thumbnail images (full and compact display modes)
    Q_INVOKABLE void uploadEntryWithThumbnails(const QString& entryId,
                                                const QImage& thumbnailFull,
                                                const QImage& thumbnailCompact);

    /// Browse community entries with filters (paginated)
    /// @param type Filter by entry type: "item", "zone", "layout", or "" for all
    /// @param variable Filter by variable tag: "%TEMP%", "%WEIGHT%", etc. or ""
    /// @param action Filter by action tag: "navigate:settings", etc. or ""
    /// @param search Free text search
    /// @param sort Sort order: "newest", "popular", "name"
    /// @param page Page number (1-based)
    Q_INVOKABLE void browseCommunity(const QString& type = QString(),
                                      const QString& variable = QString(),
                                      const QString& action = QString(),
                                      const QString& search = QString(),
                                      const QString& sort = "newest",
                                      int page = 1);

    /// Browse only entries uploaded by this device ("My Uploads")
    Q_INVOKABLE void browseMyUploads(int page = 1);

    /// Load featured/curated entries
    Q_INVOKABLE void loadFeatured();

    /// Download a community entry and import it into the local library
    Q_INVOKABLE void downloadEntry(const QString& serverId);

    /// Delete an entry from the server (only own entries, matched by device ID)
    Q_INVOKABLE void deleteFromServer(const QString& serverId);

    /// Flag/report an entry for moderation
    Q_INVOKABLE void flagEntry(const QString& serverId, const QString& reason);

signals:
    void uploadingChanged();
    void browsingChanged();
    void downloadingChanged();
    void lastErrorChanged();
    void communityEntriesChanged();
    void featuredEntriesChanged();
    void totalCommunityResultsChanged();

    void uploadSuccess(const QString& serverId);
    void uploadFailed(const QString& error);
    void downloadComplete(const QString& localEntryId);
    void downloadFailed(const QString& error);
    void deleteSuccess();
    void deleteFailed(const QString& error);

private slots:
    void onUploadFinished();
    void onBrowseFinished();
    void onFeaturedFinished();
    void onDownloadMetaFinished();
    void onDownloadDataFinished();
    void onDeleteFinished();
    void onFlagFinished();
    void onRecordDownloadFinished();

private:
    QNetworkRequest buildRequest(const QString& path) const;
    QByteArray buildMultipart(const QByteArray& entryJson,
                               const QByteArray& thumbnailFullPng,
                               const QByteArray& thumbnailCompactPng,
                               const QString& boundary) const;
    void setUploading(bool v);
    void setBrowsing(bool v);
    void setDownloading(bool v);
    void setLastError(const QString& error);
    void setTotalCommunityResults(int count);

    // Community cache
    QString cachePath() const;
    void loadCommunityCache();
    void saveCommunityCache();
    void mergeIntoCache(const QVariantList& newEntries);
    bool isUnfilteredBrowse(const QString& type, const QString& variable,
                            const QString& action, const QString& search) const;

    Settings* m_settings;
    WidgetLibrary* m_library;
    QNetworkAccessManager m_networkManager;

    bool m_uploading = false;
    bool m_browsing = false;
    bool m_downloading = false;
    bool m_browseIsIncremental = false;  // true when using since= param
    bool m_browseIsUnfiltered = false;   // true when no filters applied
    QString m_lastError;
    QVariantList m_communityEntries;
    QVariantList m_featuredEntries;
    int m_totalCommunityResults = 0;

    // Community cache
    QVariantList m_cachedEntries;
    QString m_newestCreatedAt;

    // Pending operation IDs
    QString m_pendingDownloadId;
    QString m_pendingDeleteId;
};
