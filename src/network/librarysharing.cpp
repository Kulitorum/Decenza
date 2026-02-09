#include "librarysharing.h"
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

static const char* API_BASE = "https://api.decenza.coffee/v1/library";

LibrarySharing::LibrarySharing(Settings* settings, WidgetLibrary* library, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_library(library)
{
    loadCommunityCache();
}

// ---------------------------------------------------------------------------
// Request builder
// ---------------------------------------------------------------------------

QNetworkRequest LibrarySharing::buildRequest(const QString& path) const
{
    QNetworkRequest request{QUrl(QString("%1%2").arg(API_BASE, path))};
    request.setRawHeader("User-Agent",
        QString("Decenza-DE1/%1").arg(VERSION_STRING).toUtf8());
    request.setRawHeader("X-Device-Id", m_settings->deviceId().toUtf8());
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
    QByteArray entryJson = m_library->exportEntry(entryId);
    if (entryJson.isEmpty()) {
        setLastError("Entry not found: " + entryId);
        emit uploadFailed(m_lastError);
        return;
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

    qDebug() << "LibrarySharing: Uploading entry" << entryId
             << "(" << body.size() << "bytes)";
    if (!fullPng.isEmpty())
        qDebug() << "LibrarySharing: Thumbnail full:" << fullPng.size() << "bytes";
    if (!compactPng.isEmpty())
        qDebug() << "LibrarySharing: Thumbnail compact:" << compactPng.size() << "bytes";

    QNetworkReply* reply = m_networkManager.post(request, body);

    // Capture local ID per-upload so concurrent uploads each track their own entry
    QString localId = entryId;
    connect(reply, &QNetworkReply::finished, this, [this, reply, localId]() {
        handleUploadFinished(reply, localId);
    });
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

void LibrarySharing::handleUploadFinished(QNetworkReply* reply, const QString& localEntryId)
{
    reply->deleteLater();
    setUploading(false);

    QByteArray responseBody = reply->readAll();
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qDebug() << "LibrarySharing: Upload response status:" << statusCode
             << "for local entry:" << localEntryId;

    QJsonDocument doc = QJsonDocument::fromJson(responseBody);
    QJsonObject obj = doc.object();

    // 409 = duplicate entry already exists on server
    if (statusCode == 409) {
        QString existingId = obj["existingId"].toString();
        qDebug() << "LibrarySharing: Already shared (existing ID:" << existingId << ")";

        // Still adopt the server ID so future downloads match
        if (!existingId.isEmpty() && existingId != localEntryId) {
            m_library->renameEntry(localEntryId, existingId);
        }

        setLastError("Already shared");
        emit uploadFailed("Already shared");
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QString error = obj["error"].toString(reply->errorString());
        qWarning() << "LibrarySharing: Upload failed -" << error;
        setLastError(error);
        emit uploadFailed(error);
        return;
    }

    if (obj.contains("id")) {
        QString serverId = obj["id"].toString();
        qDebug() << "LibrarySharing: Upload successful, server ID:" << serverId
                 << "(local was:" << localEntryId << ")";

        // Rename local entry to match server ID so downloads won't create duplicates
        if (!localEntryId.isEmpty() && serverId != localEntryId) {
            m_library->renameEntry(localEntryId, serverId);
        }

        emit uploadSuccess(serverId);
    } else {
        QString error = obj["error"].toString("Upload failed");
        qWarning() << "LibrarySharing: Server error -" << error;
        setLastError(error);
        emit uploadFailed(error);
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
    if (m_browsing) {
        qWarning() << "LibrarySharing: Already browsing";
        return;
    }

    bool unfiltered = isUnfilteredBrowse(type, variable, action, search);

    // For unfiltered page-1 requests, show cache immediately and do incremental fetch
    if (unfiltered && page == 1 && !m_cachedEntries.isEmpty()) {
        m_communityEntries = m_cachedEntries;
        emit communityEntriesChanged();
        setTotalCommunityResults(m_cachedEntries.size());
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

    QUrl url(QString("%1/entries").arg(API_BASE));
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent",
        QString("Decenza-DE1/%1").arg(VERSION_STRING).toUtf8());
    request.setRawHeader("X-Device-Id", m_settings->deviceId().toUtf8());

    qDebug() << "LibrarySharing: Browsing community -" << url.toString();

    QNetworkReply* reply = m_networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, &LibrarySharing::onBrowseFinished);
}

void LibrarySharing::browseMyUploads(int page)
{
    if (m_browsing) {
        qWarning() << "LibrarySharing: Already browsing";
        return;
    }

    setBrowsing(true);
    setLastError(QString());

    QUrlQuery query;
    query.addQueryItem("device_id", "mine");
    query.addQueryItem("page", QString::number(page));

    QUrl url(QString("%1/entries").arg(API_BASE));
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent",
        QString("Decenza-DE1/%1").arg(VERSION_STRING).toUtf8());
    request.setRawHeader("X-Device-Id", m_settings->deviceId().toUtf8());

    qDebug() << "LibrarySharing: Browsing my uploads";

    QNetworkReply* reply = m_networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, &LibrarySharing::onBrowseFinished);
}

void LibrarySharing::onBrowseFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    setBrowsing(false);

    if (reply->error() != QNetworkReply::NoError) {
        QString error = reply->errorString();
        qWarning() << "LibrarySharing: Browse failed -" << error;
        setLastError(error);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject obj = doc.object();

    QVariantList entries;
    QJsonArray arr = obj["entries"].toArray();
    for (const auto& val : arr) {
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

        qDebug() << "LibrarySharing: Removed" << deletedIds.size() << "deleted entries from cache";
    }

    if (m_browseIsIncremental) {
        // Incremental: merge new entries into cache
        qDebug() << "LibrarySharing: Incremental fetch returned" << entries.size() << "new entries";
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
        qDebug() << "LibrarySharing: Cached" << entries.size() << "entries, newest:" << m_newestCreatedAt;
    } else {
        // Filtered or paginated query - no caching
        m_communityEntries = entries;
    }

    emit communityEntriesChanged();
    int total = obj["total"].toInt(m_communityEntries.size());
    setTotalCommunityResults(total);

    qDebug() << "LibrarySharing: Browse total:" << total << "displayed:" << m_communityEntries.size();
    m_browseIsIncremental = false;
    m_browseIsUnfiltered = false;
}

// ---------------------------------------------------------------------------
// Featured
// ---------------------------------------------------------------------------

void LibrarySharing::loadFeatured()
{
    setLastError(QString());

    QUrlQuery query;
    query.addQueryItem("sort", "popular");
    query.addQueryItem("page", "1");
    query.addQueryItem("per_page", "10");

    QUrl url(QString("%1/entries").arg(API_BASE));
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent",
        QString("Decenza-DE1/%1").arg(VERSION_STRING).toUtf8());
    request.setRawHeader("X-Device-Id", m_settings->deviceId().toUtf8());

    qDebug() << "LibrarySharing: Loading featured entries -" << url.toString();

    QNetworkReply* reply = m_networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, &LibrarySharing::onFeaturedFinished);
}

void LibrarySharing::onFeaturedFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "LibrarySharing: Featured load failed -" << reply->errorString();
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject obj = doc.object();

    QVariantList entries;
    QJsonArray arr = obj["entries"].toArray();
    for (const auto& val : arr) {
        entries.append(val.toObject().toVariantMap());
    }

    m_featuredEntries = entries;
    emit featuredEntriesChanged();

    qDebug() << "LibrarySharing: Loaded" << entries.size() << "featured entries";
}

// ---------------------------------------------------------------------------
// Download (two-step: metadata, then full data on confirm)
// ---------------------------------------------------------------------------

void LibrarySharing::downloadEntry(const QString& serverId)
{
    if (m_downloading) {
        qWarning() << "LibrarySharing: Already downloading";
        return;
    }

    setDownloading(true);
    setLastError(QString());
    m_pendingDownloadId = serverId;

    // Fetch full entry data
    QNetworkRequest request = buildRequest("/entries/" + serverId);

    qDebug() << "LibrarySharing: Downloading entry" << serverId;

    QNetworkReply* reply = m_networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, &LibrarySharing::onDownloadDataFinished);
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
    reply->deleteLater();
    setDownloading(false);

    if (reply->error() != QNetworkReply::NoError) {
        QString error = reply->errorString();
        qWarning() << "LibrarySharing: Download failed -" << error;
        setLastError(error);
        emit downloadFailed(error);
        return;
    }

    QByteArray data = reply->readAll();

    // Check if entry already exists locally (same server ID)
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QString serverId = doc.object()["id"].toString();
    if (!serverId.isEmpty() && !m_library->getEntry(serverId).isEmpty()) {
        qDebug() << "LibrarySharing: Entry already in library:" << serverId;
        emit downloadAlreadyExists(serverId);
        return;
    }

    // Import into local library
    QString localId = m_library->importEntry(data);
    if (localId.isEmpty()) {
        QString error = "Failed to import downloaded entry";
        setLastError(error);
        emit downloadFailed(error);
        return;
    }

    qDebug() << "LibrarySharing: Downloaded and imported as" << localId;
    emit downloadComplete(localId);

    // Record download on server (fire-and-forget)
    QNetworkRequest req = buildRequest("/entries/" + m_pendingDownloadId + "/download");
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
        qDebug() << "LibrarySharing: Failed to record download (non-critical) -"
                 << reply->errorString();
    }
}

// ---------------------------------------------------------------------------
// Delete from server
// ---------------------------------------------------------------------------

void LibrarySharing::deleteFromServer(const QString& serverId)
{
    setLastError(QString());

    QNetworkRequest request = buildRequest("/entries/" + serverId);

    m_pendingDeleteId = serverId;
    qDebug() << "LibrarySharing: Deleting server entry" << serverId;

    QNetworkReply* reply = m_networkManager.deleteResource(request);
    connect(reply, &QNetworkReply::finished, this, &LibrarySharing::onDeleteFinished);
}

void LibrarySharing::onDeleteFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        QString error = reply->errorString();
        qWarning() << "LibrarySharing: Delete failed -" << error;
        setLastError(error);
        emit deleteFailed(error);
        return;
    }

    // Remove from cache
    if (!m_pendingDeleteId.isEmpty()) {
        m_cachedEntries.erase(
            std::remove_if(m_cachedEntries.begin(), m_cachedEntries.end(),
                [this](const QVariant& e) {
                    return e.toMap()["id"].toString() == m_pendingDeleteId;
                }),
            m_cachedEntries.end());
        saveCommunityCache();
    }

    qDebug() << "LibrarySharing: Server entry deleted";
    emit deleteSuccess();
}

// ---------------------------------------------------------------------------
// Flag / report
// ---------------------------------------------------------------------------

void LibrarySharing::flagEntry(const QString& serverId, const QString& reason)
{
    setLastError(QString());

    QNetworkRequest request = buildRequest("/entries/" + serverId + "/flag");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["reason"] = reason;

    QNetworkReply* reply = m_networkManager.post(request,
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, &LibrarySharing::onFlagFinished);
}

void LibrarySharing::onFlagFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "LibrarySharing: Flag failed -" << reply->errorString();
    } else {
        qDebug() << "LibrarySharing: Entry flagged";
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

    qDebug() << "LibrarySharing: Loaded community cache -"
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
