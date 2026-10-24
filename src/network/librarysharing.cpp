#include "core/diagnosticlogging.h"
#include "core/settings_app.h"
#include "librarysharing.h"
#include "communitythumbnail.h"
#include "version.h"
#include "../core/settings.h"
#include "../core/widgetlibrary.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QBuffer>
#include <QUuid>
#include <QFile>
#include <QDir>
#include <QSet>
#include <QStandardPaths>
#include <QDebug>
#include <cmath>
#include <limits>

static const QUrl kDefaultApiBase(QStringLiteral("https://api.decenza.coffee/v1/library"));
static constexpr qsizetype kMaxCommunityThumbnailBytes = 1024 * 1024;
static constexpr qsizetype kMaxCommunityThumbnailRequests = 16;

LibrarySharing::LibrarySharing(Settings* settings, WidgetLibrary* library, QObject* parent,
                               const QUrl& apiBase)
    : QObject(parent)
    , m_settings(settings)
    , m_library(library)
    , m_apiBase(apiBase.isValid() && !apiBase.isEmpty() ? apiBase : kDefaultApiBase)
{
    while (m_apiBase.path().endsWith(QLatin1Char('/')) && m_apiBase.path().size() > 1) {
        QString path = m_apiBase.path();
        path.chop(1);
        m_apiBase.setPath(path);
    }
    loadCommunityCache();
}

// ---------------------------------------------------------------------------
// Request builder
// ---------------------------------------------------------------------------

QNetworkRequest LibrarySharing::buildRequest(const QString& path) const
{
    QUrl url = m_apiBase;
    QString basePath = url.path();
    if (!path.isEmpty()) {
        if (!path.startsWith(QLatin1Char('/')))
            basePath += QLatin1Char('/');
        basePath += path;
    }
    url.setPath(basePath);
    QNetworkRequest request{url};
    request.setRawHeader("User-Agent",
        QString("Decenza/%1").arg(VERSION_STRING).toUtf8());
    request.setRawHeader("X-Device-Id", m_settings->app()->deviceId().toUtf8());
    return request;
}

// ---------------------------------------------------------------------------
// Upload
// ---------------------------------------------------------------------------

void LibrarySharing::uploadEntry(const QString& entryId)
{
    uploadEntryWithThumbnails(entryId, QImage(), QImage());
}

void LibrarySharing::uploadEntryWithThumbnails(const QString& entryId,
                                                const QImage& thumbnailFull,
                                                const QImage& thumbnailCompact)
{
    uploadEntryForRequest(QString(), entryId, thumbnailFull, thumbnailCompact);
}

bool LibrarySharing::uploadEntryForRequest(const QString& requestId, const QString& entryId,
                                            const QImage& thumbnailFull,
                                            const QImage& thumbnailCompact)
{
    if (entryId.isEmpty() || !m_library) {
        const QString error = QStringLiteral("Entry not found");
        setLastError(error);
        if (!requestId.isEmpty())
            emit uploadRequestFinished(requestId, entryId, false, QString(), false, error);
        else
            emit uploadFailed(error);
        return false;
    }
    QByteArray entryJson = m_library->exportEntry(entryId);
    if (entryJson.isEmpty()) {
        const QString error = QStringLiteral("Entry not found: ") + entryId;
        setLastError(error);
        if (!requestId.isEmpty())
            emit uploadRequestFinished(requestId, entryId, false, QString(), false, error);
        else
            emit uploadFailed(m_lastError);
        return false;
    }

    setUploading(true);
    setLastError(QString());

    // Convert thumbnails to PNG bytes
    auto imageToPng = [](const QImage& img) -> QByteArray {
        if (img.isNull()) return {};
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        return png;
    };

    QByteArray fullPng = imageToPng(thumbnailFull);
    QByteArray compactPng = imageToPng(thumbnailCompact);

    // Build multipart request
    QString boundary = "----DecenzaBoundary" + QUuid::createUuid().toString(QUuid::WithoutBraces);

    QNetworkRequest request = buildRequest("/entries");
    request.setHeader(QNetworkRequest::ContentTypeHeader,
        QString("multipart/form-data; boundary=%1").arg(boundary));

    QByteArray body = buildMultipart(entryJson, fullPng, compactPng, boundary);

    DIAG_DEBUG(APP, "LibrarySharing") << "Uploading entry" << entryId
             << "(" << body.size() << "bytes)";
    if (!fullPng.isEmpty())
        DIAG_DEBUG(APP, "LibrarySharing") << "Thumbnail full:" << fullPng.size() << "bytes";
    if (!compactPng.isEmpty())
        DIAG_DEBUG(APP, "LibrarySharing") << "Thumbnail compact:" << compactPng.size() << "bytes";

    QNetworkReply* reply = m_networkManager.post(request, body);

    // Capture local ID per-upload so concurrent uploads each track their own entry
    QString localId = entryId;
    connect(reply, &QNetworkReply::finished, this, [this, reply, localId, requestId]() {
        handleUploadFinished(reply, localId, requestId);
    });
    return true;
}

QByteArray LibrarySharing::buildMultipart(const QByteArray& entryJson,
                                            const QByteArray& thumbnailFullPng,
                                            const QByteArray& thumbnailCompactPng,
                                            const QString& boundary) const
{
    QByteArray body;
    QByteArray sep = ("--" + boundary + "\r\n").toUtf8();
    QByteArray end = ("--" + boundary + "--\r\n").toUtf8();

    // JSON part
    body += sep;
    body += "Content-Disposition: form-data; name=\"entry\"; filename=\"entry.json\"\r\n";
    body += "Content-Type: application/json\r\n\r\n";
    body += entryJson;
    body += "\r\n";

    // Full thumbnail (optional)
    if (!thumbnailFullPng.isEmpty()) {
        body += sep;
        body += "Content-Disposition: form-data; name=\"thumbnail_full\"; filename=\"thumbnail_full.png\"\r\n";
        body += "Content-Type: image/png\r\n\r\n";
        body += thumbnailFullPng;
        body += "\r\n";
    }

    // Compact thumbnail (optional)
    if (!thumbnailCompactPng.isEmpty()) {
        body += sep;
        body += "Content-Disposition: form-data; name=\"thumbnail_compact\"; filename=\"thumbnail_compact.png\"\r\n";
        body += "Content-Type: image/png\r\n\r\n";
        body += thumbnailCompactPng;
        body += "\r\n";
    }

    body += end;
    return body;
}

void LibrarySharing::handleUploadFinished(QNetworkReply* reply, const QString& localEntryId,
                                          const QString& requestId)
{
    reply->deleteLater();
    setUploading(false);

    QByteArray responseBody = reply->readAll();
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    DIAG_DEBUG(APP, "LibrarySharing") << "Upload response status:" << statusCode
             << "for local entry:" << localEntryId;

    QJsonDocument doc = QJsonDocument::fromJson(responseBody);
    QJsonObject obj = doc.object();

    // 409 = duplicate entry already exists on server
    if (statusCode == 409) {
        QString existingId = obj["existingId"].toString();
        DIAG_DEBUG(APP, "LibrarySharing") << "Already shared (existing ID:" << existingId << ")";

        // Still adopt the server ID so future downloads match
        if (!existingId.isEmpty() && existingId != localEntryId) {
            m_library->renameEntry(localEntryId, existingId);
        }

        m_lastExistingId = existingId;
        setLastError("Already shared");
        emit uploadFailed("Already shared");
        if (!requestId.isEmpty())
            emit uploadRequestFinished(requestId, localEntryId, false, existingId,
                                       true, QStringLiteral("alreadyExists"));
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QString error = obj["error"].toString(reply->errorString());
        DIAG_WARN(APP, "LibrarySharing") << "Upload failed -" << error;
        setLastError(error);
        emit uploadFailed(error);
        if (!requestId.isEmpty())
            emit uploadRequestFinished(requestId, localEntryId, false, QString(), false, error);
        return;
    }

    if (obj.contains("id")) {
        QString serverId = obj["id"].toString();
        DIAG_DEBUG(APP, "LibrarySharing") << "Upload successful, server ID:" << serverId
                 << "(local was:" << localEntryId << ")";

        // Rename local entry to match server ID so downloads won't create duplicates
        if (!localEntryId.isEmpty() && serverId != localEntryId) {
            m_library->renameEntry(localEntryId, serverId);
        }

        emit uploadSuccess(serverId);
        if (!requestId.isEmpty())
            emit uploadRequestFinished(requestId, localEntryId, true, serverId, false, QString());
    } else {
        QString error = obj["error"].toString("Upload failed");
        DIAG_WARN(APP, "LibrarySharing") << "Server error -" << error;
        setLastError(error);
        emit uploadFailed(error);
        if (!requestId.isEmpty())
            emit uploadRequestFinished(requestId, localEntryId, false, QString(), false, error);
    }
}

// ---------------------------------------------------------------------------
// Browse community
// ---------------------------------------------------------------------------

void LibrarySharing::browseCommunity(const QString& type,
                                       const QString& variable,
                                       const QString& action,
                                       const QString& search,
                                       const QString& sort,
                                       int page)
{
    if (!startCommunityBrowse(QString(), type, variable, action, search, sort, page))
        DIAG_WARN(APP, "LibrarySharing") << "Already browsing";
}

bool LibrarySharing::browseCommunityForRequest(const QString& requestId,
                                               const QString& type,
                                               const QString& variable,
                                               const QString& action,
                                               const QString& search,
                                               const QString& sort,
                                               int page)
{
    if (requestId.isEmpty())
        return false;
    return startCommunityBrowse(requestId, type, variable, action, search, sort, page);
}

bool LibrarySharing::startCommunityBrowse(const QString& requestId,
                                          const QString& type,
                                          const QString& variable,
                                          const QString& action,
                                          const QString& search,
                                          const QString& sort,
                                          int page)
{
    if (m_browsing) {
        DIAG_WARN(APP, "LibrarySharing") << "Already browsing";
        return false;
    }

    bool unfiltered = isUnfilteredBrowse(type, variable, action, search);

    // For unfiltered page-1 requests, show cache immediately and do incremental fetch
    if (unfiltered && page == 1 && !m_cachedEntries.isEmpty()) {
        m_communityEntries = m_cachedEntries;
        emit communityEntriesChanged();
        setTotalCommunityResults(static_cast<int>(m_cachedEntries.size()));
    }

    m_browseIsUnfiltered = unfiltered && page == 1;
    m_browseIsIncremental = m_browseIsUnfiltered && !m_newestCreatedAt.isEmpty();

    setBrowsing(true);
    setLastError(QString());

    QUrlQuery query;
    if (!type.isEmpty())     query.addQueryItem("type", type);
    if (!variable.isEmpty()) query.addQueryItem("variable", variable);
    if (!action.isEmpty())   query.addQueryItem("action", action);
    if (!search.isEmpty())   query.addQueryItem("search", search);
    if (!sort.isEmpty())     query.addQueryItem("sort", sort);
    query.addQueryItem("page", QString::number(page));

    // Incremental: only fetch entries newer than our cache
    if (m_browseIsIncremental)
        query.addQueryItem("since", m_newestCreatedAt);

    QUrl url = m_apiBase;
    url.setPath(url.path() + QStringLiteral("/entries"));
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent",
        QString("Decenza/%1").arg(VERSION_STRING).toUtf8());
    request.setRawHeader("X-Device-Id", m_settings->app()->deviceId().toUtf8());

    DIAG_DEBUG(APP, "LibrarySharing") << "Browsing community -" << url.toString();

    QNetworkReply* reply = m_networkManager.get(request);
    reply->setProperty("libraryBrowseRequestId", requestId);
    connect(reply, &QNetworkReply::finished, this, &LibrarySharing::onBrowseFinished);
    return true;
}

void LibrarySharing::browseMyUploads(int page)
{
    if (!startMyUploadsBrowse(QString(), page))
        DIAG_WARN(APP, "LibrarySharing") << "Already browsing";
}

bool LibrarySharing::browseMyUploadsForRequest(const QString& requestId, int page)
{
    if (requestId.isEmpty())
        return false;
    return startMyUploadsBrowse(requestId, page);
}

bool LibrarySharing::startMyUploadsBrowse(const QString& requestId, int page)
{
    if (m_browsing) {
        DIAG_WARN(APP, "LibrarySharing") << "Already browsing";
        return false;
    }

    setBrowsing(true);
    setLastError(QString());

    QUrlQuery query;
    query.addQueryItem("device_id", "mine");
    query.addQueryItem("page", QString::number(page));

    QUrl url = m_apiBase;
    url.setPath(url.path() + QStringLiteral("/entries"));
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent",
        QString("Decenza/%1").arg(VERSION_STRING).toUtf8());
    request.setRawHeader("X-Device-Id", m_settings->app()->deviceId().toUtf8());

    DIAG_DEBUG(APP, "LibrarySharing") << "Browsing my uploads";

    QNetworkReply* reply = m_networkManager.get(request);
    reply->setProperty("libraryBrowseRequestId", requestId);
    connect(reply, &QNetworkReply::finished, this, &LibrarySharing::onBrowseFinished);
    return true;
}

void LibrarySharing::onBrowseFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    finishBrowse(reply);
}

void LibrarySharing::finishBrowse(QNetworkReply* reply)
{
    const QString requestId = reply->property("libraryBrowseRequestId").toString();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        QString error = reply->errorString();
        const QVariantList resultEntries = m_communityEntries;
        const int resultTotal = m_totalCommunityResults;
        DIAG_WARN(APP, "LibrarySharing") << "Browse failed -" << error;
        setLastError(error);
        m_browseIsIncremental = false;
        m_browseIsUnfiltered = false;
        setBrowsing(false);
        emit browseRequestFinished(requestId, false, resultEntries, resultTotal, error);
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
    const auto failInvalidResponse = [this, requestId](const QString& error) {
        const QVariantList resultEntries = m_communityEntries;
        const int resultTotal = m_totalCommunityResults;
        setLastError(error);
        m_browseIsIncremental = false;
        m_browseIsUnfiltered = false;
        setBrowsing(false);
        emit browseRequestFinished(requestId, false, resultEntries, resultTotal, error);
    };
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        failInvalidResponse(QStringLiteral("Invalid community browse response JSON"));
        return;
    }
    QJsonObject obj = doc.object();

    if (!obj.value(QStringLiteral("entries")).isArray()) {
        failInvalidResponse(QStringLiteral("Invalid community browse response: entries must be an array"));
        return;
    }
    const QJsonValue totalValue = obj.value(QStringLiteral("total"));
    int total = 0;
    if (totalValue.isUndefined()) {
        total = static_cast<int>(obj.value(QStringLiteral("entries")).toArray().size());
    } else if (!totalValue.isDouble()
               || !std::isfinite(totalValue.toDouble())
               || totalValue.toDouble() < 0.0
               || totalValue.toDouble() > std::numeric_limits<int>::max()
               || std::floor(totalValue.toDouble()) != totalValue.toDouble()) {
        failInvalidResponse(QStringLiteral("Invalid community browse response: total must be a nonnegative integer"));
        return;
    } else {
        total = static_cast<int>(totalValue.toDouble());
    }
    const QJsonValue deletedValue = obj.value(QStringLiteral("deletedIds"));
    if (!deletedValue.isUndefined() && !deletedValue.isArray()) {
        failInvalidResponse(QStringLiteral("Invalid community browse response: deletedIds must be an array"));
        return;
    }
    for (const QJsonValue& deletedId : deletedValue.toArray()) {
        if (!deletedId.isString() || deletedId.toString().isEmpty()) {
            failInvalidResponse(QStringLiteral("Invalid community browse response: deletedIds must contain nonempty strings"));
            return;
        }
    }

    QVariantList entries;
    QJsonArray arr = obj["entries"].toArray();
    for (const auto& val : arr) {
        if (!val.isObject()) {
            failInvalidResponse(QStringLiteral("Invalid community browse response: entries must contain objects"));
            return;
        }
        entries.append(val.toObject().toVariantMap());
    }

    // Remove deleted entries from cache
    QJsonArray deletedArr = obj["deletedIds"].toArray();
    if (!deletedArr.isEmpty()) {
        QSet<QString> deletedIds;
        for (const auto& val : deletedArr)
            deletedIds.insert(val.toString());

        m_cachedEntries.erase(
            std::remove_if(m_cachedEntries.begin(), m_cachedEntries.end(),
                [&deletedIds](const QVariant& e) {
                    return deletedIds.contains(e.toMap()["id"].toString());
                }),
            m_cachedEntries.end());

        DIAG_DEBUG(APP, "LibrarySharing") << "Removed" << deletedIds.size() << "deleted entries from cache";
    }

    if (m_browseIsIncremental) {
        // Incremental: merge new entries into cache
        DIAG_DEBUG(APP, "LibrarySharing") << "Incremental fetch returned" << entries.size() << "new entries";
        if (!entries.isEmpty()) {
            mergeIntoCache(entries);
        }
        if (!entries.isEmpty() || !deletedArr.isEmpty()) {
            saveCommunityCache();
        }
        m_communityEntries = m_cachedEntries;
    } else if (m_browseIsUnfiltered && !entries.isEmpty()) {
        // First full unfiltered fetch - seed the cache
        m_cachedEntries = entries;
        m_newestCreatedAt.clear();
        for (const auto& e : entries) {
            QString ca = e.toMap()["createdAt"].toString();
            if (ca > m_newestCreatedAt)
                m_newestCreatedAt = ca;
        }
        saveCommunityCache();
        m_communityEntries = entries;
        DIAG_DEBUG(APP, "LibrarySharing") << "Cached" << entries.size() << "entries, newest:" << m_newestCreatedAt;
    } else {
        // Filtered or paginated query - no caching
        m_communityEntries = entries;
    }

    emit communityEntriesChanged();
    setTotalCommunityResults(total);

    DIAG_DEBUG(APP, "LibrarySharing") << "Browse total:" << total << "displayed:" << m_communityEntries.size();
    m_browseIsIncremental = false;
    m_browseIsUnfiltered = false;
    const QVariantList resultEntries = m_communityEntries;
    const int resultTotal = m_totalCommunityResults;
    setBrowsing(false);
    emit browseRequestFinished(requestId, true, resultEntries, resultTotal, QString());
}

// ---------------------------------------------------------------------------
// Featured
// ---------------------------------------------------------------------------

void LibrarySharing::loadFeatured()
{
    loadFeaturedForRequest(QString());
}

bool LibrarySharing::loadFeaturedForRequest(const QString& requestId)
{
    setLastError(QString());

    QUrlQuery query;
    query.addQueryItem("sort", "popular");
    query.addQueryItem("page", "1");
    query.addQueryItem("per_page", "10");

    QUrl url = m_apiBase;
    url.setPath(url.path() + QStringLiteral("/entries"));
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent",
        QString("Decenza/%1").arg(VERSION_STRING).toUtf8());
    request.setRawHeader("X-Device-Id", m_settings->app()->deviceId().toUtf8());

    DIAG_DEBUG(APP, "LibrarySharing") << "Loading featured entries -" << url.toString();

    QNetworkReply* reply = m_networkManager.get(request);
    reply->setProperty("libraryFeaturedRequestId", requestId);
    connect(reply, &QNetworkReply::finished, this, &LibrarySharing::onFeaturedFinished);
    return true;
}

void LibrarySharing::onFeaturedFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    const QString requestId = reply->property("libraryFeaturedRequestId").toString();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        const QString error = reply->errorString();
        DIAG_WARN(APP, "LibrarySharing") << "Featured load failed -" << error;
        if (!requestId.isEmpty())
            emit featuredRequestFinished(requestId, false, {}, error);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) {
        if (!requestId.isEmpty())
            emit featuredRequestFinished(requestId, false, {}, QStringLiteral("invalidResponse"));
        return;
    }
    QJsonObject obj = doc.object();

    QVariantList entries;
    QJsonArray arr = obj["entries"].toArray();
    for (const auto& val : arr) {
        entries.append(val.toObject().toVariantMap());
    }

    m_featuredEntries = entries;
    emit featuredEntriesChanged();

    DIAG_DEBUG(APP, "LibrarySharing") << "Loaded" << entries.size() << "featured entries";
    if (!requestId.isEmpty())
        emit featuredRequestFinished(requestId, true, entries, QString());
}

// ---------------------------------------------------------------------------
// Download (two-step: metadata, then full data on confirm)
// ---------------------------------------------------------------------------

void LibrarySharing::downloadEntry(const QString& serverId)
{
    if (!downloadEntryForRequest(QString(), serverId))
        DIAG_WARN(APP, "LibrarySharing") << "Already downloading or invalid entry";
}

bool LibrarySharing::downloadEntryForRequest(const QString& requestId, const QString& serverId)
{
    if (m_downloading) {
        DIAG_WARN(APP, "LibrarySharing") << "Already downloading";
        return false;
    }
    if (serverId.isEmpty()) {
        if (!requestId.isEmpty())
            emit downloadRequestFinished(requestId, serverId, false, QString(), false,
                                         QStringLiteral("invalidRequest"));
        return false;
    }

    setDownloading(true);
    setLastError(QString());

    // Fetch full entry data
    QNetworkRequest request = buildRequest("/entries/" + serverId);

    DIAG_DEBUG(APP, "LibrarySharing") << "Downloading entry" << serverId;

    QNetworkReply* reply = m_networkManager.get(request);
    reply->setProperty("libraryDownloadServerId", serverId);
    reply->setProperty("libraryDownloadRequestId", requestId);
    connect(reply, &QNetworkReply::finished, this, &LibrarySharing::onDownloadDataFinished);
    return true;
}

void LibrarySharing::onDownloadMetaFinished()
{
    // Reserved for future two-step preview-then-download flow
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply) reply->deleteLater();
}

void LibrarySharing::onDownloadDataFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    const QString requestId = reply->property("libraryDownloadRequestId").toString();
    const QString serverId = reply->property("libraryDownloadServerId").toString();
    reply->deleteLater();
    setDownloading(false);

    if (reply->error() != QNetworkReply::NoError) {
        QString error = reply->errorString();
        DIAG_WARN(APP, "LibrarySharing") << "Download failed -" << error;
        setLastError(error);
        emit downloadFailed(error);
        if (!requestId.isEmpty())
            emit downloadRequestFinished(requestId, serverId, false, QString(), false, error);
        return;
    }

    QByteArray data = reply->readAll();

    // Check if entry already exists locally (same server ID)
    if (!serverId.isEmpty() && m_library && !m_library->getEntry(serverId).isEmpty()) {
        DIAG_DEBUG(APP, "LibrarySharing") << "Entry already in library:" << serverId;
        emit downloadAlreadyExists(serverId);
        if (!requestId.isEmpty())
            emit downloadRequestFinished(requestId, serverId, true, serverId, true, QString());
        return;
    }

    // Import into local library
    QString localId = m_library ? m_library->importEntry(data) : QString();
    if (localId.isEmpty()) {
        QString error = "Failed to import downloaded entry";
        setLastError(error);
        emit downloadFailed(error);
        if (!requestId.isEmpty())
            emit downloadRequestFinished(requestId, serverId, false, QString(), false, error);
        return;
    }

    DIAG_DEBUG(APP, "LibrarySharing") << "Downloaded and imported as" << localId;
    emit downloadComplete(localId);
    if (!requestId.isEmpty())
        emit downloadRequestFinished(requestId, serverId, true, localId, false, QString());

    // Record download on server (fire-and-forget)
    QNetworkRequest req = buildRequest("/entries/" + serverId + "/download");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* recordReply = m_networkManager.post(req, "{}");
    connect(recordReply, &QNetworkReply::finished, this, &LibrarySharing::onRecordDownloadFinished);
}

void LibrarySharing::onRecordDownloadFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        DIAG_DEBUG(APP, "LibrarySharing") << "Failed to record download (non-critical) -"
                 << reply->errorString();
    }
}

bool LibrarySharing::fetchCommunityThumbnailForRequest(const QString& requestId,
                                                       const QString& entryId, bool compact)
{
    if (requestId.isEmpty() || requestId.size() > 128
        || !CommunityThumbnail::isCanonicalEntryId(entryId)
        || m_pendingCommunityThumbnails.size() >= kMaxCommunityThumbnailRequests) {
        return false;
    }
    for (auto it = m_pendingCommunityThumbnails.cbegin(); it != m_pendingCommunityThumbnails.cend(); ++it) {
        if (it->requestId == requestId) return false;
    }

    QUrl url = CommunityThumbnail::canonicalUpstreamUrl(entryId, compact);
#ifdef DECENZA_TESTING
    // Let the existing loopback HTTP fixture exercise response-size and PNG
    // validation without making production routing configurable.
    if (m_apiBase.host() == QLatin1String("127.0.0.1")
        || m_apiBase.host() == QLatin1String("localhost")) {
        url = m_apiBase;
        url.setPath(QStringLiteral("/library/thumbnails/%1_%2.png")
                        .arg(entryId.toLower(), compact ? QStringLiteral("compact")
                                                       : QStringLiteral("full")));
        url.setQuery(QString());
    }
#endif
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", QString("Decenza/%1").arg(VERSION_STRING).toUtf8());
    request.setRawHeader("Accept", "image/png");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(12000);
    QNetworkReply* reply = m_networkManager.get(request);
    reply->setReadBufferSize(128 * 1024);
    m_pendingCommunityThumbnails.insert(reply, {requestId, {}});
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply] {
        if (!m_pendingCommunityThumbnails.contains(reply)) return;
        bool ok = false;
        const qint64 length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&ok);
        if (ok && (length < 0 || length > kMaxCommunityThumbnailBytes)) {
            const QString id = m_pendingCommunityThumbnails.take(reply).requestId;
            emit communityThumbnailRequestFinished(id, false, {}, QStringLiteral("thumbnailTooLarge"));
            reply->abort();
        }
    });
    connect(reply, &QIODevice::readyRead, this, [this, reply] { readCommunityThumbnail(reply); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finishCommunityThumbnail(reply); });
    return true;
}

void LibrarySharing::cancelCommunityThumbnailRequest(const QString& requestId)
{
    for (auto it = m_pendingCommunityThumbnails.begin(); it != m_pendingCommunityThumbnails.end(); ++it) {
        if (it->requestId != requestId) continue;
        QNetworkReply* reply = it.key();
        m_pendingCommunityThumbnails.erase(it);
        reply->abort();
        return;
    }
}

void LibrarySharing::readCommunityThumbnail(QNetworkReply* reply)
{
    auto it = m_pendingCommunityThumbnails.find(reply);
    if (it == m_pendingCommunityThumbnails.end()) return;
    const QByteArray chunk = reply->readAll();
    if (it->png.size() + chunk.size() > kMaxCommunityThumbnailBytes) {
        const QString requestId = it->requestId;
        m_pendingCommunityThumbnails.erase(it);
        reply->abort();
        emit communityThumbnailRequestFinished(requestId, false, {}, QStringLiteral("thumbnailTooLarge"));
        return;
    }
    it->png.append(chunk);
}

void LibrarySharing::finishCommunityThumbnail(QNetworkReply* reply)
{
    auto it = m_pendingCommunityThumbnails.find(reply);
    if (it == m_pendingCommunityThumbnails.end()) {
        reply->deleteLater();
        return;
    }
    readCommunityThumbnail(reply);
    it = m_pendingCommunityThumbnails.find(reply);
    if (it == m_pendingCommunityThumbnails.end()) {
        reply->deleteLater();
        return;
    }
    const QString requestId = it->requestId;
    const QByteArray png = it->png;
    m_pendingCommunityThumbnails.erase(it);
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    const bool validPng = png.startsWith(QByteArray::fromHex("89504e470d0a1a0a"));
    const bool success = reply->error() == QNetworkReply::NoError && status == 200
        && contentType.startsWith(QStringLiteral("image/png"), Qt::CaseInsensitive)
        && validPng && !png.isEmpty();
    const QString error = success ? QString() : QStringLiteral("thumbnailUnavailable");
    reply->deleteLater();
    emit communityThumbnailRequestFinished(requestId, success, success ? png : QByteArray(), error);
}

// ---------------------------------------------------------------------------
// Delete from server
// ---------------------------------------------------------------------------

void LibrarySharing::deleteFromServer(const QString& serverId)
{
    if (!deleteFromServerForRequest(QString(), serverId))
        return;
}

bool LibrarySharing::deleteFromServerForRequest(const QString& requestId,
                                                const QString& serverId)
{
    if (serverId.isEmpty())
        return false;
    setLastError(QString());

    QNetworkRequest request = buildRequest("/entries/" + serverId);

    DIAG_DEBUG(APP, "LibrarySharing") << "Deleting server entry" << serverId;

    QNetworkReply* reply = m_networkManager.deleteResource(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId, serverId]() {
        finishDelete(reply, requestId, serverId);
    });
    return true;
}

void LibrarySharing::finishDelete(QNetworkReply* reply, const QString& requestId,
                                  const QString& serverId)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        QString error = reply->errorString();
        DIAG_WARN(APP, "LibrarySharing") << "Delete failed -" << error;
        setLastError(error);
        emit deleteFailed(error);
        emit deleteRequestFinished(requestId, serverId, false, error);
        return;
    }

    // Remove from cache
    if (!serverId.isEmpty()) {
        m_cachedEntries.erase(
            std::remove_if(m_cachedEntries.begin(), m_cachedEntries.end(),
                [&serverId](const QVariant& e) {
                    return e.toMap()["id"].toString() == serverId;
                }),
            m_cachedEntries.end());
        saveCommunityCache();
    }

    DIAG_DEBUG(APP, "LibrarySharing") << "Server entry deleted";
    emit deleteSuccess();
    emit deleteRequestFinished(requestId, serverId, true, QString());
}

// ---------------------------------------------------------------------------
// Flag / report
// ---------------------------------------------------------------------------

void LibrarySharing::flagEntry(const QString& serverId, const QString& reason)
{
    flagEntryForRequest(QString(), serverId, reason);
}

bool LibrarySharing::flagEntryForRequest(const QString& requestId, const QString& serverId,
                                         const QString& reason)
{
    if (serverId.isEmpty()) return false;
    setLastError(QString());

    QNetworkRequest request = buildRequest("/entries/" + serverId + "/flag");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["reason"] = reason;

    QNetworkReply* reply = m_networkManager.post(request,
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    reply->setProperty("libraryFlagRequestId", requestId);
    reply->setProperty("libraryFlagServerId", serverId);
    connect(reply, &QNetworkReply::finished, this, &LibrarySharing::onFlagFinished);
    return true;
}

void LibrarySharing::onFlagFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    const QString requestId = reply->property("libraryFlagRequestId").toString();
    const QString serverId = reply->property("libraryFlagServerId").toString();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        const QString error = reply->errorString();
        DIAG_WARN(APP, "LibrarySharing") << "Flag failed -" << error;
        if (!requestId.isEmpty())
            emit flagRequestFinished(requestId, serverId, false, error);
    } else {
        DIAG_DEBUG(APP, "LibrarySharing") << "Entry flagged";
        if (!requestId.isEmpty())
            emit flagRequestFinished(requestId, serverId, true, QString());
    }
}

// ---------------------------------------------------------------------------
// Community cache
// ---------------------------------------------------------------------------

QString LibrarySharing::cachePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + "/library/community_cache.json";
}

void LibrarySharing::loadCommunityCache()
{
    QFile file(cachePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject obj = doc.object();

    m_newestCreatedAt = obj["newestCreatedAt"].toString();

    QJsonArray arr = obj["entries"].toArray();
    m_cachedEntries.clear();
    for (const auto& val : arr) {
        m_cachedEntries.append(val.toObject().toVariantMap());
    }

    DIAG_DEBUG(APP, "LibrarySharing") << "Loaded community cache -"
             << m_cachedEntries.size() << "entries, newest:" << m_newestCreatedAt;
}

void LibrarySharing::saveCommunityCache()
{
    QDir().mkpath(QFileInfo(cachePath()).absolutePath());

    QJsonArray arr;
    for (const auto& entry : m_cachedEntries) {
        arr.append(QJsonObject::fromVariantMap(entry.toMap()));
    }

    QJsonObject obj;
    obj["newestCreatedAt"] = m_newestCreatedAt;
    obj["entries"] = arr;

    QFile file(cachePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }
}

void LibrarySharing::mergeIntoCache(const QVariantList& newEntries)
{
    // Build set of existing IDs for dedup
    QSet<QString> existingIds;
    for (const auto& e : m_cachedEntries) {
        existingIds.insert(e.toMap()["id"].toString());
    }

    // Prepend new entries (they're newer)
    QVariantList merged;
    for (const auto& e : newEntries) {
        QVariantMap map = e.toMap();
        QString id = map["id"].toString();
        if (!existingIds.contains(id)) {
            merged.append(e);
            // Track newest createdAt
            QString ca = map["createdAt"].toString();
            if (ca > m_newestCreatedAt)
                m_newestCreatedAt = ca;
        }
    }
    merged.append(m_cachedEntries);
    m_cachedEntries = merged;
}

bool LibrarySharing::isUnfilteredBrowse(const QString& type, const QString& variable,
                                         const QString& action, const QString& search) const
{
    return type.isEmpty() && variable.isEmpty() && action.isEmpty() && search.isEmpty();
}

// ---------------------------------------------------------------------------
// Property setters
// ---------------------------------------------------------------------------

void LibrarySharing::setUploading(bool starting)
{
    bool wasBusy = m_activeUploads > 0;
    m_activeUploads += starting ? 1 : -1;
    if (m_activeUploads < 0) m_activeUploads = 0;
    bool isBusy = m_activeUploads > 0;
    if (wasBusy != isBusy)
        emit uploadingChanged();
}

void LibrarySharing::setBrowsing(bool v)
{
    if (m_browsing != v) {
        m_browsing = v;
        emit browsingChanged();
    }
}

void LibrarySharing::setDownloading(bool v)
{
    if (m_downloading != v) {
        m_downloading = v;
        emit downloadingChanged();
    }
}

void LibrarySharing::setLastError(const QString& error)
{
    if (m_lastError != error) {
        m_lastError = error;
        emit lastErrorChanged();
    }
}

void LibrarySharing::setTotalCommunityResults(int count)
{
    if (m_totalCommunityResults != count) {
        m_totalCommunityResults = count;
        emit totalCommunityResultsChanged();
    }
}
