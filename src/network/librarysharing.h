#pragma once

#include <QObject>
#include <QHash>
#include <QNetworkAccessManager>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>
#include <QImage>
#include <QDateTime>
#include <QUrl>
#include "core/logcollapse.h"

class QNetworkReply;
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
    explicit LibrarySharing(Settings* settings, WidgetLibrary* library, QObject* parent = nullptr,
                            const QUrl& apiBase = QUrl());

    bool isUploading() const { return m_activeUploads > 0; }
    bool isBrowsing() const { return m_browsing; }
    bool isDownloading() const { return m_downloading; }
    QString lastError() const { return m_lastError; }
    QString lastExistingId() const { return m_lastExistingId; }
    QVariantList communityEntries() const { return m_communityEntries; }
    QVariantList featuredEntries() const { return m_featuredEntries; }
    int totalCommunityResults() const { return m_totalCommunityResults; }

    /// Drop the keepalive sockets in this class's private QNetworkAccessManager.
    /// Called from main.cpp before an Android APK install dispatches so no
    /// QSocketNotifier survives into the install handover (#865).
    void clearConnectionCache() { m_networkManager.clearConnectionCache(); }

    /// Upload a local library entry to the server (multipart: JSON + thumbnail)
    Q_INVOKABLE void uploadEntry(const QString& entryId);
    bool uploadEntryForRequest(const QString& requestId, const QString& entryId,
                               const QImage& thumbnailFull = QImage(),
                               const QImage& thumbnailCompact = QImage());

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

    // Request-scoped variants for host adapters. Cache/property notifications remain
    // independent; callers complete only from browseRequestFinished.
    bool browseCommunityForRequest(const QString& requestId,
                                   const QString& type = QString(),
                                   const QString& variable = QString(),
                                   const QString& action = QString(),
                                   const QString& search = QString(),
                                   const QString& sort = "newest",
                                   int page = 1);

    /// Browse only entries uploaded by this device ("My Uploads")
    Q_INVOKABLE void browseMyUploads(int page = 1);
    bool browseMyUploadsForRequest(const QString& requestId, int page = 1);

    /// Load featured/curated entries
    Q_INVOKABLE void loadFeatured();
    bool loadFeaturedForRequest(const QString& requestId);

    /// Download a community entry and import it into the local library
    Q_INVOKABLE void downloadEntry(const QString& serverId);
    bool downloadEntryForRequest(const QString& requestId, const QString& serverId);

    /// Delete an entry from the server (only own entries, matched by device ID)
    Q_INVOKABLE void deleteFromServer(const QString& serverId);
    bool deleteFromServerForRequest(const QString& requestId, const QString& serverId);

    /// Flag/report an entry for moderation
    Q_INVOKABLE void flagEntry(const QString& serverId, const QString& reason);
    bool flagEntryForRequest(const QString& requestId, const QString& serverId,
                             const QString& reason);

    // Fetches only a thumbnail derived from a canonical community entry UUID.
    // The destination host and path are fixed in this implementation; callers
    // cannot supply an arbitrary proxy URL.
    bool fetchCommunityThumbnailForRequest(const QString& requestId, const QString& entryId,
                                           bool compact = false);
    void cancelCommunityThumbnailRequest(const QString& requestId);

signals:
    void uploadingChanged();
    void browsingChanged();
    void downloadingChanged();
    void lastErrorChanged();
    void communityEntriesChanged();
    void featuredEntriesChanged();
    void totalCommunityResultsChanged();

    void browseRequestFinished(const QString& requestId, bool success,
                               const QVariantList& entries, int total,
                               const QString& error);
    void deleteRequestFinished(const QString& requestId, const QString& serverId,
                               bool success, const QString& error);
    void uploadRequestFinished(const QString& requestId, const QString& localEntryId,
                               bool success, const QString& serverId,
                               bool alreadyExists, const QString& error);
    void downloadRequestFinished(const QString& requestId, const QString& serverId,
                                 bool success, const QString& localEntryId,
                                 bool alreadyExists, const QString& error);
    void featuredRequestFinished(const QString& requestId, bool success,
                                 const QVariantList& entries, const QString& error);
    void flagRequestFinished(const QString& requestId, const QString& serverId,
                             bool success, const QString& error);
    void communityThumbnailRequestFinished(const QString& requestId, bool success,
                                           const QByteArray& png, const QString& error);

    void uploadSuccess(const QString& serverId);
    void uploadFailed(const QString& error);
    void downloadComplete(const QString& localEntryId);
    void downloadAlreadyExists(const QString& localEntryId);
    void downloadFailed(const QString& error);
    void deleteSuccess();
    void deleteFailed(const QString& error);

private slots:
    void onBrowseFinished();
    void onFeaturedFinished();
    void onDownloadMetaFinished();
    void onDownloadDataFinished();
    void onFlagFinished();
    void onRecordDownloadFinished();

private:
#ifdef DECENZA_TESTING
    friend class TestLibrarySharing;
#endif
    void handleUploadFinished(QNetworkReply* reply, const QString& localEntryId,
                              const QString& requestId = QString());
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
    bool startCommunityBrowse(const QString& requestId, const QString& type,
                              const QString& variable, const QString& action,
                              const QString& search, const QString& sort, int page);
    bool startMyUploadsBrowse(const QString& requestId, int page);
    void finishBrowse(QNetworkReply* reply);
    void finishDelete(QNetworkReply* reply, const QString& requestId,
                      const QString& serverId);
    void readCommunityThumbnail(QNetworkReply* reply);
    void finishCommunityThumbnail(QNetworkReply* reply);

    Settings* m_settings;
    WidgetLibrary* m_library;
    QNetworkAccessManager m_networkManager;
    QUrl m_apiBase;

    int m_activeUploads = 0;
    bool m_browsing = false;
    bool m_downloading = false;
    bool m_browseIsIncremental = false;  // true when using since= param
    bool m_browseIsUnfiltered = false;   // true when no filters applied
    QString m_lastError;
    // TODO: Replace QVariantList with QAbstractListModel for scalability.
    // QVariantList forces QML to re-evaluate the entire model on every page append.
    // With 1000+ entries, this causes jank and high memory usage. A proper model
    // with beginInsertRows/endInsertRows would allow incremental delegate creation.
    // Also: strip full entry "data" payload from model, only keep metadata + thumbnail URLs.
    // Also: add cacheBuffer to GridView in CommunityBrowserPage.qml.
    QVariantList m_communityEntries;
    QVariantList m_featuredEntries;
    int m_totalCommunityResults = 0;

    // Community cache
    QVariantList m_cachedEntries;
    QString m_newestCreatedAt;

    // Pending operation IDs
    QString m_lastExistingId;  // Server ID from 409 "Already shared" response
    struct PendingCommunityThumbnail {
        QString requestId;
        QByteArray png;
    };
    QHash<QNetworkReply*, PendingCommunityThumbnail> m_pendingCommunityThumbnails;
    // One episode is an overlapping request batch; flush when it drains.
    LogCollapse m_thumbnailFailureLog{LogCollapse::kChangesOnly};
};
