#include "shotserver.h"
#include "webdebuglogger.h"
#include "webtemplates.h"
#include "../history/shothistorystorage.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../screensaver/screensavervideomanager.h"
#include "../core/settings.h"
#include "../core/profilestorage.h"
#include "../core/settingsserializer.h"
#include "../ai/aimanager.h"
#include "../core/widgetlibrary.h"
#include "librarysharing.h"
#include "version.h"

#include <QNetworkInterface>
#include <QUdpSocket>
#include <QSet>
#include <QFile>
#include <QBuffer>
#include <algorithm>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QUrl>
#include <QUrlQuery>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#ifndef Q_OS_IOS
#include <QProcess>
#endif
#include <QCoreApplication>
#include <QRegularExpression>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

void ShotServer::handleLayoutApi(QTcpSocket* socket, const QString& method, const QString& path, const QByteArray& body)
{
    if (!m_settings) {
        sendResponse(socket, 500, "application/json", R"({"error":"Settings not available"})");
        return;
    }

    // GET /api/layout — return current layout configuration
    if (method == "GET" && (path == "/api/layout" || path == "/api/layout/")) {
        QString json = m_settings->layoutConfiguration();
        sendJson(socket, json.toUtf8());
        return;
    }

    // GET /api/layout/item?id=X — return item properties
    if (method == "GET" && path.startsWith("/api/layout/item")) {
        QString itemId;
        int qIdx = path.indexOf("?id=");
        if (qIdx >= 0) {
            itemId = QUrl::fromPercentEncoding(path.mid(qIdx + 4).toUtf8());
        }
        if (itemId.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing id parameter"})");
            return;
        }
        QVariantMap props = m_settings->getItemProperties(itemId);
        sendJson(socket, QJsonDocument(QJsonObject::fromVariantMap(props)).toJson(QJsonDocument::Compact));
        return;
    }

    // GET /api/library/entries — list all local library entries
    if (method == "GET" && path == "/api/library/entries") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Library not available"})");
            return;
        }
        QVariantList entries = m_widgetLibrary->entries();
        QJsonArray arr;
        for (const QVariant& v : entries) {
            QJsonObject obj = QJsonObject::fromVariantMap(v.toMap());
            QString id = obj["id"].toString();
            if (!id.isEmpty()) {
                QString encodedId = QString::fromLatin1(QUrl::toPercentEncoding(id));
                if (m_widgetLibrary->hasThumbnail(id))
                    obj["thumbnailFullUrl"] = "/api/library/thumbnail?id=" + encodedId;
                if (m_widgetLibrary->hasThumbnailCompact(id))
                    obj["thumbnailCompactUrl"] = "/api/library/thumbnail?id=" + encodedId + "&compact=1";
            }
            arr.append(obj);
        }
        sendJson(socket, QJsonDocument(arr).toJson(QJsonDocument::Compact));
        return;
    }

    // GET /api/library/thumbnail?id=X[&compact=1] — serve thumbnail PNG
    if (method == "GET" && path.startsWith("/api/library/thumbnail")) {
        if (!m_widgetLibrary) {
            sendResponse(socket, 404, "text/plain", "Not found");
            return;
        }
        int qIdx = path.indexOf("?id=");
        QString rawId = (qIdx >= 0) ? path.mid(qIdx + 4) : QString();
        int ampIdx = rawId.indexOf('&');
        if (ampIdx >= 0) rawId = rawId.left(ampIdx);
        QString entryId = QUrl::fromPercentEncoding(rawId.toUtf8());
        bool compact = path.contains("&compact=1");
        QString thumbPath;
        bool exists = false;
        if (!entryId.isEmpty()) {
            if (compact && m_widgetLibrary->hasThumbnailCompact(entryId)) {
                thumbPath = m_widgetLibrary->thumbnailCompactPath(entryId);
                exists = true;
            } else if (m_widgetLibrary->hasThumbnail(entryId)) {
                thumbPath = m_widgetLibrary->thumbnailPath(entryId);
                exists = true;
            }
        }
        if (exists) {
            QFile file(thumbPath);
            if (file.open(QIODevice::ReadOnly)) {
                sendResponse(socket, 200, "image/png", file.readAll());
            } else {
                sendResponse(socket, 404, "text/plain", "No thumbnail");
            }
        } else {
            sendResponse(socket, 404, "text/plain", "No thumbnail");
        }
        return;
    }

    // GET /api/library/entry?id=X — get full entry data
    if (method == "GET" && path.startsWith("/api/library/entry")) {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Library not available"})");
            return;
        }
        int qIdx = path.indexOf("?id=");
        QString entryId = (qIdx >= 0) ? QUrl::fromPercentEncoding(path.mid(qIdx + 4).toUtf8()) : QString();
        if (entryId.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing id parameter"})");
            return;
        }
        QVariantMap data = m_widgetLibrary->getEntryData(entryId);
        if (data.isEmpty()) {
            sendResponse(socket, 404, "application/json", R"({"error":"Entry not found"})");
            return;
        }
        sendJson(socket, QJsonDocument(QJsonObject::fromVariantMap(data)).toJson(QJsonDocument::Compact));
        return;
    }

    // GET /api/community/browse — browse community entries (async)
    if (method == "GET" && path.startsWith("/api/community/browse")) {
        if (!m_librarySharing) {
            sendJson(socket, R"({"error":"Community sharing not available"})");
            return;
        }
        QUrl url("http://localhost" + path);
        QUrlQuery query(url);
        QString type = query.queryItemValue("type");
        QString variable = query.queryItemValue("variable");
        QString action = query.queryItemValue("action");
        QString search = query.queryItemValue("search");
        QString sort = query.queryItemValue("sort", QUrl::FullyDecoded);
        int page = query.queryItemValue("page").toInt();
        if (page < 1) page = 1;
        if (sort.isEmpty()) sort = "newest";

        if (hasInFlightLibraryRequest(LibraryRequestType::Browse)) {
            sendJson(socket, R"({"error":"A browse request is already in progress"})");
            return;
        }

        // Reject if LibrarySharing is already busy (e.g. from QML)
        if (m_librarySharing->isBrowsing()) {
            sendJson(socket, R"({"error":"Community service is busy, please try again"})");
            return;
        }

        int reqId = m_nextLibraryRequestId++;
        auto* timer = new QTimer(this);
        timer->setSingleShot(true);

        PendingLibraryRequest req;
        req.type = LibraryRequestType::Browse;
        req.socket = QPointer<QTcpSocket>(socket);
        req.timeoutTimer = timer;

        req.connections.append(connect(m_librarySharing, &LibrarySharing::communityEntriesChanged, this, [this, reqId]() {
            QJsonObject resp;
            resp["success"] = true;
            QVariantList entries = m_librarySharing->communityEntries();
            QJsonArray arr;
            for (const QVariant& v : entries)
                arr.append(QJsonObject::fromVariantMap(v.toMap()));
            resp["entries"] = arr;
            resp["total"] = m_librarySharing->totalCommunityResults();
            completeLibraryRequest(reqId, resp);
        }));
        req.connections.append(connect(m_librarySharing, &LibrarySharing::lastErrorChanged, this, [this, reqId]() {
            QString error = m_librarySharing->lastError();
            if (error.isEmpty()) return;
            completeLibraryRequest(reqId, QJsonObject{{"error", error}});
        }));
        req.connections.append(connect(timer, &QTimer::timeout, this, [this, reqId]() {
            qWarning() << "ShotServer: Library browse request" << reqId << "timed out after" << kLibraryTimeoutMs / 1000 << "s";
            completeLibraryRequest(reqId, QJsonObject{{"error", "Request timed out"}});
        }));

        m_pendingLibraryRequests.insert(reqId, req);
        timer->start(kLibraryTimeoutMs);

        // Register signals BEFORE calling browseCommunity(), which may synchronously
        // emit from cache. The fired guard ensures at most one response is sent.
        m_librarySharing->browseCommunity(type, variable, action, search, sort, page);

        // Guard against TOCTOU: isBrowsing() may be false if the cache already
        // completed the request synchronously, so also check the request still exists.
        if (!m_librarySharing->isBrowsing() && m_pendingLibraryRequests.contains(reqId)) {
            qWarning() << "ShotServer: browseCommunity() was rejected (busy between check and call)";
            completeLibraryRequest(reqId, QJsonObject{{"error", "Browse request was rejected, please try again"}});
        }

        return;
    }

    // All remaining layout/library/community endpoints are POST
    if (method != "POST") {
        sendResponse(socket, 405, "application/json", R"({"error":"Method not allowed"})");
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "ShotServer: Failed to parse POST body:" << err.errorString();
        sendResponse(socket, 400, "application/json", R"({"error":"Invalid JSON body"})");
        return;
    }
    QJsonObject obj = doc.object();

    if (path == "/api/layout/add") {
        QString type = obj["type"].toString();
        QString zone = obj["zone"].toString();
        int index = obj.contains("index") ? obj["index"].toInt() : -1;
        if (type.isEmpty() || zone.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing type or zone"})");
            return;
        }
        m_settings->addItem(type, zone, index);
        sendJson(socket, R"({"success":true})");
    }
    else if (path == "/api/layout/remove") {
        QString itemId = obj["itemId"].toString();
        QString zone = obj["zone"].toString();
        if (itemId.isEmpty() || zone.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing itemId or zone"})");
            return;
        }
        m_settings->removeItem(itemId, zone);
        sendJson(socket, R"({"success":true})");
    }
    else if (path == "/api/layout/move") {
        QString itemId = obj["itemId"].toString();
        QString fromZone = obj["fromZone"].toString();
        QString toZone = obj["toZone"].toString();
        int toIndex = obj.contains("toIndex") ? obj["toIndex"].toInt() : -1;
        if (itemId.isEmpty() || fromZone.isEmpty() || toZone.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing itemId, fromZone, or toZone"})");
            return;
        }
        m_settings->moveItem(itemId, fromZone, toZone, toIndex);
        sendJson(socket, R"({"success":true})");
    }
    else if (path == "/api/layout/reorder") {
        QString zone = obj["zone"].toString();
        int fromIndex = obj["fromIndex"].toInt();
        int toIndex = obj["toIndex"].toInt();
        if (zone.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing zone"})");
            return;
        }
        m_settings->reorderItem(zone, fromIndex, toIndex);
        sendJson(socket, R"({"success":true})");
    }
    else if (path == "/api/layout/reset") {
        m_settings->resetLayoutToDefault();
        sendJson(socket, R"({"success":true})");
    }
    else if (path == "/api/layout/item") {
        QString itemId = obj["itemId"].toString();
        QString key = obj["key"].toString();
        if (itemId.isEmpty() || key.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing itemId or key"})");
            return;
        }
        QVariant value = obj["value"].toVariant();
        m_settings->setItemProperty(itemId, key, value);
        sendJson(socket, R"({"success":true})");
    }
    else if (path == "/api/layout/zone-offset") {
        QString zone = obj["zone"].toString();
        int offset = obj["offset"].toInt();
        if (zone.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing zone"})");
            return;
        }
        m_settings->setZoneYOffset(zone, offset);
        sendJson(socket, R"({"success":true})");
    }
    else if (path == "/api/layout/zone-scale") {
        QString zone = obj["zone"].toString();
        double scale = obj["scale"].toDouble(1.0);
        if (zone.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing zone"})");
            return;
        }
        m_settings->setZoneScale(zone, scale);
        sendJson(socket, R"({"success":true})");
    }
    // ========== Library API (local, synchronous) ==========

    else if (method == "POST" && path == "/api/library/save-item") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Library not available"})");
            return;
        }
        QString itemId = obj["itemId"].toString();
        if (itemId.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing itemId"})");
            return;
        }
        QString entryId = m_widgetLibrary->addItemFromLayout(itemId);
        if (entryId.isEmpty()) {
            sendJson(socket, R"({"error":"Failed to save item"})");
            return;
        }
        // Thumbnail capture is automatic via entryAdded signal
        QJsonObject resp;
        resp["success"] = true;
        resp["entryId"] = entryId;
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
    }
    else if (method == "POST" && path == "/api/library/save-zone") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Library not available"})");
            return;
        }
        QString zone = obj["zone"].toString();
        if (zone.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing zone"})");
            return;
        }
        QString entryId = m_widgetLibrary->addZoneFromLayout(zone);
        if (entryId.isEmpty()) {
            sendJson(socket, R"({"error":"Failed to save zone"})");
            return;
        }
        // Thumbnail capture is automatic via entryAdded signal
        QJsonObject resp;
        resp["success"] = true;
        resp["entryId"] = entryId;
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
    }
    else if (method == "POST" && path == "/api/library/save-layout") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Library not available"})");
            return;
        }
        QString entryId = m_widgetLibrary->addCurrentLayout(false);
        if (entryId.isEmpty()) {
            sendJson(socket, R"({"error":"Failed to save layout"})");
            return;
        }
        // Thumbnail capture is automatic via entryAdded signal
        QJsonObject resp;
        resp["success"] = true;
        resp["entryId"] = entryId;
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
    }
    else if (method == "POST" && path == "/api/library/apply") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Library not available"})");
            return;
        }
        QString entryId = obj["entryId"].toString();
        QString targetZone = obj["zone"].toString();
        if (entryId.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing entryId"})");
            return;
        }
        // Determine type from entry metadata
        QVariantMap meta = m_widgetLibrary->getEntry(entryId);
        QString type = meta["type"].toString();
        bool ok = false;
        if (type == "item") {
            if (targetZone.isEmpty()) {
                sendResponse(socket, 400, "application/json", R"({"error":"Missing zone for item apply"})");
                return;
            }
            ok = m_widgetLibrary->applyItem(entryId, targetZone);
        } else if (type == "zone") {
            if (targetZone.isEmpty()) {
                sendResponse(socket, 400, "application/json", R"({"error":"Missing zone for zone apply"})");
                return;
            }
            ok = m_widgetLibrary->applyZone(entryId, targetZone);
        } else if (type == "layout") {
            ok = m_widgetLibrary->applyLayout(entryId, false);
        }
        QJsonObject resp;
        resp["success"] = ok;
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
    }
    else if (method == "POST" && path == "/api/library/delete") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Library not available"})");
            return;
        }
        QString entryId = obj["entryId"].toString();
        if (entryId.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing entryId"})");
            return;
        }
        bool ok = m_widgetLibrary->removeEntry(entryId);
        QJsonObject resp;
        resp["success"] = ok;
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
    }

    // ========== Community API (async, signal-based) ==========

    else if (method == "POST" && path == "/api/community/download") {
        if (!m_librarySharing) {
            sendJson(socket, R"({"error":"Community sharing not available"})");
            return;
        }
        QString serverId = obj["serverId"].toString();
        if (serverId.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing serverId"})");
            return;
        }
        if (hasInFlightLibraryRequest(LibraryRequestType::Download)) {
            sendJson(socket, R"({"error":"A download is already in progress"})");
            return;
        }

        // Reject if LibrarySharing is already busy (e.g. from QML)
        if (m_librarySharing->isDownloading()) {
            sendJson(socket, R"({"error":"A download is already in progress, please try again"})");
            return;
        }

        int reqId = m_nextLibraryRequestId++;
        auto* timer = new QTimer(this);
        timer->setSingleShot(true);

        PendingLibraryRequest req;
        req.type = LibraryRequestType::Download;
        req.socket = QPointer<QTcpSocket>(socket);
        req.timeoutTimer = timer;

        req.connections.append(connect(m_librarySharing, &LibrarySharing::downloadComplete, this, [this, reqId](const QString& localEntryId) {
            QJsonObject resp;
            resp["success"] = true;
            resp["localEntryId"] = localEntryId;
            completeLibraryRequest(reqId, resp);
        }));
        req.connections.append(connect(m_librarySharing, &LibrarySharing::downloadAlreadyExists, this, [this, reqId](const QString& localEntryId) {
            QJsonObject resp;
            resp["success"] = true;
            resp["localEntryId"] = localEntryId;
            resp["alreadyExists"] = true;
            completeLibraryRequest(reqId, resp);
        }));
        req.connections.append(connect(m_librarySharing, &LibrarySharing::downloadFailed, this, [this, reqId](const QString& error) {
            completeLibraryRequest(reqId, QJsonObject{{"error", error}});
        }));
        req.connections.append(connect(timer, &QTimer::timeout, this, [this, reqId]() {
            qWarning() << "ShotServer: Library download request" << reqId << "timed out after" << kLibraryTimeoutMs / 1000 << "s";
            completeLibraryRequest(reqId, QJsonObject{{"error", "Request timed out"}});
        }));

        m_pendingLibraryRequests.insert(reqId, req);
        timer->start(kLibraryTimeoutMs);

        m_librarySharing->downloadEntry(serverId);

        // Guard against TOCTOU: downloadEntry() may silently reject if busy
        if (!m_librarySharing->isDownloading()) {
            qWarning() << "ShotServer: downloadEntry() was rejected (busy between check and call)";
            completeLibraryRequest(reqId, QJsonObject{{"error", "Download service busy, please try again"}});
        }
    }
    else if (method == "POST" && path == "/api/community/upload") {
        if (!m_librarySharing) {
            sendJson(socket, R"({"error":"Community sharing not available"})");
            return;
        }
        QString entryId = obj["entryId"].toString();
        if (entryId.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing entryId"})");
            return;
        }
        if (hasInFlightLibraryRequest(LibraryRequestType::Upload)) {
            sendJson(socket, R"({"error":"An upload is already in progress"})");
            return;
        }
        // We serialize web uploads to prevent cross-source signal confusion,
        // even though LibrarySharing supports concurrent uploads internally

        int reqId = m_nextLibraryRequestId++;
        auto* timer = new QTimer(this);
        timer->setSingleShot(true);

        PendingLibraryRequest req;
        req.type = LibraryRequestType::Upload;
        req.socket = QPointer<QTcpSocket>(socket);
        req.timeoutTimer = timer;

        req.connections.append(connect(m_librarySharing, &LibrarySharing::uploadSuccess, this, [this, reqId](const QString& serverId) {
            QJsonObject resp;
            resp["success"] = true;
            resp["serverId"] = serverId;
            completeLibraryRequest(reqId, resp);
        }));
        req.connections.append(connect(m_librarySharing, &LibrarySharing::uploadFailed, this, [this, reqId](const QString& error) {
            QJsonObject resp;
            resp["error"] = error;
            if (error == "Already shared") {
                resp["existingId"] = m_librarySharing->lastExistingId();
            }
            completeLibraryRequest(reqId, resp);
        }));
        req.connections.append(connect(timer, &QTimer::timeout, this, [this, reqId]() {
            qWarning() << "ShotServer: Library upload request" << reqId << "timed out after" << kLibraryTimeoutMs / 1000 << "s";
            completeLibraryRequest(reqId, QJsonObject{{"error", "Request timed out"}});
        }));

        m_pendingLibraryRequests.insert(reqId, req);
        timer->start(kLibraryTimeoutMs);

        // Load local thumbnails if available (generated by QML on save)
        QImage thumbnail, thumbnailCompact;
        if (m_widgetLibrary && m_widgetLibrary->hasThumbnail(entryId)) {
            if (!thumbnail.load(m_widgetLibrary->thumbnailPath(entryId)))
                qWarning() << "ShotServer: Failed to load thumbnail for upload:" << entryId;
        }
        if (m_widgetLibrary && m_widgetLibrary->hasThumbnailCompact(entryId)) {
            if (!thumbnailCompact.load(m_widgetLibrary->thumbnailCompactPath(entryId)))
                qWarning() << "ShotServer: Failed to load compact thumbnail for upload:" << entryId;
        }
        m_librarySharing->uploadEntryWithThumbnails(entryId, thumbnail, thumbnailCompact);
    }
    else if (method == "POST" && path == "/api/community/delete") {
        if (!m_librarySharing) {
            sendJson(socket, R"({"error":"Community sharing not available"})");
            return;
        }
        QString serverId = obj["serverId"].toString();
        if (serverId.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing serverId"})");
            return;
        }
        if (hasInFlightLibraryRequest(LibraryRequestType::Delete)) {
            sendJson(socket, R"({"error":"A delete is already in progress"})");
            return;
        }
        int reqId = m_nextLibraryRequestId++;
        auto* timer = new QTimer(this);
        timer->setSingleShot(true);

        PendingLibraryRequest req;
        req.type = LibraryRequestType::Delete;
        req.socket = QPointer<QTcpSocket>(socket);
        req.timeoutTimer = timer;

        req.connections.append(connect(m_librarySharing, &LibrarySharing::deleteSuccess, this, [this, reqId]() {
            completeLibraryRequest(reqId, QJsonObject{{"success", true}});
        }));
        req.connections.append(connect(m_librarySharing, &LibrarySharing::deleteFailed, this, [this, reqId](const QString& error) {
            completeLibraryRequest(reqId, QJsonObject{{"error", error}});
        }));
        req.connections.append(connect(timer, &QTimer::timeout, this, [this, reqId]() {
            qWarning() << "ShotServer: Library delete request" << reqId << "timed out after" << kLibraryTimeoutMs / 1000 << "s";
            completeLibraryRequest(reqId, QJsonObject{{"error", "Request timed out"}});
        }));

        m_pendingLibraryRequests.insert(reqId, req);
        timer->start(kLibraryTimeoutMs);

        m_librarySharing->deleteFromServer(serverId);
    }

    else {
        sendResponse(socket, 404, "application/json", R"({"error":"Unknown layout endpoint"})");
    }
}

QString ShotServer::generateLayoutPage() const
{
    QString html;

    // Part 1: Head and base CSS
    html += R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Layout Editor - Decenza DE1</title>
    <style>
)HTML";
    html += WEB_CSS_VARIABLES;
    html += WEB_CSS_HEADER;
    html += WEB_CSS_MENU;

    // Part 2: Page-specific CSS
    html += R"HTML(
        .main-layout {
            display: flex;
            flex-direction: column;
            gap: 1.5rem;
            max-width: 1400px;
            margin: 0 auto;
            padding: 1.5rem;
        }
        .zones-panel { min-width: 0; }
        .editor-panel { }
        .zone-card {
            background: var(--surface);
            border: 2px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            margin-bottom: 1rem;
            transition: border-color 0.15s;
        }
        .zone-card.selected { border-color: var(--accent); }
        .zone-header {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-bottom: 0.75rem;
        }
        .zone-title {
            color: var(--text-secondary);
            font-size: 0.8rem;
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .zone-row { display: flex; gap: 0.5rem; }
        .zone-offset-controls { display: flex; gap: 0.25rem; align-items: center; }
        .offset-separator { width: 1px; height: 20px; background: var(--border); margin: 0 0.25rem; }
        .offset-btn {
            background: none;
            border: 1px solid var(--border);
            color: var(--accent);
            width: 28px;
            height: 28px;
            border-radius: 6px;
            cursor: pointer;
            font-size: 0.75rem;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        .offset-btn:hover { background: var(--surface-hover); }
        .offset-val {
            color: var(--text-secondary);
            font-size: 0.75rem;
            min-width: 2rem;
            text-align: center;
        }
        .chips-area {
            display: flex;
            flex-wrap: wrap;
            gap: 0.5rem;
            align-items: center;
            min-height: 40px;
        }
        .chip {
            display: inline-flex;
            align-items: center;
            gap: 0.25rem;
            padding: 0.375rem 0.75rem;
            border-radius: 8px;
            background: var(--bg);
            border: 1px solid var(--border);
            color: var(--text);
            cursor: pointer;
            font-size: 0.875rem;
            user-select: none;
            transition: all 0.15s;
        }
        .chip:hover { border-color: var(--accent); }
        .chip.selected {
            border: 2px solid var(--accent);
        }
        .chip.special { color: orange; }
        .chip.selected.special { color: orange; }
        .chip.screensaver { color: #64B5F6; }
        .chip.selected.screensaver { color: #64B5F6; }
        .chip-arrow {
            cursor: pointer;
            font-size: 1rem;
            opacity: 0.8;
        }
        .chip-arrow:hover { opacity: 1; }
)HTML";
    html += R"HTML(
        .chip-remove {
            cursor: pointer;
            color: #f85149;
            font-weight: bold;
            font-size: 1rem;
            margin-left: 0.25rem;
        }
        .add-btn {
            width: 36px;
            height: 36px;
            border-radius: 8px;
            background: none;
            border: 1px solid var(--accent);
            color: var(--accent);
            font-size: 1.25rem;
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
            position: relative;
        }
        .add-btn:hover { background: rgba(201,162,39,0.1); }
        .add-dropdown {
            display: none;
            position: absolute;
            top: 100%;
            left: 0;
            margin-top: 0.25rem;
            margin-bottom: 0.25rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            box-shadow: 0 4px 12px rgba(0,0,0,0.3);
            z-index: 50;
            min-width: 160px;
            max-height: 400px;
            overflow-y: auto;
        }
        .add-dropdown.open { display: block; }
        .add-dropdown-item {
            display: block;
            padding: 0.5rem 0.75rem;
            color: var(--text);
            cursor: pointer;
            font-size: 0.875rem;
            white-space: nowrap;
        }
        .add-dropdown-item:hover { background: var(--surface-hover); }
        .add-dropdown-item.special { color: orange; }
        .add-dropdown-item.screensaver { color: #64B5F6; }
        .reset-btn {
            background: none;
            border: 1px solid var(--border);
            color: var(--text-secondary);
            padding: 0.375rem 0.75rem;
            border-radius: 6px;
            cursor: pointer;
            font-size: 0.8rem;
        }
        .reset-btn:hover { color: var(--accent); border-color: var(--accent); }

        /* Text editor panel */
        .editor-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1.25rem;
        }
        .editor-card h3 {
            font-size: 0.9rem;
            margin-bottom: 1rem;
            color: var(--accent);
        }
        .editor-hidden { display: none; }
        .ss-editor-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1.25rem;
        }
        .ss-editor-card h3 {
            margin: 0 0 1rem;
            font-size: 1rem;
            color: var(--text);
        }
        .ss-slider-row {
            display: flex;
            align-items: center;
            gap: 0.75rem;
            margin-bottom: 0.75rem;
        }
        .ss-slider-label {
            font-size: 0.8rem;
            color: var(--text-secondary);
            min-width: 50px;
        }
        .ss-slider {
            flex: 1;
            -webkit-appearance: none;
            appearance: none;
            height: 6px;
            border-radius: 3px;
            background: var(--border);
            outline: none;
        }
        .ss-slider::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 18px;
            height: 18px;
            border-radius: 50%;
            background: var(--accent);
            cursor: pointer;
        }
        .ss-btn-group {
            display: flex;
            gap: 0.375rem;
            margin-bottom: 0.75rem;
        }
        .ss-btn-option {
            flex: 1;
            padding: 0.375rem 0.25rem;
            border-radius: 6px;
            border: 1px solid var(--border);
            background: none;
            color: var(--text);
            font-size: 0.8rem;
            cursor: pointer;
            text-align: center;
        }
        .ss-btn-option:hover { border-color: var(--accent); }
        .ss-btn-option.active {
            background: var(--accent);
            border-color: var(--accent);
            color: #000;
        }
        .ss-no-settings {
            color: var(--text-secondary);
            font-size: 0.875rem;
            text-align: center;
            padding: 1rem 0;
        }
        .toolbar {
            display: flex;
            flex-wrap: wrap;
            gap: 0.25rem;
            margin: 0.75rem 0;
        }
        .tool-btn {
            width: 32px;
            height: 32px;
            border-radius: 4px;
            background: var(--bg);
            border: 1px solid var(--border);
            color: var(--text);
            cursor: pointer;
            font-size: 0.8rem;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        .tool-btn:hover { border-color: var(--accent); }
        .tool-btn.active { background: var(--accent); color: #000; border-color: var(--accent); }
        .tool-sep { width: 1px; height: 24px; background: var(--border); align-self: center; margin: 0 0.25rem; }
        .section-label {
            font-size: 0.75rem;
            color: var(--text-secondary);
            margin: 0.5rem 0 0.25rem;
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .var-list {
            max-height: 180px;
            overflow-y: auto;
            border: 1px solid var(--border);
            border-radius: 6px;
            background: var(--bg);
        }
        .var-item {
            padding: 0.375rem 0.5rem;
            cursor: pointer;
            font-size: 0.8rem;
            color: var(--accent);
            border-bottom: 1px solid var(--border);
        }
        .var-item:last-child { border-bottom: none; }
        .var-item:hover { background: var(--surface-hover); }
        .editor-buttons {
            display: flex;
            gap: 0.5rem;
            justify-content: flex-end;
            margin-top: 0.75rem;
        }
        .btn {
            padding: 0.5rem 1rem;
            border-radius: 6px;
            cursor: pointer;
            font-size: 0.875rem;
            border: 1px solid var(--border);
        }
        .btn-cancel { background: var(--bg); color: var(--text); }
        .btn-cancel:hover { border-color: var(--accent); }
        .btn-save { background: var(--accent); color: #000; border-color: var(--accent); font-weight: 600; }
        .btn-save:hover { background: var(--accent-dim); }
)HTML";
    html += R"HTML(
        /* --- WYSIWYG Editor (matching tablet design) --- */
        .wysiwyg-editor {
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: 6px;
            padding: 0.5rem;
            min-height: 100px;
            max-height: 200px;
            overflow-y: auto;
            color: var(--text);
            font-size: 0.9rem;
            outline: none;
            white-space: pre-wrap;
            word-wrap: break-word;
        }
        .wysiwyg-editor:focus { border-color: var(--accent); }
        .wysiwyg-editor:empty::before {
            content: "Enter text...";
            color: var(--text-secondary);
            pointer-events: none;
        }

        /* Row 1: Icon/Emoji */
        .editor-icon-row {
            display: flex;
            align-items: center;
            gap: 0.5rem;
            margin-bottom: 0.5rem;
        }
        .icon-preview {
            width: 40px;
            height: 40px;
            border-radius: 6px;
            background: var(--bg);
            border: 1px solid var(--border);
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 1.5rem;
        }
        .icon-preview img { width: 28px; height: 28px; filter: brightness(0) invert(1); }
        .icon-btn {
            padding: 4px 10px;
            border-radius: 6px;
            cursor: pointer;
            font-size: 0.75rem;
            border: 1px solid var(--accent);
            background: none;
            color: var(--accent);
        }
        .icon-btn:hover { background: rgba(201,162,39,0.1); }
        .icon-btn.danger { border-color: #f85149; color: #f85149; }
        .icon-btn.danger:hover { background: rgba(248,81,73,0.1); }
        .emoji-picker-area { margin-bottom: 0.5rem; }
        .emoji-tabs {
            display: flex;
            gap: 2px;
            margin-bottom: 4px;
            flex-wrap: wrap;
        }
        .emoji-tab {
            padding: 2px 8px;
            border-radius: 4px;
            background: var(--bg);
            border: 1px solid var(--border);
            color: var(--text-secondary);
            cursor: pointer;
            font-size: 0.7rem;
        }
        .emoji-tab:hover { border-color: var(--accent); }
        .emoji-tab.active { background: var(--accent); color: #000; border-color: var(--accent); }
        .emoji-grid {
            display: flex;
            flex-wrap: wrap;
            gap: 2px;
            max-height: 140px;
            overflow-y: auto;
            border: 1px solid var(--border);
            border-radius: 6px;
            padding: 4px;
            background: var(--bg);
        }
        .emoji-cell {
            width: 36px;
            height: 36px;
            display: flex;
            align-items: center;
            justify-content: center;
            border-radius: 4px;
            cursor: pointer;
            font-size: 1.25rem;
        }
        .emoji-cell:hover { background: var(--surface-hover); }
        .emoji-cell.selected { background: var(--accent); border-radius: 6px; }
        .emoji-cell img { width: 24px; height: 24px; filter: brightness(0) invert(1); }
)HTML";
    html += R"HTML(
        /* Row 2: Content + Preview */
        .editor-content-row {
            display: flex;
            gap: 0.75rem;
            margin-bottom: 0.5rem;
        }
        .editor-content-col { flex: 1; min-width: 0; }
        .editor-preview-col {
            flex: 0 0 auto;
            display: flex;
            flex-direction: column;
            gap: 4px;
        }
        .preview-label {
            font-size: 0.65rem;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .preview-full {
            min-width: 120px;
            height: 80px;
            border-radius: 8px;
            background: var(--bg);
            border: 1px solid var(--border);
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            gap: 2px;
            overflow: hidden;
            padding: 4px;
            color: var(--text);
            font-size: 0.75rem;
            text-align: center;
        }
        .preview-full.has-action { border-color: var(--accent); border-width: 2px; }
        .preview-full img { width: 28px; height: 28px; filter: brightness(0) invert(1); }
        .preview-full .pv-emoji { font-size: 1.5rem; }
        .preview-full .pv-text {
            max-width: 100%;
            overflow: hidden;
            text-overflow: ellipsis;
            display: -webkit-box;
            -webkit-line-clamp: 2;
            -webkit-box-orient: vertical;
        }
        .preview-bar {
            min-width: 120px;
            height: 32px;
            border-radius: 8px;
            background: var(--bg);
            border: 1px solid var(--border);
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 4px;
            overflow: hidden;
            padding: 0 6px;
            color: var(--text);
            font-size: 0.75rem;
            white-space: nowrap;
        }
        .preview-bar.has-action { border-color: var(--accent); border-width: 2px; }
        .preview-bar img { width: 18px; height: 18px; filter: brightness(0) invert(1); }
        .preview-bar .pv-emoji { font-size: 1rem; }
        .preview-bar .pv-text { overflow: hidden; text-overflow: ellipsis; }
)HTML";
    html += R"HTML(
        /* Row 3: Format | Variables | Actions */
        .editor-tools-row {
            display: flex;
            gap: 0.75rem;
            min-height: 0;
        }
        .editor-tools-format {
            flex: 0 0 auto;
        }
        .editor-tools-vars {
            flex: 0 0 180px;
            min-width: 0;
            display: flex;
            flex-direction: column;
        }
        .editor-tools-vars .var-list { flex: 1; min-height: 0; max-height: 200px; }
        .editor-tools-actions {
            flex: 0 0 auto;
            min-width: 140px;
            max-width: 200px;
            display: flex;
            flex-direction: column;
            gap: 4px;
        }
        .action-selector {
            padding: 6px 8px;
            border-radius: 6px;
            background: var(--bg);
            border: 1px solid var(--border);
            cursor: pointer;
            font-size: 0.75rem;
            display: flex;
            gap: 4px;
            align-items: center;
        }
        .action-selector:hover { border-color: var(--accent); }
        .action-selector.has-action { border-color: var(--accent); }
        .action-selector .action-label-prefix {
            color: var(--text-secondary);
            flex: 0 0 auto;
        }
        .action-selector .action-label-value {
            color: var(--text);
            overflow: hidden;
            text-overflow: ellipsis;
            white-space: nowrap;
        }
        .action-selector.has-action .action-label-value { color: var(--accent); }
        .color-row {
            display: flex;
            align-items: center;
            gap: 6px;
            flex-wrap: wrap;
        }
)HTML";
    html += R"HTML(
        .color-swatch {
            width: 26px;
            height: 26px;
            border-radius: 50%;
            border: 1px solid var(--border);
            cursor: pointer;
            position: relative;
        }
        .color-swatch:hover { border-color: white; }
        .color-swatch.active { border-color: white; border-width: 2px; }
        .color-swatch-x {
            width: 22px;
            height: 22px;
            border-radius: 50%;
            border: 1px solid #f85149;
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 0.7rem;
            color: #f85149;
            background: none;
        }
        .color-swatch-x:hover { background: rgba(248,81,73,0.15); }
        .toggle-pill {
            display: inline-flex;
            align-items: center;
            justify-content: center;
            padding: 2px 8px;
            height: 22px;
            border-radius: 11px;
            border: 1px solid var(--border);
            cursor: pointer;
            font-size: 0.65rem;
            color: var(--text-secondary);
            background: none;
            margin-left: 4px;
            user-select: none;
        }
        .toggle-pill:hover { border-color: var(--primary); color: var(--primary); }
        .toggle-pill.active { background: var(--primary); border-color: var(--primary); color: white; }
)HTML";
    html += R"HTML(
        .color-popup-overlay {
            display: none;
            position: fixed;
            top: 0; left: 0; right: 0; bottom: 0;
            background: rgba(0,0,0,0.5);
            z-index: 2000;
            align-items: center;
            justify-content: center;
        }
        .color-popup-overlay.open { display: flex; }
        .color-popup {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            min-width: 300px;
            max-width: 400px;
        }
        .color-popup h4 {
            color: var(--text);
            margin: -0.5rem -0.5rem 0.75rem;
            padding: 0.5rem;
            font-size: 0.85rem;
            text-align: center;
            cursor: move;
            user-select: none;
            border-bottom: 1px solid var(--border);
            border-radius: 12px 12px 0 0;
        }
        .color-popup h4:hover { background: var(--surface-hover); }
        .color-popup-group {
            margin-bottom: 0.5rem;
        }
        .color-popup-group-label {
            font-size: 0.65rem;
            color: var(--text-secondary);
            margin-bottom: 4px;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        .color-popup-grid {
            display: flex;
            gap: 6px;
            flex-wrap: wrap;
        }
        .cp-swatch {
            width: 28px;
            height: 28px;
            border-radius: 50%;
            border: 2px solid transparent;
            cursor: pointer;
            transition: transform 0.1s, border-color 0.1s;
            position: relative;
        }
        .cp-swatch:hover { border-color: white; transform: scale(1.15); }
        .cp-swatch[title]:hover::after {
            content: attr(title);
            position: absolute;
            bottom: -20px;
            left: 50%;
            transform: translateX(-50%);
            font-size: 0.6rem;
            color: var(--text-secondary);
            white-space: nowrap;
            pointer-events: none;
        }
)HTML";
    html += R"HTML(
        .color-popup-footer {
            display: flex;
            gap: 6px;
            margin-top: 0.75rem;
            justify-content: flex-end;
        }
        .color-popup-footer button {
            padding: 0.3rem 0.75rem;
            border-radius: 6px;
            border: 1px solid var(--border);
            background: var(--bg);
            color: var(--text);
            cursor: pointer;
            font-size: 0.75rem;
        }
        .color-popup-footer button:hover { border-color: var(--accent); }
        .color-popup-footer .cp-custom-btn { border-color: var(--accent); color: var(--accent); }
        .color-popup-footer .cp-apply-btn { background: var(--accent); color: #000; border-color: var(--accent); font-weight: 600; }
        .color-popup-footer .cp-apply-btn:hover { filter: brightness(1.1); }
        .cp-picker-row {
            display: flex;
            align-items: center;
            gap: 12px;
            margin-top: 0.5rem;
            padding-top: 0.5rem;
            border-top: 1px solid var(--border);
        }
        .cp-picker-row #iroPickerContainer { flex-shrink: 0; }
        .cp-picker-right { display: flex; flex-direction: column; gap: 8px; flex: 1; }
        .cp-hex-row { display: flex; align-items: center; gap: 6px; }
        .cp-hex-row label { font-size: 0.7rem; color: var(--text-secondary); }
        .cp-hex-input {
            width: 80px;
            padding: 4px 6px;
            border: 1px solid var(--border);
            border-radius: 4px;
            background: var(--bg);
            color: var(--text);
            font-family: monospace;
            font-size: 0.8rem;
        }
        .cp-hex-input:focus { border-color: var(--accent); outline: none; }
        .cp-preview-swatch {
            width: 36px; height: 36px;
            border-radius: 6px;
            border: 2px solid var(--border);
        }
        .color-label {
            font-size: 0.75rem;
            color: var(--text-secondary);
        }

        /* Action picker overlay */
        .action-overlay {
            display: none;
            position: fixed;
            inset: 0;
            background: rgba(0,0,0,0.5);
            z-index: 200;
            align-items: center;
            justify-content: center;
        }
        .action-overlay.open { display: flex; }
        .action-dialog {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            width: min(80vw, 280px);
            max-height: 400px;
            overflow-y: auto;
        }
        .action-dialog h4 { color: var(--text); margin: 0 0 0.5rem; font-size: 0.9rem; text-align: center; }
        .action-dialog-item {
            padding: 0.4rem 0.6rem;
            cursor: pointer;
            font-size: 0.8rem;
            color: var(--text);
            border-radius: 4px;
        }
        .action-dialog-item:hover { background: var(--surface-hover); }
        .action-dialog-item.selected { background: var(--accent); color: #000; }

        .chip-emoji { margin-right: 2px; font-size: 0.8rem; }
        .chip-emoji img { width: 14px; height: 14px; vertical-align: middle; filter: brightness(0) invert(1); }

        @media (max-width: 700px) {
            .editor-content-row { flex-direction: column; }
            .editor-tools-row { flex-direction: column; }
            .editor-preview-col { flex-direction: row; gap: 0.5rem; }
        }

        /* Library panel */
        .main-wrapper {
            display: flex;
            gap: 0;
            max-width: 1800px;
            margin: 0 auto;
        }
        .main-wrapper .main-layout { flex: 1; min-width: 0; }
        .library-panel {
            width: 340px;
            min-width: 340px;
            background: var(--surface);
            border-left: 1px solid var(--border);
            padding: 1rem;
            display: flex;
            flex-direction: column;
            height: calc(100vh - 60px);
            overflow: hidden;
            position: sticky;
            top: 60px;
        }
        #libLocalContent, #libCommunityContent {
            flex: 1;
            display: flex;
            flex-direction: column;
            min-height: 0;
        }
        .lib-tabs {
            display: flex;
            gap: 0;
            margin-bottom: 1rem;
            border-radius: 8px;
            overflow: hidden;
            border: 1px solid var(--border);
        }
        .lib-tab {
            flex: 1;
            padding: 0.5rem;
            text-align: center;
            cursor: pointer;
            background: var(--bg);
            color: var(--text-secondary);
            font-size: 0.8rem;
            font-weight: 600;
            border: none;
            transition: all 0.15s;
        }
        .lib-tab.active {
            background: var(--accent);
            color: #fff;
        }
        .lib-tab:hover:not(.active) { background: var(--surface-hover); }
        .lib-actions {
            display: flex;
            gap: 0.4rem;
            margin-bottom: 0.75rem;
            align-items: center;
        }
        .lib-icon-btn {
            width: 30px;
            height: 30px;
            border-radius: 6px;
            border: 1px solid var(--border);
            background: var(--bg);
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
            transition: all 0.15s;
            padding: 0;
            position: relative;
        }
        .lib-icon-btn svg { width: 16px; height: 16px; }
        .lib-icon-btn { color: var(--text-secondary); }
        .lib-icon-btn:hover { border-color: var(--accent); color: var(--accent); }
        .lib-icon-btn.accent { border-color: var(--accent); color: var(--accent); }
        .lib-icon-btn.danger:hover { border-color: #ff4444; color: #ff4444; }
        .lib-icon-btn:disabled { opacity: 0.4; cursor: default; }
        .lib-icon-btn:disabled:hover { border-color: var(--border); color: var(--text-secondary); }
        .lib-display-toggle {
            width: 28px; height: 28px;
            border-radius: 4px;
            border: 1px solid var(--border);
            background: transparent;
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 14px;
            color: var(--text-secondary);
            transition: all 0.15s;
            padding: 0;
        }
        .lib-display-toggle.active {
            background: var(--accent);
            border-color: var(--accent);
            color: #fff;
        }
        .lib-save-dropdown {
            position: absolute;
            top: 100%;
            left: 0;
            margin-top: 4px;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 4px;
            z-index: 100;
            min-width: 140px;
            box-shadow: 0 4px 12px rgba(0,0,0,0.3);
            display: none;
        }
        .lib-save-dropdown.open { display: block; }
        .lib-save-option {
            padding: 0.4rem 0.6rem;
            border-radius: 4px;
            cursor: pointer;
            font-size: 0.8rem;
            color: var(--text);
            transition: background 0.1s;
        }
        .lib-save-option:hover { background: rgba(78,133,244,0.12); }
        .lib-save-option.disabled { opacity: 0.4; cursor: default; }
        .lib-save-option.disabled:hover { background: transparent; }
        .lib-action-btn {
            padding: 0.35rem 0.6rem;
            border-radius: 6px;
            border: 1px solid var(--border);
            background: var(--bg);
            color: var(--text);
            cursor: pointer;
            font-size: 0.75rem;
            transition: all 0.15s;
        }
        .lib-action-btn:hover { border-color: var(--accent); color: var(--accent); }
        .lib-action-btn.accent { border-color: var(--accent); color: var(--accent); }
        .lib-action-btn:disabled { opacity: 0.4; cursor: default; }
        .lib-entries {
            flex: 1;
            overflow-y: auto;
            display: flex;
            flex-direction: column;
            gap: 0.5rem;
        }
        .lib-entry {
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 0.6rem;
            cursor: pointer;
            transition: all 0.15s;
            position: relative;
        }
        .lib-entry:hover { border-color: var(--accent); }
        .lib-entry.selected { border: 2px solid var(--accent); }
        .lib-entry.compact {
            padding: 0.3rem 0.5rem;
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }
        .lib-entry.compact .lib-entry-visual {
            min-height: 24px;
            padding: 0.2rem 0.4rem;
            flex: 1;
        }
        .lib-entry.compact .lib-entry-visual img.lib-thumb {
            max-height: 32px;
        }
        .lib-entry.compact .lib-type-overlay {
            position: static;
            flex-shrink: 0;
        }
        .lib-entry-visual {
            border-radius: 6px;
            padding: 0.4rem 0.6rem;
            display: flex;
            align-items: center;
            gap: 0.4rem;
            min-height: 32px;
            overflow: hidden;
        }
        .lib-entry-visual img.lib-thumb {
            width: 100%;
            max-height: 120px;
            object-fit: contain;
            border-radius: 4px;
        }
        .lib-entry-visual .lib-item-emoji { width: 22px; height: 22px; flex-shrink: 0; }
        .lib-entry-visual .lib-item-emoji img { width: 100%; height: 100%; filter: brightness(0) invert(1); }
        .lib-entry-visual .lib-item-text {
            color: white;
            font-size: 0.8rem;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }
        .lib-entry-visual .lib-zone-chips {
            display: flex;
            flex-wrap: wrap;
            gap: 3px;
        }
        .lib-zone-mini-chip {
            display: inline-flex;
            align-items: center;
            gap: 2px;
            padding: 1px 6px;
            border-radius: 4px;
            font-size: 0.65rem;
            color: white;
            white-space: nowrap;
        }
        .lib-zone-mini-chip img { width: 12px; height: 12px; filter: brightness(0) invert(1); }
        .lib-type-overlay {
            position: absolute;
            top: 3px;
            left: 3px;
            z-index: 1;
            font-size: 0.55rem;
            font-weight: 700;
            text-transform: uppercase;
            padding: 1px 4px;
            border-radius: 3px;
            opacity: 0.85;
            letter-spacing: 0.03em;
        }
        .lib-type-overlay.item { background: #1a3a5c; color: #4e85f4; }
        .lib-type-overlay.zone { background: #3a3520; color: #c9a227; }
        .lib-type-overlay.layout { background: #1a3a2a; color: #00cc6d; }
        .lib-empty {
            color: var(--text-secondary);
            text-align: center;
            padding: 2rem 1rem;
            font-size: 0.85rem;
        }
        .lib-community-filters {
            display: flex;
            gap: 0.5rem;
            margin-bottom: 0.75rem;
            flex-wrap: wrap;
        }
        .lib-filter-select, .lib-filter-input {
            padding: 0.35rem 0.5rem;
            border-radius: 6px;
            border: 1px solid var(--border);
            background: var(--bg);
            color: var(--text);
            font-size: 0.75rem;
            flex: 1;
            min-width: 0;
        }
        .lib-filter-input { flex: 2; }
        .lib-load-more {
            padding: 0.5rem;
            text-align: center;
            color: var(--accent);
            cursor: pointer;
            font-size: 0.8rem;
            border: 1px dashed var(--border);
            border-radius: 6px;
            margin-top: 0.5rem;
        }
        .lib-load-more:hover { background: var(--surface-hover); }
        .lib-toast {
            position: fixed;
            bottom: 1.5rem;
            left: 50%;
            transform: translateX(-50%);
            background: var(--accent);
            color: #fff;
            padding: 0.6rem 1.2rem;
            border-radius: 8px;
            font-size: 0.85rem;
            z-index: 9999;
            opacity: 0;
            transition: opacity 0.3s;
            pointer-events: none;
        }
        .lib-toast.show { opacity: 1; }
        .lib-spinner-overlay {
            display: none;
            position: absolute;
            inset: 0;
            background: rgba(0,0,0,0.5);
            z-index: 100;
            align-items: center;
            justify-content: center;
            flex-direction: column;
            gap: 0.8rem;
            border-radius: 8px;
        }
        .lib-spinner-overlay.active { display: flex; }
        .lib-spinner {
            width: 32px; height: 32px;
            border: 3px solid rgba(255,255,255,0.2);
            border-top-color: var(--accent);
            border-radius: 50%;
            animation: spin 0.8s linear infinite;
        }
        @keyframes spin { to { transform: rotate(360deg); } }
        .lib-spinner-text { color: #fff; font-size: 0.8rem; }
    </style>
    <script src="https://cdn.jsdelivr.net/npm/@jaames/iro@5"></script>
</head>
<body>
)HTML";

    // Part 3: Header
    html += R"HTML(
    <header class="header">
        <div class="header-content">
            <div style="display:flex;align-items:center;gap:1rem">
                <a href="/" class="back-btn">&larr;</a>
                <h1>Layout Editor</h1>
            </div>
            <div class="header-right">
                <button class="reset-btn" onclick="resetLayout()">Reset to Default</button>
)HTML";
    html += generateMenuHtml();
    html += R"HTML(
            </div>
        </div>
    </header>
)HTML";

    // Part 4: Main content
    html += R"HTML(
    <!-- Action Picker Overlay -->
    <div class="action-overlay" id="actionOverlay" onclick="if(event.target===this)closeActionPicker()">
        <div class="action-dialog">
            <h4 id="actionPickerTitle">Tap Action</h4>
            <div id="actionPickerList"></div>
        </div>
    </div>

    <div class="main-wrapper">
    <div class="main-layout">
        <div class="zones-panel" id="zonesPanel"></div>
        <div class="editor-panel editor-hidden" id="editorPanel">
            <div class="editor-card">
                <h3>Edit Custom Widget</h3>

                <!-- ROW 1: Icon/Emoji selector -->
                <div class="editor-icon-row">
                    <span class="section-label" style="margin:0">Icon</span>
                    <div class="icon-preview" id="iconPreview"><span style="color:var(--text-secondary)">&#8212;</span></div>
                    <button class="icon-btn" id="emojiToggleBtn" onclick="toggleEmojiPicker()">Pick Icon</button>
                    <button class="icon-btn danger" id="emojiClearBtn" onclick="clearEmoji()" style="display:none">Clear</button>
                </div>
                <div class="emoji-picker-area" id="emojiPickerArea" style="display:none">
                    <div class="emoji-tabs" id="emojiTabs"></div>
                    <div class="emoji-grid" id="emojiGrid"></div>
                </div>
)HTML";
    html += R"HTML(
                <!-- ROW 2: WYSIWYG Content + Dual Preview -->
                <div class="editor-content-row">
                    <div class="editor-content-col">
                        <div class="section-label">Content</div>
                        <div contenteditable="true" id="wysiwygEditor" class="wysiwyg-editor"></div>
                        <div class="toolbar" style="margin-top:0.375rem">
                            <button class="tool-btn" id="btnBold" onclick="execBold()" title="Bold"><b>B</b></button>
                            <button class="tool-btn" id="btnItalic" onclick="execItalic()" title="Italic"><i>I</i></button>
                            <div class="tool-sep"></div>
                            <button class="tool-btn" onclick="execFontSize(12)" title="Small">S</button>
                            <button class="tool-btn" onclick="execFontSize(18)" title="Medium">M</button>
                            <button class="tool-btn" onclick="execFontSize(28)" title="Large">L</button>
                            <button class="tool-btn" onclick="execFontSize(48)" title="XL">XL</button>
                            <div class="tool-sep"></div>
                            <button class="tool-btn" id="alignLeft" onclick="setAlign('left')" title="Left">&#9664;</button>
                            <button class="tool-btn active" id="alignCenter" onclick="setAlign('center')" title="Center">&#9679;</button>
                            <button class="tool-btn" id="alignRight" onclick="setAlign('right')" title="Right">&#9654;</button>
                            <div class="tool-sep"></div>
                            <button class="tool-btn" onclick="execClearFormat()" title="Clear Formatting">&#10005;</button>
                        </div>
                    </div>
                    <div class="editor-preview-col">
                        <span class="preview-label">Full</span>
                        <div class="preview-full" id="previewFull"></div>
                        <span class="preview-label">Bar</span>
                        <div class="preview-bar" id="previewBar"></div>
                    </div>
                </div>
)HTML";
    html += R"HTML(
                <!-- ROW 3: Format/Color | Variables | Actions -->
                <div class="editor-tools-row">
                    <div class="editor-tools-format">
                        <div class="color-row">
                            <span class="color-label">Color</span>
                            <div class="color-swatch" id="textColorSwatch" style="background:#ffffff" onclick="openColorPopup('text')"></div>
                            <input type="color" id="textColorInput" value="#ffffff" style="position:absolute;visibility:hidden;width:0;height:0" onchange="applyTextColor(this.value)">
                            <span class="color-swatch-x" onclick="clearTextColor()" title="Reset text color">&#10005;</span>
                            <span class="color-label" style="margin-left:6px">Bg</span>
                            <div class="color-swatch" id="bgColorSwatch" style="background:transparent" onclick="openColorPopup('bg')">
                                <span id="bgNoneX" style="color:var(--text-secondary);font-size:0.6rem">&#10005;</span>
                            </div>
                            <input type="color" id="bgColorInput" value="#555555" style="position:absolute;visibility:hidden;width:0;height:0" onchange="setBgColor(this.value)">
                            <span class="color-swatch-x" id="bgClearBtn" onclick="clearBgColor()" title="Remove background" style="display:none">&#10005;</span>
                            <span id="hideBgToggle" class="toggle-pill" onclick="toggleHideBackground()" title="Hide background even with actions">No Bg</span>
                        </div>
                    </div>
                    <div class="editor-tools-vars">
                        <div class="section-label" style="margin-top:0">Variables</div>
                        <div class="var-list">
                            <div class="var-item" onclick="insertVar('%TEMP%')">Temp (&deg;C)</div>
                            <div class="var-item" onclick="insertVar('%STEAM_TEMP%')">Steam (&deg;C)</div>
                            <div class="var-item" onclick="insertVar('%PRESSURE%')">Pressure</div>
                            <div class="var-item" onclick="insertVar('%FLOW%')">Flow</div>
                            <div class="var-item" onclick="insertVar('%WATER%')">Water %</div>
                            <div class="var-item" onclick="insertVar('%WATER_ML%')">Water ml</div>
                            <div class="var-item" onclick="insertVar('%WEIGHT%')">Weight</div>
                            <div class="var-item" onclick="insertVar('%SHOT_TIME%')">Shot Time</div>
                            <div class="var-item" onclick="insertVar('%TARGET_WEIGHT%')">Target Wt</div>
                            <div class="var-item" onclick="insertVar('%VOLUME%')">Volume</div>
                            <div class="var-item" onclick="insertVar('%PROFILE%')">Profile</div>
                            <div class="var-item" onclick="insertVar('%STATE%')">State</div>
                            <div class="var-item" onclick="insertVar('%TARGET_TEMP%')">Tgt Temp</div>
                            <div class="var-item" onclick="insertVar('%SCALE%')">Scale</div>
                            <div class="var-item" onclick="insertVar('%TIME%')">Time</div>
                            <div class="var-item" onclick="insertVar('%DATE%')">Date</div>
                            <div class="var-item" onclick="insertVar('%RATIO%')">Ratio</div>
                            <div class="var-item" onclick="insertVar('%DOSE%')">Dose</div>
                            <div class="var-item" onclick="insertVar('%MACHINE_READY%')">Ready</div>
                            <div class="var-item" onclick="insertVar('%MACHINE_READY_COLOR%')">Ready Clr</div>
                            <div class="var-item" onclick="insertVar('%CONNECTED%')">Online</div>
                            <div class="var-item" onclick="insertVar('%CONNECTED_COLOR%')">Status Clr</div>
                            <div class="var-item" onclick="insertVar('%DEVICES%')">Devices</div>
                            <div class="var-item" onclick="insertVar('%MACHINE_CONNECTED%')">Machine ✓/✗</div>
                            <div class="var-item" onclick="insertVar('%SCALE_CONNECTED%')">Scale ✓/✗</div>
                        </div>
                    </div>
                    <div class="editor-tools-actions">
                        <div class="section-label" style="margin-top:0">Actions</div>
                        <div class="action-selector" id="tapActionSel" onclick="openActionPicker('click')">
                            <span class="action-label-prefix">Click:</span>
                            <span class="action-label-value" id="tapActionLabel">None</span>
                        </div>
                        <div class="action-selector" id="longPressActionSel" onclick="openActionPicker('longpress')">
                            <span class="action-label-prefix">Long:</span>
                            <span class="action-label-value" id="longPressActionLabel">None</span>
                        </div>
                        <div class="action-selector" id="dblClickActionSel" onclick="openActionPicker('doubleclick')">
                            <span class="action-label-prefix">DblClk:</span>
                            <span class="action-label-value" id="dblClickActionLabel">None</span>
                        </div>
                    </div>
                </div>

                <!-- ROW 4: Buttons -->
                <div class="editor-buttons">
                    <div style="flex:1"></div>
                    <button class="btn btn-cancel" onclick="closeEditor()">Done</button>
                </div>
            </div>
        </div>
    </div>

    <!-- Screensaver Editor Panel -->
    <div class="editor-panel editor-hidden" id="ssEditorPanel">
        <div class="ss-editor-card">
            <h3 id="ssEditorTitle">Screensaver Settings</h3>

            <!-- Flip Clock: Size slider -->
            <div id="ssClockSettings" style="display:none">
                <div class="section-label">Size</div>
                <div class="ss-slider-row">
                    <span class="ss-slider-label">Small</span>
                    <input type="range" class="ss-slider" id="ssClockScale" min="0" max="1" step="0.05" value="1" oninput="ssClockScaleChanged(this.value)">
                    <span class="ss-slider-label" style="text-align:right">Large</span>
                </div>
            </div>

            <!-- Shot Map: Width slider + Background picker -->
            <div id="ssMapSettings" style="display:none">
                <div class="section-label">Width</div>
                <div class="ss-slider-row">
                    <span class="ss-slider-label">Narrow</span>
                    <input type="range" class="ss-slider" id="ssMapScale" min="1" max="1.7" step="0.05" value="1" oninput="ssMapScaleChanged(this.value)">
                    <span class="ss-slider-label" style="text-align:right">Wide</span>
                </div>
                <div class="section-label">Background</div>
                <div class="ss-btn-group" id="ssMapTextureGroup">
                    <button class="ss-btn-option active" onclick="ssSelectMapTexture('')">Global</button>
                    <button class="ss-btn-option" onclick="ssSelectMapTexture('dark')">Dark</button>
                    <button class="ss-btn-option" onclick="ssSelectMapTexture('bright')">Bright</button>
                    <button class="ss-btn-option" onclick="ssSelectMapTexture('satellite')">Satellite</button>
                </div>
            </div>

            <!-- Last Shot: Width slider + Labels toggles -->
            <div id="ssLastShotSettings" style="display:none">
                <div class="section-label">Width</div>
                <div class="ss-slider-row">
                    <span class="ss-slider-label">1x</span>
                    <input type="range" class="ss-slider" id="ssShotScale" min="1" max="2.5" step="0.1" value="1" oninput="ssShotScaleChanged(this.value)">
                    <span class="ss-slider-label" style="text-align:right">2.5x</span>
                </div>
                <div class="section-label">Show axis labels</div>
                <div class="ss-slider-row">
                    <label style="display:flex;align-items:center;gap:0.5rem;cursor:pointer">
                        <input type="checkbox" id="ssShotShowLabels" onchange="ssShotToggleChanged()">
                        <span style="color:var(--text-secondary)">Pressure, flow, weight scales</span>
                    </label>
                </div>
                <div class="section-label">Show frame labels</div>
                <div class="ss-slider-row">
                    <label style="display:flex;align-items:center;gap:0.5rem;cursor:pointer">
                        <input type="checkbox" id="ssShotShowPhaseLabels" checked onchange="ssShotToggleChanged()">
                        <span style="color:var(--text-secondary)">Frame transition markers</span>
                    </label>
                </div>
            </div>
)HTML";
    html += R"HTML(
            <!-- No settings message -->
            <div id="ssNoSettings" style="display:none">
                <div class="ss-no-settings">No additional settings for this screensaver.</div>
            </div>

            <div class="editor-buttons">
                <div style="flex:1"></div>
                <button class="btn btn-cancel" onclick="closeScreensaverEditor()">Done</button>
            </div>
        </div>
    </div>

    <!-- Library Panel (right sidebar) -->
    <div class="library-panel" id="libraryPanel">
        <div class="lib-spinner-overlay" id="libSpinner"><div class="lib-spinner"></div><div class="lib-spinner-text" id="libSpinnerText">Loading...</div></div>
        <div class="lib-tabs">
            <button class="lib-tab active" id="libTabLocal" onclick="switchLibTab('local')">My Library</button>
            <button class="lib-tab" id="libTabCommunity" onclick="switchLibTab('community')">Community</button>
        </div>

        <!-- Local library content -->
        <div id="libLocalContent">
            <div class="lib-actions">
                <button class="lib-icon-btn" id="libSaveBtn" onclick="toggleSaveMenu(event)" title="Save to library">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/></svg>
                    <div class="lib-save-dropdown" id="libSaveMenu">
                        <div class="lib-save-option" id="saveItemOpt" onclick="event.stopPropagation();closeSaveMenu();saveToLibrary('item')">Save Item</div>
                        <div class="lib-save-option" id="saveZoneOpt" onclick="event.stopPropagation();closeSaveMenu();saveToLibrary('zone')">Save Zone</div>
                        <div class="lib-save-option" onclick="event.stopPropagation();closeSaveMenu();saveToLibrary('layout')">Save Layout</div>
                    </div>
                </button>
                <button class="lib-icon-btn accent" id="libApplyBtn" onclick="applyFromLibrary()" disabled title="Apply selected entry">
                    <svg viewBox="0 0 24 24" fill="none"><g transform="translate(24,0) scale(-1,1)"><path fill-rule="evenodd" clip-rule="evenodd" d="M12.293 4.293a1 1 0 011.414 0l7 7a1 1 0 010 1.414l-7 7a1 1 0 01-1.414-1.414L17.586 13H4a1 1 0 110-2h13.586l-5.293-5.293a1 1 0 010-1.414z" fill="currentColor"/></g></svg>
                </button>
                <button class="lib-icon-btn" id="libUploadBtn" onclick="uploadToComm()" disabled title="Share to community">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 15V3m0 0l-4 4m4-4l4 4"/><path d="M2 17l.621 2.485A2 2 0 004.561 21h14.878a2 2 0 001.94-1.515L22 17"/></svg>
                </button>
                <button class="lib-icon-btn danger" id="libDeleteBtn" onclick="deleteFromLibrary()" disabled title="Delete selected entry">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 01-2 2H7a2 2 0 01-2-2V6m3 0V4a2 2 0 012-2h4a2 2 0 012 2v2"/></svg>
                </button>
                <span style="flex:1"></span>
                <button class="lib-display-toggle active" id="libModeFull" onclick="setLibDisplayMode(0)" title="Full preview">&#x25A3;</button>
                <button class="lib-display-toggle" id="libModeCompact" onclick="setLibDisplayMode(1)" title="Compact list">&#x2630;</button>
            </div>
            <div class="lib-entries" id="libLocalEntries">
                <div class="lib-empty">No saved entries yet.<br>Select a widget and click <b>+</b> to save it.</div>
            </div>
        </div>

        <!-- Community content -->
        <div id="libCommunityContent" style="display:none">
            <div class="lib-actions">
                <button class="lib-action-btn accent" id="commApplyBtn" onclick="applyFromLibrary()" disabled title="Download &amp; apply">Apply</button>
                <button class="lib-action-btn" id="commDownloadBtn" onclick="downloadOnly()" disabled title="Download to My Library">Download</button>
                <span style="flex:1"></span>
                <button class="lib-display-toggle active" id="commModeFull" onclick="setLibDisplayMode(0)" title="Full preview">&#x25A3;</button>
                <button class="lib-display-toggle" id="commModeCompact" onclick="setLibDisplayMode(1)" title="Compact list">&#x2630;</button>
            </div>
            <div class="lib-community-filters">
                <select class="lib-filter-select" id="commTypeFilter" onchange="browseCommunity()">
                    <option value="">All types</option>
                    <option value="item">Items</option>
                    <option value="zone">Zones</option>
                    <option value="layout">Layouts</option>
                </select>
                <select class="lib-filter-select" id="commVariableFilter" onchange="browseCommunity()">
                    <option value="">Any variable</option>
                    <option value="%TEMP%">%TEMP%</option>
                    <option value="%STEAM_TEMP%">%STEAM_TEMP%</option>
                    <option value="%PRESSURE%">%PRESSURE%</option>
                    <option value="%FLOW%">%FLOW%</option>
                    <option value="%WEIGHT%">%WEIGHT%</option>
                    <option value="%WATER%">%WATER%</option>
                    <option value="%SHOT_TIME%">%SHOT_TIME%</option>
                    <option value="%PROFILE%">%PROFILE%</option>
                    <option value="%STATE%">%STATE%</option>
                    <option value="%TIME%">%TIME%</option>
                    <option value="%DATE%">%DATE%</option>
                    <option value="%RATIO%">%RATIO%</option>
                    <option value="%DOSE%">%DOSE%</option>
                    <option value="%TARGET_WEIGHT%">%TARGET_WEIGHT%</option>
                </select>
                <select class="lib-filter-select" id="commActionFilter" onchange="browseCommunity()">
                    <option value="">Any action</option>
                    <option value="navigate:settings">Settings</option>
                    <option value="navigate:history">History</option>
                    <option value="navigate:profiles">Profiles</option>
                    <option value="navigate:autofavorites">Favorites</option>
                    <option value="command:sleep">Sleep</option>
                    <option value="command:startEspresso">Start Espresso</option>
                    <option value="command:startSteam">Start Steam</option>
                    <option value="command:startHotWater">Start Hot Water</option>
                    <option value="command:startFlush">Start Flush</option>
                    <option value="command:tare">Tare Scale</option>
                    <option value="command:scanDE1">Scan for DE1</option>
                    <option value="command:scanScale">Scan for Scale</option>
                    <option value="command:quit">Quit</option>
                </select>
                <input class="lib-filter-input" id="commSearchInput" type="text" placeholder="Search..."
                       onkeydown="if(event.key==='Enter')browseCommunity()">
                <select class="lib-filter-select" id="commSortFilter" onchange="browseCommunity()">
                    <option value="newest">Newest</option>
                    <option value="popular">Popular</option>
                    <option value="name">Name</option>
                </select>
            </div>
            <div class="lib-entries" id="libCommunityEntries">
                <div class="lib-empty">Click a tab or search to browse community entries.</div>
            </div>
            <div class="lib-load-more" id="commLoadMore" style="display:none" onclick="loadMoreCommunity()">Load more...</div>
        </div>
    </div>
    </div><!-- end main-wrapper -->

    <!-- Toast notification -->
    <div class="lib-toast" id="libToast"></div>
)HTML";
    html += R"HTML(
    <!-- Color picker popup with theme swatches -->
    <div class="color-popup-overlay" id="colorPopupOverlay" onclick="if(event.target===this)closeColorPopup()">
        <div class="color-popup" id="colorPopupBox">
            <h4 id="colorPopupTitle">Text Color</h4>

            <div class="color-popup-group">
                <div class="color-popup-group-label">Text</div>
                <div class="color-popup-grid">
                    <div class="cp-swatch" style="background:#ffffff" title="White" onclick="pickColor('#ffffff')"></div>
                    <div class="cp-swatch" style="background:#e6edf3" title="Light" onclick="pickColor('#e6edf3')"></div>
                    <div class="cp-swatch" style="background:#c0c5e3" title="Secondary" onclick="pickColor('#c0c5e3')"></div>
                    <div class="cp-swatch" style="background:#8b949e" title="Muted" onclick="pickColor('#8b949e')"></div>
                    <div class="cp-swatch" style="background:#000000;border-color:var(--border)" title="Black" onclick="pickColor('#000000')"></div>
                </div>
            </div>

            <div class="color-popup-group">
                <div class="color-popup-group-label">UI</div>
                <div class="color-popup-grid">
                    <div class="cp-swatch" style="background:#4e85f4" title="Primary" onclick="pickColor('#4e85f4')"></div>
                    <div class="cp-swatch" style="background:#e94560" title="Accent" onclick="pickColor('#e94560')"></div>
                    <div class="cp-swatch" style="background:#c9a227" title="Gold" onclick="pickColor('#c9a227')"></div>
                    <div class="cp-swatch" style="background:#00cc6d" title="Success" onclick="pickColor('#00cc6d')"></div>
                    <div class="cp-swatch" style="background:#ffaa00" title="Warning" onclick="pickColor('#ffaa00')"></div>
                    <div class="cp-swatch" style="background:#ff4444" title="Error" onclick="pickColor('#ff4444')"></div>
                </div>
            </div>

            <div class="color-popup-group">
                <div class="color-popup-group-label">Graph</div>
                <div class="color-popup-grid">
                    <div class="cp-swatch" style="background:#18c37e" title="Pressure" onclick="pickColor('#18c37e')"></div>
                    <div class="cp-swatch" style="background:#69fdb3" title="Pressure Goal" onclick="pickColor('#69fdb3')"></div>
                    <div class="cp-swatch" style="background:#4e85f4" title="Flow" onclick="pickColor('#4e85f4')"></div>
                    <div class="cp-swatch" style="background:#7aaaff" title="Flow Goal" onclick="pickColor('#7aaaff')"></div>
                    <div class="cp-swatch" style="background:#e73249" title="Temperature" onclick="pickColor('#e73249')"></div>
                    <div class="cp-swatch" style="background:#ffa5a6" title="Temp Goal" onclick="pickColor('#ffa5a6')"></div>
                    <div class="cp-swatch" style="background:#a2693d" title="Weight" onclick="pickColor('#a2693d')"></div>
                </div>
            </div>

            <div class="color-popup-group">
                <div class="color-popup-group-label">Extras</div>
                <div class="color-popup-grid">
                    <div class="cp-swatch" style="background:#9C27B0" title="Purple" onclick="pickColor('#9C27B0')"></div>
                    <div class="cp-swatch" style="background:#FF9800" title="Orange" onclick="pickColor('#FF9800')"></div>
                    <div class="cp-swatch" style="background:#6F4E37" title="Coffee" onclick="pickColor('#6F4E37')"></div>
                    <div class="cp-swatch" style="background:#555555" title="Grey" onclick="pickColor('#555555')"></div>
                    <div class="cp-swatch" style="background:#252538" title="Surface" onclick="pickColor('#252538')"></div>
                    <div class="cp-swatch" style="background:#1a1a2e;border-color:var(--border)" title="Dark" onclick="pickColor('#1a1a2e')"></div>
                </div>
            </div>

            <div class="cp-picker-row">
                <div id="iroPickerContainer"></div>
                <div class="cp-picker-right">
                    <div class="cp-hex-row">
                        <label>Hex</label>
                        <input type="text" class="cp-hex-input" id="cpHexInput" value="#ffffff" maxlength="7"
                               oninput="onHexInput(this.value)">
                    </div>
                    <div class="cp-preview-swatch" id="cpPreviewSwatch" style="background:#ffffff"></div>
                </div>
            </div>

            <div class="color-popup-footer">
                <button class="cp-apply-btn" onclick="applyPickerColor()">Apply</button>
            </div>
        </div>
    </div>
)HTML";

    // Part 5: JavaScript
    html += R"HTML(
    <script>
)HTML";
    html += WEB_JS_MENU;
    html += R"HTML(

    var layoutData = null;
    var selectedChip = null; // {id, zone}
    var editingItem = null;  // {id, zone}
    var currentAlign = "center";
    var currentAction = "";
    var currentLongPressAction = "";
    var currentDoubleclickAction = "";
    var currentEmoji = "";
    var currentBgColor = "";
    var currentHideBackground = false;
    var emojiCategory = 0;
    var itemPropsCache = {}; // id -> {emoji, content, backgroundColor, ...}

    var DECENZA_ICONS = [
        {value:"/icons/espresso.svg",label:"Espresso"},
        {value:"/icons/steam.svg",label:"Steam"},
        {value:"/icons/water.svg",label:"Water"},
        {value:"/icons/flush.svg",label:"Flush"},
        {value:"/icons/coffeebeans.svg",label:"Beans"},
        {value:"/icons/sleep.svg",label:"Sleep"},
        {value:"/icons/settings.svg",label:"Settings"},
        {value:"/icons/history.svg",label:"History"},
        {value:"/icons/star.svg",label:"Star"},
        {value:"/icons/star-outline.svg",label:"Star"},
        {value:"/icons/temperature.svg",label:"Temp"},
        {value:"/icons/tea.svg",label:"Tea"},
        {value:"/icons/grind.svg",label:"Grind"},
        {value:"/icons/filter.svg",label:"Filter"},
        {value:"/icons/bluetooth.svg",label:"BT"},
        {value:"/icons/wifi.svg",label:"WiFi"},
        {value:"/icons/edit.svg",label:"Edit"},
        {value:"/icons/sparkle.svg",label:"AI"},
        {value:"/icons/hand.svg",label:"Hand"},
        {value:"/icons/tick.svg",label:"Tick"},
        {value:"/icons/cross.svg",label:"Cross"},
        {value:"/icons/decent-de1.svg",label:"DE1"},
        {value:"/icons/scale.svg",label:"Scale"},
        {value:"/icons/quit.svg",label:"Quit"},
        {value:"/icons/Graph.svg",label:"Graph"}
    ];

    var EMOJI_CATEGORIES = [
        {name:"Decenza",isSvg:true},
        {name:"Symbols",emoji:[
            "✅","❌","❗","❓","⚠️","🚫","⛔","🔞",
            "⭐","✨","💡","🔋","🔌","🚨","🔔","🔕",
            "🔒","🔓","🔑","🗝️","🔄","♻️",
            "🔴","🟠","🟡","🟢","🔵","🟣","🟤","⚫","⚪",
            "🟥","🟧","🟨","🟩","🟦","🟪","🟫","⬛","⬜",
            "🔶","🔷","🔸","🔹","🔺","🔻","💠","🔘",
            "❤️","🧡","💛","💚","💙","💜","🖤","🤍","🤎",
            "💔","❣️","💕","💖","💗","💓","💞","💘","💝",
            "⬆️","⬇️","➡️","⬅️","↗️","↘️","↙️","↖️",
            "↩️","↪️","🔃","🔄",
            "➕","➖","✖️","➗","♾️",
            "‼️","⁉️",
            "▶️","⏸️","⏹️","⏺️","⏯️","⏭️","⏮️",
            "🔀","🔁","🔂","🔅","🔆",
            "🌟","🌠","💫","🎇","🎆",
            "♈","♉","♊","♋","♌","♍",
            "♎","♏","♐","♑","♒","♓"
        ]},
        {name:"Food",emoji:[
            "☕","🫖","🍵","🧋","🧉",
            "🥤","🍺","🍻","🍷","🍸",
            "🍹","🍾","🥂","🥃","🧃",
            "🥛","🍼","🧊",
            "🍇","🍈","🍉","🍊","🍋","🍌","🍍","🥭",
            "🍎","🍏","🍐","🍑","🍒","🍓","🫐","🥝","🍅","🥥",
            "🥑","🍆","🥔","🥕","🌽","🌶️","🥒","🥬","🥦",
            "🧄","🧅","🥜","🌰","🍄",
            "🍞","🥐","🥖","🥨","🥞","🧇","🧀",
            "🍖","🍗","🥩","🥓",
            "🍔","🍟","🍕","🌭","🥪","🌮","🌯","🥙",
            "🍳","🥘","🍲","🥣","🥗","🍿",
            "🍱","🍜","🍝","🍣","🍤","🍡","🥟","🥠",
            "🍦","🍧","🍨","🍩","🍪","🎂","🍰","🧁","🥧",
            "🍫","🍬","🍭","🍮","🍯",
            "🍴","🥄","🔪","🍽️","🥢"
        ]},
        {name:"Objects",emoji:[
            "🔧","🔩","⚙️","🛠️","⛏️","🔨","🪓","🪚","🪛",
            "⚖️","🔗","⛓️","🧲","🧰","🪜",
            "🧪","🧫","🧬","🔬","🔭","📡",
            "💻","🖥️","🖨️","⌨️","🖱️","💾","💿","📀",
            "📱","📲","☎️","📞",
            "📷","📸","📹","🎥","📽️","🎬","📺","📻",
            "💡","🔦","🕯️",
            "⌚","⏰","⏱️","⏲️","🕰️","⌛","⏳",
            "💰","🪙","💳","💵","💸",
            "✉️","📨","📩","📦","📬",
            "📝","📋","📅","📆",
            "📈","📉","📊","📌","📏","📐","✂️","💼",
            "📔","📕","📖","📗","📘","📙","📚","📓","📰",
            "🎵","🎶","🎧","🎤","🎹","🎷","🎸","🎺","🎻","🥁",
            "💉","💊","🩹","🩺",
            "🚪","🪞","🪟","🛏️","🛋️","🚿","🛁",
            "🧹","🧴","🧼","🧷",
            "🎈","🎁","🏆","🏅","🎗️","🎟️","🎫",
            "🔮","🎰","🧩","🧸",
            "💎","🪄"
        ]},
        {name:"Nature",emoji:[
            "☀️","🌤️","⛅","🌥️","☁️",
            "🌦️","🌧️","⛈️","🌩️","🌨️",
            "🌫️","🌁","🌀",
            "❄️","☃️","⛄","🌬️","💨","🌪️",
            "🌈","☂️","🌂","☔","⛱️",
            "🌡️","⚡",
            "💧","💦","🌊","🔥","☄️",
            "🌙","🌚","🌛","🌜","🌝","🌞",
            "🌑","🌒","🌓","🌔","🌕","🌖","🌗","🌘",
            "🌌","🌍","🌎","🌏","🌋",
            "🌱","🪴","🌲","🌳","🌴","🌵","🍀","☘️","🌾",
            "💐","🌸","🌷","🌹","🌺","🌻","🌼","🥀",
            "🍁","🍂","🍃",
            "🐶","🐱","🐭","🐹","🐰","🦊","🐻","🐼",
            "🐨","🐯","🦁","🐮","🐷","🐸","🐵",
            "🙈","🙉","🙊",
            "🐔","🐧","🐦","🦅","🦉","🐺",
            "🐴","🦄","🐝","🦋","🐌","🐞","🐜",
            "🐢","🐍","🐙","🐬","🐳","🦈",
            "🐘","🦒","🐪","🐄","🐑","🐕","🐈",
            "🐾"
        ]},
        {name:"Smileys",emoji:[
            "😀","😃","😄","😁","😆","😅","😂","🤣","🥲",
            "😊","😇","🙂","🙃",
            "😉","😍","🥰","😘","😗","☺️","😚","😙","🤩",
            "😋","😛","😜","🤪","😝","🤑",
            "🤗","🤭","🤫","🤔",
            "🤐","🤨","😐","😑","😶","😏","😒","🙄","😬","🤥",
            "😌","😔","😪","🤤","😴",
            "😷","🤒","🤕","🤢","🤮","🤧","🥵","🥶","🥴","😵","🤯",
            "😕","😟","🙁","☹️","😮","😯","😲","😳",
            "🥺","😦","😧","😨","😰","😥","😢","😭",
            "😱","😖","😣","😩","🥱",
            "😤","😡","😠","🤬",
            "😈","👿","💀","☠️","💩","🤡","👻","👽","👾","🤖",
            "😺","😸","😹","😻","😼","😽","🙀","😿","😾",
            "🙈","🙉","🙊",
            "💯","💢","💥","💫","💤","💬","💭","🗯️"
        ]},
        {name:"People",emoji:[
            "👋","🤚","🖐️","✋","🖖",
            "👌","🤌","🤏","✌️","🤞","🤟","🤘","🤙",
            "👈","👉","👆","🖕","👇","☝️",
            "👍","👎","✊","👊","🤛","🤜",
            "👏","🙌","👐","🤲","🤝","🙏",
            "✍️","💅","🤳",
            "💪","🦵","🦶",
            "👂","👃","🧠","🦷",
            "👀","👁️","👅","👄",
            "👶","👦","👧","👨","👩","👴","👵",
            "👓","🕶️","🥽","👔","👕","👖",
            "👗","👘","👟","👞","👠","👢",
            "👑","👒","🎩","🎓","🧢","⛑️",
            "💍","💎","💄"
        ]},
        {name:"Travel",emoji:[
            "🚗","🚕","🚙","🚌","🏎️","🚓","🚑","🚒",
            "🚚","🚛","🚜","🏍️","🛵","🚲","🛴","🛹",
            "✈️","🛫","🛬","🪂","🚁","🚀","🛸",
            "⛵","🚤","🛳️","🚢",
            "🚨","🚥","🚦","🛑","🚧","⛽",
            "🏠","🏡","🏢","🏣","🏥","🏦","🏨",
            "🏪","🏫","🏬","🏭","🏯","🏰",
            "🗼","🗽","⛪","🕌","🕍",
            "⛲","🎠","🎡","🎢",
            "🏖️","🏕️","🏜️",
            "🌅","🌄","🏙️","🌆","🌇","🌉","🌁",
            "🗺️","🧭"
        ]},
        {name:"Activity",emoji:[
            "⚽","🏀","🏈","⚾","🎾","🏐","🏉",
            "🎱","🏓","🏸","🏒","🏑","🏏",
            "⛳","🏹","🎣","🥊","🥋","🎽",
            "🛷","⛸️","🥌","🎿",
            "🏆","🥇","🥈","🥉","🏅",
            "🎃","🎄","🎆","🎇","🧨",
            "🎈","🎉","🎊","🎋","🎍",
            "🎎","🎏","🎐","🎑",
            "🎀","🎁","🎗️","🎟️","🎫",
            "🎯","🎰","🎲","♟️","🧩","🧸",
            "🎭","🎨","🖼️","🧵","🧶",
            "🎮","🕹️"
        ]}
    ];

    var ZONES = [
        {key: "statusBar", label: "Status Bar (All Pages)", hasOffset: false},
        {key: "topLeft", label: "Top Bar (Left)", hasOffset: false},
        {key: "topRight", label: "Top Bar (Right)", hasOffset: false},
        {key: "centerStatus", label: "Center - Top", hasOffset: true},
        {key: "centerTop", label: "Center - Action Buttons", hasOffset: true},
        {key: "centerMiddle", label: "Center - Info", hasOffset: true},
        {key: "bottomLeft", label: "Bottom Bar (Left)", hasOffset: false},
        {key: "bottomRight", label: "Bottom Bar (Right)", hasOffset: false}
    ];

    // Grouped by color (white, orange, blue), sorted by name within each group
    var WIDGET_TYPES = [
        // Actions & readouts (white)
        {type:"beans",label:"Beans"},
        {type:"connectionStatus",label:"Connection"},
        {type:"espresso",label:"Espresso"},
        {type:"autofavorites",label:"Favorites"},
        {type:"flush",label:"Flush"},
        {type:"history",label:"History"},
        {type:"hotwater",label:"Hot Water"},
        {type:"scaleWeight",label:"Scale Weight"},
        {type:"settings",label:"Settings"},
        {type:"shotPlan",label:"Shot Plan"},
        {type:"sleep",label:"Sleep"},
        {type:"steam",label:"Steam"},
        {type:"steamTemperature",label:"Steam Temp"},
        {type:"temperature",label:"Temperature"},
        {type:"waterLevel",label:"Water Level"},
        // Utility (orange)
        {type:"custom",label:"Custom",special:true},
        {type:"pageTitle",label:"Page Title",special:true},
        {type:"quit",label:"Quit",special:true},
        {type:"separator",label:"Separator",special:true},
        {type:"spacer",label:"Spacer",special:true},
        {type:"weather",label:"Weather",special:true},
        // Screensavers & widgets (blue)
        {type:"screensaverPipes",label:"3D Pipes",screensaver:true},
        {type:"screensaverAttractor",label:"Attractor",screensaver:true},
        {type:"screensaverFlipClock",label:"Flip Clock",screensaver:true},
        {type:"lastShot",label:"Last Shot",screensaver:true},
        {type:"screensaverShotMap",label:"Shot Map",screensaver:true}
    ];

    var DISPLAY_NAMES = {
        espresso:"Espresso",steam:"Steam",hotwater:"Hot Water",flush:"Flush",
        beans:"Beans",history:"History",autofavorites:"Favorites",sleep:"Sleep",
        settings:"Settings",temperature:"Temp",steamTemperature:"Steam",
        waterLevel:"Water",connectionStatus:"Connection",scaleWeight:"Scale",
        shotPlan:"Shot Plan",pageTitle:"Title",spacer:"Spacer",separator:"Sep",
        custom:"Custom",weather:"Weather",quit:"Quit",
        screensaverFlipClock:"Flip Clock",screensaverPipes:"3D Pipes",
        screensaverAttractor:"Attractor",screensaverShotMap:"Shot Map",
        lastShot:"Last Shot"
    };

    var ACTIONS = [
        {id:"",label:"None",contexts:["idle","espresso","steam","hotwater","flush","all"]},
        {id:"navigate:settings",label:"Go to Settings",contexts:["idle","all"]},
        {id:"navigate:history",label:"Go to History",contexts:["idle","all"]},
        {id:"navigate:profiles",label:"Go to Profiles",contexts:["idle","all"]},
        {id:"navigate:profileEditor",label:"Go to Profile Editor",contexts:["idle","all"]},
        {id:"navigate:recipes",label:"Go to Recipes",contexts:["idle","all"]},
        {id:"navigate:descaling",label:"Go to Descaling",contexts:["idle","all"]},
        {id:"navigate:ai",label:"Go to AI Settings",contexts:["idle","all"]},
        {id:"navigate:visualizer",label:"Go to Visualizer",contexts:["idle","all"]},
        {id:"navigate:autofavorites",label:"Go to Favorites",contexts:["idle","all"]},
        {id:"command:sleep",label:"Sleep",contexts:["idle"]},
        {id:"command:startEspresso",label:"Start Espresso",contexts:["idle"]},
        {id:"command:startSteam",label:"Start Steam",contexts:["idle"]},
        {id:"command:startHotWater",label:"Start Hot Water",contexts:["idle"]},
        {id:"command:startFlush",label:"Start Flush",contexts:["idle"]},
        {id:"command:idle",label:"Stop (Idle)",contexts:["idle","espresso","steam","hotwater","flush"]},
        {id:"command:tare",label:"Tare Scale",contexts:["idle","espresso","all"]},
        {id:"command:scanDE1",label:"Scan for DE1",contexts:["idle","all"]},
        {id:"command:scanScale",label:"Scan for Scale",contexts:["idle","all"]},
        {id:"command:quit",label:"Quit App",contexts:["idle"]}
    ];
    var PAGE_CONTEXT = "idle";
    function getFilteredActions() {
        return ACTIONS.filter(function(a) {
            return a.contexts.indexOf(PAGE_CONTEXT) >= 0 || a.contexts.indexOf("all") >= 0;
        });
    }
)HTML";
    html += R"HTML(
    function loadLayout() {
        fetch("/api/layout").then(function(r){
            if (!r.ok) throw new Error('Server error (' + r.status + ')');
            return r.json();
        }).then(function(data) {
            layoutData = data;
            // Fetch properties for custom items to render mini previews
            var customIds = [];
            if (data && data.zones) {
                for (var zk in data.zones) {
                    var zoneItems = data.zones[zk] || [];
                    for (var ci = 0; ci < zoneItems.length; ci++) {
                        if (zoneItems[ci].type === "custom" && !itemPropsCache[zoneItems[ci].id]) {
                            customIds.push(zoneItems[ci].id);
                        }
                    }
                }
            }
            if (customIds.length > 0) {
                var loaded = 0;
                for (var pi = 0; pi < customIds.length; pi++) {
                    (function(cid) {
                        fetch("/api/layout/item?id=" + encodeURIComponent(cid))
                            .then(function(r){
                                if (!r.ok) throw new Error('Server error (' + r.status + ')');
                                return r.json();
                            })
                            .then(function(props) {
                                itemPropsCache[cid] = props;
                                loaded++;
                                if (loaded >= customIds.length) renderZones();
                            }).catch(function(e) {
                                console.warn('Failed to load properties for custom item ' + cid + ':', e);
                                loaded++;
                                if (loaded >= customIds.length) renderZones();
                            });
                    })(customIds[pi]);
                }
            } else {
                renderZones();
            }
        }).catch(function(e) {
            console.warn('loadLayout failed:', e);
            showLibToast('Failed to load layout');
        });
    }

    function stripHtml(text) {
        var tmp = document.createElement("div");
        tmp.innerHTML = text;
        return tmp.textContent || tmp.innerText || "";
    }
)HTML";
    html += R"HTML(
    function renderZones() {
        var panel = document.getElementById("zonesPanel");
        var html = "";
        for (var z = 0; z < ZONES.length; z++) {
            var zone = ZONES[z];
            var items = (layoutData && layoutData.zones && layoutData.zones[zone.key]) || [];

            // Pair top and bottom zones side by side
            var isPairStart = (zone.key === "topLeft" || zone.key === "bottomLeft");
            var isPairEnd = (zone.key === "topRight" || zone.key === "bottomRight");
            if (isPairStart) html += '<div class="zone-row">';

            var zoneSelected = selectedChip && selectedChip.zone === zone.key;
            html += '<div class="zone-card' + (zoneSelected ? ' selected' : '') + '" style="' + (isPairStart || isPairEnd ? 'flex:1' : '') + '" onclick="zoneClick(\'' + zone.key + '\',event)">';
            html += '<div class="zone-header"><span class="zone-title">' + zone.label + '</span>';

            if (zone.hasOffset) {
                var offset = 0;
                if (layoutData && layoutData.offsets && layoutData.offsets[zone.key] !== undefined)
                    offset = layoutData.offsets[zone.key];
                html += '<div class="zone-offset-controls">';
                html += '<button class="offset-btn" onclick="changeOffset(\'' + zone.key + '\',-5)">&#9650;</button>';
                html += '<span class="offset-val">' + (offset !== 0 ? (offset > 0 ? "+" : "") + offset : "0") + '</span>';
                html += '<button class="offset-btn" onclick="changeOffset(\'' + zone.key + '\',5)">&#9660;</button>';
                var scale = (layoutData && layoutData.scales && layoutData.scales[zone.key]) ? layoutData.scales[zone.key] : 1.0;
                html += '<div class="offset-separator"></div>';
                html += '<span class="offset-val">' + (scale !== 1.0 ? '&times;' + scale.toFixed(2) : '') + '</span>';
                html += '<button class="offset-btn" onclick="changeScale(\'' + zone.key + '\',-0.05)" style="font-weight:bold">&minus;</button>';
                html += '<button class="offset-btn" onclick="changeScale(\'' + zone.key + '\',0.05)" style="font-weight:bold">+</button>';
                html += '</div>';
            }
            html += '</div>';

            html += '<div class="chips-area">';
            for (var i = 0; i < items.length; i++) {
                var item = items[i];
                var isSS = item.type.indexOf("screensaver") === 0 || item.type === "lastShot";
                var isSpecial = item.type === "spacer" || item.type === "custom" || item.type === "weather" || item.type === "separator" || item.type === "pageTitle" || item.type === "quit";
                var isSel = selectedChip && selectedChip.id === item.id;
                var cls = "chip" + (isSel ? " selected" : "") + (isSS ? " screensaver" : (isSpecial ? " special" : ""));
                var chipStyle = "";
                var props = item.type === "custom" ? itemPropsCache[item.id] : null;
                if (props && props.backgroundColor && !isSel && !props.hideBackground) {
                    chipStyle = "background:" + props.backgroundColor + ";border-color:" + props.backgroundColor + ";color:white";
                }
                html += '<span class="' + cls + '" style="' + chipStyle + '" onclick="chipClick(\'' + item.id + '\',\'' + zone.key + '\',\'' + item.type + '\')">';

                if (isSel && i > 0) {
                    html += '<span class="chip-arrow" onclick="event.stopPropagation();reorder(\'' + zone.key + '\',' + i + ',' + (i-1) + ')">&#9664;</span>';
                }
                // Mini preview for custom items
                if (item.type === "custom" && props) {
                    if (props.emoji) {
                        if (props.emoji.indexOf("qrc:") === 0) {
                            html += '<span class="chip-emoji"><img src="' + props.emoji.replace("qrc:","") + '"></span>';
                        } else {
                            html += '<span class="chip-emoji">' + props.emoji + '</span>';
                        }
                    }
                    var chipLabel = stripHtml(props.content || "");
                    chipLabel = chipLabel.length > 12 ? chipLabel.substring(0, 10) + ".." : (chipLabel || "Custom");
                    html += chipLabel;
                } else {
                    html += DISPLAY_NAMES[item.type] || item.type;
                }
                if (isSel && i < items.length - 1) {
                    html += '<span class="chip-arrow" onclick="event.stopPropagation();reorder(\'' + zone.key + '\',' + i + ',' + (i+1) + ')">&#9654;</span>';
                }
                if (isSel) {
                    html += '<span class="chip-remove" onclick="event.stopPropagation();removeItem(\'' + item.id + '\',\'' + zone.key + '\')">&times;</span>';
                }
                html += '</span>';
            }

            // Add button with dropdown
            html += '<div style="position:relative;display:inline-block">';
            html += '<button class="add-btn" onclick="event.stopPropagation();toggleAddMenu(this)">+</button>';
            html += '<div class="add-dropdown">';
            for (var w = 0; w < WIDGET_TYPES.length; w++) {
                var wt = WIDGET_TYPES[w];
                html += '<div class="add-dropdown-item' + (wt.screensaver ? ' screensaver' : (wt.special ? ' special' : '')) + '" ';
                html += 'onclick="event.stopPropagation();addItem(\'' + wt.type + '\',\'' + zone.key + '\');this.parentElement.classList.remove(\'open\')">';
                html += wt.label + '</div>';
            }
            html += '</div></div>';

            html += '</div></div>';

            if (isPairEnd) html += '</div>';
        }
        panel.innerHTML = html;
    }
)HTML";

    // Part 5b: Layout editor JS - interaction handlers
    html += R"HTML(
    function zoneClick(zoneKey, event) {
        // Only handle clicks on the zone card itself, not on chips/buttons inside
        if (event.target.closest('.chip, .add-btn, .add-dropdown, .offset-btn')) return;
        if (selectedChip && selectedChip.zone === zoneKey && !selectedChip.id) {
            selectedChip = null;
        } else {
            selectedChip = {id: null, zone: zoneKey};
        }
        renderZones();
    }

    function chipClick(itemId, zone, type) {
        if (selectedChip && selectedChip.id === itemId) {
            // Deselect
            selectedChip = null;
        } else {
            selectedChip = {id: itemId, zone: zone};
            if (type === "custom") {
                openEditor(itemId, zone);
            } else if (type.indexOf("screensaver") === 0 || type === "lastShot") {
                openScreensaverEditor(itemId, zone, type);
            }
        }
        renderZones();
    }

    function toggleAddMenu(btn) {
        var dropdown = btn.nextElementSibling;
        // Close all other dropdowns
        document.querySelectorAll(".add-dropdown.open").forEach(function(d) {
            if (d !== dropdown) d.classList.remove("open");
        });
        // Reset position before measuring
        dropdown.style.top = "";
        dropdown.style.bottom = "";
        dropdown.style.maxHeight = "";
        dropdown.classList.toggle("open");
        if (dropdown.classList.contains("open")) {
            // Check if dropdown overflows the viewport and flip upward if needed
            var rect = dropdown.getBoundingClientRect();
            var viewH = window.innerHeight;
            if (rect.bottom > viewH) {
                var spaceBelow = viewH - rect.top;
                var spaceAbove = rect.top;
                if (spaceAbove > spaceBelow) {
                    // Open upward
                    dropdown.style.top = "auto";
                    dropdown.style.bottom = "100%";
                    dropdown.style.maxHeight = Math.min(400, spaceAbove - 8) + "px";
                } else {
                    // Keep downward but clamp height
                    dropdown.style.maxHeight = Math.max(120, spaceBelow - 8) + "px";
                }
            }
        }
    }

    // Close dropdowns when clicking outside
    document.addEventListener("click", function(e) {
        if (!e.target.closest(".add-btn") && !e.target.closest(".add-dropdown")) {
            document.querySelectorAll(".add-dropdown.open").forEach(function(d) { d.classList.remove("open"); });
        }
    });

    function addItem(type, zone) {
        apiPost("/api/layout/add", {type: type, zone: zone}, function() {
            loadLayout();
        });
    }

    function removeItem(itemId, zone) {
        apiPost("/api/layout/remove", {itemId: itemId, zone: zone}, function() {
            if (selectedChip && selectedChip.id === itemId) selectedChip = null;
            if (editingItem && editingItem.id === itemId) closeEditor();
            if (ssEditingItem && ssEditingItem.id === itemId) closeScreensaverEditor();
            loadLayout();
        });
    }

    function reorder(zone, fromIdx, toIdx) {
        apiPost("/api/layout/reorder", {zone: zone, fromIndex: fromIdx, toIndex: toIdx}, function() {
            loadLayout();
        });
    }

    function changeOffset(zone, delta) {
        var current = 0;
        if (layoutData && layoutData.offsets && layoutData.offsets[zone] !== undefined)
            current = layoutData.offsets[zone];
        apiPost("/api/layout/zone-offset", {zone: zone, offset: current + delta}, function() {
            loadLayout();
        });
    }

    function changeScale(zone, delta) {
        var current = 1.0;
        if (layoutData && layoutData.scales && layoutData.scales[zone] !== undefined)
            current = layoutData.scales[zone];
        var newScale = Math.round((current + delta) * 100) / 100;
        apiPost("/api/layout/zone-scale", {zone: zone, scale: newScale}, function() {
            loadLayout();
        });
    }

    function resetLayout() {
        if (!confirm("Reset layout to default?")) return;
        apiPost("/api/layout/reset", {}, function() {
            selectedChip = null;
            closeEditor();
            closeScreensaverEditor();
            loadLayout();
        });
    }

    // ---- Screensaver Editor ----

    var ssEditingItem = null;
    var ssEditingType = "";
    var ssCurrentMapTexture = "";

    var SS_TITLES = {
        screensaverFlipClock: "Flip Clock Settings",
        screensaverPipes: "3D Pipes Settings",
        screensaverAttractor: "Attractor Settings",
        screensaverShotMap: "Shot Map Settings",
        lastShot: "Last Shot Settings"
    };

    function openScreensaverEditor(itemId, zone, type) {
        // Close custom editor if open
        closeEditor();
        ssEditingItem = {id: itemId, zone: zone};
        ssEditingType = type;
        document.getElementById("ssEditorTitle").textContent = SS_TITLES[type] || "Screensaver Settings";

        // Hide all setting sections
        document.getElementById("ssClockSettings").style.display = "none";
        document.getElementById("ssMapSettings").style.display = "none";
        document.getElementById("ssLastShotSettings").style.display = "none";
        document.getElementById("ssNoSettings").style.display = "none";

        // Fetch current properties
        fetch("/api/layout/item?id=" + encodeURIComponent(itemId))
            .then(function(r) {
                if (!r.ok) throw new Error('Server error (' + r.status + ')');
                return r.json();
            })
            .then(function(props) {
                if (type === "screensaverFlipClock") {
                    var scale = 1.0;
                    if (typeof props.clockScale === "number") scale = props.clockScale;
                    else if (props.fitMode === "width") scale = 0.0;
                    document.getElementById("ssClockScale").value = scale;
                    document.getElementById("ssClockSettings").style.display = "";
                } else if (type === "screensaverShotMap") {
                    var mapScale = typeof props.mapScale === "number" ? props.mapScale : 1.0;
                    document.getElementById("ssMapScale").value = mapScale;
                    ssCurrentMapTexture = (typeof props.mapTexture === "string") ? props.mapTexture : "";
                    ssUpdateTextureButtons();
                    document.getElementById("ssMapSettings").style.display = "";
                } else if (type === "lastShot") {
                    var shotScale = typeof props.shotScale === "number" ? props.shotScale : 1.0;
                    document.getElementById("ssShotScale").value = shotScale;
                    document.getElementById("ssShotShowLabels").checked = typeof props.shotShowLabels === "boolean" ? props.shotShowLabels : false;
                    document.getElementById("ssShotShowPhaseLabels").checked = typeof props.shotShowPhaseLabels === "boolean" ? props.shotShowPhaseLabels : true;
                    document.getElementById("ssLastShotSettings").style.display = "";
                } else {
                    document.getElementById("ssNoSettings").style.display = "";
                }
                document.getElementById("ssEditorPanel").classList.remove("editor-hidden");
            }).catch(function(e) {
                console.warn('openScreensaverEditor failed:', e);
                showLibToast('Failed to load item properties');
            });
    }

    function closeScreensaverEditor() {
        if (ssAutoSaveTimer) { clearTimeout(ssAutoSaveTimer); ssAutoSaveTimer = null; ssSaveProperty(); }
        ssEditingItem = null;
        ssEditingType = "";
        document.getElementById("ssEditorPanel").classList.add("editor-hidden");
    }

    var ssAutoSaveTimer = null;
    function ssAutoSave() {
        if (ssAutoSaveTimer) clearTimeout(ssAutoSaveTimer);
        ssAutoSaveTimer = setTimeout(function() { ssAutoSaveTimer = null; ssSaveProperty(); }, 200);
    }

    function ssSaveProperty() {
        if (!ssEditingItem) return;
        var id = ssEditingItem.id;
        if (ssEditingType === "screensaverFlipClock") {
            var clockScale = parseFloat(document.getElementById("ssClockScale").value);
            apiPost("/api/layout/item", {itemId: id, key: "clockScale", value: clockScale}, function() {});
        } else if (ssEditingType === "screensaverShotMap") {
            var mapScale = parseFloat(document.getElementById("ssMapScale").value);
            apiPost("/api/layout/item", {itemId: id, key: "mapScale", value: mapScale}, function() {});
            apiPost("/api/layout/item", {itemId: id, key: "mapTexture", value: ssCurrentMapTexture}, function() {});
        } else if (ssEditingType === "lastShot") {
            var shotScale = parseFloat(document.getElementById("ssShotScale").value);
            var showLabels = document.getElementById("ssShotShowLabels").checked;
            var showPhaseLabels = document.getElementById("ssShotShowPhaseLabels").checked;
            apiPost("/api/layout/item", {itemId: id, key: "shotScale", value: shotScale}, function() {});
            apiPost("/api/layout/item", {itemId: id, key: "shotShowLabels", value: showLabels}, function() {});
            apiPost("/api/layout/item", {itemId: id, key: "shotShowPhaseLabels", value: showPhaseLabels}, function() {});
        }
    }

    function ssClockScaleChanged(val) {
        ssAutoSave();
    }

    function ssMapScaleChanged(val) {
        ssAutoSave();
    }

    function ssShotScaleChanged(val) {
        ssAutoSave();
    }
    function ssShotToggleChanged() {
        ssAutoSave();
    }

    function ssSelectMapTexture(value) {
        ssCurrentMapTexture = value;
        ssUpdateTextureButtons();
        ssSaveProperty();
    }

    function ssUpdateTextureButtons() {
        var btns = document.getElementById("ssMapTextureGroup").querySelectorAll(".ss-btn-option");
        var values = ["", "dark", "bright", "satellite"];
        for (var i = 0; i < btns.length; i++) {
            if (values[i] === ssCurrentMapTexture) {
                btns[i].classList.add("active");
            } else {
                btns[i].classList.remove("active");
            }
        }
    }

    // ---- WYSIWYG Text Editor ----

    var wysiwygEl = document.getElementById("wysiwygEditor");
    var actionPickerGesture = "";
    var savedRange = null;

    // Save/restore selection so color pickers etc. don't lose it
    function saveSelection() {
        var sel = window.getSelection();
        if (sel.rangeCount > 0 && wysiwygEl.contains(sel.anchorNode)) {
            savedRange = sel.getRangeAt(0).cloneRange();
        }
    }
    function restoreSelection() {
        if (savedRange) {
            wysiwygEl.focus();
            var sel = window.getSelection();
            sel.removeAllRanges();
            sel.addRange(savedRange);
        } else {
            wysiwygEl.focus();
        }
    }
    wysiwygEl.addEventListener("mouseup", saveSelection);
    wysiwygEl.addEventListener("keyup", saveSelection);
    wysiwygEl.addEventListener("blur", saveSelection);

    // Format state feedback: highlight bold/italic buttons when active
    function updateFormatState() {
        var b = document.getElementById("btnBold");
        var i = document.getElementById("btnItalic");
        if (b) b.classList.toggle("active", document.queryCommandState("bold"));
        if (i) i.classList.toggle("active", document.queryCommandState("italic"));
    }
    wysiwygEl.addEventListener("keyup", updateFormatState);
    wysiwygEl.addEventListener("mouseup", updateFormatState);
    document.addEventListener("selectionchange", function() {
        if (editingItem) updateFormatState();
    });
)HTML";
    html += R"HTML(
    // ---- Segment model (portable rich text format) ----

    function rgbToHex(rgb) {
        if (!rgb) return "";
        if (rgb.charAt(0) === "#") return rgb;
        var m = rgb.match(/(\d+)/g);
        if (!m || m.length < 3) return rgb;
        return "#" + ((1<<24)+(parseInt(m[0])<<16)+(parseInt(m[1])<<8)+parseInt(m[2])).toString(16).slice(1);
    }

    function domToSegments(el) {
        var segments = [];
        function walk(node, inherited) {
            if (node.nodeType === 3) {
                var text = node.textContent;
                if (text) {
                    var seg = {text: text};
                    if (inherited.bold) seg.bold = true;
                    if (inherited.italic) seg.italic = true;
                    if (inherited.color) seg.color = inherited.color;
                    if (inherited.size) seg.size = inherited.size;
                    segments.push(seg);
                }
                return;
            }
            if (node.nodeType !== 1) return;
            var tag = node.tagName.toLowerCase();
            var fmt = {};
            for (var k in inherited) fmt[k] = inherited[k];
            if (tag === "b" || tag === "strong") fmt.bold = true;
            if (tag === "i" || tag === "em") fmt.italic = true;
            if (tag === "br") { segments.push({text: "\n"}); return; }
            // Browsers create <div> or <p> on Enter — treat as line break before content
            if ((tag === "div" || tag === "p") && segments.length > 0) {
                var lastSeg = segments[segments.length - 1];
                if (lastSeg.text !== "\n") segments.push({text: "\n"});
            }
            var st = node.style;
            if (st.color) fmt.color = rgbToHex(st.color);
            if (st.fontSize) { var px = parseInt(st.fontSize); if (px > 0) fmt.size = px; }
            if (st.fontWeight === "bold" || parseInt(st.fontWeight) >= 700) fmt.bold = true;
            if (st.fontStyle === "italic") fmt.italic = true;
            if (tag === "font" && node.getAttribute("color")) fmt.color = rgbToHex(node.getAttribute("color"));
            for (var i = 0; i < node.childNodes.length; i++) walk(node.childNodes[i], fmt);
        }
        walk(el, {});
        return mergeAdjacentSegments(segments);
    }

    function mergeAdjacentSegments(segments) {
        if (segments.length <= 1) return segments;
        var result = [];
        for (var i = 0; i < segments.length; i++) {
            var cur = segments[i];
            if (result.length === 0) { result.push({text:cur.text, bold:cur.bold, italic:cur.italic, color:cur.color, size:cur.size}); continue; }
            var prev = result[result.length - 1];
            if (prev.text !== "\n" && cur.text !== "\n" &&
                !!prev.bold === !!cur.bold && !!prev.italic === !!cur.italic &&
                (prev.color||"") === (cur.color||"") && (prev.size||0) === (cur.size||0)) {
                prev.text += cur.text;
            } else {
                result.push({text:cur.text, bold:cur.bold, italic:cur.italic, color:cur.color, size:cur.size});
            }
        }
        // Clean undefined values
        for (var j = 0; j < result.length; j++) {
            var s = result[j];
            if (!s.bold) delete s.bold;
            if (!s.italic) delete s.italic;
            if (!s.color) delete s.color;
            if (!s.size) delete s.size;
        }
        return result;
    }

    function segmentsToHtml(segments) {
        var html = "";
        for (var i = 0; i < segments.length; i++) {
            var seg = segments[i];
            var text = seg.text;
            if (!text) continue;
            if (text === "\n") { html += "<br>"; continue; }
            var escaped = text.replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;");
            var styles = [];
            if (seg.color) styles.push("color:" + seg.color);
            if (seg.size) styles.push("font-size:" + seg.size + "px");
            if (styles.length > 0) escaped = '<span style="' + styles.join("; ") + '">' + escaped + '</span>';
            if (seg.bold) escaped = "<b>" + escaped + "</b>";
            if (seg.italic) escaped = "<i>" + escaped + "</i>";
            html += escaped;
        }
        return html || "Text";
    }
)HTML";
    html += R"HTML(
    // Detect malformed HTML (tags inside attribute values) and strip to plain text
    function sanitizeHtml(html) {
        if (!html || html.indexOf("<") < 0) return html;
        var inTag = false, inQuote = false;
        for (var i = 0; i < html.length; i++) {
            var ch = html[i];
            if (inQuote) {
                if (ch === '"') inQuote = false;
                else if (ch === '<') {
                    console.warn("Malformed HTML detected, stripping tags");
                    return html.replace(/<[^>]*>/g, "");
                }
            } else if (inTag) {
                if (ch === '"') inQuote = true;
                else if (ch === '>') inTag = false;
            } else {
                if (ch === '<') inTag = true;
            }
        }
        return html;
    }

    function openEditor(itemId, zone) {
        // Close screensaver editor if open
        closeScreensaverEditor();
        // Flush any pending auto-save from previously edited item
        if (editingItem && autoSaveTimer) {
            clearTimeout(autoSaveTimer); autoSaveTimer = null; saveText();
        }
        editingItem = {id: itemId, zone: zone};
        document.getElementById("emojiPickerArea").style.display = "none";
        document.getElementById("emojiToggleBtn").textContent = "Pick Icon";
        fetch("/api/layout/item?id=" + encodeURIComponent(itemId))
            .then(function(r){
                if (!r.ok) throw new Error('Server error (' + r.status + ')');
                return r.json();
            })
            .then(function(props) {
                // Load segments if available, otherwise fall back to HTML content
                if (props.segments && props.segments.length > 0) {
                    wysiwygEl.innerHTML = segmentsToHtml(props.segments);
                } else {
                    wysiwygEl.innerHTML = sanitizeHtml(props.content || "Text");
                }
                currentAlign = props.align || "center";
                currentAction = props.action || "";
                currentLongPressAction = props.longPressAction || "";
                currentDoubleclickAction = props.doubleclickAction || "";
                currentEmoji = props.emoji || "";
                currentBgColor = props.backgroundColor || "";
                currentHideBackground = props.hideBackground || false;
                wysiwygEl.style.textAlign = currentAlign;
                updateAlignButtons();
                updateActionSelectors();
                updateIconPreview();
                renderEmojiTabs();
                renderEmojiGrid();
                updateBgColorUI();
                updateHideBgUI();
                updateTextColorUI("#ffffff");
                updatePreview();
                document.getElementById("editorPanel").classList.remove("editor-hidden");
                wysiwygEl.focus();
            }).catch(function(e) {
                console.warn('openEditor failed:', e);
                showLibToast('Failed to load item properties');
            });
    }

    function closeEditor() {
        // Flush any pending auto-save before closing
        if (autoSaveTimer) { clearTimeout(autoSaveTimer); autoSaveTimer = null; saveText(); }
        editingItem = null;
        document.getElementById("editorPanel").classList.add("editor-hidden");
    }

    var autoSaveTimer = null;
    function autoSave() {
        if (autoSaveTimer) clearTimeout(autoSaveTimer);
        autoSaveTimer = setTimeout(function() { autoSaveTimer = null; saveText(); }, 500);
    }

    function saveText() {
        if (!editingItem) return;
        // Extract segments from the contenteditable DOM and compile to HTML
        var segments = domToSegments(wysiwygEl);
        var content = segmentsToHtml(segments);
        if (!content || content === "<br>") content = "Text";
        var id = editingItem.id;
        var done = 0;
        var total = 9;
        function check() { done++; if (done >= total) { itemPropsCache[id] = null; loadLayout(); } }
        apiPost("/api/layout/item", {itemId: id, key: "content", value: content}, check);
        apiPost("/api/layout/item", {itemId: id, key: "segments", value: segments}, check);
        apiPost("/api/layout/item", {itemId: id, key: "align", value: currentAlign}, check);
        apiPost("/api/layout/item", {itemId: id, key: "action", value: currentAction}, check);
        apiPost("/api/layout/item", {itemId: id, key: "longPressAction", value: currentLongPressAction}, check);
        apiPost("/api/layout/item", {itemId: id, key: "doubleclickAction", value: currentDoubleclickAction}, check);
        apiPost("/api/layout/item", {itemId: id, key: "emoji", value: currentEmoji}, check);
        apiPost("/api/layout/item", {itemId: id, key: "backgroundColor", value: currentBgColor}, check);
        apiPost("/api/layout/item", {itemId: id, key: "hideBackground", value: currentHideBackground}, check);
    }
)HTML";
    html += R"HTML(
    // ---- WYSIWYG formatting (execCommand) ----

    function execBold() {
        restoreSelection();
        document.execCommand("bold", false, null);
        saveSelection();
        updateFormatState();
        updatePreview();
        autoSave();
    }

    function execItalic() {
        restoreSelection();
        document.execCommand("italic", false, null);
        saveSelection();
        updateFormatState();
        updatePreview();
        autoSave();
    }

    function execClearFormat() {
        restoreSelection();
        document.execCommand("removeFormat", false, null);
        // removeFormat doesn't strip <span style="font-size:..."> — clean those up
        var sel = window.getSelection();
        if (sel.rangeCount && !sel.isCollapsed) {
            var range = sel.getRangeAt(0);
            var spans = wysiwygEl.querySelectorAll('span[style]');
            for (var i = spans.length - 1; i >= 0; i--) {
                if (range.intersectsNode(spans[i])) {
                    spans[i].style.fontSize = "";
                    spans[i].style.color = "";
                    if (!spans[i].style.cssText.trim()) {
                        while (spans[i].firstChild) spans[i].parentNode.insertBefore(spans[i].firstChild, spans[i]);
                        spans[i].parentNode.removeChild(spans[i]);
                    }
                }
            }
        }
        saveSelection();
        updateFormatState();
        updatePreview();
        autoSave();
    }

    function execFontSize(px) {
        restoreSelection();
        var sel = window.getSelection();
        if (!sel.rangeCount || sel.isCollapsed) return;
        // Use execCommand fontSize (handles cross-boundary selections correctly)
        // then convert the <font size="7"> tags to proper <span style="font-size:...">
        document.execCommand("fontSize", false, "7");
        var fonts = wysiwygEl.querySelectorAll('font[size="7"]');
        for (var i = 0; i < fonts.length; i++) {
            var span = document.createElement("span");
            span.style.fontSize = px + "px";
            while (fonts[i].firstChild) span.appendChild(fonts[i].firstChild);
            fonts[i].parentNode.replaceChild(span, fonts[i]);
        }
        saveSelection();
        updatePreview();
        autoSave();
    }

    function applyTextColor(color) {
        restoreSelection();
        document.execCommand("foreColor", false, color);
        saveSelection();
        updateTextColorUI(color);
        updatePreview();
        autoSave();
    }

    function clearTextColor() {
        restoreSelection();
        // Apply default color instead of removeFormat (which strips ALL formatting)
        document.execCommand("foreColor", false, "#ffffff");
        saveSelection();
        updateTextColorUI("#ffffff");
        updatePreview();
        autoSave();
    }
)HTML";
    html += R"HTML(
    // --- Color popup with iro.js picker + theme swatches ---
    var colorPopupMode = "text"; // "text" or "bg"
    var iroPicker = null;
    var iroPickerColor = "#ffffff";

    function initIroPicker() {
        if (iroPicker) return;
        iroPicker = new iro.ColorPicker("#iroPickerContainer", {
            width: 140,
            color: "#ffffff",
            borderWidth: 1,
            borderColor: "#444",
            layoutDirection: "vertical",
            layout: [
                { component: iro.ui.Wheel },
                { component: iro.ui.Slider, options: { sliderType: "value" } }
            ]
        });
        iroPicker.on("color:change", function(color) {
            iroPickerColor = color.hexString;
            document.getElementById("cpHexInput").value = iroPickerColor;
            document.getElementById("cpPreviewSwatch").style.background = iroPickerColor;
        });
    }

    function openColorPopup(mode) {
        saveSelection();
        colorPopupMode = mode;
        document.getElementById("colorPopupTitle").textContent = mode === "text" ? "Text Color" : "Background Color";
        // Reset dragged position so it centers
        var box = document.getElementById("colorPopupBox");
        box.style.position = "";
        box.style.left = "";
        box.style.top = "";
        // Initialize iro.js picker (lazy, first open)
        initIroPicker();
        // Set picker to current color
        var startColor = mode === "bg" ? (currentBgColor || "#555555") : "#ffffff";
        iroPicker.color.hexString = startColor;
        iroPickerColor = startColor;
        document.getElementById("cpHexInput").value = startColor;
        document.getElementById("cpPreviewSwatch").style.background = startColor;
        document.getElementById("colorPopupOverlay").classList.add("open");
    }

    function closeColorPopup() {
        document.getElementById("colorPopupOverlay").classList.remove("open");
    }

    function pickColor(color) {
        if (colorPopupMode === "text") {
            applyTextColor(color);
        } else {
            setBgColor(color);
        }
        closeColorPopup();
    }

    function applyPickerColor() {
        pickColor(iroPickerColor);
    }

    function onHexInput(val) {
        if (/^#[0-9a-fA-F]{6}$/.test(val)) {
            iroPickerColor = val;
            iroPicker.color.hexString = val;
            document.getElementById("cpPreviewSwatch").style.background = val;
        }
    }

    // Make popup draggable
    (function() {
        var box = document.getElementById("colorPopupBox");
        var dragging = false, startX, startY, origX, origY;
        box.querySelector("h4").style.cursor = "move";
        box.querySelector("h4").addEventListener("mousedown", function(e) {
            dragging = true;
            startX = e.clientX; startY = e.clientY;
            var rect = box.getBoundingClientRect();
            origX = rect.left; origY = rect.top;
            box.style.position = "fixed";
            box.style.margin = "0";
            e.preventDefault();
        });
        document.addEventListener("mousemove", function(e) {
            if (!dragging) return;
            box.style.left = (origX + e.clientX - startX) + "px";
            box.style.top = (origY + e.clientY - startY) + "px";
        });
        document.addEventListener("mouseup", function() { dragging = false; });
    })();

    function updateTextColorUI(color) {
        document.getElementById("textColorSwatch").style.background = color;
        document.getElementById("textColorInput").value = color;
    }

    function insertVar(token) {
        restoreSelection();
        document.execCommand("insertText", false, token);
        saveSelection();
        updatePreview();
        autoSave();
    }

    function setAlign(a) {
        currentAlign = a;
        wysiwygEl.style.textAlign = a;
        updateAlignButtons();
        updatePreview();
        autoSave();
    }

    function updateAlignButtons() {
        ["Left","Center","Right"].forEach(function(d) {
            var btn = document.getElementById("align" + d);
            if (btn) btn.classList.toggle("active", currentAlign === d.toLowerCase());
        });
    }
)HTML";
    html += R"HTML(
    // ---- Background Color ----

    function setBgColor(color) {
        currentBgColor = color;
        updateBgColorUI();
        updatePreview();
        autoSave();
    }

    function clearBgColor() {
        currentBgColor = "";
        updateBgColorUI();
        updatePreview();
        autoSave();
    }

    function updateBgColorUI() {
        var swatch = document.getElementById("bgColorSwatch");
        var noneX = document.getElementById("bgNoneX");
        var clearBtn = document.getElementById("bgClearBtn");
        if (currentBgColor) {
            swatch.style.background = currentBgColor;
            noneX.style.display = "none";
            clearBtn.style.display = "";
        } else {
            swatch.style.background = "transparent";
            noneX.style.display = "";
            clearBtn.style.display = "none";
        }
    }

    function toggleHideBackground() {
        currentHideBackground = !currentHideBackground;
        updateHideBgUI();
        updatePreview();
        autoSave();
    }

    function updateHideBgUI() {
        var el = document.getElementById("hideBgToggle");
        if (el) {
            if (currentHideBackground) {
                el.classList.add("active");
            } else {
                el.classList.remove("active");
            }
        }
    }

)HTML";

    // Part 5b2: Layout editor JS - action picker, emoji, preview
    html += R"HTML(
    // ---- Action Picker (popup, matching tablet design) ----

    function getActionLabel(id) {
        if (!id) return "None";
        for (var i = 0; i < ACTIONS.length; i++) {
            if (ACTIONS[i].id === id) return ACTIONS[i].label;
        }
        return id;
    }

    function updateActionSelectors() {
        document.getElementById("tapActionLabel").textContent = getActionLabel(currentAction);
        document.getElementById("longPressActionLabel").textContent = getActionLabel(currentLongPressAction);
        document.getElementById("dblClickActionLabel").textContent = getActionLabel(currentDoubleclickAction);
        document.getElementById("tapActionSel").className = "action-selector" + (currentAction ? " has-action" : "");
        document.getElementById("longPressActionSel").className = "action-selector" + (currentLongPressAction ? " has-action" : "");
        document.getElementById("dblClickActionSel").className = "action-selector" + (currentDoubleclickAction ? " has-action" : "");
    }

    function openActionPicker(gesture) {
        actionPickerGesture = gesture;
        var titles = {click: "Tap Action", longpress: "Long Press Action", doubleclick: "Double-Click Action"};
        document.getElementById("actionPickerTitle").textContent = titles[gesture] || "Action";
        var currentVal = gesture === "click" ? currentAction : gesture === "longpress" ? currentLongPressAction : currentDoubleclickAction;
        var filtered = getFilteredActions();
        var html = "";
        for (var i = 0; i < filtered.length; i++) {
            var a = filtered[i];
            var cls = "action-dialog-item" + (currentVal === a.id ? " selected" : "");
            html += '<div class="' + cls + '" onclick="pickAction(\'' + a.id + '\')">' + a.label + '</div>';
        }
        document.getElementById("actionPickerList").innerHTML = html;
        document.getElementById("actionOverlay").classList.add("open");
    }

    function closeActionPicker() {
        document.getElementById("actionOverlay").classList.remove("open");
    }

    function pickAction(id) {
        if (actionPickerGesture === "click") currentAction = id;
        else if (actionPickerGesture === "longpress") currentLongPressAction = id;
        else if (actionPickerGesture === "doubleclick") currentDoubleclickAction = id;
        updateActionSelectors();
        closeActionPicker();
        updatePreview();
        autoSave();
    }
)HTML";
    html += R"HTML(
    // ---- Emoji / Icon Picker ----

    function toggleEmojiPicker() {
        var area = document.getElementById("emojiPickerArea");
        var btn = document.getElementById("emojiToggleBtn");
        if (area.style.display === "none") {
            area.style.display = "";
            btn.textContent = "Hide Picker";
        } else {
            area.style.display = "none";
            btn.textContent = "Pick Icon";
        }
    }

    function renderEmojiTabs() {
        var html = "";
        for (var i = 0; i < EMOJI_CATEGORIES.length; i++) {
            var cls = "emoji-tab" + (emojiCategory === i ? " active" : "");
            html += '<span class="' + cls + '" onclick="setEmojiCategory(' + i + ')">' + EMOJI_CATEGORIES[i].name + '</span>';
        }
        document.getElementById("emojiTabs").innerHTML = html;
    }

    function renderEmojiGrid() {
        var cat = EMOJI_CATEGORIES[emojiCategory];
        var html = "";
        if (cat.isSvg) {
            for (var i = 0; i < DECENZA_ICONS.length; i++) {
                var icon = DECENZA_ICONS[i];
                var sel = currentEmoji === ("qrc:" + icon.value) ? " selected" : "";
                html += '<span class="emoji-cell' + sel + '" title="' + icon.label + '" onclick="selectEmoji(\'qrc:' + icon.value + '\')">';
                html += '<img src="' + icon.value + '" alt="' + icon.label + '">';
                html += '</span>';
            }
        } else {
            var emojis = cat.emoji || [];
            for (var i = 0; i < emojis.length; i++) {
                var e = emojis[i];
                var sel = currentEmoji === e ? " selected" : "";
                html += '<span class="emoji-cell' + sel + '" onclick="selectEmoji(\'' + e + '\')">' + e + '</span>';
            }
        }
        document.getElementById("emojiGrid").innerHTML = html;
    }

    function setEmojiCategory(idx) {
        emojiCategory = idx;
        renderEmojiTabs();
        renderEmojiGrid();
    }

    function selectEmoji(value) {
        currentEmoji = value;
        updateIconPreview();
        renderEmojiGrid();
        updatePreview();
        autoSave();
    }

    function clearEmoji() {
        currentEmoji = "";
        updateIconPreview();
        renderEmojiGrid();
        updatePreview();
        autoSave();
    }

    function updateIconPreview() {
        var preview = document.getElementById("iconPreview");
        var clearBtn = document.getElementById("emojiClearBtn");
        if (!currentEmoji) {
            preview.innerHTML = '<span style="color:var(--text-secondary)">&#8212;</span>';
            clearBtn.style.display = "none";
        } else if (currentEmoji.indexOf("qrc:") === 0) {
            var src = currentEmoji.replace("qrc:", "");
            preview.innerHTML = '<img src="' + src + '">';
            clearBtn.style.display = "";
        } else {
            preview.innerHTML = '<span style="font-size:1.5rem">' + currentEmoji + '</span>';
            clearBtn.style.display = "";
        }
    }
)HTML";
    html += R"HTML(
    // ---- Dual Preview (Full + Bar, matching tablet) ----

    function updatePreview() {
        var rawHtml = wysiwygEl.innerHTML || "";
        // Substitute variables in the formatted HTML (tokens are in text, not in tags)
        var formattedPreview = substitutePreview(rawHtml);
        var hasAction = currentAction || currentLongPressAction || currentDoubleclickAction;
        var hasEmoji = currentEmoji !== "";
        var showBg = !currentHideBackground;
        var bgColor = showBg ? (currentBgColor || ((hasAction || hasEmoji) ? "#555555" : "")) : "";
        var defaultColor = (showBg && (hasAction || hasEmoji || currentBgColor)) ? "white" : "var(--text)";

        // Full preview (center zones: vertical emoji + text)
        var fullEl = document.getElementById("previewFull");
        var fullHtml = "";
        if (hasEmoji) {
            if (currentEmoji.indexOf("qrc:") === 0) {
                fullHtml += '<img src="' + currentEmoji.replace("qrc:","") + '">';
            } else {
                fullHtml += '<span class="pv-emoji">' + currentEmoji + '</span>';
            }
        }
        fullHtml += '<span class="pv-text" style="color:' + defaultColor + '">' + formattedPreview + '</span>';
        fullEl.innerHTML = fullHtml;
        fullEl.style.background = bgColor || "var(--bg)";
        fullEl.style.textAlign = currentAlign;
        fullEl.className = "preview-full" + (hasAction ? " has-action" : "");

        // Bar preview (bar zones: horizontal emoji + text)
        var barEl = document.getElementById("previewBar");
        var barHtml = "";
        if (hasEmoji) {
            if (currentEmoji.indexOf("qrc:") === 0) {
                barHtml += '<img src="' + currentEmoji.replace("qrc:","") + '">';
            } else {
                barHtml += '<span class="pv-emoji">' + currentEmoji + '</span>';
            }
        }
        barHtml += '<span class="pv-text" style="color:' + defaultColor + '">' + formattedPreview + '</span>';
        barEl.innerHTML = barHtml;
        barEl.style.background = bgColor || "var(--bg)";
        barEl.className = "preview-bar" + (hasAction ? " has-action" : "");
    }

    function substitutePreview(t) {
        var now = new Date();
        var hh = String(now.getHours()).padStart(2,"0");
        var mm = String(now.getMinutes()).padStart(2,"0");
        return t
            .replace(/%TEMP%/g,"92.3").replace(/%STEAM_TEMP%/g,"155\u00B0")
            .replace(/%PRESSURE%/g,"9.0").replace(/%FLOW%/g,"2.1")
            .replace(/%WATER%/g,"78").replace(/%WATER_ML%/g,"850")
            .replace(/%STATE%/g,"Idle").replace(/%WEIGHT%/g,"36.2")
            .replace(/%SHOT_TIME%/g,"28.5").replace(/%VOLUME%/g,"42")
            .replace(/%TARGET_WEIGHT%/g,"36.0").replace(/%PROFILE%/g,"Adaptive v2")
            .replace(/%TARGET_TEMP%/g,"93.0").replace(/%RATIO%/g,"2.0")
            .replace(/%DOSE%/g,"18.0").replace(/%SCALE%/g,"Lunar")
            .replace(/%MACHINE_READY%/g,"Ready").replace(/%MACHINE_READY_COLOR%/g,"#18c37e")
            .replace(/%CONNECTED%/g,"Online").replace(/%CONNECTED_COLOR%/g,"#18c37e")
            .replace(/%DEVICES%/g,"Machine + Scale")
            .replace(/%MACHINE_CONNECTED%/g,"✅").replace(/%SCALE_CONNECTED%/g,"✅")
            .replace(/%TIME%/g,hh+":"+mm)
            .replace(/%DATE%/g,now.toISOString().split("T")[0]);
    }

    // Live preview updates on WYSIWYG input
    wysiwygEl.addEventListener("input", function() { updatePreview(); autoSave(); });

    function apiPost(url, data, cb) {
        var ctrl = new AbortController();
        var tid = setTimeout(function() { ctrl.abort(); }, 45000);
        fetch(url, {
            method: "POST",
            headers: {"Content-Type": "application/json"},
            body: JSON.stringify(data),
            signal: ctrl.signal
        }).then(function(r) {
            clearTimeout(tid);
            if (!r.ok) throw new Error('Server error (' + r.status + ')');
            return r.json();
        }).then(function(result) {
            if (cb) cb(result);
        }).catch(function(e) {
            clearTimeout(tid);
            hideLibSpinner();
            showLibToast(e.name === 'AbortError' ? 'Request timed out' : (e.message || 'Network error'));
        });
    }
)HTML";

    html += R"HTML(
    function escapeHtml(str) {
        if (!str) return '';
        var div = document.createElement("div");
        div.textContent = str;
        return div.innerHTML;
    }

    // ===== Library panel =====
    var libCurrentTab = 'local';
    var libLocalData = [];
    var libCommunityData = [];
    var libSelectedId = null;
    var libDisplayMode = 0; // 0=full, 1=compact
    var commPage = 1;
    var commTotal = 0;

    function setLibDisplayMode(mode) {
        libDisplayMode = mode;
        document.getElementById('libModeFull').classList.toggle('active', mode === 0);
        document.getElementById('libModeCompact').classList.toggle('active', mode === 1);
        document.getElementById('commModeFull').classList.toggle('active', mode === 0);
        document.getElementById('commModeCompact').classList.toggle('active', mode === 1);
        if (libCurrentTab === 'local') renderLocalEntries();
        else renderCommunityEntries();
    }

    function toggleSaveMenu(e) {
        e.stopPropagation();
        var menu = document.getElementById('libSaveMenu');
        var isOpen = menu.classList.contains('open');
        menu.classList.toggle('open', !isOpen);
        if (!isOpen) {
            // Update disabled state of options
            var hasItem = selectedChip && selectedChip.id;
            var hasZone = selectedChip && selectedChip.zone;
            document.getElementById('saveItemOpt').classList.toggle('disabled', !hasItem);
            document.getElementById('saveZoneOpt').classList.toggle('disabled', !hasZone);
            document.getElementById('saveItemOpt').onclick = hasItem ? function(e){e.stopPropagation();closeSaveMenu();saveToLibrary('item');} : function(e){e.stopPropagation();};
            document.getElementById('saveZoneOpt').onclick = hasZone ? function(e){e.stopPropagation();closeSaveMenu();saveToLibrary('zone');} : function(e){e.stopPropagation();};
        }
    }
    function closeSaveMenu() { document.getElementById('libSaveMenu').classList.remove('open'); }
    document.addEventListener('click', function() { closeSaveMenu(); });

    function switchLibTab(tab) {
        libCurrentTab = tab;
        document.getElementById('libTabLocal').classList.toggle('active', tab === 'local');
        document.getElementById('libTabCommunity').classList.toggle('active', tab === 'community');
        document.getElementById('libLocalContent').style.display = tab === 'local' ? '' : 'none';
        document.getElementById('libCommunityContent').style.display = tab === 'community' ? '' : 'none';
        libSelectedId = null;
        updateLibButtons();
        if (tab === 'local') loadLibrary();
        else if (libCommunityData.length === 0) browseCommunity();
        else renderCommunityEntries();
    }

    function loadLibrary() {
        fetch('/api/library/entries').then(function(r){
            if (!r.ok) throw new Error('Server error (' + r.status + ')');
            return r.json();
        }).then(function(entries) {
            libLocalData = entries;
            renderLocalEntries();
        }).catch(function(e) {
            console.warn('loadLibrary failed:', e);
            showLibToast('Failed to load library');
        });
    }

    var SAMPLE_VARS = {
        "%TEMP%":"93.2","%STEAM_TEMP%":"155\u00B0","%PRESSURE%":"9.0","%FLOW%":"2.1",
        "%WATER%":"78","%WATER_ML%":"850","%WEIGHT%":"36.2","%SHOT_TIME%":"28.5",
        "%TARGET_WEIGHT%":"36.0","%VOLUME%":"42","%PROFILE%":"Profile","%STATE%":"Idle",
        "%TARGET_TEMP%":"93.0","%SCALE%":"Scale","%RATIO%":"2.0","%DOSE%":"18.0",
        "%TIME%":"12:30","%DATE%":"2025-01-15",
        "%MACHINE_READY%":"Ready","%MACHINE_READY_COLOR%":"#00cc6d",
        "%CONNECTED%":"Online","%CONNECTED_COLOR%":"#00cc6d","%DEVICES%":"Machine",
        "%MACHINE_CONNECTED%":"✅","%SCALE_CONNECTED%":"❌"
    };

    function resolveVars(text) {
        if (!text) return '';
        var r = text.replace(/<[^>]*>/g, '');
        for (var k in SAMPLE_VARS) r = r.split(k).join(SAMPLE_VARS[k]);
        return r;
    }

    function emojiImgHtml(emoji, cls) {
        if (!emoji) return '';
        if (emoji.indexOf('qrc:') === 0) {
            return '<img class="' + (cls||'') + '" src="' + emoji.replace('qrc:','') + '">';
        }
        // Unicode emoji - just show as text
        return '<span class="' + (cls||'') + '" style="font-size:1.1em">' + emoji + '</span>';
    }

    function renderItemVisual(item) {
        var bg = item.backgroundColor || '';
        var hasAction = (item.action||'') !== '' || (item.longPressAction||'') !== '' || (item.doubleclickAction||'') !== '';
        var hideBg = item.hideBackground || false;
        var bgStyle = !hideBg ? (bg || (hasAction ? '#555555' : 'var(--bg)')) : 'var(--bg)';
        var html = '<div class="lib-entry-visual" style="background:' + bgStyle + '">';
        if (item.emoji) html += '<span class="lib-item-emoji">' + emojiImgHtml(item.emoji) + '</span>';
        var text = resolveVars(item.content || item.type || '');
        html += '<span class="lib-item-text">' + escapeHtml(text) + '</span>';
        html += '</div>';
        return html;
    }

    function renderZoneVisual(data) {
        var items = data.items || [];
        var html = '<div class="lib-entry-visual" style="background:var(--bg)">';
        html += '<div class="lib-zone-chips">';
        for (var i = 0; i < items.length; i++) {
            var it = items[i];
            var bg = it.backgroundColor || '';
            var hasAct = (it.action||'') !== '';
            var hideBg = it.hideBackground || false;
            var chipBg = !hideBg ? (bg || (hasAct ? '#555555' : 'var(--surface)')) : 'var(--surface)';
            html += '<span class="lib-zone-mini-chip" style="background:' + chipBg + '">';
            if (it.emoji) html += emojiImgHtml(it.emoji);
            if (it.type !== 'custom') {
                html += (DISPLAY_NAMES[it.type] || it.type);
            } else {
                var t = resolveVars(it.content || '');
                html += escapeHtml(t.length > 10 ? t.substring(0,8)+'..' : (t||'Custom'));
            }
            html += '</span>';
        }
        html += '</div></div>';
        return html;
    }

    function renderLayoutVisual(data) {
        var layout = data.layout || {};
        var zones = layout.zones || {};
        var count = 0;
        for (var z in zones) count += zones[z].length;
        var html = '<div class="lib-entry-visual" style="background:var(--bg);flex-wrap:wrap">';
        // Show a mini summary of all zones
        for (var zn in zones) {
            var zItems = zones[zn];
            if (!zItems.length) continue;
            for (var j = 0; j < Math.min(zItems.length, 4); j++) {
                var it = zItems[j];
                var bg = it.backgroundColor || '';
                var hasAct = (it.action||'') !== '';
                var hideBg = it.hideBackground || false;
                var chipBg = !hideBg ? (bg || (hasAct ? '#555555' : 'var(--surface)')) : 'var(--surface)';
                html += '<span class="lib-zone-mini-chip" style="background:' + chipBg + '">';
                if (it.type !== 'custom') html += (DISPLAY_NAMES[it.type] || it.type);
                else {
                    var t = resolveVars(it.content || '');
                    html += escapeHtml(t.length > 6 ? t.substring(0,5)+'..' : (t||'C'));
                }
                html += '</span>';
            }
            if (zItems.length > 4) html += '<span class="lib-zone-mini-chip" style="background:var(--surface)">+' + (zItems.length-4) + '</span>';
        }
        html += '</div>';
        return html;
    }

    function renderEntryCard(entry, id, isLocal) {
        var compact = libDisplayMode === 1;
        var sel = id === libSelectedId ? ' selected' : '';
        var compactCls = compact ? ' compact' : '';
        var safeId = escapeHtml(id);
        var onclick = isLocal ? "selectLibEntry('" + safeId + "')" : "selectCommEntry('" + safeId + "')";
        var html = '<div class="lib-entry' + sel + compactCls + '" data-entry-id="' + safeId + '" onclick="' + onclick + '">';

        // Type badge overlay
        var safeType = escapeHtml(entry.type || '');
        html += '<span class="lib-type-overlay ' + safeType + '">' + (safeType || '?') + '</span>';

        // Choose thumbnail URL based on display mode
        var thumbUrl = compact
            ? (entry.thumbnailCompactUrl || entry.thumbnailFullUrl || '')
            : (entry.thumbnailFullUrl || '');

        // thumbMode: 'known' = URL from server (thumbnail exists), 'probe' = try loading (might not exist yet)
        var thumbMode = '';
        if (thumbUrl) {
            thumbMode = 'known';
            html += '<div class="lib-entry-visual" style="background:var(--bg);justify-content:center">';
            html += '<img class="lib-thumb" src="' + escapeHtml(thumbUrl) + '" onerror="this.parentElement.style.display=\'none\';var fb=this.parentElement.nextElementSibling;if(fb)fb.style.display=\'\'">';
            html += '</div>';
        } else if (isLocal) {
            thumbMode = 'probe';
            // Thumbnail might not exist yet (newly saved) — probe with retry
            var thumbSrc = '/api/library/thumbnail?id=' + encodeURIComponent(id) + '&t=' + Date.now();
            html += '<div class="lib-entry-visual" style="background:var(--bg);justify-content:center;display:none">';
            html += '<img class="lib-thumb" src="' + thumbSrc + '" onload="var p=this.parentElement;p.style.display=\'\';if(p.nextElementSibling)p.nextElementSibling.style.display=\'none\'" onerror="if(!this.dataset.retried){this.dataset.retried=\'1\';var img=this;setTimeout(function(){img.src=\'/api/library/thumbnail?id=' + encodeURIComponent(id) + '&t=\'+Date.now()},1000)}">';
            html += '</div>';
        }

        // Visual preview (fallback when thumbnail fails or is not available)
        if (entry.data) {
            var fallbackId = 'lf_' + id.replace(/[^a-zA-Z0-9]/g,'_');
            var wrap = isLocal ? ' id="' + fallbackId + '"' : '';
            // Hide fallback when a known thumbnail URL exists (shown on thumbnail error)
            var fallbackHide = thumbMode === 'known' ? ' style="display:none"' : '';
            if (compact) {
                // Compact mode: show type name and brief summary
                var summary = '';
                if (entry.type === 'item' && entry.data.item) {
                    var it = entry.data.item;
                    summary = resolveVars(it.content || it.type || 'Item');
                } else if (entry.type === 'zone' && entry.data.items) {
                    summary = (entry.data.items.length) + ' items';
                } else if (entry.type === 'layout') {
                    var lz = (entry.data.layout||{}).zones||{};
                    var cnt = 0; for (var zk in lz) cnt += lz[zk].length;
                    summary = cnt + ' widgets';
                }
                var compactStyle = (thumbMode === 'known' ? 'display:none;' : '') + 'flex:1;font-size:0.8rem;color:var(--text);overflow:hidden;text-overflow:ellipsis;white-space:nowrap';
                html += '<div' + wrap + ' style="' + compactStyle + '">' + escapeHtml(summary) + '</div>';
            } else {
                if (entry.type === 'item' && entry.data.item) {
                    html += '<div' + wrap + fallbackHide + '>' + renderItemVisual(entry.data.item) + '</div>';
                } else if (entry.type === 'zone' && entry.data.items) {
                    html += '<div' + wrap + fallbackHide + '>' + renderZoneVisual(entry.data) + '</div>';
                } else if (entry.type === 'layout') {
                    html += '<div' + wrap + fallbackHide + '>' + renderLayoutVisual(entry.data) + '</div>';
                }
            }
        }

        html += '</div>';
        return html;
    }
)HTML";
    html += R"HTML(
    function renderLocalEntries() {
        var el = document.getElementById('libLocalEntries');
        if (!libLocalData.length) {
            el.innerHTML = '<div class="lib-empty">No saved entries yet.<br>Select a widget and click <b>+</b> to save it.</div>';
            return;
        }
        var scrollTop = el.scrollTop;
        var html = '';
        for (var i = 0; i < libLocalData.length; i++) {
            var e = libLocalData[i];
            html += renderEntryCard(e, e.id, true);
        }
        el.innerHTML = html;
        el.scrollTop = scrollTop;
    }

    function selectLibEntry(id) {
        var prev = libSelectedId;
        libSelectedId = libSelectedId === id ? null : id;
        // Toggle selection classes in-place without rebuilding the list
        var container = libCurrentTab === 'local'
            ? document.getElementById('libLocalEntries')
            : document.getElementById('libCommunityEntries');
        if (container) {
            if (prev) {
                var prevEl = container.querySelector('[data-entry-id="' + prev + '"]');
                if (prevEl) prevEl.classList.remove('selected');
            }
            if (libSelectedId) {
                var newEl = container.querySelector('[data-entry-id="' + id + '"]');
                if (newEl) newEl.classList.add('selected');
            }
        }
        updateLibButtons();
    }

    function updateLibButtons() {
        var hasSelection = !!libSelectedId;
        var applyBtn = document.getElementById('libApplyBtn');
        var deleteBtn = document.getElementById('libDeleteBtn');
        var uploadBtn = document.getElementById('libUploadBtn');
        var commApplyBtn = document.getElementById('commApplyBtn');
        var commDownloadBtn = document.getElementById('commDownloadBtn');
        if (applyBtn) applyBtn.disabled = !hasSelection;
        if (deleteBtn) deleteBtn.disabled = !hasSelection;
        if (uploadBtn) uploadBtn.disabled = !hasSelection;
        if (commApplyBtn) commApplyBtn.disabled = !hasSelection;
        if (commDownloadBtn) commDownloadBtn.disabled = !hasSelection;
    }

    function saveToLibrary(type) {
        if (type === 'item') {
            if (!selectedChip || !selectedChip.id) { showLibToast('Select a widget first'); return; }
            apiPost('/api/library/save-item', {itemId: selectedChip.id}, function(r) {
                if (r.success) { showLibToast('Item saved to library'); loadLibrary(); }
                else showLibToast(r.error || 'Failed to save');
            });
        } else if (type === 'zone') {
            if (!selectedChip) { showLibToast('Select a zone first'); return; }
            apiPost('/api/library/save-zone', {zone: selectedChip.zone}, function(r) {
                if (r.success) { showLibToast('Zone saved to library'); loadLibrary(); }
                else showLibToast(r.error || 'Failed to save');
            });
        } else if (type === 'layout') {
            apiPost('/api/library/save-layout', {}, function(r) {
                if (r.success) { showLibToast('Layout saved to library'); loadLibrary(); }
                else showLibToast(r.error || 'Failed to save');
            });
        }
    }

    function applyFromLibrary() {
        if (!libSelectedId) return;
        var entry = null;
        var list = libCurrentTab === 'local' ? libLocalData : libCommunityData;
        for (var i = 0; i < list.length; i++) {
            if (list[i].id === libSelectedId) { entry = list[i]; break; }
        }
        if (!entry) return;

        // For community entries, download first then apply
        if (libCurrentTab === 'community') {
            downloadAndApply(entry);
            return;
        }

        // Layout applies globally, item/zone apply to the selected chip's zone
        if (entry.type === 'layout') {
            apiPost('/api/library/apply', {entryId: libSelectedId}, function(r) {
                if (r.success) { showLibToast('Layout applied'); loadLayout(); }
                else showLibToast(r.error || 'Failed to apply');
            });
        } else {
            if (!selectedChip) { showLibToast('Select a zone to apply to'); return; }
            var zone = selectedChip.zone;
            apiPost('/api/library/apply', {entryId: libSelectedId, zone: zone}, function(r) {
                if (r.success) { showLibToast(entry.type + ' applied to ' + zone); loadLayout(); }
                else showLibToast(r.error || 'Failed to apply');
            });
        }
    }

    function deleteFromLibrary() {
        if (!libSelectedId) return;
        if (!confirm('Delete this library entry?')) return;
        apiPost('/api/library/delete', {entryId: libSelectedId}, function(r) {
            if (r.success) {
                showLibToast('Entry deleted');
                libSelectedId = null;
                loadLibrary();
                updateLibButtons();
            } else showLibToast(r.error || 'Failed to delete');
        });
    }

    // Community
    function buildCommunityUrl(page) {
        var type = document.getElementById('commTypeFilter').value;
        var variable = document.getElementById('commVariableFilter').value;
        var action = document.getElementById('commActionFilter').value;
        var search = document.getElementById('commSearchInput').value;
        var sort = document.getElementById('commSortFilter').value;
        var url = '/api/community/browse?page=' + page + '&sort=' + encodeURIComponent(sort);
        if (type) url += '&type=' + encodeURIComponent(type);
        if (variable) url += '&variable=' + encodeURIComponent(variable);
        if (action) url += '&action=' + encodeURIComponent(action);
        if (search) url += '&search=' + encodeURIComponent(search);
        return url;
    }

    function browseCommunity() {
        commPage = 1;
        var url = buildCommunityUrl(commPage);
        var ctrl = new AbortController();
        var tid = setTimeout(function() { ctrl.abort(); }, 45000);

        showLibSpinner('Browsing community...');

        fetch(url, {signal: ctrl.signal}).then(function(r) {
            clearTimeout(tid);
            if (!r.ok) throw new Error('Server error (' + r.status + ')');
            return r.json();
        }).then(function(data) {
            hideLibSpinner();
            if (data.error) {
                document.getElementById('libCommunityEntries').innerHTML = '<div class="lib-empty">' + escapeHtml(data.error) + '</div>';
                return;
            }
            libCommunityData = data.entries || [];
            commTotal = data.total || 0;
            renderCommunityEntries();
        }).catch(function(e) {
            clearTimeout(tid);
            hideLibSpinner();
            var el = document.getElementById('libCommunityEntries');
            var msgDiv = document.createElement('div');
            msgDiv.className = 'lib-empty';
            msgDiv.textContent = e.name === 'AbortError' ? 'Request timed out.' : (e.message || 'Failed to load community entries.');
            el.innerHTML = '';
            el.appendChild(msgDiv);
        });
    }

    function loadMoreCommunity() {
        var btn = document.getElementById('commLoadMore');
        if (btn) btn.style.pointerEvents = 'none';
        var nextPage = commPage + 1;
        var url = buildCommunityUrl(nextPage);
        var ctrl = new AbortController();
        var tid = setTimeout(function() { ctrl.abort(); }, 45000);

        fetch(url, {signal: ctrl.signal}).then(function(r) {
            clearTimeout(tid);
            if (!r.ok) throw new Error('Server error (' + r.status + ')');
            return r.json();
        }).then(function(data) {
            if (btn) btn.style.pointerEvents = '';
            if (data.error) {
                showLibToast(data.error);
                return;
            }
            if (data.entries) {
                commPage = nextPage;
                libCommunityData = libCommunityData.concat(data.entries);
                commTotal = data.total || commTotal;
                renderCommunityEntries();
            }
        }).catch(function(e) {
            clearTimeout(tid);
            if (btn) btn.style.pointerEvents = '';
            showLibToast(e.name === 'AbortError' ? 'Request timed out' : (e.message || 'Failed to load more entries'));
        });
    }

    function renderCommunityEntries() {
        var el = document.getElementById('libCommunityEntries');
        if (!libCommunityData.length) {
            el.innerHTML = '<div class="lib-empty">No community entries found.</div>';
            document.getElementById('commLoadMore').style.display = 'none';
            return;
        }
        var scrollTop = el.scrollTop;
        var html = '';
        for (var i = 0; i < libCommunityData.length; i++) {
            var e = libCommunityData[i];
            var id = e.serverId || e.id || '';
            html += renderEntryCard(e, id, false);
        }
        el.innerHTML = html;
        el.scrollTop = scrollTop;
        document.getElementById('commLoadMore').style.display = libCommunityData.length < commTotal ? '' : 'none';
    }

    function selectCommEntry(id) {
        var prev = libSelectedId;
        libSelectedId = libSelectedId === id ? null : id;
        // Toggle selection classes in-place without rebuilding the list
        var container = document.getElementById('libCommunityEntries');
        if (container) {
            if (prev) {
                var prevEl = container.querySelector('[data-entry-id="' + prev + '"]');
                if (prevEl) prevEl.classList.remove('selected');
            }
            if (libSelectedId) {
                var newEl = container.querySelector('[data-entry-id="' + id + '"]');
                if (newEl) newEl.classList.add('selected');
            }
        }
        updateLibButtons();
    }

    function downloadOnly() {
        if (!libSelectedId) return;
        var entry = null;
        for (var i = 0; i < libCommunityData.length; i++) {
            var eid = libCommunityData[i].serverId || libCommunityData[i].id;
            if (eid === libSelectedId) { entry = libCommunityData[i]; break; }
        }
        if (!entry) return;
        var serverId = entry.serverId || entry.id;
        showLibSpinner('Downloading...');
        apiPost('/api/community/download', {serverId: serverId}, function(r) {
            if (r.success) {
                showLibToast(r.alreadyExists ? 'Already in My Library' : 'Downloaded to My Library');
                loadLibrary();
            }
            else showLibToast(r.error || 'Download failed');
        });
    }

    function downloadAndApply(entry) {
        var serverId = entry.serverId || entry.id;
        if (entry.type !== 'layout' && !selectedChip) {
            showLibToast('Select a zone to apply to');
            return;
        }
        var zone = selectedChip ? selectedChip.zone : '';
        showLibSpinner('Downloading & applying...');
        apiPost('/api/community/download', {serverId: serverId}, function(r) {
            if (r.success && r.localEntryId) {
                var localId = r.localEntryId;
                apiPost('/api/library/apply', {entryId: localId, zone: zone}, function(r2) {
                    loadLibrary();
                    if (r2.success) { showLibToast('Applied!'); loadLayout(); }
                    else showLibToast(r2.error || 'Failed to apply');
                });
            } else showLibToast(r.error || 'Download failed');
        });
    }

    function uploadToComm() {
        if (!libSelectedId) return;
        if (!confirm('Share this entry to the community?')) return;
        showLibSpinner('Uploading...');
        apiPost('/api/community/upload', {entryId: libSelectedId}, function(r) {
            if (r.success) showLibToast('Shared to community!');
            else showLibToast(r.error || 'Upload failed');
        });
    }

    function showLibSpinner(msg) {
        document.getElementById('libSpinnerText').textContent = msg || 'Loading...';
        document.getElementById('libSpinner').classList.add('active');
    }
    function hideLibSpinner() {
        document.getElementById('libSpinner').classList.remove('active');
    }

    function showLibToast(msg) {
        hideLibSpinner();
        var el = document.getElementById('libToast');
        el.textContent = msg;
        el.classList.add('show');
        clearTimeout(window._toastTimer);
        window._toastTimer = setTimeout(function() { el.classList.remove('show'); }, 3000);
    }

    // Initial load
    loadLayout();
    loadLibrary();

    // Listen for layout changes pushed from the tablet via SSE
    var layoutEvents = new EventSource("/api/layout/events");
    layoutEvents.addEventListener("layout-changed", function() {
        if (editingItem || ssEditingItem) return;
        loadLayout();
    });
    layoutEvents.onerror = function() {
        console.warn('Layout SSE connection lost, will auto-reconnect');
    };

    </script>
</body>
</html>
)HTML";

    return html;
}
