#include "shotserver.h"
#include "webdebuglogger.h"
#include "webtemplates.h"
#include "../history/shothistorystorage.h"
#include "../ble/de1device.h"
#include "../screensaver/screensavervideomanager.h"

#include <QNetworkInterface>
#include <QFile>
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
#include <QProcess>
#include <QCoreApplication>
#include <QRegularExpression>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

ShotServer::ShotServer(ShotHistoryStorage* storage, DE1Device* device, QObject* parent)
    : QObject(parent)
    , m_storage(storage)
    , m_device(device)
{
    // Timer to cleanup stale connections
    m_cleanupTimer = new QTimer(this);
    m_cleanupTimer->setInterval(30000);  // Check every 30 seconds
    connect(m_cleanupTimer, &QTimer::timeout, this, &ShotServer::cleanupStaleConnections);
}

ShotServer::~ShotServer()
{
    stop();
    // Cleanup any pending requests
    for (auto it = m_pendingRequests.begin(); it != m_pendingRequests.end(); ++it) {
        cleanupPendingRequest(it.key());
    }
    m_pendingRequests.clear();
}

QString ShotServer::url() const
{
    if (!isRunning()) return QString();
    return QString("http://%1:%2").arg(getLocalIpAddress()).arg(m_port);
}

void ShotServer::setPort(int port)
{
    if (m_port != port) {
        m_port = port;
        emit portChanged();
    }
}

bool ShotServer::start()
{
    if (m_server) {
        stop();
    }

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &ShotServer::onNewConnection);

    if (!m_server->listen(QHostAddress::Any, m_port)) {
        qWarning() << "ShotServer: Failed to start on port" << m_port << m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return false;
    }

    m_cleanupTimer->start();
    qDebug() << "ShotServer: Started on" << url();
    emit runningChanged();
    emit urlChanged();
    return true;
}

void ShotServer::stop()
{
    if (m_server) {
        m_cleanupTimer->stop();
        m_server->close();
        delete m_server;
        m_server = nullptr;
        emit runningChanged();
        emit urlChanged();
        qDebug() << "ShotServer: Stopped";
    }
}

void ShotServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, &ShotServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &ShotServer::onDisconnected);
        emit clientConnected(socket->peerAddress().toString());
    }
}

void ShotServer::onReadyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    try {
        PendingRequest& pending = m_pendingRequests[socket];
        pending.lastActivity.start();

        // Read available data
        QByteArray chunk = socket->readAll();

        // If we haven't found headers yet, accumulate header data
        if (pending.headerEnd < 0) {
            pending.headerData.append(chunk);

            // Check header size limit
            if (pending.headerData.size() > MAX_HEADER_SIZE) {
                qWarning() << "ShotServer: Headers too large, rejecting";
                sendResponse(socket, 413, "text/plain", "Headers too large");
                cleanupPendingRequest(socket);
                m_pendingRequests.remove(socket);
                socket->close();
                return;
            }

            pending.headerEnd = pending.headerData.indexOf("\r\n\r\n");
            if (pending.headerEnd < 0) {
                // Headers not complete yet
                return;
            }

            // Parse headers
            QString headers = QString::fromUtf8(pending.headerData.left(pending.headerEnd));
            QStringList lines = headers.split("\r\n");
            QString requestLine = lines.isEmpty() ? "" : lines.first();

            // Parse Content-Length
            for (const QString& line : std::as_const(lines)) {
                if (line.startsWith("Content-Length:", Qt::CaseInsensitive)) {
                    pending.contentLength = line.mid(15).trimmed().toLongLong();
                    break;
                }
            }

            if (pending.contentLength < 0) {
                pending.contentLength = 0;
            }

            // Check if this is a media upload (POST to /upload/media)
            pending.isMediaUpload = requestLine.contains("POST") && requestLine.contains("/upload/media");

            // Check upload size limit for media uploads
            if (pending.isMediaUpload && pending.contentLength > MAX_UPLOAD_SIZE) {
                qWarning() << "ShotServer: Upload too large:" << pending.contentLength << "bytes (max:" << MAX_UPLOAD_SIZE << ")";
                sendResponse(socket, 413, "text/plain",
                    QString("File too large. Maximum size is %1 MB").arg(MAX_UPLOAD_SIZE / (1024*1024)).toUtf8());
                cleanupPendingRequest(socket);
                m_pendingRequests.remove(socket);
                socket->close();
                return;
            }

            // Check concurrent upload limit
            if (pending.isMediaUpload && m_activeMediaUploads >= MAX_CONCURRENT_UPLOADS) {
                qWarning() << "ShotServer: Too many concurrent uploads";
                sendResponse(socket, 503, "text/plain", "Server busy. Please wait and try again.");
                cleanupPendingRequest(socket);
                m_pendingRequests.remove(socket);
                socket->close();
                return;
            }

            // For large uploads (> 1MB), stream to temp file instead of memory
            if (pending.contentLength > MAX_SMALL_BODY_SIZE) {
                QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
                pending.tempFilePath = tempDir + "/upload_stream_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".tmp";
                pending.tempFile = new QFile(pending.tempFilePath);
                if (!pending.tempFile->open(QIODevice::WriteOnly)) {
                    qWarning() << "ShotServer: Failed to create temp file for streaming";
                    sendResponse(socket, 500, "text/plain", "Server error: cannot create temp file");
                    cleanupPendingRequest(socket);
                    m_pendingRequests.remove(socket);
                    socket->close();
                    return;
                }
                if (pending.isMediaUpload) {
                    m_activeMediaUploads++;
                }
                qDebug() << "ShotServer: Streaming large upload to" << pending.tempFilePath;
            }

            // Handle any body data that came with headers
            int bodyStart = pending.headerEnd + 4;
            if (bodyStart < pending.headerData.size()) {
                QByteArray bodyPart = pending.headerData.mid(bodyStart);
                if (pending.tempFile) {
                    // Write body to temp file and truncate headerData to just headers
                    pending.tempFile->write(bodyPart);
                    pending.headerData.truncate(pending.headerEnd);
                }
                // For small requests without temp file, keep everything in headerData
                pending.bodyReceived = bodyPart.size();
            }

            chunk.clear();  // Already processed
        } else {
            // Headers already received, this is body data
            if (pending.tempFile) {
                // Stream to temp file
                pending.tempFile->write(chunk);
            } else {
                // Append to header buffer (for small requests)
                pending.headerData.append(chunk);
            }
            pending.bodyReceived += chunk.size();
        }

        // Log progress for large uploads
        if (pending.contentLength > 5 * 1024 * 1024) {
            static QHash<QTcpSocket*, qint64> lastLog;
            qint64& last = lastLog[socket];
            if (pending.bodyReceived - last > 5 * 1024 * 1024) {
                qDebug() << "Upload progress:" << pending.bodyReceived / (1024*1024) << "MB /" << pending.contentLength / (1024*1024) << "MB";
                last = pending.bodyReceived;
            }
        }

        // Check if we have all the body data
        if (pending.bodyReceived < pending.contentLength) {
            return;  // Still waiting for more data
        }

        // Request complete
        if (pending.tempFile) {
            pending.tempFile->close();
            qDebug() << "ShotServer: Upload complete, temp file:" << pending.tempFilePath
                     << "size:" << QFileInfo(pending.tempFilePath).size() << "bytes";
        }

        // Handle the request
        if (pending.isMediaUpload && pending.tempFile) {
            // Media upload with streamed body - pass temp file path
            QString headers = QString::fromUtf8(pending.headerData);
            QString tempPath = pending.tempFilePath;
            pending.tempFile = nullptr;  // Transfer ownership
            pending.tempFilePath.clear();
            if (pending.isMediaUpload) {
                m_activeMediaUploads--;
            }
            m_pendingRequests.remove(socket);
            handleMediaUpload(socket, tempPath, headers);
        } else {
            // Small request or non-media - headerData contains full request (headers + \r\n\r\n + body)
            QByteArray request = pending.headerData;
            if (!pending.tempFilePath.isEmpty() && QFile::exists(pending.tempFilePath)) {
                // Large non-media request with temp file - reconstruct
                request = pending.headerData + "\r\n\r\n";
                QFile f(pending.tempFilePath);
                if (f.open(QIODevice::ReadOnly)) {
                    request.append(f.readAll());
                }
            }
            cleanupPendingRequest(socket);
            m_pendingRequests.remove(socket);
            handleRequest(socket, request);
        }

    } catch (const std::exception& e) {
        qWarning() << "ShotServer: Exception in onReadyRead:" << e.what();
        cleanupPendingRequest(socket);
        m_pendingRequests.remove(socket);
        socket->close();
    } catch (...) {
        qWarning() << "ShotServer: Unknown exception in onReadyRead";
        cleanupPendingRequest(socket);
        m_pendingRequests.remove(socket);
        socket->close();
    }
}

void ShotServer::onDisconnected()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket) {
        cleanupPendingRequest(socket);
        m_pendingRequests.remove(socket);
        socket->deleteLater();
    }
}

void ShotServer::cleanupPendingRequest(QTcpSocket* socket)
{
    if (!m_pendingRequests.contains(socket)) return;

    PendingRequest& pending = m_pendingRequests[socket];
    if (pending.tempFile) {
        pending.tempFile->close();
        delete pending.tempFile;
        pending.tempFile = nullptr;
    }
    if (!pending.tempFilePath.isEmpty() && QFile::exists(pending.tempFilePath)) {
        QFile::remove(pending.tempFilePath);
        qDebug() << "ShotServer: Cleaned up temp file:" << pending.tempFilePath;
    }
    if (pending.isMediaUpload && m_activeMediaUploads > 0) {
        m_activeMediaUploads--;
    }
}

void ShotServer::cleanupStaleConnections()
{
    QList<QTcpSocket*> staleConnections;
    for (auto it = m_pendingRequests.begin(); it != m_pendingRequests.end(); ++it) {
        if (it.value().lastActivity.isValid() &&
            it.value().lastActivity.elapsed() > CONNECTION_TIMEOUT_MS) {
            staleConnections.append(it.key());
        }
    }

    for (QTcpSocket* socket : staleConnections) {
        qWarning() << "ShotServer: Cleaning up stale connection from" << socket->peerAddress().toString();
        cleanupPendingRequest(socket);
        m_pendingRequests.remove(socket);
        socket->close();
        socket->deleteLater();
    }
}

void ShotServer::handleRequest(QTcpSocket* socket, const QByteArray& request)
{
    QString requestStr = QString::fromUtf8(request);
    QStringList lines = requestStr.split("\r\n");
    if (lines.isEmpty()) {
        socket->close();
        return;
    }

    QStringList requestLine = lines.first().split(" ");
    if (requestLine.size() < 2) {
        socket->close();
        return;
    }

    QString method = requestLine[0];
    QString path = requestLine[1];

    // Don't log debug polling requests (too noisy)
    if (!path.startsWith("/api/debug")) {
        qDebug() << "ShotServer:" << method << path;
    }

    // Route requests
    if (path == "/" || path == "/index.html") {
        sendHtml(socket, generateShotListPage());
    }
    else if (path == "/shots" || path == "/shots/") {
        sendHtml(socket, generateShotListPage());
    }
    else if (path.startsWith("/compare/")) {
        // /compare/1,2,3 - compare shots with IDs 1, 2, 3
        QString idsStr = path.mid(9);
        QStringList idParts = idsStr.split(",");
        QList<qint64> ids;
        for (const QString& p : std::as_const(idParts)) {
            bool ok;
            qint64 id = p.toLongLong(&ok);
            if (ok) ids << id;
        }
        if (ids.size() >= 2) {
            sendHtml(socket, generateComparisonPage(ids));
        } else {
            sendResponse(socket, 400, "text/plain", "Need at least 2 shot IDs to compare");
        }
    }
    else if (path.startsWith("/shot/") && path.endsWith("/profile.json")) {
        // /shot/123/profile.json - download profile JSON for a shot
        QString idPart = path.mid(6);  // Remove "/shot/"
        idPart = idPart.left(idPart.indexOf("/profile.json"));
        bool ok;
        qint64 shotId = idPart.toLongLong(&ok);
        if (ok) {
            QVariantMap shot = m_storage->getShot(shotId);
            QString profileJson = shot["profileJson"].toString();
            QString profileName = shot["profileName"].toString();
            if (!profileJson.isEmpty()) {
                // Pretty-print the JSON for readability
                QJsonDocument doc = QJsonDocument::fromJson(profileJson.toUtf8());
                QByteArray prettyJson = doc.toJson(QJsonDocument::Indented);
                // Set Content-Disposition to suggest filename
                QString filename = profileName.isEmpty() ? "profile" : profileName;
                filename = filename.replace(QRegularExpression("[^a-zA-Z0-9_-]"), "_");
                QByteArray headers = QString("Content-Disposition: attachment; filename=\"%1.json\"\r\n").arg(filename).toUtf8();
                sendResponse(socket, 200, "application/json", prettyJson, headers);
            } else {
                sendResponse(socket, 404, "application/json", R"({"error":"No profile data for this shot"})");
            }
        } else {
            sendResponse(socket, 400, "application/json", R"({"error":"Invalid shot ID"})");
        }
    }
    else if (path.startsWith("/shot/")) {
        bool ok;
        qint64 shotId = path.mid(6).split("?").first().toLongLong(&ok);
        if (ok) {
            sendHtml(socket, generateShotDetailPage(shotId));
        } else {
            sendResponse(socket, 400, "text/plain", "Invalid shot ID");
        }
    }
    else if (path == "/api/shots") {
        QVariantList shots = m_storage->getShots(0, 1000);
        QJsonArray arr;
        for (const QVariant& v : std::as_const(shots)) {
            arr.append(QJsonObject::fromVariantMap(v.toMap()));
        }
        sendJson(socket, QJsonDocument(arr).toJson(QJsonDocument::Compact));
    }
    else if (path.startsWith("/api/shot/")) {
        bool ok;
        qint64 shotId = path.mid(10).toLongLong(&ok);
        if (ok) {
            QVariantMap shot = m_storage->getShot(shotId);
            sendJson(socket, QJsonDocument(QJsonObject::fromVariantMap(shot)).toJson());
        } else {
            sendResponse(socket, 400, "application/json", R"({"error":"Invalid shot ID"})");
        }
    }
    else if (path == "/api/database" || path == "/database.db") {
        // Checkpoint WAL to ensure all data is in main .db file before download
        m_storage->checkpoint();
        QString dbPath = m_storage->databasePath();
        sendFile(socket, dbPath, "application/x-sqlite3");
    }
    else if (path == "/debug") {
        sendHtml(socket, generateDebugPage());
    }
    else if (path == "/remote") {
        sendHtml(socket, QString(WEB_REMOTE_PAGE));
    }
    else if (path == "/api/debug" || path.startsWith("/api/debug?")) {
        // Get afterIndex from query string
        int afterIndex = 0;
        if (path.contains("?")) {
            QUrlQuery query(path.mid(path.indexOf("?") + 1));
            afterIndex = query.queryItemValue("after").toInt();
        }

        int lastIndex = 0;
        QStringList lines;
        if (WebDebugLogger::instance()) {
            lines = WebDebugLogger::instance()->getLines(afterIndex, &lastIndex);
        }

        QJsonObject result;
        result["lastIndex"] = lastIndex;
        QJsonArray linesArray;
        for (const QString& line : std::as_const(lines)) {
            linesArray.append(line);
        }
        result["lines"] = linesArray;
        sendJson(socket, QJsonDocument(result).toJson(QJsonDocument::Compact));
    }
    else if (path == "/api/debug/clear") {
        if (WebDebugLogger::instance()) {
            WebDebugLogger::instance()->clear();
        }
        QJsonObject result;
        result["success"] = true;
        sendJson(socket, QJsonDocument(result).toJson(QJsonDocument::Compact));
    }
    else if (path == "/api/power" || path == "/api/power/status") {
        // Return current power state
        QJsonObject result;
        if (m_device) {
            bool isAwake = m_device->isConnected() &&
                          (m_device->state() != DE1::State::Sleep &&
                           m_device->state() != DE1::State::GoingToSleep);
            result["connected"] = m_device->isConnected();
            result["state"] = m_device->stateString();
            result["substate"] = m_device->subStateString();
            result["awake"] = isAwake;
        } else {
            result["connected"] = false;
            result["state"] = "Unknown";
            result["awake"] = false;
        }
        sendJson(socket, QJsonDocument(result).toJson(QJsonDocument::Compact));
    }
    else if (path == "/api/power/wake") {
        if (m_device) {
            m_device->wakeUp();
            qDebug() << "ShotServer: Wake command sent via web";
        }
        sendJson(socket, R"({"success":true,"action":"wake"})");
    }
    else if (path == "/api/power/sleep") {
        if (m_device) {
            m_device->goToSleep();
            qDebug() << "ShotServer: Sleep command sent via web";
        }
        sendJson(socket, R"({"success":true,"action":"sleep"})");
    }
    else if (path == "/upload") {
        if (method == "GET") {
            sendHtml(socket, generateUploadPage());
        } else if (method == "POST") {
            handleUpload(socket, request);
        }
    }
    else if (path == "/upload/media") {
        if (method == "GET") {
            sendHtml(socket, generateMediaUploadPage());
        } else if (method == "POST") {
            // For small uploads that weren't streamed, save body to temp file
            int headerEnd = request.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                sendResponse(socket, 400, "text/plain", "Invalid request");
                return;
            }
            QString headers = QString::fromUtf8(request.left(headerEnd));
            QByteArray body = request.mid(headerEnd + 4);

            qDebug() << "ShotServer: Small media upload - request size:" << request.size()
                     << "headerEnd:" << headerEnd << "body size:" << body.size();

            // Save to temp file
            QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
            QString tempPath = tempDir + "/upload_small_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".tmp";
            QFile tempFile(tempPath);
            if (!tempFile.open(QIODevice::WriteOnly)) {
                sendResponse(socket, 500, "text/plain", "Failed to create temp file");
                return;
            }
            tempFile.write(body);
            tempFile.close();

            handleMediaUpload(socket, tempPath, headers);
        }
    }
    else if (path == "/api/media/personal") {
        if (!m_screensaverManager) {
            sendJson(socket, R"({"error":"Screensaver manager not available"})");
            return;
        }
        QVariantList media = m_screensaverManager->getPersonalMediaList();
        QJsonArray arr;
        for (const QVariant& v : std::as_const(media)) {
            arr.append(QJsonObject::fromVariantMap(v.toMap()));
        }
        sendJson(socket, QJsonDocument(arr).toJson(QJsonDocument::Compact));
    }
    else if (path == "/api/media/personal" && method == "DELETE") {
        // Delete ALL personal media
        if (!m_screensaverManager) {
            sendJson(socket, R"({"error":"Screensaver manager not available"})");
            return;
        }
        m_screensaverManager->clearPersonalMedia();
        sendJson(socket, R"({"success":true})");
    }
    else if (path.startsWith("/api/media/personal/") && method == "DELETE") {
        // Delete single personal media by ID
        if (!m_screensaverManager) {
            sendJson(socket, R"({"error":"Screensaver manager not available"})");
            return;
        }
        bool ok;
        int mediaId = path.mid(20).toInt(&ok);
        if (ok && m_screensaverManager->deletePersonalMedia(mediaId)) {
            sendJson(socket, R"({"success":true})");
        } else {
            sendResponse(socket, 404, "application/json", R"({"error":"Media not found"})");
        }
    }
    else {
        sendResponse(socket, 404, "text/plain", "Not Found");
    }
}

void ShotServer::sendResponse(QTcpSocket* socket, int statusCode, const QString& contentType,
                               const QByteArray& body, const QByteArray& extraHeaders)
{
    QString statusText;
    switch (statusCode) {
        case 200: statusText = "OK"; break;
        case 400: statusText = "Bad Request"; break;
        case 404: statusText = "Not Found"; break;
        default: statusText = "Unknown"; break;
    }

    QByteArray response;
    response.append(QString("HTTP/1.1 %1 %2\r\n").arg(statusCode).arg(statusText).toUtf8());
    response.append(QString("Content-Type: %1\r\n").arg(contentType).toUtf8());
    response.append(QString("Content-Length: %1\r\n").arg(body.size()).toUtf8());
    response.append("Access-Control-Allow-Origin: *\r\n");
    response.append("Connection: close\r\n");
    if (!extraHeaders.isEmpty()) {
        response.append(extraHeaders);
    }
    response.append("\r\n");
    response.append(body);

    socket->write(response);
    socket->flush();
    socket->close();
}

void ShotServer::sendJson(QTcpSocket* socket, const QByteArray& json)
{
    sendResponse(socket, 200, "application/json", json);
}

void ShotServer::sendHtml(QTcpSocket* socket, const QString& html)
{
    sendResponse(socket, 200, "text/html; charset=utf-8", html.toUtf8());
}

void ShotServer::sendFile(QTcpSocket* socket, const QString& path, const QString& contentType)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        sendResponse(socket, 404, "text/plain", "File not found");
        return;
    }

    QByteArray data = file.readAll();
    QByteArray extraHeaders = QString("Content-Disposition: attachment; filename=\"shots.db\"\r\n").toUtf8();
    sendResponse(socket, 200, contentType, data, extraHeaders);
}

QString ShotServer::getLocalIpAddress() const
{
    QString fallbackAddress;

    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        // Skip down, loopback, or virtual interfaces
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            !(iface.flags() & QNetworkInterface::IsRunning) ||
            (iface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }

        // Skip known virtual interface names (WSL, Docker, Hyper-V, VirtualBox, VMware)
        QString name = iface.name().toLower() + iface.humanReadableName().toLower();
        if (name.contains("wsl") || name.contains("docker") || name.contains("vethernet") ||
            name.contains("virtualbox") || name.contains("vmware") || name.contains("vmnet") ||
            name.contains("hyper-v") || name.contains("vbox")) {
            continue;
        }

        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            QHostAddress addr = entry.ip();
            if (addr.protocol() != QAbstractSocket::IPv4Protocol || addr.isLoopback()) {
                continue;
            }

            QString ip = addr.toString();

            // Prefer 192.168.x.x and 10.x.x.x (typical home/office LANs)
            if (ip.startsWith("192.168.") || ip.startsWith("10.")) {
                return ip;
            }

            // Keep 172.x.x.x as fallback (could be legitimate but often virtual)
            if (fallbackAddress.isEmpty()) {
                fallbackAddress = ip;
            }
        }
    }

    return fallbackAddress.isEmpty() ? "127.0.0.1" : fallbackAddress;
}

QString ShotServer::generateIndexPage() const
{
    return generateShotListPage();
}

QString ShotServer::generateShotListPage() const
{
    QVariantList shots = m_storage->getShots(0, 1000);  // Get more shots for filtering

    // Collect unique values for filter dropdowns
    QSet<QString> profilesSet, brandsSet, coffeesSet;
    for (const QVariant& v : std::as_const(shots)) {
        QVariantMap shot = v.toMap();
        QString profile = shot["profileName"].toString().trimmed();
        QString brand = shot["beanBrand"].toString().trimmed();
        QString coffee = shot["beanType"].toString().trimmed();
        if (!profile.isEmpty()) profilesSet.insert(profile);
        if (!brand.isEmpty()) brandsSet.insert(brand);
        if (!coffee.isEmpty()) coffeesSet.insert(coffee);
    }

    // Convert to sorted lists for dropdowns
    QStringList profiles = profilesSet.values();
    QStringList brands = brandsSet.values();
    QStringList coffees = coffeesSet.values();
    std::sort(profiles.begin(), profiles.end(), [](const QString& a, const QString& b) { return a.toLower() < b.toLower(); });
    std::sort(brands.begin(), brands.end(), [](const QString& a, const QString& b) { return a.toLower() < b.toLower(); });
    std::sort(coffees.begin(), coffees.end(), [](const QString& a, const QString& b) { return a.toLower() < b.toLower(); });

    // Generate filter options HTML
    auto generateOptions = [](const QStringList& items) -> QString {
        QString html;
        for (const QString& item : items) {
            html += QString("<option value=\"%1\">%1</option>").arg(item.toHtmlEscaped());
        }
        return html;
    };

    QString profileOptions = generateOptions(profiles);
    QString brandOptions = generateOptions(brands);
    QString coffeeOptions = generateOptions(coffees);

    QString rows;
    for (const QVariant& v : std::as_const(shots)) {
        QVariantMap shot = v.toMap();

        int rating = qRound(shot["enjoyment"].toDouble());  // 0-100
        QString ratingStr = QString::number(rating);

        double ratio = 0;
        if (shot["doseWeight"].toDouble() > 0) {
            ratio = shot["finalWeight"].toDouble() / shot["doseWeight"].toDouble();
        }

        QString profileName = shot["profileName"].toString();
        QString beanBrand = shot["beanBrand"].toString();
        QString beanType = shot["beanType"].toString();
        QString dateTime = shot["dateTime"].toString();
        double doseWeight = shot["doseWeight"].toDouble();
        double finalWeight = shot["finalWeight"].toDouble();
        double duration = shot["duration"].toDouble();

        // Escape for JavaScript string (single quotes) and HTML attribute
        auto escapeForJs = [](const QString& s) -> QString {
            QString escaped = s;
            escaped.replace("\\", "\\\\");
            escaped.replace("'", "\\'");
            escaped.replace("\"", "&quot;");
            escaped.replace("<", "&lt;");
            escaped.replace(">", "&gt;");
            return escaped;
        };

        QString profileJs = escapeForJs(profileName);
        QString brandJs = escapeForJs(beanBrand);
        QString coffeeJs = escapeForJs(beanType);
        QString profileHtml = profileName.toHtmlEscaped();
        QString brandHtml = beanBrand.toHtmlEscaped();
        QString coffeeHtml = beanType.toHtmlEscaped();

        rows += QString(R"HTML(
            <div class="shot-card" onclick="toggleSelect(%1, this)" data-id="%1"
                 data-profile="%2" data-brand="%3" data-coffee="%4" data-rating="%5"
                 data-ratio="%6" data-duration="%7" data-date="%8" data-dose="%9" data-yield="%10">
                <a href="/shot/%1" onclick="event.stopPropagation()" style="text-decoration:none;color:inherit;display:block;">
                    <div class="shot-header">
                        <span class="shot-profile clickable" onclick="event.preventDefault(); event.stopPropagation(); addFilter('profile', '%11')">%2</span>
                        <div class="shot-header-right">
                            <span class="shot-date">%8</span>
                            <input type="checkbox" class="shot-checkbox" data-id="%1" onclick="event.stopPropagation(); toggleSelect(%1, this.closest('.shot-card'))">
                        </div>
                    </div>
                    <div class="shot-metrics">
                        <div class="dose-group">
                            <div class="shot-metric">
                                <span class="metric-value">%9g</span>
                                <span class="metric-label">in</span>
                            </div>
                            <div class="shot-arrow">&#8594;</div>
                            <div class="shot-metric">
                                <span class="metric-value">%10g</span>
                                <span class="metric-label">out</span>
                            </div>
                        </div>
                        <div class="shot-metric">
                            <span class="metric-value">1:%6</span>
                            <span class="metric-label">ratio</span>
                        </div>
                        <div class="shot-metric">
                            <span class="metric-value">%7s</span>
                            <span class="metric-label">time</span>
                        </div>
                    </div>
                    <div class="shot-footer">
                        <span class="shot-beans">
                            <span class="clickable" onclick="event.preventDefault(); event.stopPropagation(); addFilter('brand', '%12')">%3</span>
                            <span class="clickable" onclick="event.preventDefault(); event.stopPropagation(); addFilter('coffee', '%13')">%4</span>
                        </span>
                        <span class="shot-rating clickable" onclick="event.preventDefault(); event.stopPropagation(); addFilter('rating', '%5')">rating: %5</span>
                    </div>
                </a>
            </div>
        )HTML")
        .arg(shot["id"].toLongLong())       // %1
        .arg(profileHtml)                   // %2
        .arg(brandHtml)                     // %3
        .arg(coffeeHtml)                    // %4
        .arg(rating)                        // %5
        .arg(ratio, 0, 'f', 1)              // %6
        .arg(duration, 0, 'f', 1)           // %7
        .arg(dateTime)                      // %8
        .arg(doseWeight, 0, 'f', 1)         // %9
        .arg(finalWeight, 0, 'f', 1)        // %10
        .arg(profileJs)                     // %11
        .arg(brandJs)                       // %12
        .arg(coffeeJs);                     // %13
    }

    // Build HTML in chunks to avoid MSVC string literal size limit
    QString html;

    // Part 1: DOCTYPE and head start
    html += R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Shot History - Decenza DE1</title>
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --surface-hover: #1f2937;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
            --accent-dim: #a68a1f;
            --pressure: #18c37e;
            --flow: #4e85f4;
            --temp: #e73249;
            --weight: #a2693d;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.5;
            min-height: 100vh;
        }
)HTML";

    // Part 2: Header and layout CSS
    html += R"HTML(
        .header {
            background: var(--surface);
            border-bottom: 1px solid var(--border);
            padding: 1rem 1.5rem;
            position: sticky;
            top: 0;
            z-index: 100;
        }
        .header-content {
            max-width: 1200px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            justify-content: space-between;
        }
        .logo {
            font-size: 1.25rem;
            font-weight: 600;
            color: var(--accent);
            text-decoration: none;
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }
        .shot-count { color: var(--text-secondary); font-size: 0.875rem; }
        .container { max-width: 1200px; margin: 0 auto; padding: 1.5rem; }
        .shot-grid {
            display: grid;
            gap: 1rem;
            grid-template-columns: repeat(auto-fill, minmax(340px, 1fr));
        }
)HTML";

    // Part 3: Shot card CSS
    html += R"HTML(
        .shot-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 0.5rem 0.75rem;
            text-decoration: none;
            color: inherit;
            transition: all 0.2s ease;
            display: block;
            position: relative;
        }
        .shot-card:hover { background: var(--surface-hover); border-color: var(--accent); }
        .shot-card.selected { border-color: var(--accent); }
        .shot-header { display: flex; justify-content: space-between; align-items: center; }
        .shot-header-right { display: flex; align-items: center; gap: 0.5rem; }
        .shot-profile { font-weight: 600; font-size: 1rem; color: var(--text); }
        .shot-date { font-size: 0.75rem; color: var(--text-secondary); white-space: nowrap; }
        .shot-metrics { display: flex; align-items: center; justify-content: space-between; }
        .dose-group {
            display: flex;
            align-items: center;
            gap: 0.3rem;
            padding: 0 0.3rem;
            border: 1px solid var(--border);
            border-radius: 4px;
        }
        .shot-metric { display: flex; flex-direction: column; align-items: center; }
        .shot-metric .metric-value { font-size: 1.125rem; font-weight: 600; color: var(--accent); }
        .shot-metric .metric-label { font-size: 0.625rem; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 0.05em; }
        .shot-arrow { color: var(--text-secondary); font-size: 1rem; }
        .shot-footer { display: flex; justify-content: space-between; align-items: center; }
        .shot-beans { font-size: 0.8125rem; color: var(--text-secondary); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 60%%; }
        .shot-rating { color: var(--accent); font-size: 0.875rem; }
        .empty-state { text-align: center; padding: 4rem 2rem; color: var(--text-secondary); }
        .empty-state h2 { margin-bottom: 0.5rem; color: var(--text); }
)HTML";

    // Part 4: Search and compare bar CSS
    html += R"HTML(
        .search-bar { display: flex; gap: 1rem; margin-bottom: 1.5rem; flex-wrap: wrap; align-items: center; }
        .search-help { font-size: 0.8rem; color: var(--text-secondary); margin-bottom: 0.5rem; }
        .search-input {
            flex: 1;
            min-width: 200px;
            padding: 0.75rem 1rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            color: var(--text);
            font-size: 1rem;
        }
        .search-input:focus { outline: none; border-color: var(--accent); }
        .search-input::placeholder { color: var(--text-secondary); }
        .compare-bar {
            position: fixed;
            bottom: 0;
            left: 0;
            right: 0;
            background: var(--surface);
            border-top: 1px solid var(--border);
            padding: 1rem 1.5rem;
            display: none;
            justify-content: center;
            align-items: center;
            gap: 1rem;
            z-index: 100;
        }
        .compare-bar.visible { display: flex; }
        .compare-btn {
            padding: 0.75rem 2rem;
            background: var(--accent);
            color: var(--bg);
            border: none;
            border-radius: 8px;
            font-size: 1rem;
            font-weight: 600;
            cursor: pointer;
        }
        .compare-btn:hover { opacity: 0.9; }
        .clear-btn {
            padding: 0.75rem 1.5rem;
            background: transparent;
            color: var(--text-secondary);
            border: 1px solid var(--border);
            border-radius: 8px;
            cursor: pointer;
        }
)HTML";

    // Part 5: Checkbox and menu CSS
    html += R"HTML(
        .shot-checkbox {
            width: 24px;
            height: 24px;
            min-width: 24px;
            appearance: none;
            -webkit-appearance: none;
            background: var(--bg);
            border: 2px solid var(--border);
            border-radius: 4px;
            cursor: pointer;
            display: flex;
            justify-content: center;
            align-items: center;
        }
        .shot-checkbox:checked { background: var(--accent); border-color: var(--accent); }
        .shot-checkbox:checked::after { content: "✓"; color: var(--bg); font-size: 18px; font-weight: bold; line-height: 1; }
        .header-right { display: flex; align-items: center; gap: 1rem; }
        .menu-wrapper { position: relative; }
        .menu-btn {
            background: none;
            border: none;
            color: var(--text);
            font-size: 1.5rem;
            cursor: pointer;
            padding: 0.25rem 0.5rem;
            line-height: 1;
        }
        .menu-btn:hover { color: var(--accent); }
        .menu-dropdown {
            position: absolute;
            top: 100%%;
            right: 0;
            margin-top: 0.5rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            min-width: max-content;
            display: none;
            box-shadow: 0 4px 12px rgba(0,0,0,0.3);
            z-index: 200;
        }
        .menu-dropdown.open { display: block; }
        .menu-item {
            display: block;
            padding: 0.75rem 1rem;
            color: var(--text);
            text-decoration: none;
            border-bottom: 1px solid var(--border);
            white-space: nowrap;
        }
        .menu-item:last-child { border-bottom: none; }
        .menu-item:hover { background: var(--surface-hover); }
        .menu-item:first-child { border-radius: 7px 7px 0 0; }
        .menu-item:last-child { border-radius: 0 0 7px 7px; }
        .menu-item:only-child { border-radius: 7px; }
        .clickable { cursor: pointer; transition: color 0.2s; }
        .clickable:hover { color: var(--accent) !important; text-decoration: underline; }
)HTML";

    // Part 6: Collapsible and filter CSS
    html += R"HTML(
        .collapsible-section {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            margin-bottom: 1rem;
        }
        .collapsible-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 0.75rem 1rem;
            cursor: pointer;
            user-select: none;
        }
        .collapsible-header:hover { background: var(--surface-hover); border-radius: 8px; }
        .collapsible-header h3 { font-size: 0.9rem; font-weight: 600; color: var(--text); margin: 0; }
        .collapsible-arrow { color: var(--text-secondary); transition: transform 0.2s; }
        .collapsible-section.open .collapsible-arrow { transform: rotate(180deg); }
        .collapsible-content { display: none; padding: 0 1rem 1rem; border-top: 1px solid var(--border); }
        .collapsible-section.open .collapsible-content { display: block; }
        .filter-controls { display: flex; flex-wrap: wrap; gap: 0.75rem; padding-top: 0.75rem; }
        .filter-group { display: flex; flex-direction: column; gap: 0.25rem; min-width: 140px; }
        .filter-label { font-size: 0.75rem; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 0.05em; }
        .filter-select {
            padding: 0.5rem 0.75rem;
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: 6px;
            color: var(--text);
            font-size: 0.875rem;
            cursor: pointer;
            min-width: 120px;
        }
        .filter-select:focus { outline: none; border-color: var(--accent); }
        .filter-select option { background: var(--surface); color: var(--text); }
)HTML";

    // Part 7: Active filters and sort CSS
    html += R"HTML(
        .active-filters {
            display: none;
            flex-wrap: wrap;
            gap: 0.5rem;
            padding: 0.75rem 1rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            margin-bottom: 1rem;
            align-items: center;
        }
        .active-filters.visible { display: flex; }
        .active-filters-label { font-size: 0.8rem; color: var(--text-secondary); margin-right: 0.5rem; }
        .filter-tag {
            display: inline-flex;
            align-items: center;
            gap: 0.4rem;
            padding: 0.3rem 0.6rem;
            background: var(--accent);
            color: var(--bg);
            border-radius: 4px;
            font-size: 0.8rem;
            font-weight: 500;
        }
        .filter-tag-remove { cursor: pointer; font-size: 1rem; line-height: 1; opacity: 0.8; }
        .filter-tag-remove:hover { opacity: 1; }
        .clear-all-btn {
            padding: 0.3rem 0.6rem;
            background: transparent;
            color: var(--text-secondary);
            border: 1px solid var(--border);
            border-radius: 4px;
            font-size: 0.8rem;
            cursor: pointer;
            margin-left: auto;
        }
        .clear-all-btn:hover { background: var(--surface-hover); color: var(--text); }
        .sort-controls { display: flex; flex-wrap: wrap; gap: 0.75rem; padding-top: 0.75rem; align-items: flex-end; }
        .sort-btn {
            padding: 0.5rem 1rem;
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: 6px;
            color: var(--text);
            font-size: 0.8rem;
            cursor: pointer;
            transition: all 0.2s;
        }
        .sort-btn:hover { border-color: var(--accent); }
        .sort-btn.active { background: var(--accent); color: var(--bg); border-color: var(--accent); }
        .sort-btn .sort-dir { margin-left: 0.3rem; }
        .filter-row { display: flex; flex-wrap: wrap; gap: 1rem; }
        .visible-count { font-size: 0.8rem; color: var(--text-secondary); margin-bottom: 0.5rem; }
        @media (max-width: 600px) {
            .shot-grid { grid-template-columns: 1fr; }
            .container { padding: 1rem; padding-bottom: 5rem; }
            .filter-controls, .sort-controls { flex-direction: column; }
            .filter-group, .filter-select { width: 100%%; }
        }
    </style>
</head>
)HTML";

    // Part 8: Body header with menu
    html += QString(R"HTML(<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="logo">&#9749; Decenza DE1</a>
            <div class="header-right">
                <span class="shot-count">%1 shots</span>)HTML").arg(m_storage->totalShots());

    html += generateMenuHtml(true);

    html += R"HTML(
            </div>
        </div>
    </header>
)HTML";

    // Part 9: Main content - filters
    html += QString(R"HTML(
    <main class="container">
        <div class="active-filters" id="activeFilters">
            <span class="active-filters-label">Filters:</span>
            <div id="filterTags"></div>
            <button class="clear-all-btn" onclick="clearAllFilters()">Clear All</button>
        </div>
        <div class="collapsible-section" id="filterSection">
            <div class="collapsible-header" onclick="toggleSection('filterSection')">
                <h3>&#128269; Filter</h3>
                <span class="collapsible-arrow">&#9660;</span>
            </div>
            <div class="collapsible-content">
                <div class="filter-controls">
                    <div class="filter-group">
                        <label class="filter-label">Profile</label>
                        <select class="filter-select" id="filterProfile" onchange="onFilterChange()">
                            <option value="">All Profiles</option>
                            %1
                        </select>
                    </div>
                    <div class="filter-group">
                        <label class="filter-label">Roaster</label>
                        <select class="filter-select" id="filterBrand" onchange="onFilterChange()">
                            <option value="">All Roasters</option>
                            %2
                        </select>
                    </div>
                    <div class="filter-group">
                        <label class="filter-label">Coffee</label>
                        <select class="filter-select" id="filterCoffee" onchange="onFilterChange()">
                            <option value="">All Coffees</option>
                            %3
                        </select>
                    </div>
                    <div class="filter-group">
                        <label class="filter-label">Min Rating</label>
                        <select class="filter-select" id="filterRating" onchange="onFilterChange()">
                            <option value="">Any Rating</option>
                            <option value="90">90+</option>
                            <option value="80">80+</option>
                            <option value="70">70+</option>
                            <option value="60">60+</option>
                            <option value="50">50+</option>
                        </select>
                    </div>
                </div>
                <div class="filter-controls" style="margin-top:0.5rem;">
                    <div class="filter-group">
                        <label class="filter-label">Text Search</label>
                        <input type="text" class="filter-select" id="searchInput" placeholder="Search..." oninput="onFilterChange()" style="min-width:200px;">
                    </div>
                </div>
            </div>
        </div>
)HTML").arg(profileOptions).arg(brandOptions).arg(coffeeOptions);

    // Part 10: Sort section and grid
    html += QString(R"HTML(
        <div class="collapsible-section" id="sortSection">
            <div class="collapsible-header" onclick="toggleSection('sortSection')">
                <h3>&#8645; Sort</h3>
                <span class="collapsible-arrow">&#9660;</span>
            </div>
            <div class="collapsible-content">
                <div class="sort-controls">
                    <button class="sort-btn active" data-sort="date" data-dir="desc" onclick="setSort('date')">Date <span class="sort-dir">&#9660;</span></button>
                    <button class="sort-btn" data-sort="profile" data-dir="asc" onclick="setSort('profile')">Profile <span class="sort-dir">&#9650;</span></button>
                    <button class="sort-btn" data-sort="brand" data-dir="asc" onclick="setSort('brand')">Roaster <span class="sort-dir">&#9650;</span></button>
                    <button class="sort-btn" data-sort="coffee" data-dir="asc" onclick="setSort('coffee')">Coffee <span class="sort-dir">&#9650;</span></button>
                    <button class="sort-btn" data-sort="rating" data-dir="desc" onclick="setSort('rating')">Rating <span class="sort-dir">&#9660;</span></button>
                    <button class="sort-btn" data-sort="ratio" data-dir="desc" onclick="setSort('ratio')">Ratio <span class="sort-dir">&#9660;</span></button>
                    <button class="sort-btn" data-sort="duration" data-dir="asc" onclick="setSort('duration')">Duration <span class="sort-dir">&#9650;</span></button>
                    <button class="sort-btn" data-sort="dose" data-dir="desc" onclick="setSort('dose')">Dose <span class="sort-dir">&#9660;</span></button>
                    <button class="sort-btn" data-sort="yield" data-dir="desc" onclick="setSort('yield')">Yield <span class="sort-dir">&#9660;</span></button>
                </div>
            </div>
        </div>
        <div class="visible-count" id="visibleCount">Showing %1 shots</div>
        <div class="shot-grid" id="shotGrid">
            %2
        </div>
    </main>
    <div class="compare-bar" id="compareBar">
        <span id="selectedCount">0 selected</span>
        <button class="compare-btn" onclick="compareSelected()">Compare Shots</button>
        <button class="clear-btn" onclick="clearSelection()">Clear</button>
    </div>
)HTML").arg(m_storage->totalShots())
      .arg(rows.isEmpty() ? "<div class='empty-state'><h2>No shots yet</h2><p>Pull some espresso to see your history here</p></div>" : rows);

    // Part 11: Script - selection functions
    html += R"HTML(
    <script>
        var selectedShots = [];
        var currentSort = { field: 'date', dir: 'desc' };
        var filters = { profile: '', brand: '', coffee: '', rating: '', search: '' };
        var filterLabels = { profile: 'Profile', brand: 'Roaster', coffee: 'Coffee', rating: 'Rating' };

        function toggleSelect(id, card) {
            var idx = selectedShots.indexOf(id);
            if (idx >= 0) {
                selectedShots.splice(idx, 1);
                card.classList.remove("selected");
            } else {
                if (selectedShots.length < 5) {
                    selectedShots.push(id);
                    card.classList.add("selected");
                }
            }
            updateCompareBar();
        }

        function updateCompareBar() {
            var bar = document.getElementById("compareBar");
            var count = document.getElementById("selectedCount");
            if (selectedShots.length >= 2) {
                bar.classList.add("visible");
                count.textContent = selectedShots.length + " selected";
            } else {
                bar.classList.remove("visible");
            }
            document.querySelectorAll(".shot-checkbox").forEach(function(cb) {
                cb.checked = selectedShots.indexOf(parseInt(cb.dataset.id)) >= 0;
            });
        }

        function clearSelection() {
            selectedShots = [];
            document.querySelectorAll(".shot-card").forEach(function(c) { c.classList.remove("selected"); });
            updateCompareBar();
        }

        function compareSelected() {
            if (selectedShots.length >= 2) {
                window.location.href = "/compare/" + selectedShots.join(",");
            }
        }

        function toggleSection(id) {
            document.getElementById(id).classList.toggle('open');
        }
)HTML";

    // Part 12: Script - filter functions
    html += R"HTML(
        function addFilter(type, value) {
            if (!value || value.trim() === '') return;
            filters[type] = value;
            var select = document.getElementById('filter' + type.charAt(0).toUpperCase() + type.slice(1));
            if (select) select.value = value;
            if (type === 'rating') {
                var ratingSelect = document.getElementById('filterRating');
                if (ratingSelect) {
                    var opts = ratingSelect.options;
                    for (var i = 0; i < opts.length; i++) {
                        if (parseInt(opts[i].value) <= parseInt(value)) {
                            ratingSelect.value = opts[i].value;
                            filters.rating = opts[i].value;
                            break;
                        }
                    }
                }
            }
            updateActiveFilters();
            filterAndSortShots();
        }

        function removeFilter(type) {
            filters[type] = '';
            var select = document.getElementById('filter' + type.charAt(0).toUpperCase() + type.slice(1));
            if (select) select.value = '';
            updateActiveFilters();
            filterAndSortShots();
        }

        function clearAllFilters() {
            filters = { profile: '', brand: '', coffee: '', rating: '', search: '' };
            document.getElementById('filterProfile').value = '';
            document.getElementById('filterBrand').value = '';
            document.getElementById('filterCoffee').value = '';
            document.getElementById('filterRating').value = '';
            document.getElementById('searchInput').value = '';
            updateActiveFilters();
            filterAndSortShots();
        }

        function onFilterChange() {
            filters.profile = document.getElementById('filterProfile').value;
            filters.brand = document.getElementById('filterBrand').value;
            filters.coffee = document.getElementById('filterCoffee').value;
            filters.rating = document.getElementById('filterRating').value;
            filters.search = document.getElementById('searchInput').value.toLowerCase();
            updateActiveFilters();
            filterAndSortShots();
        }

        function updateActiveFilters() {
            var container = document.getElementById('activeFilters');
            var tags = document.getElementById('filterTags');
            tags.innerHTML = '';
            var hasFilters = false;
            for (var key in filters) {
                if (key !== 'search' && filters[key]) {
                    hasFilters = true;
                    var label = filterLabels[key] || key;
                    var displayVal = key === 'rating' ? filters[key] + '+' : filters[key];
                    tags.innerHTML += '<span class="filter-tag">' + label + ': ' + displayVal +
                        ' <span class="filter-tag-remove" onclick="removeFilter(\'' + key + '\')">&times;</span></span>';
                }
            }
            container.classList.toggle('visible', hasFilters);
        }
)HTML";

    // Part 13: Script - filter and sort logic
    html += R"HTML(
        function filterAndSortShots() {
            var cards = Array.from(document.querySelectorAll('.shot-card'));
            var visibleCount = 0;
            cards.forEach(function(card) {
                var show = true;
                if (filters.profile && card.dataset.profile !== filters.profile) show = false;
                if (filters.brand && card.dataset.brand !== filters.brand) show = false;
                if (filters.coffee && card.dataset.coffee !== filters.coffee) show = false;
                if (filters.rating && parseInt(card.dataset.rating) < parseInt(filters.rating)) show = false;
                if (filters.search && !card.textContent.toLowerCase().includes(filters.search)) show = false;
                card.style.display = show ? '' : 'none';
                if (show) visibleCount++;
            });
            var grid = document.getElementById('shotGrid');
            var visibleCards = cards.filter(function(c) { return c.style.display !== 'none'; });
            visibleCards.sort(function(a, b) {
                var aVal, bVal;
                var field = currentSort.field;
                var dir = currentSort.dir === 'asc' ? 1 : -1;
                if (field === 'date') { aVal = a.dataset.date || ''; bVal = b.dataset.date || ''; return dir * aVal.localeCompare(bVal); }
                else if (field === 'profile') { aVal = (a.dataset.profile || '').toLowerCase(); bVal = (b.dataset.profile || '').toLowerCase(); return dir * aVal.localeCompare(bVal); }
                else if (field === 'brand') { aVal = (a.dataset.brand || '').toLowerCase(); bVal = (b.dataset.brand || '').toLowerCase(); return dir * aVal.localeCompare(bVal); }
                else if (field === 'coffee') { aVal = (a.dataset.coffee || '').toLowerCase(); bVal = (b.dataset.coffee || '').toLowerCase(); return dir * aVal.localeCompare(bVal); }
                else if (field === 'rating') { aVal = parseFloat(a.dataset.rating) || 0; bVal = parseFloat(b.dataset.rating) || 0; return dir * (aVal - bVal); }
                else if (field === 'ratio') { aVal = parseFloat(a.dataset.ratio) || 0; bVal = parseFloat(b.dataset.ratio) || 0; return dir * (aVal - bVal); }
                else if (field === 'duration') { aVal = parseFloat(a.dataset.duration) || 0; bVal = parseFloat(b.dataset.duration) || 0; return dir * (aVal - bVal); }
                else if (field === 'dose') { aVal = parseFloat(a.dataset.dose) || 0; bVal = parseFloat(b.dataset.dose) || 0; return dir * (aVal - bVal); }
                else if (field === 'yield') { aVal = parseFloat(a.dataset.yield) || 0; bVal = parseFloat(b.dataset.yield) || 0; return dir * (aVal - bVal); }
                return 0;
            });
            visibleCards.forEach(function(card) { grid.appendChild(card); });
            document.getElementById('visibleCount').textContent = 'Showing ' + visibleCount + ' shots';
        }
)HTML";

    // Part 14: Script - sort and menu functions
    html += R"HTML(
        function setSort(field) {
            var btns = document.querySelectorAll('.sort-btn');
            btns.forEach(function(btn) {
                if (btn.dataset.sort === field) {
                    if (btn.classList.contains('active')) {
                        var newDir = btn.dataset.dir === 'asc' ? 'desc' : 'asc';
                        btn.dataset.dir = newDir;
                        btn.querySelector('.sort-dir').innerHTML = newDir === 'asc' ? '&#9650;' : '&#9660;';
                    }
                    btn.classList.add('active');
                    currentSort.field = field;
                    currentSort.dir = btn.dataset.dir;
                } else {
                    btn.classList.remove('active');
                }
            });
            filterAndSortShots();
        }

        function toggleMenu() {
            document.getElementById("menuDropdown").classList.toggle("open");
        }

        document.addEventListener("click", function(e) {
            var menu = document.getElementById("menuDropdown");
            if (!e.target.closest(".menu-btn") && menu.classList.contains("open")) {
                menu.classList.remove("open");
            }
        });
)HTML";

    // Part 15: Script - power functions
    html += R"HTML(
        var powerState = {awake: false, state: "Unknown"};

        function updatePowerButton() {
            var btn = document.getElementById("powerToggle");
            if (powerState.state === "Unknown" || !powerState.connected) {
                btn.innerHTML = "&#128268; Disconnected";
            } else if (powerState.awake) {
                btn.innerHTML = "&#128164; Put to Sleep";
            } else {
                btn.innerHTML = "&#9889; Wake Up";
            }
        }

        function fetchPowerState() {
            fetch("/api/power/status")
                .then(function(r) { return r.json(); })
                .then(function(data) { powerState = data; updatePowerButton(); })
                .catch(function() {});
        }

        function togglePower() {
            var action = powerState.awake ? "sleep" : "wake";
            fetch("/api/power/" + action)
                .then(function(r) { return r.json(); })
                .then(function() { setTimeout(fetchPowerState, 1000); });
        }

        fetchPowerState();
        setInterval(fetchPowerState, 5000);
    </script>
</body>
</html>
)HTML";

    return html;
}

QString ShotServer::generateShotDetailPage(qint64 shotId) const
{
    QVariantMap shot = m_storage->getShot(shotId);
    if (shot.isEmpty()) {
        return QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Not Found</title></head>"
                  "<body style=\"background:#0d1117;color:#fff;font-family:sans-serif;padding:2rem;\">"
                  "<h1>Shot not found</h1><a href=\"/\" style=\"color:#c9a227;\">Back to list</a></body></html>");
    }

    double ratio = 0;
    if (shot["doseWeight"].toDouble() > 0) {
        ratio = shot["finalWeight"].toDouble() / shot["doseWeight"].toDouble();
    }

    int rating = qRound(shot["enjoyment"].toDouble() / 20.0);
    QString stars;
    for (int i = 0; i < 5; i++) {
        stars += (i < rating) ? "&#9733;" : "&#9734;";
    }

    // Convert time-series data to JSON arrays for Chart.js
    auto pointsToJson = [](const QVariantList& points) -> QString {
        QStringList items;
        for (const QVariant& p : points) {
            QVariantMap pt = p.toMap();
            items << QString("{x:%1,y:%2}").arg(pt["x"].toDouble(), 0, 'f', 2).arg(pt["y"].toDouble(), 0, 'f', 2);
        }
        return "[" + items.join(",") + "]";
    };

    // Convert goal data with nulls at gaps (where time jumps > 0.5s)
    auto goalPointsToJson = [](const QVariantList& points) -> QString {
        QStringList items;
        double lastX = -999;
        for (const QVariant& p : points) {
            QVariantMap pt = p.toMap();
            double x = pt["x"].toDouble();
            double y = pt["y"].toDouble();
            // Insert null to break line if there's a gap > 0.5 seconds
            if (lastX >= 0 && (x - lastX) > 0.5) {
                items << QString("{x:%1,y:null}").arg((lastX + x) / 2, 0, 'f', 2);
            }
            items << QString("{x:%1,y:%2}").arg(x, 0, 'f', 2).arg(y, 0, 'f', 2);
            lastX = x;
        }
        return "[" + items.join(",") + "]";
    };

    QString pressureData = pointsToJson(shot["pressure"].toList());
    QString flowData = pointsToJson(shot["flow"].toList());
    QString tempData = pointsToJson(shot["temperature"].toList());
    QString weightData = pointsToJson(shot["weight"].toList());
    QString pressureGoalData = goalPointsToJson(shot["pressureGoal"].toList());
    QString flowGoalData = goalPointsToJson(shot["flowGoal"].toList());

    return QString(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>%1 - Decenza DE1</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>)HTML" R"HTML(
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --surface-hover: #1f2937;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
            --pressure: #18c37e;
            --flow: #4e85f4;
            --temp: #e73249;
            --weight: #a2693d;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.5;
        }
        .header {
            background: var(--surface);
            border-bottom: 1px solid var(--border);
            padding: 1rem 1.5rem;
            position: sticky;
            top: 0;
            z-index: 100;
        }
        .header-content {
            max-width: 1400px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            gap: 1rem;
        }
        .back-btn {
            color: var(--text-secondary);
            text-decoration: none;
            font-size: 1.5rem;
            line-height: 1;
            padding: 0.25rem;
        }
        .back-btn:hover { color: var(--accent); }
        .header-title {
            flex: 1;
        }
        .header-title h1 {
            font-size: 1.125rem;
            font-weight: 600;
        }
        .header-title .subtitle {
            font-size: 0.75rem;
            color: var(--text-secondary);
        }
        .container {
            max-width: 1400px;
            margin: 0 auto;
            padding: 1.5rem;
        }
        .metrics-bar {
            display: flex;
            gap: 1rem;
            flex-wrap: wrap;
            margin-bottom: 1.5rem;
        }
        .metric-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 1rem 1.25rem;
            min-width: 100px;
            text-align: center;
        }
        .metric-card .value {
            font-size: 1.5rem;
            font-weight: 700;
            color: var(--accent);
        }
        .metric-card .label {
            font-size: 0.6875rem;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .chart-container {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            margin-bottom: 1.5rem;
        }
        .chart-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 1rem;
            flex-wrap: wrap;
            gap: 0.5rem;
        }
        .chart-title {
            font-size: 1rem;
            font-weight: 600;
        }
        .chart-toggles {
            display: flex;
            gap: 0.5rem;
            flex-wrap: wrap;
        }
        .toggle-btn {
            padding: 0.375rem 0.75rem;
            border: 1px solid var(--border);
            border-radius: 6px;
            background: transparent;
            color: var(--text-secondary);
            font-size: 0.75rem;
            cursor: pointer;
            transition: all 0.15s ease;
            display: flex;
            align-items: center;
            gap: 0.375rem;
        }
        .toggle-btn:hover { border-color: var(--text-secondary); }
        .toggle-btn.active { background: var(--surface-hover); color: var(--text); }
        .toggle-btn .dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
        }
        .toggle-btn.pressure .dot { background: var(--pressure); }
        .toggle-btn.flow .dot { background: var(--flow); }
        .toggle-btn.temp .dot { background: var(--temp); }
        .toggle-btn.weight .dot { background: var(--weight); }
        .chart-wrapper {
            position: relative;
            height: 400px;
        }
        .info-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 1rem;
        }
        .info-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1.25rem;
        }
        .info-card h3 {
            font-size: 0.875rem;
            font-weight: 600;
            margin-bottom: 0.75rem;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .info-row {
            display: flex;
            justify-content: space-between;
            padding: 0.5rem 0;
            border-bottom: 1px solid var(--border);
        }
        .info-row:last-child { border-bottom: none; }
        .info-row .label { color: var(--text-secondary); }
        .info-row .value { font-weight: 500; }
        .notes-text {
            color: var(--text-secondary);
            font-style: italic;
        }
        .rating { color: var(--accent); font-size: 1.125rem; }
        .menu-wrapper { position: relative; margin-left: auto; }
        .menu-btn {
            background: none;
            border: none;
            color: var(--text);
            font-size: 1.5rem;
            cursor: pointer;
            padding: 0.25rem 0.5rem;
            line-height: 1;
        }
        .menu-btn:hover { color: var(--accent); }
        .menu-dropdown {
            position: absolute;
            top: 100%;
            right: 0;
            margin-top: 0.5rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            min-width: max-content;
            display: none;
            box-shadow: 0 4px 12px rgba(0,0,0,0.3);
            z-index: 200;
        }
        .menu-dropdown.open { display: block; }
        .menu-item {
            display: block;
            padding: 0.75rem 1rem;
            color: var(--text);
            text-decoration: none;
            white-space: nowrap;
        }
        .menu-item:hover { background: var(--surface-hover); }
        @media (max-width: 600px) {
            .container { padding: 1rem; }
            .chart-wrapper { height: 300px; }
            .metrics-bar { justify-content: center; }
        }
    </style>
</head>)HTML" R"HTML(
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&#8592;</a>
            <div class="header-title">
                <h1>%1</h1>
                <div class="subtitle">%2</div>
            </div>
            <div class="menu-wrapper">
                <button class="menu-btn" onclick="toggleMenu()" aria-label="Menu">&#9776;</button>
                <div class="menu-dropdown" id="menuDropdown">
                    <a href="#" class="menu-item" id="powerToggle" onclick="togglePower(); return false;">&#9889; Loading...</a>
                    <a href="/" class="menu-item">&#127866; Shot History</a>
                    <a href="/debug" class="menu-item">&#128196; Live Debug Log</a>
                    <a href="/remote" class="menu-item">&#128421; Remote Control</a>)HTML"
#ifdef Q_OS_ANDROID
                    R"HTML(<a href="/upload" class="menu-item">&#128230; Upload APK</a>)HTML"
#endif
                    R"HTML(<a href="/database.db" class="menu-item">&#128190; Download Database</a>
                    <a href="/upload/media" class="menu-item">&#127912; Upload Screensaver Media</a>
                </div>
            </div>
        </div>
    </header>
    <main class="container">
        <div class="metrics-bar">
            <div class="metric-card">
                <div class="value">%3g</div>
                <div class="label">Dose</div>
            </div>
            <div class="metric-card">
                <div class="value">%4g</div>
                <div class="label">Yield</div>
            </div>
            <div class="metric-card">
                <div class="value">1:%5</div>
                <div class="label">Ratio</div>
            </div>
            <div class="metric-card">
                <div class="value">%6s</div>
                <div class="label">Time</div>
            </div>
            <div class="metric-card">
                <div class="value rating">%7</div>
                <div class="label">Rating</div>
            </div>
        </div>

        <div class="chart-container">
            <div class="chart-header">
                <div class="chart-title">Extraction Curves</div>
                <div class="chart-toggles">
                    <button class="toggle-btn pressure active" onclick="toggleDataset(0, this)">
                        <span class="dot"></span> Pressure
                    </button>
                    <button class="toggle-btn flow active" onclick="toggleDataset(1, this)">
                        <span class="dot"></span> Flow
                    </button>
                    <button class="toggle-btn weight active" onclick="toggleDataset(2, this)">
                        <span class="dot"></span> Yield
                    </button>
                    <button class="toggle-btn temp active" onclick="toggleDataset(3, this)">
                        <span class="dot"></span> Temp
                    </button>
                </div>
            </div>
            <div class="chart-wrapper">
                <canvas id="shotChart"></canvas>
            </div>
        </div>

        <div class="info-grid">
            <div class="info-card">
                <h3>Beans</h3>
                <div class="info-row">
                    <span class="label">Brand</span>
                    <span class="value">%8</span>
                </div>
                <div class="info-row">
                    <span class="label">Type</span>
                    <span class="value">%9</span>
                </div>
                <div class="info-row">
                    <span class="label">Roast Date</span>
                    <span class="value">%10</span>
                </div>
                <div class="info-row">
                    <span class="label">Roast Level</span>
                    <span class="value">%11</span>
                </div>
            </div>
            <div class="info-card">
                <h3>Grinder</h3>
                <div class="info-row">
                    <span class="label">Model</span>
                    <span class="value">%12</span>
                </div>
                <div class="info-row">
                    <span class="label">Setting</span>
                    <span class="value">%13</span>
                </div>
            </div>
            <div class="info-card">
                <h3>Notes</h3>
                <p class="notes-text">%14</p>
            </div>
        </div>

        <div class="actions-bar" style="margin-top:1.5rem;display:flex;gap:1rem;flex-wrap:wrap;">
            <button onclick="downloadProfile()" style="display:inline-flex;align-items:center;gap:0.5rem;padding:0.75rem 1.25rem;background:var(--surface);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:0.875rem;cursor:pointer;">
                &#128196; Download Profile JSON
            </button>
            <button onclick="var c=document.getElementById('debugLogContainer'); if(c){if(c.style.display==='none'){c.style.display='block';c.scrollIntoView({behavior:'smooth'});}else{c.style.display='none';}}" style="display:inline-flex;align-items:center;gap:0.5rem;padding:0.75rem 1.25rem;background:var(--surface);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:0.875rem;cursor:pointer;">
                &#128203; View Debug Log
            </button>
        </div>

        <div id="debugLogContainer" style="display:none;margin-top:1rem;">
            <div class="info-card">
                <h3>Debug Log</h3>
                <pre id="debugLogContent" style="background:var(--bg);padding:1rem;border-radius:8px;overflow-x:auto;font-size:0.75rem;line-height:1.4;white-space:pre-wrap;word-break:break-all;max-height:500px;overflow-y:auto;">%21</pre>
                <button onclick="copyDebugLog()" style="margin-top:0.75rem;padding:0.5rem 1rem;background:var(--accent);border:none;border-radius:6px;color:#000;font-weight:500;cursor:pointer;">Copy to Clipboard</button>
            </div>
        </div>
    </main>

    <script>
        function downloadProfile() {
            window.location.href = window.location.pathname + '/profile.json';
        }
        function showDebugLog() {
            var container = document.getElementById('debugLogContainer');
            if (container) {
                container.style.display = container.style.display === 'none' ? 'block' : 'none';
            } else {
                alert('Debug log container not found');
            }
        }
        function copyDebugLog() {
            var text = document.getElementById('debugLogContent').textContent;
            // Use fallback for non-HTTPS (clipboard API requires secure context)
            var textarea = document.createElement('textarea');
            textarea.value = text;
            textarea.style.position = 'fixed';
            textarea.style.opacity = '0';
            document.body.appendChild(textarea);
            textarea.select();
            try {
                document.execCommand('copy');
            } catch (err) {
                alert('Failed to copy: ' + err);
            }
            document.body.removeChild(textarea);
        }
    </script>
    <script>
        const pressureData = %15;
        const flowData = %16;
        const weightData = %17;
        const tempData = %18;
        const pressureGoalData = %19;
        const flowGoalData = %20;

        // Track mouse position for tooltip
        var mouseX = 0, mouseY = 0;
        document.addEventListener("mousemove", function(e) {
            mouseX = e.pageX;
            mouseY = e.pageY;
        });

        // Find closest data point to a given x value
        function findClosestPoint(data, targetX) {
            if (!data || data.length === 0) return null;
            var closest = data[0];
            var closestDist = Math.abs(data[0].x - targetX);
            for (var i = 1; i < data.length; i++) {
                var dist = Math.abs(data[i].x - targetX);
                if (dist < closestDist) {
                    closestDist = dist;
                    closest = data[i];
                }
            }
            return closest;
        }

        // External tooltip showing all curves
        function externalTooltip(context) {
            var tooltipEl = document.getElementById("chartTooltip");
            if (!tooltipEl) {
                tooltipEl = document.createElement("div");
                tooltipEl.id = "chartTooltip";
                tooltipEl.style.cssText = "position:absolute;background:#161b22;border:1px solid #30363d;border-radius:8px;padding:10px 14px;pointer-events:none;font-size:13px;color:#e6edf3;z-index:100;";
                document.body.appendChild(tooltipEl);
            }

            var tooltip = context.tooltip;
            if (tooltip.opacity === 0) {
                tooltipEl.style.opacity = 0;
                return;
            }

            if (!tooltip.dataPoints || !tooltip.dataPoints.length) {
                tooltipEl.style.opacity = 0;
                return;
            }

            var targetX = tooltip.dataPoints[0].parsed.x;
            var datasets = context.chart.data.datasets;
            var lines = [];)HTML" R"HTML(

            for (var i = 0; i < datasets.length; i++) {
                var ds = datasets[i];
                var meta = context.chart.getDatasetMeta(i);
                if (meta.hidden) continue;

                var pt = findClosestPoint(ds.data, targetX);
                if (!pt || pt.y === null) continue;

                var unit = "";
                if (ds.label.includes("Pressure")) unit = " bar";
                else if (ds.label.includes("Flow")) unit = " ml/s";
                else if (ds.label.includes("Yield")) unit = " g";
                else if (ds.label.includes("Temp")) unit = " °C";

                lines.push('<div style="display:flex;align-items:center;gap:6px;"><span style="display:inline-block;width:12px;height:12px;background:' + ds.borderColor + ';border-radius:2px;"></span>' + ds.label + ': ' + pt.y.toFixed(1) + unit + '</div>');
            }

            tooltipEl.innerHTML = '<div style="font-weight:600;margin-bottom:6px;">' + targetX.toFixed(1) + 's</div>' + lines.join('');
            tooltipEl.style.opacity = 1;
            tooltipEl.style.left = (mouseX + 15) + "px";
            tooltipEl.style.top = (mouseY - 10) + "px";
        }

        const ctx = document.getElementById('shotChart').getContext('2d');
        const chart = new Chart(ctx, {
            type: 'line',
            data: {
                datasets: [
                    {
                        label: 'Pressure',
                        data: pressureData,
                        borderColor: '#18c37e',
                        backgroundColor: 'rgba(24, 195, 126, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y'
                    },
                    {
                        label: 'Flow',
                        data: flowData,
                        borderColor: '#4e85f4',
                        backgroundColor: 'rgba(78, 133, 244, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y'
                    },
                    {
                        label: 'Yield',
                        data: weightData,
                        borderColor: '#a2693d',
                        backgroundColor: 'rgba(162, 105, 61, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y2'
                    },
                    {
                        label: 'Temp',
                        data: tempData,
                        borderColor: '#e73249',
                        backgroundColor: 'rgba(231, 50, 73, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y3'
                    },
                    {
                        label: 'Pressure Goal',
                        data: pressureGoalData,
                        borderColor: '#69fdb3',
                        borderWidth: 1,
                        borderDash: [5, 5],
                        pointRadius: 0,
                        tension: 0.1,
                        yAxisID: 'y',
                        spanGaps: false
                    },
                    {
                        label: 'Flow Goal',
                        data: flowGoalData,
                        borderColor: '#7aaaff',
                        borderWidth: 1,
                        borderDash: [5, 5],
                        pointRadius: 0,
                        tension: 0.1,
                        yAxisID: 'y',
                        spanGaps: false
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                interaction: {
                    mode: 'nearest',
                    axis: 'x',
                    intersect: false
                },
                plugins: {
                    legend: { display: false },
                    tooltip: {
                        enabled: false,
                        external: externalTooltip
                    }
                },
                scales: {
                    x: {
                        type: 'linear',
                        title: { display: true, text: 'Time (s)', color: '#8b949e' },
                        grid: { color: 'rgba(48, 54, 61, 0.5)' },
                        ticks: { color: '#8b949e' }
                    },
                    y: {
                        type: 'linear',
                        position: 'left',
                        title: { display: true, text: 'Pressure / Flow', color: '#8b949e' },
                        min: 0,
                        max: 12,
                        grid: { color: 'rgba(48, 54, 61, 0.5)' },
                        ticks: { color: '#8b949e' }
                    },)HTML" R"HTML(
                    y2: {
                        type: 'linear',
                        position: 'right',
                        title: { display: true, text: 'Yield (g)', color: '#a2693d' },
                        min: 0,
                        grid: { display: false },
                        ticks: { color: '#a2693d' }
                    },
                    y3: {
                        type: 'linear',
                        position: 'right',
                        title: { display: false },
                        min: 80,
                        max: 100,
                        display: false
                    }
                }
            }
        });

        function toggleDataset(index, btn) {
            const meta = chart.getDatasetMeta(index);
            meta.hidden = !meta.hidden;
            btn.classList.toggle('active');

            // Also toggle goal lines for pressure/flow
            if (index === 0) chart.getDatasetMeta(4).hidden = meta.hidden;
            if (index === 1) chart.getDatasetMeta(5).hidden = meta.hidden;

            chart.update();
        }

        function toggleMenu() {
            var menu = document.getElementById("menuDropdown");
            menu.classList.toggle("open");
        }

        document.addEventListener("click", function(e) {
            var menu = document.getElementById("menuDropdown");
            var btn = e.target.closest(".menu-btn");
            if (!btn && menu.classList.contains("open")) {
                menu.classList.remove("open");
            }
        });

        // Power toggle
        var powerState = {awake: false, state: "Unknown"};
        function updatePowerButton() {
            var btn = document.getElementById("powerToggle");
            if (powerState.state === "Unknown" || !powerState.connected) {
                btn.innerHTML = "&#128268; Disconnected";
            } else if (powerState.awake) {
                btn.innerHTML = "&#128164; Put to Sleep";
            } else {
                btn.innerHTML = "&#9889; Wake Up";
            }
        }
        function fetchPowerState() {
            fetch("/api/power/status")
                .then(function(r) { return r.json(); })
                .then(function(data) { powerState = data; updatePowerButton(); })
                .catch(function() {});
        }
        function togglePower() {
            var action = powerState.awake ? "sleep" : "wake";
            fetch("/api/power/" + action)
                .then(function(r) { return r.json(); })
                .then(function() { setTimeout(fetchPowerState, 1000); });
        }
        fetchPowerState();
        setInterval(fetchPowerState, 5000);
    </script>
</body>
</html>
)HTML")
    .arg(shot["profileName"].toString().toHtmlEscaped())
    .arg(shot["dateTime"].toString())
    .arg(shot["doseWeight"].toDouble(), 0, 'f', 1)
    .arg(shot["finalWeight"].toDouble(), 0, 'f', 1)
    .arg(ratio, 0, 'f', 1)
    .arg(shot["duration"].toDouble(), 0, 'f', 1)
    .arg(stars)
    .arg(shot["beanBrand"].toString().isEmpty() ? "-" : shot["beanBrand"].toString().toHtmlEscaped())
    .arg(shot["beanType"].toString().isEmpty() ? "-" : shot["beanType"].toString().toHtmlEscaped())
    .arg(shot["roastDate"].toString().isEmpty() ? "-" : shot["roastDate"].toString())
    .arg(shot["roastLevel"].toString().isEmpty() ? "-" : shot["roastLevel"].toString().toHtmlEscaped())
    .arg(shot["grinderModel"].toString().isEmpty() ? "-" : shot["grinderModel"].toString().toHtmlEscaped())
    .arg(shot["grinderSetting"].toString().isEmpty() ? "-" : shot["grinderSetting"].toString().toHtmlEscaped())
    .arg(shot["espressoNotes"].toString().isEmpty() ? "No notes" : shot["espressoNotes"].toString().toHtmlEscaped())
    .arg(pressureData)
    .arg(flowData)
    .arg(weightData)
    .arg(tempData)
    .arg(pressureGoalData)
    .arg(flowGoalData)
    .arg(shot["debugLog"].toString().isEmpty() ? "No debug log available" : shot["debugLog"].toString().toHtmlEscaped());
}

QString ShotServer::generateComparisonPage(const QList<qint64>& shotIds) const
{
    // Load all shots
    QList<QVariantMap> shots;
    for (qint64 id : shotIds) {
        QVariantMap shot = m_storage->getShot(id);
        if (!shot.isEmpty()) {
            shots << shot;
        }
    }

    if (shots.size() < 2) {
        return QStringLiteral("<!DOCTYPE html><html><body>Not enough valid shots to compare</body></html>");
    }

    // Colors for each shot (up to 5)
    QStringList shotColors = {"#c9a227", "#e85d75", "#4ecdc4", "#a855f7", "#f97316"};

    auto pointsToJson = [](const QVariantList& points) -> QString {
        QStringList items;
        for (const QVariant& p : points) {
            QVariantMap pt = p.toMap();
            items << QString("{x:%1,y:%2}").arg(pt["x"].toDouble(), 0, 'f', 2).arg(pt["y"].toDouble(), 0, 'f', 2);
        }
        return "[" + items.join(",") + "]";
    };

    // Build datasets for each shot
    QString datasets;
    QString legendItems;
    int shotIndex = 0;

    for (const QVariantMap& shot : std::as_const(shots)) {
        QString color = shotColors[shotIndex % shotColors.size()];
        QString name = shot["profileName"].toString();
        QString date = shot["dateTime"].toString().left(10);
        QString label = QString("%1 (%2)").arg(name, date);

        QString pressureData = pointsToJson(shot["pressure"].toList());
        QString flowData = pointsToJson(shot["flow"].toList());
        QString weightData = pointsToJson(shot["weight"].toList());
        QString tempData = pointsToJson(shot["temperature"].toList());

        // Add datasets for this shot
        datasets += QString(R"HTML(
            { label: "Pressure - %1", data: %2, borderColor: "%3", borderWidth: 2, pointRadius: 0, tension: 0.3, yAxisID: "y", shotIndex: %4, curveType: "pressure" },
            { label: "Flow - %1", data: %5, borderColor: "%3", borderWidth: 2, pointRadius: 0, tension: 0.3, yAxisID: "y", borderDash: [5,3], shotIndex: %4, curveType: "flow" },
            { label: "Yield - %1", data: %6, borderColor: "%3", borderWidth: 2, pointRadius: 0, tension: 0.3, yAxisID: "y2", borderDash: [2,2], shotIndex: %4, curveType: "weight" },
            { label: "Temp - %1", data: %7, borderColor: "%3", borderWidth: 1, pointRadius: 0, tension: 0.3, yAxisID: "y3", borderDash: [8,4], shotIndex: %4, curveType: "temp" },
        )HTML").arg(label.toHtmlEscaped(), pressureData, color).arg(shotIndex).arg(flowData, weightData, tempData);

        double ratio = shot["doseWeight"].toDouble() > 0 ?
            shot["finalWeight"].toDouble() / shot["doseWeight"].toDouble() : 0;

        legendItems += QString(R"HTML(
            <div class="legend-item">
                <span class="legend-color" style="background:%1"></span>
                <div class="legend-info">
                    <div class="legend-name">%2</div>
                    <div class="legend-details">%3 | %4g in | %5g out | 1:%6 | %7s</div>
                </div>
            </div>
        )HTML").arg(color)
               .arg(label.toHtmlEscaped())
               .arg(date)
               .arg(shot["doseWeight"].toDouble(), 0, 'f', 1)
               .arg(shot["finalWeight"].toDouble(), 0, 'f', 1)
               .arg(ratio, 0, 'f', 1)
               .arg(shot["duration"].toDouble(), 0, 'f', 1);

        shotIndex++;
    }

    return QString(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Compare Shots - Decenza DE1</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
            --pressure: #18c37e;
            --flow: #4e85f4;
            --temp: #e73249;
            --weight: #a2693d;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.5;
        }
        .header {
            background: var(--surface);
            border-bottom: 1px solid var(--border);
            padding: 1rem 1.5rem;
            position: sticky;
            top: 0;
            z-index: 100;
        }
        .header-content {
            max-width: 1400px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            gap: 1rem;
        }
        .back-btn {
            color: var(--text-secondary);
            text-decoration: none;
            font-size: 1.5rem;
        }
        .back-btn:hover { color: var(--accent); }
        h1 { font-size: 1.125rem; font-weight: 600; }
        .container {
            max-width: 1400px;
            margin: 0 auto;
            padding: 1.5rem;
        }
        .chart-container {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            margin-bottom: 1.5rem;
        }
        .chart-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 1rem;
            flex-wrap: wrap;
            gap: 0.75rem;
        }
        .chart-title { font-size: 1rem; font-weight: 600; }
        .curve-toggles {
            display: flex;
            gap: 0.5rem;
            flex-wrap: wrap;
        }
        .toggle-btn {
            padding: 0.5rem 1rem;
            border: 1px solid var(--border);
            border-radius: 6px;
            background: transparent;
            color: var(--text-secondary);
            font-size: 0.8125rem;
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }
        .toggle-btn:hover { border-color: var(--text-secondary); }
        .toggle-btn.active { background: var(--surface); color: var(--text); border-color: var(--text); }
        .toggle-btn .dot { width: 10px; height: 10px; border-radius: 50%; }
        .toggle-btn.pressure .dot { background: var(--pressure); }
        .toggle-btn.flow .dot { background: var(--flow); }
        .toggle-btn.weight .dot { background: var(--weight); }
        .toggle-btn.temp .dot { background: var(--temp); }
        .chart-wrapper { position: relative; height: 450px; }
        .legend {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
        }
        .legend-title {
            font-size: 0.875rem;
            font-weight: 600;
            margin-bottom: 0.75rem;
            color: var(--text-secondary);
        }
        .legend-item {
            display: flex;
            align-items: center;
            gap: 0.75rem;
            padding: 0.5rem 0;
            border-bottom: 1px solid var(--border);
        }
        .legend-item:last-child { border-bottom: none; }
        .legend-color {
            width: 16px;
            height: 16px;
            border-radius: 4px;
            flex-shrink: 0;
        }
        .legend-name { font-weight: 500; }
        .legend-details { font-size: 0.75rem; color: var(--text-secondary); }
        .curve-legend {
            display: flex;
            gap: 1.5rem;
            margin-top: 1rem;
            padding-top: 1rem;
            border-top: 1px solid var(--border);
            flex-wrap: wrap;
        }
        .curve-legend-item {
            display: flex;
            align-items: center;
            gap: 0.5rem;
            font-size: 0.75rem;
            color: var(--text-secondary);
        }
        .curve-line {
            width: 24px;
            height: 2px;
        }
        .curve-line.solid { background: var(--text-secondary); }
        .curve-line.dashed { background: repeating-linear-gradient(90deg, var(--text-secondary) 0, var(--text-secondary) 4px, transparent 4px, transparent 7px); }
        .curve-line.dotted { background: repeating-linear-gradient(90deg, var(--text-secondary) 0, var(--text-secondary) 2px, transparent 2px, transparent 5px); }
        .curve-line.longdash { background: repeating-linear-gradient(90deg, var(--text-secondary) 0, var(--text-secondary) 8px, transparent 8px, transparent 12px); }
        .menu-wrapper { position: relative; margin-left: auto; }
        .menu-btn {
            background: none;
            border: none;
            color: var(--text);
            font-size: 1.5rem;
            cursor: pointer;
            padding: 0.25rem 0.5rem;
            line-height: 1;
        }
        .menu-btn:hover { color: var(--accent); }
        .menu-dropdown {
            position: absolute;
            top: 100%;
            right: 0;
            margin-top: 0.5rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            min-width: max-content;
            display: none;
            box-shadow: 0 4px 12px rgba(0,0,0,0.3);
            z-index: 200;
        }
        .menu-dropdown.open { display: block; }
        .menu-item {
            display: block;
            padding: 0.75rem 1rem;
            color: var(--text);
            text-decoration: none;
            white-space: nowrap;
        }
        .menu-item:hover { background: var(--surface); }
        @media (max-width: 600px) {
            .container { padding: 1rem; }
            .chart-wrapper { height: 350px; }
        }
    </style>
</head>)HTML" R"HTML(
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&#8592;</a>
            <h1>Compare %1 Shots</h1>
            <div class="menu-wrapper">
                <button class="menu-btn" onclick="toggleMenu()" aria-label="Menu">&#9776;</button>
                <div class="menu-dropdown" id="menuDropdown">
                    <a href="#" class="menu-item" id="powerToggle" onclick="togglePower(); return false;">&#9889; Loading...</a>
                    <a href="/" class="menu-item">&#127866; Shot History</a>
                    <a href="/debug" class="menu-item">&#128196; Live Debug Log</a>
                    <a href="/remote" class="menu-item">&#128421; Remote Control</a>)HTML"
#ifdef Q_OS_ANDROID
                    R"HTML(<a href="/upload" class="menu-item">&#128230; Upload APK</a>)HTML"
#endif
                    R"HTML(<a href="/database.db" class="menu-item">&#128190; Download Database</a>
                    <a href="/upload/media" class="menu-item">&#127912; Upload Screensaver Media</a>
                </div>
            </div>
        </div>
    </header>
    <main class="container">
        <div class="chart-container">
            <div class="chart-header">
                <div class="chart-title">Extraction Curves</div>
                <div class="curve-toggles">
                    <button class="toggle-btn pressure active" onclick="toggleCurve('pressure', this)">
                        <span class="dot"></span> Pressure
                    </button>
                    <button class="toggle-btn flow active" onclick="toggleCurve('flow', this)">
                        <span class="dot"></span> Flow
                    </button>
                    <button class="toggle-btn weight active" onclick="toggleCurve('weight', this)">
                        <span class="dot"></span> Yield
                    </button>
                    <button class="toggle-btn temp active" onclick="toggleCurve('temp', this)">
                        <span class="dot"></span> Temp
                    </button>
                </div>
            </div>
            <div class="chart-wrapper">
                <canvas id="compareChart"></canvas>
            </div>
        </div>
        <div class="legend">
            <div class="legend-title">Shots</div>
            %2
            <div class="curve-legend">
                <div class="curve-legend-item"><span class="curve-line solid"></span> Pressure</div>
                <div class="curve-legend-item"><span class="curve-line dashed"></span> Flow</div>
                <div class="curve-legend-item"><span class="curve-line dotted"></span> Yield</div>
                <div class="curve-legend-item"><span class="curve-line longdash"></span> Temp</div>
            </div>
        </div>
    </main>
    <script>
        var visibleCurves = { pressure: true, flow: true, weight: true, temp: true };

        // Find closest data point in a dataset to a given x value
        function findClosestPoint(data, targetX) {
            if (!data || data.length === 0) return null;
            var closest = data[0];
            var closestDist = Math.abs(data[0].x - targetX);
            for (var i = 1; i < data.length; i++) {
                var dist = Math.abs(data[i].x - targetX);
                if (dist < closestDist) {
                    closestDist = dist;
                    closest = data[i];
                }
            }
            return closest;
        }

        // Track mouse position for tooltip
        var mouseX = 0, mouseY = 0;
        document.addEventListener("mousemove", function(e) {
            mouseX = e.pageX;
            mouseY = e.pageY;
        });

        // Custom external tooltip
        function externalTooltip(context) {
            var tooltipEl = document.getElementById("chartTooltip");
            if (!tooltipEl) {
                tooltipEl = document.createElement("div");
                tooltipEl.id = "chartTooltip";
                tooltipEl.style.cssText = "position:absolute;background:#161b22;border:1px solid #30363d;border-radius:8px;padding:10px 14px;pointer-events:none;font-size:13px;color:#e6edf3;z-index:100;max-width:400px;";
                document.body.appendChild(tooltipEl);
            }

            var tooltip = context.tooltip;
            if (tooltip.opacity === 0) {
                tooltipEl.style.opacity = 0;
                return;
            }

            // Get x position from the nearest point
            if (!tooltip.dataPoints || !tooltip.dataPoints.length) {
                tooltipEl.style.opacity = 0;
                return;
            }

            var targetX = tooltip.dataPoints[0].parsed.x;
            var datasets = context.chart.data.datasets;

            // Group by shot, collect all curve values at this time
            var shotData = {};
            for (var i = 0; i < datasets.length; i++) {
                var ds = datasets[i];
                var meta = context.chart.getDatasetMeta(i);
                if (meta.hidden || !visibleCurves[ds.curveType]) continue;

                var pt = findClosestPoint(ds.data, targetX);
                if (!pt) continue;

                var key = ds.shotIndex;
                if (!shotData[key]) {
                    shotData[key] = { color: ds.borderColor, label: ds.label.split(" - ")[1] || ds.label, values: {} };
                }
                shotData[key].values[ds.curveType] = pt.y;
            }

            // Build HTML
            var html = "<div style='font-weight:600;margin-bottom:6px;'>" + targetX.toFixed(1) + "s</div>";
            var curveInfo = { pressure: {l:"P", u:"bar"}, flow: {l:"F", u:"ml/s"}, weight: {l:"W", u:"g"}, temp: {l:"T", u:"°C"} };

            for (var shotIdx in shotData) {
                var shot = shotData[shotIdx];
                var parts = [];
                ["pressure", "flow", "weight", "temp"].forEach(function(ct) {
                    if (shot.values[ct] !== undefined && visibleCurves[ct]) {
                        parts.push("<span style='color:" + shot.color + "'>" + curveInfo[ct].l + ":</span>" + shot.values[ct].toFixed(1) + curveInfo[ct].u);
                    }
                });
                if (parts.length > 0) {
                    html += "<div style='margin-top:4px;'><span style='display:inline-block;width:10px;height:10px;border-radius:2px;background:" + shot.color + ";margin-right:6px;'></span>" + shot.label + "</div>";
                    html += "<div style='color:#8b949e;margin-left:16px;'>" + parts.join(" &nbsp;") + "</div>";
                }
            }

            tooltipEl.innerHTML = html;
            tooltipEl.style.opacity = 1;

            // Position tooltip near mouse cursor (offset to avoid covering cursor)
            tooltipEl.style.left = (mouseX + 15) + "px";
            tooltipEl.style.top = (mouseY - 10) + "px";
        })HTML" R"HTML(

        var ctx = document.getElementById("compareChart").getContext("2d");
        var chart = new Chart(ctx, {
            type: "line",
            data: {
                datasets: [
                    %3
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                interaction: { mode: "nearest", axis: "x", intersect: false },
                plugins: {
                    legend: { display: false },
                    tooltip: {
                        enabled: false,
                        external: externalTooltip
                    }
                },
                scales: {
                    x: {
                        type: "linear",
                        title: { display: true, text: "Time (s)", color: "#8b949e" },
                        grid: { color: "rgba(48, 54, 61, 0.5)" },
                        ticks: { color: "#8b949e" }
                    },
                    y: {
                        type: "linear",
                        position: "left",
                        title: { display: true, text: "Pressure / Flow", color: "#8b949e" },
                        min: 0, max: 12,
                        grid: { color: "rgba(48, 54, 61, 0.5)" },
                        ticks: { color: "#8b949e" }
                    },
                    y2: {
                        type: "linear",
                        position: "right",
                        title: { display: true, text: "Yield (g)", color: "#a2693d" },
                        min: 0,
                        grid: { display: false },
                        ticks: { color: "#a2693d" }
                    },
                    y3: {
                        type: "linear",
                        position: "right",
                        title: { display: false },
                        min: 80, max: 100,
                        display: false
                    }
                }
            }
        });

        function toggleCurve(curveType, btn) {
            visibleCurves[curveType] = !visibleCurves[curveType];
            btn.classList.toggle("active");

            chart.data.datasets.forEach(function(ds, i) {
                if (ds.curveType === curveType) {
                    chart.getDatasetMeta(i).hidden = !visibleCurves[curveType];
                }
            });
            chart.update();
        }

        function toggleMenu() {
            var menu = document.getElementById("menuDropdown");
            menu.classList.toggle("open");
        }

        document.addEventListener("click", function(e) {
            var menu = document.getElementById("menuDropdown");
            var btn = e.target.closest(".menu-btn");
            if (!btn && menu.classList.contains("open")) {
                menu.classList.remove("open");
            }
        });

        // Power toggle
        var powerState = {awake: false, state: "Unknown"};
        function updatePowerButton() {
            var btn = document.getElementById("powerToggle");
            if (powerState.state === "Unknown" || !powerState.connected) {
                btn.innerHTML = "&#128268; Disconnected";
            } else if (powerState.awake) {
                btn.innerHTML = "&#128164; Put to Sleep";
            } else {
                btn.innerHTML = "&#9889; Wake Up";
            }
        }
        function fetchPowerState() {
            fetch("/api/power/status")
                .then(function(r) { return r.json(); })
                .then(function(data) { powerState = data; updatePowerButton(); })
                .catch(function() {});
        }
        function togglePower() {
            var action = powerState.awake ? "sleep" : "wake";
            fetch("/api/power/" + action)
                .then(function(r) { return r.json(); })
                .then(function() { setTimeout(fetchPowerState, 1000); });
        }
        fetchPowerState();
        setInterval(fetchPowerState, 5000);
    </script>
</body>
</html>
)HTML").arg(shots.size()).arg(legendItems).arg(datasets);
}

QString ShotServer::generateDebugPage() const
{
    return QString(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Debug Log - Decenza DE1</title>
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.5;
        }
        .header {
            background: var(--surface);
            border-bottom: 1px solid var(--border);
            padding: 1rem 1.5rem;
            position: sticky;
            top: 0;
            z-index: 100;
        }
        .header-content {
            max-width: 1400px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            gap: 1rem;
        }
        .back-btn {
            color: var(--text-secondary);
            text-decoration: none;
            font-size: 1.5rem;
        }
        .back-btn:hover { color: var(--accent); }
        h1 { font-size: 1.125rem; font-weight: 600; flex: 1; }
        .status {
            font-size: 0.75rem;
            color: var(--text-secondary);
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }
        .status-dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background: #18c37e;
            animation: pulse 2s infinite;
        }
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.5; }
        }
        .controls {
            display: flex;
            gap: 0.5rem;
        }
        .btn {
            padding: 0.5rem 1rem;
            border: 1px solid var(--border);
            border-radius: 6px;
            background: transparent;
            color: var(--text);
            cursor: pointer;
            font-size: 0.875rem;
        }
        .btn:hover { border-color: var(--accent); color: var(--accent); }
        .btn.active { background: var(--accent); color: var(--bg); border-color: var(--accent); }
        .container {
            max-width: 1400px;
            margin: 0 auto;
            padding: 1rem;
        }
        .log-container {
            background: #000;
            border: 1px solid var(--border);
            border-radius: 8px;
            height: calc(100vh - 120px);
            overflow-y: auto;
            font-family: "Consolas", "Monaco", "Courier New", monospace;
            font-size: 12px;
            padding: 0.5rem;
        }
        .log-line {
            white-space: pre;
            padding: 1px 0;
        }
        .log-line:hover { background: rgba(255,255,255,0.05); }
        .DEBUG { color: #8b949e; }
        .INFO { color: #58a6ff; }
        .WARN { color: #d29922; }
        .ERROR { color: #f85149; }
        .FATAL { color: #ff0000; font-weight: bold; }
    </style>
</head>
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&#8592;</a>
            <h1>Live Debug Log</h1>
            <div class="status">
                <span class="status-dot"></span>
                <span id="lineCount">0 lines</span>
            </div>
            <div class="controls">
                <button class="btn active" id="autoScrollBtn" onclick="toggleAutoScroll()">Auto-scroll</button>
                <button class="btn" onclick="clearLog()">Clear</button>
            </div>
        </div>
    </header>
    <main class="container">
        <div class="log-container" id="logContainer"></div>
    </main>
    <script>
        var lastIndex = 0;
        var autoScroll = true;
        var container = document.getElementById("logContainer");
        var lineCountEl = document.getElementById("lineCount");

        function colorize(line) {
            var category = "";
            if (line.includes("] DEBUG ")) category = "DEBUG";
            else if (line.includes("] INFO ")) category = "INFO";
            else if (line.includes("] WARN ")) category = "WARN";
            else if (line.includes("] ERROR ")) category = "ERROR";
            else if (line.includes("] FATAL ")) category = "FATAL";
            return "<div class=\"log-line " + category + "\">" + escapeHtml(line) + "</div>";
        }

        function escapeHtml(text) {
            var div = document.createElement("div");
            div.textContent = text;
            return div.innerHTML;
        }

        function fetchLogs() {
            fetch("/api/debug?after=" + lastIndex)
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    if (data.lines && data.lines.length > 0) {
                        var html = "";
                        for (var i = 0; i < data.lines.length; i++) {
                            html += colorize(data.lines[i]);
                        }
                        container.insertAdjacentHTML("beforeend", html);
                        if (autoScroll) {
                            container.scrollTop = container.scrollHeight;
                        }
                    }
                    lastIndex = data.lastIndex;
                    lineCountEl.textContent = lastIndex + " lines";
                });
        }

        function toggleAutoScroll() {
            autoScroll = !autoScroll;
            document.getElementById("autoScrollBtn").classList.toggle("active", autoScroll);
            if (autoScroll) {
                container.scrollTop = container.scrollHeight;
            }
        }

        function clearLog() {
            fetch("/api/debug/clear", { method: "POST" })
                .then(function() {
                    container.innerHTML = "";
                    lastIndex = 0;
                });
        }

        // Poll every 500ms
        setInterval(fetchLogs, 500);
        fetchLogs();
    </script>
</body>
</html>
)HTML");
}

QString ShotServer::generateUploadPage() const
{
    return QString(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Upload APK - Decenza DE1</title>
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
            --success: #18c37e;
            --error: #f85149;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.5;
        }
        .header {
            background: var(--surface);
            border-bottom: 1px solid var(--border);
            padding: 1rem 1.5rem;
        }
        .header-content {
            max-width: 800px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            gap: 1rem;
        }
        .back-btn {
            color: var(--text-secondary);
            text-decoration: none;
            font-size: 1.5rem;
        }
        .back-btn:hover { color: var(--accent); }
        h1 { font-size: 1.125rem; font-weight: 600; }
        .container {
            max-width: 800px;
            margin: 0 auto;
            padding: 2rem 1.5rem;
        }
        .upload-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 2rem;
        }
        .upload-zone {
            border: 2px dashed var(--border);
            border-radius: 8px;
            padding: 3rem 2rem;
            text-align: center;
            cursor: pointer;
            transition: all 0.2s;
        }
        .upload-zone:hover, .upload-zone.dragover {
            border-color: var(--accent);
            background: rgba(201, 162, 39, 0.05);
        }
        .upload-zone.uploading {
            border-color: var(--text-secondary);
            cursor: default;
        }
        .upload-icon {
            font-size: 3rem;
            margin-bottom: 1rem;
        }
        .upload-text {
            color: var(--text-secondary);
            margin-bottom: 0.5rem;
        }
        .upload-hint {
            color: var(--text-secondary);
            font-size: 0.875rem;
        }
        input[type="file"] { display: none; }
        .progress-bar {
            display: none;
            height: 8px;
            background: var(--border);
            border-radius: 4px;
            margin-top: 1.5rem;
            overflow: hidden;
        }
        .progress-fill {
            height: 100%;
            background: var(--accent);
            width: 0%;
            transition: width 0.3s;
        }
        .status-message {
            margin-top: 1rem;
            padding: 1rem;
            border-radius: 8px;
            display: none;
        }
        .status-message.success {
            display: block;
            background: rgba(24, 195, 126, 0.1);
            border: 1px solid var(--success);
            color: var(--success);
        }
        .status-message.error {
            display: block;
            background: rgba(248, 81, 73, 0.1);
            border: 1px solid var(--error);
            color: var(--error);
        }
        .file-info {
            margin-top: 1rem;
            padding: 1rem;
            background: var(--bg);
            border-radius: 8px;
            display: none;
        }
        .file-name {
            font-weight: 600;
            margin-bottom: 0.25rem;
        }
        .file-size {
            color: var(--text-secondary);
            font-size: 0.875rem;
        }
        .warning {
            margin-top: 1.5rem;
            padding: 1rem;
            background: rgba(210, 153, 34, 0.1);
            border: 1px solid #d29922;
            border-radius: 8px;
            color: #d29922;
            font-size: 0.875rem;
        }
    </style>
</head>
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&#8592;</a>
            <h1>Upload APK</h1>
        </div>
    </header>
    <main class="container">
        <div class="upload-card">
            <div class="upload-zone" id="uploadZone" onclick="document.getElementById('fileInput').click()">
                <div class="upload-icon">&#128230;</div>
                <div class="upload-text">Click or drag APK file here</div>
                <div class="upload-hint">Decenza_DE1_*.apk</div>
            </div>
            <input type="file" id="fileInput" accept=".apk" onchange="handleFile(this.files[0])">
            <div class="file-info" id="fileInfo">
                <div class="file-name" id="fileName"></div>
                <div class="file-size" id="fileSize"></div>
            </div>
            <div class="progress-bar" id="progressBar">
                <div class="progress-fill" id="progressFill"></div>
            </div>
            <div class="status-message" id="statusMessage"></div>
            <div class="warning">
                &#9888; After upload completes, Android will prompt to install the APK.
                The current app will close during installation.
            </div>
        </div>
    </main>
    <script>
        var uploadZone = document.getElementById("uploadZone");
        var fileInfo = document.getElementById("fileInfo");
        var progressBar = document.getElementById("progressBar");
        var progressFill = document.getElementById("progressFill");
        var statusMessage = document.getElementById("statusMessage");

        uploadZone.addEventListener("dragover", function(e) {
            e.preventDefault();
            uploadZone.classList.add("dragover");
        });
        uploadZone.addEventListener("dragleave", function(e) {
            e.preventDefault();
            uploadZone.classList.remove("dragover");
        });
        uploadZone.addEventListener("drop", function(e) {
            e.preventDefault();
            uploadZone.classList.remove("dragover");
            if (e.dataTransfer.files.length > 0) {
                handleFile(e.dataTransfer.files[0]);
            }
        });

        function formatSize(bytes) {
            if (bytes < 1024) return bytes + " B";
            if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
            return (bytes / (1024 * 1024)).toFixed(1) + " MB";
        }

        function handleFile(file) {
            if (!file) return;
            if (!file.name.endsWith(".apk")) {
                showStatus("error", "Please select an APK file");
                return;
            }

            document.getElementById("fileName").textContent = file.name;
            document.getElementById("fileSize").textContent = formatSize(file.size);
            fileInfo.style.display = "block";

            uploadFile(file);
        }

        function uploadFile(file) {
            uploadZone.classList.add("uploading");
            progressBar.style.display = "block";
            progressFill.style.width = "0%";
            statusMessage.className = "status-message";
            statusMessage.style.display = "none";

            var xhr = new XMLHttpRequest();
            xhr.open("POST", "/upload", true);

            xhr.upload.onprogress = function(e) {
                if (e.lengthComputable) {
                    var pct = (e.loaded / e.total) * 100;
                    progressFill.style.width = pct + "%";
                }
            };

            xhr.onload = function() {
                uploadZone.classList.remove("uploading");
                if (xhr.status === 200) {
                    showStatus("success", "Upload complete! Installing...");
                } else {
                    showStatus("error", "Upload failed: " + xhr.responseText);
                }
            };

            xhr.onerror = function() {
                uploadZone.classList.remove("uploading");
                showStatus("error", "Network error during upload");
            };

            xhr.setRequestHeader("Content-Type", "application/octet-stream");
            xhr.setRequestHeader("X-Filename", file.name);
            xhr.send(file);
        }

        function showStatus(type, message) {
            statusMessage.className = "status-message " + type;
            statusMessage.textContent = message;
            statusMessage.style.display = "block";
        }
    </script>
</body>
</html>
)HTML");
}

void ShotServer::handleUpload(QTcpSocket* socket, const QByteArray& request)
{
    // Parse headers to get filename and content
    int headerEnd = request.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        sendResponse(socket, 400, "text/plain", "Invalid request");
        return;
    }

    QString headers = QString::fromUtf8(request.left(headerEnd));
    QByteArray body = request.mid(headerEnd + 4);

    // Get filename from X-Filename header
    QString filename = "uploaded.apk";
    for (const QString& line : headers.split("\r\n")) {
        if (line.startsWith("X-Filename:", Qt::CaseInsensitive)) {
            filename = line.mid(11).trimmed();
            break;
        }
    }

    if (!filename.endsWith(".apk", Qt::CaseInsensitive)) {
        sendResponse(socket, 400, "text/plain", "Only APK files are allowed");
        return;
    }

    // Save to cache/downloads directory
    QString savePath;
#ifdef Q_OS_ANDROID
    savePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
#else
    savePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
#endif

    QDir().mkpath(savePath);
    QString fullPath = savePath + "/" + filename;

    QFile file(fullPath);
    if (!file.open(QIODevice::WriteOnly)) {
        sendResponse(socket, 500, "text/plain", "Failed to save file: " + file.errorString().toUtf8());
        return;
    }

    file.write(body);
    file.close();

    qDebug() << "APK uploaded:" << fullPath << "size:" << body.size();

    // Trigger installation on Android
    installApk(fullPath);

    sendResponse(socket, 200, "text/plain", "Upload complete: " + fullPath.toUtf8());
}

void ShotServer::installApk(const QString& apkPath)
{
#ifdef Q_OS_ANDROID
    qDebug() << "Installing APK:" << apkPath;

    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;");

    if (!activity.isValid()) {
        qWarning() << "Failed to get Android activity";
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

    qDebug() << "APK install intent launched";
#else
    qDebug() << "APK installation only supported on Android. File saved to:" << apkPath;
#endif
}

// ============================================================================
// Personal Media Upload
// ============================================================================

QString ShotServer::generateMediaUploadPage() const
{
    // Get current personal media list for display
    QString mediaListHtml;
    if (m_screensaverManager) {
        QVariantList media = m_screensaverManager->getPersonalMediaList();
        if (!media.isEmpty()) {
            mediaListHtml = R"HTML(
            <div class="media-list">
                <h3>Current Personal Media</h3>
                <div class="media-grid">)HTML";

            for (const QVariant& v : std::as_const(media)) {
                QVariantMap m = v.toMap();
                QString type = m["type"].toString();
                QString filename = m["filename"].toString();
                qint64 bytes = m["bytes"].toLongLong();
                int id = m["id"].toInt();
                QString sizeStr = bytes < 1024*1024
                    ? QString::number(bytes/1024) + " KB"
                    : QString::number(bytes/(1024*1024)) + " MB";

                mediaListHtml += QString(R"HTML(
                    <div class="media-item" data-id="%1">
                        <div class="media-icon">%2</div>
                        <div class="media-info">
                            <div class="media-name">%3</div>
                            <div class="media-size">%4</div>
                        </div>
                        <button class="delete-btn" onclick="deleteMedia(%1)">&#128465;</button>
                    </div>)HTML")
                    .arg(id)
                    .arg(type == "video" ? "&#127909;" : "&#128247;")
                    .arg(filename.toHtmlEscaped())
                    .arg(sizeStr);
            }

            mediaListHtml += QString(R"HTML(
                </div>
                <button class="delete-all-btn" onclick="deleteAllMedia(%1)">Delete All (%1 items)</button>
            </div>)HTML").arg(media.size());
        }
    }

    // Build HTML in chunks to avoid MSVC string literal size limit
    QString html;

    // Part 1: Head and CSS variables
    html += R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Upload Screensaver Media - Decenza DE1</title>
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
            --success: #18c37e;
            --error: #f85149;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.5;
        }
)HTML";

    // Part 2: More CSS
    html += R"HTML(
        .header {
            background: var(--surface);
            border-bottom: 1px solid var(--border);
            padding: 1rem 1.5rem;
        }
        .header-content {
            max-width: 800px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            gap: 1rem;
        }
        .back-btn {
            color: var(--text-secondary);
            text-decoration: none;
            font-size: 1.5rem;
        }
        .back-btn:hover { color: var(--accent); }
        h1 { font-size: 1.125rem; font-weight: 600; }
        .container {
            max-width: 800px;
            margin: 0 auto;
            padding: 2rem 1.5rem;
        }
        .upload-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 2rem;
            margin-bottom: 1.5rem;
        }
)HTML";

    // Part 3: Upload zone CSS
    html += R"HTML(
        .upload-zone {
            border: 2px dashed var(--border);
            border-radius: 8px;
            padding: 3rem 2rem;
            text-align: center;
            cursor: pointer;
            transition: all 0.2s;
        }
        .upload-zone:hover, .upload-zone.dragover {
            border-color: var(--accent);
            background: rgba(201, 162, 39, 0.05);
        }
        .upload-zone.uploading {
            border-color: var(--text-secondary);
            cursor: default;
        }
        .upload-icon { font-size: 3rem; margin-bottom: 1rem; }
        .upload-text { color: var(--text-secondary); margin-bottom: 0.5rem; }
        .upload-hint { color: var(--text-secondary); font-size: 0.875rem; }
        input[type="file"] { display: none; }
)HTML";

    // Part 4: Progress and status CSS
    html += R"HTML(
        .progress-bar {
            display: none;
            height: 8px;
            background: var(--border);
            border-radius: 4px;
            margin-top: 1.5rem;
            overflow: hidden;
        }
        .progress-fill {
            height: 100%;
            background: var(--accent);
            width: 0%;
            transition: width 0.3s;
        }
        .status-message {
            margin-top: 1rem;
            padding: 1rem;
            border-radius: 8px;
            display: none;
        }
        .status-message.success {
            display: block;
            background: rgba(24, 195, 126, 0.1);
            border: 1px solid var(--success);
            color: var(--success);
        }
        .status-message.error {
            display: block;
            background: rgba(248, 81, 73, 0.1);
            border: 1px solid var(--error);
            color: var(--error);
        }
        .status-message.processing {
            display: block;
            background: rgba(201, 162, 39, 0.1);
            border: 1px solid var(--accent);
            color: var(--accent);
        }
)HTML";

    // Part 5: File info and media list CSS
    html += R"HTML(
        .file-info {
            margin-top: 1rem;
            padding: 1rem;
            background: var(--bg);
            border-radius: 8px;
            display: none;
        }
        .file-name { font-weight: 600; margin-bottom: 0.25rem; }
        .file-size { color: var(--text-secondary); font-size: 0.875rem; }
        .info-box {
            margin-top: 1.5rem;
            padding: 1rem;
            background: rgba(201, 162, 39, 0.1);
            border: 1px solid var(--accent);
            border-radius: 8px;
            font-size: 0.875rem;
        }
        .info-box h4 { margin-bottom: 0.5rem; color: var(--accent); }
        .info-box ul { margin-left: 1.25rem; color: var(--text-secondary); }
        .media-list {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1.5rem;
        }
        .media-list h3 { margin-bottom: 1rem; font-size: 1rem; }
        .media-grid { display: flex; flex-direction: column; gap: 0.75rem; }
)HTML";

    // Part 6: Media item CSS
    html += R"HTML(
        .media-item {
            display: flex;
            align-items: center;
            gap: 1rem;
            padding: 0.75rem;
            background: var(--bg);
            border-radius: 8px;
        }
        .media-icon { font-size: 1.5rem; }
        .media-info { flex: 1; min-width: 0; }
        .media-name { font-weight: 500; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
        .media-size { color: var(--text-secondary); font-size: 0.75rem; }
        .delete-btn {
            background: transparent;
            border: none;
            color: var(--text-secondary);
            font-size: 1.25rem;
            cursor: pointer;
            padding: 0.5rem;
            border-radius: 4px;
        }
        .delete-btn:hover { background: rgba(248, 81, 73, 0.2); color: var(--error); }
        .delete-all-btn {
            margin-top: 1rem;
            padding: 0.75rem 1.5rem;
            background: var(--error);
            color: white;
            border: none;
            border-radius: 8px;
            cursor: pointer;
            font-size: 0.9rem;
        }
        .delete-all-btn:hover { background: #c93c37; }
    </style>
</head>
)HTML";

    // Part 7: Body and upload form
    html += R"HTML(<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&#8592;</a>
            <h1>Upload Screensaver Media</h1>
        </div>
    </header>
    <main class="container">
        <div class="upload-card">
            <div class="upload-zone" id="uploadZone" onclick="document.getElementById('fileInput').click()">
                <div class="upload-icon">&#127912;</div>
                <div class="upload-text">Click or drag media files here</div>
                <div class="upload-hint">JPG, PNG, GIF, WebP, MP4, WebM</div>
            </div>
            <input type="file" id="fileInput" accept=".jpg,.jpeg,.png,.gif,.webp,.mp4,.webm,.mov" multiple onchange="handleFiles(this.files)">
            <div class="file-info" id="fileInfo">
                <div class="file-name" id="fileName"></div>
                <div class="file-size" id="fileSize"></div>
            </div>
            <div class="progress-bar" id="progressBar">
                <div class="progress-fill" id="progressFill"></div>
            </div>
            <div class="status-message" id="statusMessage"></div>
            <div class="info-box">
                <h4>Processing</h4>
                <ul>
                    <li><b>Images</b> - Resized in browser (no tools needed)</li>
                    <li><b>Videos</b> - Resized on server (requires FFmpeg)</li>
                    <li><b>Photo dates</b> - Best results with exiftool</li>
                </ul>
                <details style="margin-top:0.75rem">
                    <summary style="cursor:pointer;color:var(--accent)">Windows install commands (for videos)</summary>
                    <pre style="background:var(--bg);padding:0.5rem;margin-top:0.5rem;border-radius:4px;font-size:0.75rem;overflow-x:auto">winget install Gyan.FFmpeg
winget install OliverBetz.ExifTool</pre>
                </details>
            </div>
        </div>
)HTML";

    // Insert media list HTML
    html += mediaListHtml;

    // Part 8: Script - event listeners
    html += R"HTML(
    </main>
    <script>
        var uploadZone = document.getElementById("uploadZone");
        var fileInfo = document.getElementById("fileInfo");
        var progressBar = document.getElementById("progressBar");
        var progressFill = document.getElementById("progressFill");
        var statusMessage = document.getElementById("statusMessage");

        uploadZone.addEventListener("dragover", function(e) {
            e.preventDefault();
            uploadZone.classList.add("dragover");
        });
        uploadZone.addEventListener("dragleave", function(e) {
            e.preventDefault();
            uploadZone.classList.remove("dragover");
        });
        uploadZone.addEventListener("drop", function(e) {
            e.preventDefault();
            uploadZone.classList.remove("dragover");
            if (e.dataTransfer.files.length > 0) {
                handleFiles(e.dataTransfer.files);
            }
        });
)HTML";

    // Part 9: Script - utility functions
    html += R"HTML(
        function formatSize(bytes) {
            if (bytes < 1024) return bytes + " B";
            if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
            return (bytes / (1024 * 1024)).toFixed(1) + " MB";
        }

        var uploadQueue = [];
        var isUploading = false;
        var totalFiles = 0;
        var completedFiles = 0;
        var skippedFiles = [];
        var failedFiles = [];

        function handleFiles(files) {
            // Reset counters for new batch
            if (!isUploading) {
                totalFiles = 0;
                completedFiles = 0;
                skippedFiles = [];
                failedFiles = [];
            }
            for (var i = 0; i < files.length; i++) {
                var file = files[i];
                var ext = file.name.split('.').pop().toLowerCase();
                var validExts = ['jpg','jpeg','png','gif','webp','mp4','webm','mov'];
                if (validExts.indexOf(ext) === -1) {
                    showStatus("error", "Unsupported file type: " + file.name);
                    continue;
                }
                uploadQueue.push(file);
                totalFiles++;
            }
            processQueue();
        }

        function processQueue() {
            if (isUploading || uploadQueue.length === 0) {
                // All done - check if we should reload
                var processed = completedFiles + skippedFiles.length + failedFiles.length;
                if (!isUploading && totalFiles > 0 && processed === totalFiles) {
                    var msg = "Uploaded: " + completedFiles;
                    if (skippedFiles.length > 0) msg += ", Skipped: " + skippedFiles.length;
                    if (failedFiles.length > 0) {
                        msg += ", Failed: " + failedFiles.length;
                        // Show details for first few failed files
                        var details = failedFiles.slice(0, 3).map(function(f) {
                            return f.name + " (" + f.error + ")";
                        }).join("; ");
                        if (failedFiles.length > 3) details += "...";
                        msg += " - " + details;
                    }
                    if (failedFiles.length > 0) {
                        showStatus("error", msg);
                        // Still reload after delay to show successfully uploaded files
                        if (completedFiles > 0) {
                            setTimeout(function() { location.reload(); }, 5000);
                        }
                    } else {
                        showStatus("success", msg);
                        if (completedFiles > 0) {
                            setTimeout(function() { location.reload(); }, 1500);
                        }
                    }
                }
                return;
            }
            isUploading = true;
            var file = uploadQueue.shift();
            uploadFile(file);
        }
)HTML";

    // Part 10: Script - upload function
    html += R"HTML(
        function resizeImageInBrowser(file, maxWidth, maxHeight, callback) {
            var img = new Image();
            img.onload = function() {
                // Create canvas at exact target size
                var canvas = document.createElement("canvas");
                canvas.width = maxWidth;
                canvas.height = maxHeight;
                var ctx = canvas.getContext("2d");

                // Fill with black background
                ctx.fillStyle = "black";
                ctx.fillRect(0, 0, maxWidth, maxHeight);

                // Scale image to fit within bounds (letterbox/pillarbox)
                var scale = Math.min(maxWidth / img.width, maxHeight / img.height, 1);
                var scaledWidth = Math.round(img.width * scale);
                var scaledHeight = Math.round(img.height * scale);

                // Center the image on the canvas
                var x = Math.round((maxWidth - scaledWidth) / 2);
                var y = Math.round((maxHeight - scaledHeight) / 2);

                ctx.drawImage(img, x, y, scaledWidth, scaledHeight);
                canvas.toBlob(function(blob) {
                    callback(blob);
                }, "image/jpeg", 0.85);
            };
            img.onerror = function() { callback(null); };
            img.src = URL.createObjectURL(file);
        }

        function uploadFile(file) {
            var currentNum = completedFiles + failedFiles.length + 1;
            var statusText = totalFiles > 1 ? " (" + currentNum + "/" + totalFiles + ")" : "";

            document.getElementById("fileName").textContent = file.name + statusText;
            document.getElementById("fileSize").textContent = formatSize(file.size);
            fileInfo.style.display = "block";

            uploadZone.classList.add("uploading");
            progressBar.style.display = "block";
            progressFill.style.width = "0%";

            var ext = file.name.split(".").pop().toLowerCase();
            var isStandardImage = ["jpg","jpeg","png","gif","webp"].indexOf(ext) >= 0;

            if (isStandardImage) {
                showStatus("processing", "Resizing" + statusText + "...");
                resizeImageInBrowser(file, 1280, 800, function(resizedBlob) {
                    if (resizedBlob) {
                        showStatus("processing", "Uploading" + statusText + " (" + formatSize(resizedBlob.size) + ")...");
                        doUpload(file.name.replace(/\.[^.]+$/, ".jpg"), resizedBlob);
                    } else {
                        showStatus("processing", "Uploading" + statusText + " (resize failed, sending original)...");
                        doUpload(file.name, file);
                    }
                });
            } else {
                showStatus("processing", "Uploading" + statusText + "... (server will process)");
                doUpload(file.name, file);
            }
        }

        function doUpload(filename, blob, retryCount) {
            retryCount = retryCount || 0;
            var xhr = new XMLHttpRequest();
            xhr.open("POST", "/upload/media", true);
            xhr.timeout = 600000;  // 10 minute timeout for large files

            xhr.upload.onprogress = function(e) {
                if (e.lengthComputable) {
                    var pct = (e.loaded / e.total) * 100;
                    progressFill.style.width = pct + "%";
                }
            };

            xhr.onload = function() {
                uploadZone.classList.remove("uploading");
                isUploading = false;
                if (xhr.status === 200) {
                    completedFiles++;
                    showStatus("success", "Uploaded: " + filename + (uploadQueue.length > 0 ? " - continuing..." : ""));
                    processQueue();
                } else if (xhr.status === 409) {
                    skippedFiles.push(filename);
                    showStatus("processing", "Skipped (exists): " + filename + (uploadQueue.length > 0 ? " - continuing..." : ""));
                    processQueue();
                } else if (xhr.status === 413) {
                    // File too large
                    failedFiles.push({name: filename, error: "File too large (max 500MB)"});
                    showStatus("error", "Skipped: " + filename + " - file too large (max 500MB)");
                    processQueue();
                } else if (xhr.status === 503 && retryCount < 3) {
                    // Server busy - retry after delay
                    showStatus("processing", "Server busy, retrying " + filename + " in 5s... (attempt " + (retryCount + 2) + "/4)");
                    setTimeout(function() {
                        doUpload(filename, blob, retryCount + 1);
                    }, 5000);
                } else {
                    failedFiles.push({name: filename, error: xhr.responseText || "Unknown error"});
                    showStatus("error", "Failed: " + filename + " - " + (xhr.responseText || "Server error"));
                    processQueue();
                }
            };

            xhr.onerror = function() {
                uploadZone.classList.remove("uploading");
                isUploading = false;
                if (retryCount < 2) {
                    // Network error - retry once
                    showStatus("processing", "Connection lost, retrying " + filename + "...");
                    setTimeout(function() {
                        doUpload(filename, blob, retryCount + 1);
                    }, 2000);
                } else {
                    failedFiles.push({name: filename, error: "Network error"});
                    showStatus("error", "Network error: " + filename + " (check connection)");
                    processQueue();
                }
            };

            xhr.ontimeout = function() {
                uploadZone.classList.remove("uploading");
                isUploading = false;
                failedFiles.push({name: filename, error: "Upload timed out"});
                showStatus("error", "Timeout: " + filename + " - upload took too long");
                processQueue();
            };

            xhr.setRequestHeader("Content-Type", "application/octet-stream");
            xhr.setRequestHeader("X-Filename", encodeURIComponent(filename));
            xhr.send(blob);
        }
)HTML";

    // Part 11: Script - status and delete functions
    html += R"HTML(
        function showStatus(type, message) {
            statusMessage.className = "status-message " + type;
            statusMessage.textContent = message;
            statusMessage.style.display = "block";
        }

        function deleteMedia(id) {
            if (!confirm("Delete this media?")) return;
            var xhr = new XMLHttpRequest();
            xhr.open("DELETE", "/api/media/personal/" + id, true);
            xhr.onload = function() {
                if (xhr.status === 200) {
                    location.reload();
                } else {
                    alert("Failed to delete media");
                }
            };
            xhr.send();
        }

        function deleteAllMedia(count) {
            if (!confirm("Delete all " + count + " personal media items? This cannot be undone.")) return;
            var xhr = new XMLHttpRequest();
            xhr.open("DELETE", "/api/media/personal", true);
            xhr.onload = function() {
                if (xhr.status === 200) {
                    location.reload();
                } else {
                    alert("Failed to delete media: " + xhr.responseText);
                }
            };
            xhr.send();
        }
    </script>
</body>
</html>
)HTML";

    return html;
}

void ShotServer::handleMediaUpload(QTcpSocket* socket, const QString& uploadedTempPath, const QString& headers)
{
    // Ensure temp file cleanup on any exit path
    QString tempPathToCleanup = uploadedTempPath;
    auto cleanupTempFile = [&tempPathToCleanup]() {
        if (!tempPathToCleanup.isEmpty() && QFile::exists(tempPathToCleanup)) {
            QFile::remove(tempPathToCleanup);
        }
    };

    try {
        if (!m_screensaverManager) {
            sendResponse(socket, 500, "text/plain", "Screensaver manager not available");
            cleanupTempFile();
            return;
        }

        // Get filename from X-Filename header (URL-encoded)
        QString filename = "uploaded_media";
        for (const QString& line : headers.split("\r\n")) {
            if (line.startsWith("X-Filename:", Qt::CaseInsensitive)) {
                filename = QUrl::fromPercentEncoding(line.mid(11).trimmed().toUtf8());
                break;
            }
        }

        // Validate file type
        QString ext = QFileInfo(filename).suffix().toLower();
        bool isImage = (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "gif" || ext == "webp");
        bool isVideo = (ext == "mp4" || ext == "webm" || ext == "mov");

        if (!isImage && !isVideo) {
            sendResponse(socket, 400, "text/plain", "Unsupported file type. Use JPG, PNG, GIF, WebP, MP4, or WebM.");
            cleanupTempFile();
            return;
        }

        // Check for duplicate before doing expensive resize work
        if (m_screensaverManager->hasPersonalMediaWithName(filename)) {
            sendResponse(socket, 409, "text/plain", "File already exists: " + filename.toUtf8());
            cleanupTempFile();
            return;
        }

        // Rename the streamed temp file to have proper extension
        QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QString tempPath = tempDir + "/upload_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + "." + ext;

        if (!QFile::rename(uploadedTempPath, tempPath)) {
            // If rename fails (cross-device?), try copy
            if (!QFile::copy(uploadedTempPath, tempPath)) {
                sendResponse(socket, 500, "text/plain", "Failed to process uploaded file");
                cleanupTempFile();
                return;
            }
            QFile::remove(uploadedTempPath);
        }
        tempPathToCleanup = tempPath;  // Update cleanup path

        qDebug() << "Media uploaded to temp:" << tempPath << "size:" << QFileInfo(tempPath).size() << "bytes";

        // Extract date from original file BEFORE resizing (resize strips EXIF)
        QDateTime mediaDate;
        if (isImage) {
            mediaDate = extractImageDate(tempPath);
        } else if (isVideo) {
            mediaDate = extractVideoDate(tempPath);
        }

        // Resize the media
        QString outputPath = tempDir + "/resized_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + "." + ext;

        // Target resolution matches shared screensaver media (1280x800)
        const int targetWidth = 1280;
        const int targetHeight = 800;

        if (isImage) {
            if (resizeImage(tempPath, outputPath, targetWidth, targetHeight)) {
                QFile::remove(tempPath);
                tempPathToCleanup.clear();  // Successfully processed
                qDebug() << "Image resized successfully:" << outputPath;
            } else {
                // Use original if resize fails
                outputPath = tempPath;
                tempPathToCleanup.clear();  // Will use original, don't delete
                qDebug() << "Image resize failed, using original";
            }
        } else if (isVideo) {
            if (resizeVideo(tempPath, outputPath, targetWidth, targetHeight)) {
                QFile::remove(tempPath);
                tempPathToCleanup.clear();  // Successfully processed
                qDebug() << "Video resized successfully:" << outputPath;
            } else {
                // Use original if resize fails
                outputPath = tempPath;
                tempPathToCleanup.clear();  // Will use original, don't delete
                qDebug() << "Video resize not available or failed, using original";
            }
        }

        // Add to screensaver personal media with extracted date
        if (m_screensaverManager->addPersonalMedia(outputPath, filename, mediaDate)) {
            sendResponse(socket, 200, "text/plain", "Media uploaded successfully");
        } else {
            QFile::remove(outputPath);
            sendResponse(socket, 500, "text/plain", "Failed to add media to screensaver");
        }

    } catch (const std::exception& e) {
        qWarning() << "ShotServer: Exception in handleMediaUpload:" << e.what();
        cleanupTempFile();
        sendResponse(socket, 500, "text/plain", QString("Server error: %1").arg(e.what()).toUtf8());
    } catch (...) {
        qWarning() << "ShotServer: Unknown exception in handleMediaUpload";
        cleanupTempFile();
        sendResponse(socket, 500, "text/plain", "Server error: unexpected exception");
    }
}

bool ShotServer::resizeImage(const QString& inputPath, const QString& outputPath, int maxWidth, int maxHeight)
{
    try {
        QImage image(inputPath);
        if (image.isNull()) {
            qWarning() << "Failed to load image:" << inputPath;
            return false;
        }

        // Scale maintaining aspect ratio (fit within bounds)
        QImage scaled = image.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (scaled.isNull()) {
            qWarning() << "Failed to scale image (memory?):" << inputPath;
            return false;
        }

        // Create target-sized canvas with black background (letterbox/pillarbox)
        QImage result(maxWidth, maxHeight, QImage::Format_RGB32);
        result.fill(Qt::black);

        // Center the scaled image on the canvas using QPainter
        int x = (maxWidth - scaled.width()) / 2;
        int y = (maxHeight - scaled.height()) / 2;

        QPainter painter(&result);
        painter.drawImage(x, y, scaled);
        painter.end();

        // Save with good quality
        QString ext = QFileInfo(outputPath).suffix().toLower();
        if (ext == "jpg" || ext == "jpeg") {
            return result.save(outputPath, "JPEG", 85);
        } else if (ext == "png") {
            return result.save(outputPath, "PNG");
        } else {
            return result.save(outputPath);
        }
    } catch (const std::exception& e) {
        qWarning() << "Exception in resizeImage:" << e.what();
        return false;
    } catch (...) {
        qWarning() << "Unknown exception in resizeImage";
        return false;
    }
}

bool ShotServer::resizeVideo(const QString& inputPath, const QString& outputPath, int maxWidth, int maxHeight)
{
    // Use FFmpeg for video resizing
    // FFmpeg command: ffmpeg -i input.mp4 -vf "scale='min(1920,iw)':min'(1080,ih)':force_original_aspect_ratio=decrease" -c:a copy output.mp4

    QString ffmpegPath = "ffmpeg";  // Assume it's in PATH

#ifdef Q_OS_WIN
    // On Windows, try common locations if not in PATH
    QStringList possiblePaths = {
        "ffmpeg",
        "C:/ffmpeg/bin/ffmpeg.exe",
        "C:/Program Files/ffmpeg/bin/ffmpeg.exe",
        QCoreApplication::applicationDirPath() + "/ffmpeg.exe"
    };

    for (const QString& path : possiblePaths) {
        if (QFile::exists(path) || path == "ffmpeg") {
            ffmpegPath = path;
            break;
        }
    }
#endif

    // Build filter chain: scale to fit, then pad with black to exact size (letterbox/pillarbox)
    QString filterChain = QString(
        "scale='min(%1,iw)':'min(%2,ih)':force_original_aspect_ratio=decrease,"
        "pad=%1:%2:(ow-iw)/2:(oh-ih)/2:black"
    ).arg(maxWidth).arg(maxHeight);

    QStringList args;
    args << "-y"  // Overwrite output
         << "-i" << inputPath
         << "-vf" << filterChain
         << "-c:v" << "libx264"
         << "-preset" << "fast"
         << "-crf" << "23"
         << "-c:a" << "aac"
         << "-b:a" << "128k"
         << outputPath;

    qDebug() << "Running FFmpeg:" << ffmpegPath << args.join(" ");

    QProcess process;
    process.start(ffmpegPath, args);

    if (!process.waitForStarted(5000)) {
        qWarning() << "FFmpeg failed to start. Is it installed?";
        return false;
    }

    // Wait up to 5 minutes for video processing
    if (!process.waitForFinished(300000)) {
        qWarning() << "FFmpeg timeout";
        process.kill();
        return false;
    }

    if (process.exitCode() != 0) {
        qWarning() << "FFmpeg error:" << process.readAllStandardError();
        return false;
    }

    qDebug() << "FFmpeg completed successfully";
    return QFile::exists(outputPath);
}

QDateTime ShotServer::extractDateWithExiftool(const QString& filePath) const
{
    // Use exiftool for robust date extraction from any format
    QString exiftoolPath = "exiftool";

#ifdef Q_OS_WIN
    QStringList possiblePaths = {
        "exiftool",
        "C:/exiftool/exiftool.exe",
        "C:/Program Files/exiftool/exiftool.exe",
        QCoreApplication::applicationDirPath() + "/exiftool.exe"
    };
    for (const QString& path : possiblePaths) {
        if (QFile::exists(path) || path == "exiftool") {
            exiftoolPath = path;
            break;
        }
    }
#endif

    QStringList args;
    args << "-DateTimeOriginal" << "-CreateDate" << "-s3" << "-d" << "%Y-%m-%d %H:%M:%S" << filePath;

    QProcess process;
    process.start(exiftoolPath, args);

    if (!process.waitForStarted(3000) || !process.waitForFinished(10000)) {
        return QDateTime();
    }

    QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (!output.isEmpty()) {
        // Take first non-empty line (DateTimeOriginal preferred)
        QString dateStr = output.split('\n').first().trimmed();
        QDateTime dt = QDateTime::fromString(dateStr, "yyyy-MM-dd HH:mm:ss");
        if (dt.isValid()) {
            qDebug() << "Exiftool extracted date:" << dt;
            return dt;
        }
    }

    return QDateTime();
}

QDateTime ShotServer::extractImageDate(const QString& imagePath) const
{
    // Try exiftool first (handles all formats including RAW/HEIC)
    QDateTime dt = extractDateWithExiftool(imagePath);
    if (dt.isValid()) {
        return dt;
    }

    // Fallback: try to extract EXIF DateTimeOriginal from JPEG files manually
    QFile file(imagePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QDateTime();
    }

    QByteArray data = file.read(65536);  // Read first 64KB for EXIF
    file.close();

    // Check for JPEG magic bytes
    if (data.size() < 4 || (uchar)data[0] != 0xFF || (uchar)data[1] != 0xD8) {
        return QDateTime();  // Not a JPEG
    }

    // Search for EXIF marker (APP1 = 0xFFE1)
    int pos = 2;
    while (pos < data.size() - 4) {
        if ((uchar)data[pos] != 0xFF) {
            pos++;
            continue;
        }

        uchar marker = (uchar)data[pos + 1];
        if (marker == 0xE1) {  // APP1 (EXIF)
            int length = ((uchar)data[pos + 2] << 8) | (uchar)data[pos + 3];
            QByteArray exifData = data.mid(pos + 4, length - 2);

            // Check for "Exif\0\0" header
            if (exifData.startsWith("Exif\0\0")) {
                // Search for DateTimeOriginal tag (0x9003) in EXIF data
                // Format: "YYYY:MM:DD HH:MM:SS"
                QString exifStr = QString::fromLatin1(exifData);
                QRegularExpression dateRe("(\\d{4}):(\\d{2}):(\\d{2}) (\\d{2}):(\\d{2}):(\\d{2})");
                QRegularExpressionMatch match = dateRe.match(exifStr);
                if (match.hasMatch()) {
                    int year = match.captured(1).toInt();
                    int month = match.captured(2).toInt();
                    int day = match.captured(3).toInt();
                    int hour = match.captured(4).toInt();
                    int minute = match.captured(5).toInt();
                    int second = match.captured(6).toInt();

                    QDateTime dt(QDate(year, month, day), QTime(hour, minute, second));
                    if (dt.isValid() && year >= 1990 && year <= 2100) {
                        qDebug() << "Extracted EXIF date:" << dt;
                        return dt;
                    }
                }
            }
            break;
        } else if (marker == 0xD9 || marker == 0xDA) {
            break;  // End of image or start of scan
        } else if (marker >= 0xE0 && marker <= 0xEF) {
            // Skip other APP markers
            int length = ((uchar)data[pos + 2] << 8) | (uchar)data[pos + 3];
            pos += 2 + length;
        } else {
            pos += 2;
        }
    }

    return QDateTime();  // No date found
}

QDateTime ShotServer::extractVideoDate(const QString& videoPath) const
{
    // Use FFprobe to extract creation_time metadata
    QString ffprobePath = "ffprobe";

#ifdef Q_OS_WIN
    QStringList possiblePaths = {
        "ffprobe",
        "C:/ffmpeg/bin/ffprobe.exe",
        "C:/Program Files/ffmpeg/bin/ffprobe.exe",
        QCoreApplication::applicationDirPath() + "/ffprobe.exe"
    };
    for (const QString& path : possiblePaths) {
        if (QFile::exists(path) || path == "ffprobe") {
            ffprobePath = path;
            break;
        }
    }
#endif

    QStringList args;
    args << "-v" << "quiet"
         << "-print_format" << "json"
         << "-show_format"
         << videoPath;

    QProcess process;
    process.start(ffprobePath, args);

    if (!process.waitForStarted(3000) || !process.waitForFinished(10000)) {
        return QDateTime();
    }

    QByteArray output = process.readAllStandardOutput();
    QJsonDocument doc = QJsonDocument::fromJson(output);
    if (!doc.isObject()) {
        return QDateTime();
    }

    // Look for creation_time in format tags
    QJsonObject root = doc.object();
    QJsonObject format = root["format"].toObject();
    QJsonObject tags = format["tags"].toObject();

    QString creationTime = tags["creation_time"].toString();
    if (creationTime.isEmpty()) {
        creationTime = tags["com.apple.quicktime.creationdate"].toString();  // iOS
    }

    if (!creationTime.isEmpty()) {
        // Parse ISO 8601 format: "2024-01-15T10:30:00.000000Z"
        QDateTime dt = QDateTime::fromString(creationTime.left(19), "yyyy-MM-ddTHH:mm:ss");
        if (dt.isValid()) {
            qDebug() << "Extracted video date:" << dt;
            return dt;
        }
    }

    return QDateTime();
}
