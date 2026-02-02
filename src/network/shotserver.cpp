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

    // Start UDP discovery socket
    m_discoverySocket = new QUdpSocket(this);
    if (m_discoverySocket->bind(QHostAddress::Any, DISCOVERY_PORT, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        connect(m_discoverySocket, &QUdpSocket::readyRead, this, &ShotServer::onDiscoveryDatagram);
        qDebug() << "ShotServer: Discovery listener started on UDP port" << DISCOVERY_PORT;
    } else {
        qWarning() << "ShotServer: Failed to bind discovery socket on port" << DISCOVERY_PORT << m_discoverySocket->errorString();
        delete m_discoverySocket;
        m_discoverySocket = nullptr;
        // Continue anyway - discovery is optional
    }

    m_cleanupTimer->start();
    qDebug() << "ShotServer: Started on" << url();
    emit runningChanged();
    emit urlChanged();
    return true;
}

void ShotServer::stop()
{
    if (m_discoverySocket) {
        m_discoverySocket->close();
        delete m_discoverySocket;
        m_discoverySocket = nullptr;
    }

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

            pending.headerEnd = static_cast<int>(pending.headerData.indexOf("\r\n\r\n"));
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
            pending.isBackupRestore = requestLine.contains("POST") && requestLine.contains("/api/backup/restore");

            // Check upload size limit for media uploads
            if ((pending.isMediaUpload || pending.isBackupRestore) && pending.contentLength > MAX_UPLOAD_SIZE) {
                qWarning() << "ShotServer: Upload too large:" << pending.contentLength << "bytes (max:" << MAX_UPLOAD_SIZE << ")";
                sendResponse(socket, 413, "text/plain",
                    QString("File too large. Maximum size is %1 MB").arg(MAX_UPLOAD_SIZE / (1024*1024)).toUtf8());
                cleanupPendingRequest(socket);
                m_pendingRequests.remove(socket);
                socket->close();
                return;
            }

            // Check concurrent upload limit
            if ((pending.isMediaUpload || pending.isBackupRestore) && m_activeMediaUploads >= MAX_CONCURRENT_UPLOADS) {
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
                if (pending.isMediaUpload || pending.isBackupRestore) {
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
        if ((pending.isMediaUpload || pending.isBackupRestore) && pending.tempFile) {
            // Large upload with streamed body - pass temp file path
            QString headers = QString::fromUtf8(pending.headerData);
            QString tempPath = pending.tempFilePath;
            bool wasBackupRestore = pending.isBackupRestore;
            pending.tempFile = nullptr;  // Transfer ownership
            pending.tempFilePath.clear();
            m_activeMediaUploads--;
            m_pendingRequests.remove(socket);
            if (wasBackupRestore) {
                handleBackupRestore(socket, tempPath, headers);
            } else {
                handleMediaUpload(socket, tempPath, headers);
            }
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
    if ((pending.isMediaUpload || pending.isBackupRestore) && m_activeMediaUploads > 0) {
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

void ShotServer::onDiscoveryDatagram()
{
    while (m_discoverySocket && m_discoverySocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_discoverySocket->pendingDatagramSize());
        QHostAddress senderAddress;
        quint16 senderPort;

        m_discoverySocket->readDatagram(datagram.data(), datagram.size(), &senderAddress, &senderPort);

        // Check if this is a discovery request
        if (datagram.trimmed() == "DECENZA_DISCOVER") {
            qDebug() << "ShotServer: Discovery request from" << senderAddress.toString() << ":" << senderPort;

            // Build response with device info
            QString deviceName = QSysInfo::machineHostName();
            if (deviceName.isEmpty() || deviceName == "localhost") {
                QString productName = QSysInfo::prettyProductName();
                if (!productName.isEmpty()) {
                    deviceName = productName;
                } else {
                    deviceName = QSysInfo::productType() + " device";
                }
            }

            QJsonObject response;
            response["type"] = "DECENZA_SERVER";
            response["deviceName"] = deviceName;
            response["platform"] = QSysInfo::productType();
            response["appVersion"] = QString(VERSION_STRING);
            response["serverUrl"] = url();
            response["port"] = m_port;

            QByteArray responseData = QJsonDocument(response).toJson(QJsonDocument::Compact);
            m_discoverySocket->writeDatagram(responseData, senderAddress, senderPort);
            qDebug() << "ShotServer: Sent discovery response to" << senderAddress.toString();
        }
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
    else if (path == "/settings") {
        sendHtml(socket, generateSettingsPage());
    }
    else if (path == "/api/settings") {
        if (method == "POST") {
            // Extract body from request
            int bodyStart = request.indexOf("\r\n\r\n");
            if (bodyStart != -1) {
                QByteArray body = request.mid(bodyStart + 4);
                handleSaveSettings(socket, body);
            } else {
                sendJson(socket, R"({"error": "Invalid request"})");
            }
        } else {
            handleGetSettings(socket);
        }
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
            WebDebugLogger::instance()->clear(false);  // Don't clear file by default
        }
        QJsonObject result;
        result["success"] = true;
        sendJson(socket, QJsonDocument(result).toJson(QJsonDocument::Compact));
    }
    else if (path == "/api/debug/clearall") {
        if (WebDebugLogger::instance()) {
            WebDebugLogger::instance()->clear(true);  // Clear memory and file
        }
        QJsonObject result;
        result["success"] = true;
        sendJson(socket, QJsonDocument(result).toJson(QJsonDocument::Compact));
    }
    else if (path == "/api/debug/file") {
        // Return persisted log file content (survives crashes)
        QJsonObject result;
        if (WebDebugLogger::instance()) {
            result["log"] = WebDebugLogger::instance()->getPersistedLog();
            result["path"] = WebDebugLogger::instance()->logFilePath();
        } else {
            result["log"] = "";
            result["path"] = "";
        }
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
        emit sleepRequested();
        sendJson(socket, R"({"success":true,"action":"sleep"})");
    }
    // Home Automation API endpoints
    else if (path == "/api/state") {
        QJsonObject result;
        if (m_device) {
            result["connected"] = m_device->isConnected();
            result["state"] = m_device->stateString();
            result["substate"] = m_device->subStateString();
        }
        if (m_machineState) {
            result["phase"] = m_machineState->phaseString();
            result["isFlowing"] = m_machineState->isFlowing();
            result["isHeating"] = m_machineState->isHeating();
            result["isReady"] = m_machineState->isReady();
        }
        sendJson(socket, QJsonDocument(result).toJson(QJsonDocument::Compact));
    }
    else if (path == "/api/telemetry") {
        QJsonObject result;
        if (m_device) {
            result["connected"] = m_device->isConnected();
            result["pressure"] = m_device->pressure();
            result["flow"] = m_device->flow();
            result["temperature"] = m_device->temperature();
            result["mixTemperature"] = m_device->mixTemperature();
            result["steamTemperature"] = m_device->steamTemperature();
            result["waterLevel"] = m_device->waterLevel();
            result["waterLevelMm"] = m_device->waterLevelMm();
            result["waterLevelMl"] = m_device->waterLevelMl();
            result["firmwareVersion"] = m_device->firmwareVersion();
            result["state"] = m_device->stateString();
            result["substate"] = m_device->subStateString();
        }
        if (m_machineState) {
            result["phase"] = m_machineState->phaseString();
            result["shotTime"] = m_machineState->shotTime();
            result["scaleWeight"] = m_machineState->scaleWeight();
            result["scaleFlowRate"] = m_machineState->scaleFlowRate();
            result["targetWeight"] = m_machineState->targetWeight();
        }
        result["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        sendJson(socket, QJsonDocument(result).toJson(QJsonDocument::Compact));
    }
    else if (path == "/api/command" && method == "POST") {
        // Parse JSON body from request
        qsizetype bodyStart = request.indexOf("\r\n\r\n");
        if (bodyStart >= 0) {
            QByteArray body = request.mid(bodyStart + 4);
            QJsonDocument doc = QJsonDocument::fromJson(body);
            QString command = doc.object()["command"].toString().toLower();

            if (command == "wake") {
                if (m_device) {
                    m_device->wakeUp();
                    qDebug() << "ShotServer: Wake command sent via /api/command";
                }
                sendJson(socket, R"({"success":true,"command":"wake"})");
            } else if (command == "sleep") {
                if (m_device) {
                    m_device->goToSleep();
                    qDebug() << "ShotServer: Sleep command sent via /api/command";
                }
                emit sleepRequested();
                sendJson(socket, R"({"success":true,"command":"sleep"})");
            } else {
                sendResponse(socket, 400, "application/json",
                    R"({"error":"Invalid command. Valid commands: wake, sleep"})");
            }
        } else {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing request body"})");
        }
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
            qsizetype headerEndPos = request.indexOf("\r\n\r\n");
            if (headerEndPos < 0) {
                sendResponse(socket, 400, "text/plain", "Invalid request");
                return;
            }
            QString headers = QString::fromUtf8(request.left(headerEndPos));
            QByteArray body = request.mid(headerEndPos + 4);

            qDebug() << "ShotServer: Small media upload - request size:" << request.size()
                     << "headerEnd:" << headerEndPos << "body size:" << body.size();

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
    // Data migration backup API
    else if (path == "/api/backup/manifest") {
        handleBackupManifest(socket);
    }
    else if (path == "/api/backup/settings" || path.startsWith("/api/backup/settings?")) {
        bool includeSensitive = path.contains("includeSensitive=true");
        handleBackupSettings(socket, includeSensitive);
    }
    else if (path == "/api/backup/profiles") {
        handleBackupProfilesList(socket);
    }
    else if (path.startsWith("/api/backup/profile/")) {
        // /api/backup/profile/{category}/{filename} - download individual profile
        QString remainder = path.mid(20);  // After "/api/backup/profile/"
        int slashIdx = remainder.indexOf('/');
        if (slashIdx > 0) {
            QString category = remainder.left(slashIdx);
            QString filename = QUrl::fromPercentEncoding(remainder.mid(slashIdx + 1).toUtf8());
            handleBackupProfileFile(socket, category, filename);
        } else {
            sendResponse(socket, 400, "application/json", R"({"error":"Invalid profile path"})");
        }
    }
    else if (path == "/api/backup/shots") {
        // Checkpoint WAL and send database file (reuse existing logic)
        m_storage->checkpoint();
        QString dbPath = m_storage->databasePath();
        sendFile(socket, dbPath, "application/x-sqlite3");
    }
    else if (path == "/api/backup/media") {
        handleBackupMediaList(socket);
    }
    else if (path.startsWith("/api/backup/media/")) {
        // /api/backup/media/{filename} - download individual media file
        QString filename = QUrl::fromPercentEncoding(path.mid(18).toUtf8());
        handleBackupMediaFile(socket, filename);
    }
    // Full backup download/restore
    else if (path == "/api/backup/full") {
        handleBackupFull(socket);
    }
    else if (path == "/restore") {
        sendHtml(socket, generateRestorePage());
    }
    else if (path == "/api/backup/restore" && method == "POST") {
        // Small restore uploads (< 1MB) that were not streamed to temp file
        qsizetype headerEndPos = request.indexOf("\r\n\r\n");
        if (headerEndPos < 0) {
            sendResponse(socket, 400, "text/plain", "Invalid request");
            return;
        }
        QString headers = QString::fromUtf8(request.left(headerEndPos));
        QByteArray body = request.mid(headerEndPos + 4);

        QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QString tempPath = tempDir + "/restore_small_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".tmp";
        QFile tempFile(tempPath);
        if (!tempFile.open(QIODevice::WriteOnly)) {
            sendResponse(socket, 500, "text/plain", "Failed to create temp file");
            return;
        }
        tempFile.write(body);
        tempFile.close();

        handleBackupRestore(socket, tempPath, headers);
    }
    // Layout editor
    else if (path == "/layout") {
        sendHtml(socket, generateLayoutPage());
    }
    else if (path == "/api/layout" || path.startsWith("/api/layout/") || path.startsWith("/api/layout?")) {
        qsizetype headerEndPos = request.indexOf("\r\n\r\n");
        QByteArray body = (headerEndPos >= 0) ? request.mid(headerEndPos + 4) : QByteArray();
        handleLayoutApi(socket, method, path, body);
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
    // Inject vital stats (temperature, water level, connection) into the header of every page
    QString finalHtml = html;
    static const QString vitalScript = generateVitalStatsScript();
    finalHtml.replace(QLatin1String("</body>"), vitalScript + QStringLiteral("</body>"));
    sendResponse(socket, 200, "text/html; charset=utf-8", finalHtml.toUtf8());
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
    // First, try to determine the primary IP by checking which local address
    // would be used for an outbound connection (most reliable method)
    QUdpSocket testSocket;
    testSocket.connectToHost(QHostAddress("8.8.8.8"), 53);
    if (testSocket.waitForConnected(100)) {
        QHostAddress localAddr = testSocket.localAddress();
        testSocket.close();
        if (!localAddr.isNull() && !localAddr.isLoopback() &&
            localAddr.protocol() == QAbstractSocket::IPv4Protocol) {
            return localAddr.toString();
        }
    }
    testSocket.close();

    // Fallback: iterate through interfaces
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
        QString grinderSetting = shot["grinderSetting"].toString();
        double tempOverride = shot["temperatureOverride"].toDouble();  // Always has value
        double yieldOverride = shot["yieldOverride"].toDouble();  // Always has value

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

        // Build profile header: "Profile (Temp°C)"
        QString profileDisplay = profileHtml;
        if (tempOverride > 0) {
            profileDisplay += QString(" <span class=\"shot-temp\">(%1&deg;C)</span>")
                .arg(tempOverride, 0, 'f', 0);
        }

        // Build yield display: "Actual (Target) out" or just "Actual out"
        QString yieldDisplay;
        if (yieldOverride > 0 && qAbs(yieldOverride - finalWeight) > 0.5) {
            yieldDisplay = QString("<span class=\"metric-value\">%1g</span><span class=\"metric-target\">(%2g)</span>")
                .arg(finalWeight, 0, 'f', 1)
                .arg(yieldOverride, 0, 'f', 0);
        } else {
            yieldDisplay = QString("<span class=\"metric-value\">%1g</span>")
                .arg(finalWeight, 0, 'f', 1);
        }

        // Build bean display: "Brand Type (Grind)"
        QString beanDisplay;
        if (!beanBrand.isEmpty() || !beanType.isEmpty()) {
            beanDisplay = QString("<span class=\"clickable\" onclick=\"event.preventDefault(); event.stopPropagation(); addFilter('brand', '%1')\">%2</span>"
                                  "<span class=\"clickable\" onclick=\"event.preventDefault(); event.stopPropagation(); addFilter('coffee', '%3')\">%4</span>")
                .arg(brandJs, brandHtml, coffeeJs, coffeeHtml);
            if (!grinderSetting.isEmpty()) {
                beanDisplay += QString(" <span class=\"shot-grind\">(%1)</span>")
                    .arg(grinderSetting.toHtmlEscaped());
            }
        }

        rows += QString(R"HTML(
            <div class="shot-card" onclick="toggleSelect(%1, this)" data-id="%1"
                 data-profile="%2" data-brand="%3" data-coffee="%4" data-rating="%5"
                 data-ratio="%6" data-duration="%7" data-date="%8" data-dose="%9" data-yield="%10">
                <a href="/shot/%1" onclick="event.stopPropagation()" style="text-decoration:none;color:inherit;display:block;">
                    <div class="shot-header">
                        <span class="shot-profile clickable" onclick="event.preventDefault(); event.stopPropagation(); addFilter('profile', '%11')">%12</span>
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
                                %13
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
                        <span class="shot-beans">%14</span>
                        <span class="shot-rating clickable" onclick="event.preventDefault(); event.stopPropagation(); addFilter('rating', '%5')">rating: %5</span>
                    </div>
                </a>
            </div>
        )HTML")
        .arg(shot["id"].toLongLong())       // %1
        .arg(profileHtml)                   // %2 (data attr, undecorated)
        .arg(brandHtml)                     // %3
        .arg(coffeeHtml)                    // %4
        .arg(rating)                        // %5
        .arg(ratio, 0, 'f', 1)              // %6
        .arg(duration, 0, 'f', 1)           // %7
        .arg(dateTime)                      // %8
        .arg(doseWeight, 0, 'f', 1)         // %9
        .arg(finalWeight, 0, 'f', 1)        // %10
        .arg(profileJs)                     // %11
        .arg(profileDisplay)                // %12 (profile with temp)
        .arg(yieldDisplay)                  // %13 (yield with target)
        .arg(beanDisplay);                  // %14 (beans with grind)
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
        .shot-temp { color: var(--text-secondary); font-weight: normal; }
        .shot-grind { color: var(--text-secondary); font-weight: normal; }
        .metric-target { font-size: 0.75rem; color: var(--text-secondary); margin-left: 2px; }
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

    // Temperature and yield overrides (always have values)
    double tempOverride = shot["temperatureOverride"].toDouble();
    double yieldOverride = shot["yieldOverride"].toDouble();
    double finalWeight = shot["finalWeight"].toDouble();

    // Build yield display with optional target
    QString yieldDisplay = QString("%1g").arg(finalWeight, 0, 'f', 1);
    if (yieldOverride > 0 && qAbs(yieldOverride - finalWeight) > 0.5) {
        yieldDisplay += QString(" <span class=\"target\">(%1g)</span>").arg(yieldOverride, 0, 'f', 0);
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

    // Convert phase markers to JSON for Chart.js
    auto phasesToJson = [](const QVariantList& phases) -> QString {
        QStringList items;
        for (const QVariant& p : phases) {
            QVariantMap phase = p.toMap();
            QString label = phase["label"].toString();
            if (label == "Start") continue;  // Skip start marker
            items << QString("{time:%1,label:\"%2\",reason:\"%3\"}")
                .arg(phase["time"].toDouble(), 0, 'f', 2)
                .arg(label.replace(QStringLiteral("\""), QStringLiteral("\\\"")))
                .arg(phase["transitionReason"].toString());
        }
        return "[" + items.join(",") + "]";
    };
    QString phaseData = phasesToJson(shot["phases"].toList());

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
        .metric-card .value .target {
            font-size: 0.875rem;
            font-weight: 400;
            color: var(--text-secondary);
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
                    <a href="/remote" class="menu-item">&#128421; Remote Control</a>
                    <a href="/upload/media" class="menu-item">&#127912; Upload Screensaver Media</a>
                    <a href="/debug" class="menu-item">&#128736; Debug &amp; Dev Tools</a>
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
                <div class="value">%4</div>
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
                <h3>Beans (%13)</h3>
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
        const phaseData = %22;

        // Chart.js plugin: draw vertical phase marker lines and labels
        const phaseMarkerPlugin = {
            id: 'phaseMarkers',
            afterDraw: function(chart) {
                if (!phaseData || phaseData.length === 0) return;
                const ctx = chart.ctx;
                const xScale = chart.scales.x;
                const yScale = chart.scales.y;
                const top = yScale.top;
                const bottom = yScale.bottom;

                ctx.save();
                for (var i = 0; i < phaseData.length; i++) {
                    var marker = phaseData[i];
                    var x = xScale.getPixelForValue(marker.time);
                    if (x < xScale.left || x > xScale.right) continue;

                    // Draw vertical dotted line
                    ctx.beginPath();
                    ctx.setLineDash([3, 3]);
                    ctx.strokeStyle = marker.label === 'End' ? '#FF6B6B' : 'rgba(255,255,255,0.4)';
                    ctx.lineWidth = 1;
                    ctx.moveTo(x, top);
                    ctx.lineTo(x, bottom);
                    ctx.stroke();
                    ctx.setLineDash([]);

                    // Draw label
                    var suffix = '';
                    if (marker.reason === 'weight') suffix = ' [W]';
                    else if (marker.reason === 'pressure') suffix = ' [P]';
                    else if (marker.reason === 'flow') suffix = ' [F]';
                    else if (marker.reason === 'time') suffix = ' [T]';
                    var text = marker.label + suffix;

                    ctx.save();
                    ctx.translate(x + 4, top + 10);
                    ctx.rotate(-Math.PI / 2);
                    ctx.font = (marker.label === 'End' ? 'bold ' : '') + '11px sans-serif';
                    ctx.fillStyle = marker.label === 'End' ? '#FF6B6B' : 'rgba(255,255,255,0.8)';
                    ctx.textAlign = 'right';
                    ctx.fillText(text, 0, 0);
                    ctx.restore();
                }
                ctx.restore();
            }
        };

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
            plugins: [phaseMarkerPlugin],
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
    .arg(tempOverride > 0
         ? shot["profileName"].toString().toHtmlEscaped() + QString(" (%1&deg;C)").arg(tempOverride, 0, 'f', 0)
         : shot["profileName"].toString().toHtmlEscaped())
    .arg(shot["dateTime"].toString())
    .arg(shot["doseWeight"].toDouble(), 0, 'f', 1)
    .arg(yieldDisplay)
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
    .arg(shot["debugLog"].toString().isEmpty() ? "No debug log available" : shot["debugLog"].toString().toHtmlEscaped())
    .arg(phaseData);
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

        // Build yield text with optional target
        double cmpFinalWeight = shot["finalWeight"].toDouble();
        double cmpYieldOverride = shot["yieldOverride"].toDouble();
        QString cmpYieldText = QString("%1g").arg(cmpFinalWeight, 0, 'f', 1);
        if (cmpYieldOverride > 0 && qAbs(cmpYieldOverride - cmpFinalWeight) > 0.5) {
            cmpYieldText += QString("(%1g)").arg(cmpYieldOverride, 0, 'f', 0);
        }

        // Build profile label with temp: "Profile (Temp°C) (date)"
        double cmpTemp = shot["temperatureOverride"].toDouble();
        QString profileWithTemp = name;
        if (cmpTemp > 0) {
            profileWithTemp += QString(" (%1&deg;C)").arg(cmpTemp, 0, 'f', 0);
        }
        QString legendLabel = QString("%1 (%2)").arg(profileWithTemp, date);

        legendItems += QString(R"HTML(
            <div class="legend-item">
                <span class="legend-color" style="background:%1"></span>
                <div class="legend-info">
                    <div class="legend-name">%2</div>
                    <div class="legend-details">%3 | %4g in | %5 out | 1:%6 | %7s</div>
                </div>
            </div>
        )HTML").arg(color)
               .arg(legendLabel.toHtmlEscaped())
               .arg(date)
               .arg(shot["doseWeight"].toDouble(), 0, 'f', 1)
               .arg(cmpYieldText)
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
                    <a href="/remote" class="menu-item">&#128421; Remote Control</a>
                    <a href="/upload/media" class="menu-item">&#127912; Upload Screensaver Media</a>
                    <a href="/debug" class="menu-item">&#128736; Debug &amp; Dev Tools</a>
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
    <title>Debug &amp; Dev Tools - Decenza DE1</title>
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
            <h1>Debug &amp; Dev Tools</h1>
            <div class="status">
                <span class="status-dot"></span>
                <span id="lineCount">0 lines</span>
            </div>
            <div class="controls">
                <button class="btn active" id="autoScrollBtn" onclick="toggleAutoScroll()">Auto-scroll</button>
                <button class="btn" onclick="clearLog()">Clear</button>
                <button class="btn" onclick="loadPersistedLog()">Load Saved Log</button>
                <button class="btn" onclick="clearAll()">Clear All</button>
            </div>
        </div>
    </header>
    <main class="container">
        <div style="margin-bottom:1rem;display:flex;gap:0.5rem;flex-wrap:wrap;">
            <a href="/database.db" class="btn" style="text-decoration:none;">&#128190; Download Database</a>
            <a href="/upload" class="btn" style="text-decoration:none;">&#128230; Upload APK</a>
        </div>
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

        function clearAll() {
            if (confirm("Clear both live log and saved log file?")) {
                fetch("/api/debug/clearall", { method: "POST" })
                    .then(function() {
                        container.innerHTML = "";
                        lastIndex = 0;
                    });
            }
        }

        function loadPersistedLog() {
            fetch("/api/debug/file")
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    if (data.log) {
                        container.innerHTML = "";
                        var lines = data.log.split("\n");
                        var html = "";
                        for (var i = 0; i < lines.length; i++) {
                            if (lines[i]) html += colorize(lines[i]);
                        }
                        container.innerHTML = html;
                        lineCountEl.textContent = lines.length + " lines (from file)";
                        if (autoScroll) {
                            container.scrollTop = container.scrollHeight;
                        }
                    } else {
                        alert("No saved log file found");
                    }
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
    qsizetype headerEndPos = request.indexOf("\r\n\r\n");
    if (headerEndPos < 0) {
        sendResponse(socket, 400, "text/plain", "Invalid request");
        return;
    }

    QString headers = QString::fromUtf8(request.left(headerEndPos));
    QByteArray body = request.mid(headerEndPos + 4);

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
#ifdef Q_OS_IOS
    // QProcess is not available on iOS - can't run FFmpeg
    Q_UNUSED(inputPath)
    Q_UNUSED(outputPath)
    Q_UNUSED(maxWidth)
    Q_UNUSED(maxHeight)
    qWarning() << "Video resizing not supported on iOS (no QProcess)";
    return false;
#else
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
#endif
}

QDateTime ShotServer::extractDateWithExiftool(const QString& filePath) const
{
#ifdef Q_OS_IOS
    // QProcess is not available on iOS
    Q_UNUSED(filePath)
    return QDateTime();
#else
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
#endif
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
#ifdef Q_OS_IOS
    // QProcess is not available on iOS
    Q_UNUSED(videoPath)
    return QDateTime();
#else
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
#endif
}

// ============================================================================
// Data Migration Backup API
// ============================================================================

void ShotServer::handleBackupManifest(QTcpSocket* socket)
{
    QJsonObject manifest;

    // Device and app info
    QString deviceName = QSysInfo::machineHostName();
    if (deviceName.isEmpty() || deviceName == "localhost") {
        // Android devices often don't have a proper hostname
        // Use a more descriptive name
        QString productName = QSysInfo::prettyProductName();
        if (!productName.isEmpty()) {
            deviceName = productName;
        } else {
            deviceName = QSysInfo::productType() + " device";
        }
    }
    manifest["deviceName"] = deviceName;
    manifest["platform"] = QSysInfo::productType();
    manifest["appVersion"] = QString(VERSION_STRING);

    // Settings info
    if (m_settings) {
        manifest["hasSettings"] = true;
        // Estimate settings size (serialized JSON)
        QJsonObject settingsJson = SettingsSerializer::exportToJson(m_settings, false);
        QByteArray settingsData = QJsonDocument(settingsJson).toJson(QJsonDocument::Compact);
        manifest["settingsSize"] = settingsData.size();
    } else {
        manifest["hasSettings"] = false;
        manifest["settingsSize"] = 0;
    }

    // Profiles info
    // Profiles are stored flat in externalProfilesPath() and/or fallbackPath()
    // NOT in /user/ or /downloaded/ subdirectories
    if (m_profileStorage) {
        QString extPath = m_profileStorage->externalProfilesPath();
        QString fallbackPath = m_profileStorage->fallbackPath();

        qDebug() << "ShotServer: Profile paths for backup manifest:";
        qDebug() << "  External path:" << extPath;
        qDebug() << "  Fallback path:" << fallbackPath;

        int profileCount = 0;
        qint64 profilesSize = 0;
        QSet<QString> seenFiles;  // Avoid counting duplicates

        // Check external storage
        if (!extPath.isEmpty()) {
            QDir extDir(extPath);
            qDebug() << "  External dir exists:" << extDir.exists();
            if (extDir.exists()) {
                QFileInfoList files = extDir.entryInfoList(QStringList() << "*.json", QDir::Files);
                for (const QFileInfo& fi : files) {
                    // Skip temp/internal files
                    if (!fi.fileName().startsWith("_")) {
                        seenFiles.insert(fi.fileName());
                        profileCount++;
                        profilesSize += fi.size();
                    }
                }
                qDebug() << "  External profile .json files found:" << profileCount;
            }
        }

        // Check fallback path (avoid duplicates)
        QDir fallbackDir(fallbackPath);
        qDebug() << "  Fallback dir exists:" << fallbackDir.exists();
        if (fallbackDir.exists()) {
            QFileInfoList files = fallbackDir.entryInfoList(QStringList() << "*.json", QDir::Files);
            int fallbackCount = 0;
            for (const QFileInfo& fi : files) {
                if (!fi.fileName().startsWith("_") && !seenFiles.contains(fi.fileName())) {
                    seenFiles.insert(fi.fileName());
                    profileCount++;
                    profilesSize += fi.size();
                    fallbackCount++;
                }
            }
            qDebug() << "  Fallback profile .json files found:" << fallbackCount;
        }

        qDebug() << "  Total profile count:" << profileCount;
        manifest["profileCount"] = profileCount;
        manifest["profilesSize"] = profilesSize;
    } else {
        qDebug() << "ShotServer: m_profileStorage is null, cannot enumerate profiles";
        manifest["profileCount"] = 0;
        manifest["profilesSize"] = 0;
    }

    // Shots info
    if (m_storage) {
        manifest["shotCount"] = m_storage->totalShots();
        QString dbPath = m_storage->databasePath();
        QFileInfo dbInfo(dbPath);
        manifest["shotsSize"] = dbInfo.exists() ? dbInfo.size() : 0;
    } else {
        manifest["shotCount"] = 0;
        manifest["shotsSize"] = 0;
    }

    // Personal media info
    if (m_screensaverManager) {
        manifest["mediaCount"] = m_screensaverManager->personalMediaCount();
        QString mediaDir = m_screensaverManager->personalMediaDirectory();
        qint64 mediaSize = 0;
        QDir dir(mediaDir);
        if (dir.exists()) {
            QFileInfoList files = dir.entryInfoList(QDir::Files);
            for (const QFileInfo& fi : files) {
                if (fi.fileName() != "index.json") {
                    mediaSize += fi.size();
                }
            }
        }
        manifest["mediaSize"] = mediaSize;
    } else {
        manifest["mediaCount"] = 0;
        manifest["mediaSize"] = 0;
    }

    sendJson(socket, QJsonDocument(manifest).toJson(QJsonDocument::Compact));
}

void ShotServer::handleBackupSettings(QTcpSocket* socket, bool includeSensitive)
{
    if (!m_settings) {
        sendResponse(socket, 500, "application/json", R"({"error":"Settings not available"})");
        return;
    }

    QJsonObject settingsJson = SettingsSerializer::exportToJson(m_settings, includeSensitive);
    sendJson(socket, QJsonDocument(settingsJson).toJson(QJsonDocument::Compact));
}

void ShotServer::handleBackupProfilesList(QTcpSocket* socket)
{
    if (!m_profileStorage) {
        sendResponse(socket, 500, "application/json", R"({"error":"Profile storage not available"})");
        return;
    }

    QJsonArray profiles;
    QSet<QString> seenFiles;

    // Add profiles from external storage
    QString extPath = m_profileStorage->externalProfilesPath();
    if (!extPath.isEmpty()) {
        QDir extDir(extPath);
        if (extDir.exists()) {
            QFileInfoList files = extDir.entryInfoList(QStringList() << "*.json", QDir::Files);
            for (const QFileInfo& fi : files) {
                if (!fi.fileName().startsWith("_")) {
                    seenFiles.insert(fi.fileName());
                    QJsonObject profile;
                    profile["category"] = "external";
                    profile["filename"] = fi.fileName();
                    profile["size"] = fi.size();
                    profiles.append(profile);
                }
            }
        }
    }

    // Add profiles from fallback path (avoid duplicates)
    QString fallbackPath = m_profileStorage->fallbackPath();
    QDir fallbackDir(fallbackPath);
    if (fallbackDir.exists()) {
        QFileInfoList files = fallbackDir.entryInfoList(QStringList() << "*.json", QDir::Files);
        for (const QFileInfo& fi : files) {
            if (!fi.fileName().startsWith("_") && !seenFiles.contains(fi.fileName())) {
                QJsonObject profile;
                profile["category"] = "fallback";
                profile["filename"] = fi.fileName();
                profile["size"] = fi.size();
                profiles.append(profile);
            }
        }
    }

    QJsonDocument doc(profiles);
    sendJson(socket, doc.toJson(QJsonDocument::Compact));
}

void ShotServer::handleBackupProfileFile(QTcpSocket* socket, const QString& category, const QString& filename)
{
    if (!m_profileStorage) {
        sendResponse(socket, 500, "application/json", R"({"error":"Profile storage not available"})");
        return;
    }

    QString basePath;
    if (category == "external") {
        basePath = m_profileStorage->externalProfilesPath();
    } else if (category == "fallback") {
        basePath = m_profileStorage->fallbackPath();
    } else {
        sendResponse(socket, 400, "application/json", R"({"error":"Invalid category"})");
        return;
    }

    QString filePath = basePath + "/" + filename;
    QFileInfo fi(filePath);

    // Security check: ensure file is within expected directory
    if (!fi.absoluteFilePath().startsWith(basePath) || !fi.exists()) {
        sendResponse(socket, 404, "application/json", R"({"error":"Profile not found"})");
        return;
    }

    sendFile(socket, filePath, "application/json");
}

void ShotServer::handleBackupMediaList(QTcpSocket* socket)
{
    if (!m_screensaverManager) {
        sendResponse(socket, 500, "application/json", R"({"error":"Screensaver manager not available"})");
        return;
    }

    QString mediaDir = m_screensaverManager->personalMediaDirectory();
    QDir dir(mediaDir);

    QJsonArray mediaFiles;

    if (dir.exists()) {
        QFileInfoList files = dir.entryInfoList(QDir::Files);
        for (const QFileInfo& fi : files) {
            QJsonObject media;
            media["filename"] = fi.fileName();
            media["size"] = fi.size();
            mediaFiles.append(media);
        }
    }

    QJsonDocument doc(mediaFiles);
    sendJson(socket, doc.toJson(QJsonDocument::Compact));
}

void ShotServer::handleBackupMediaFile(QTcpSocket* socket, const QString& filename)
{
    if (!m_screensaverManager) {
        sendResponse(socket, 500, "application/json", R"({"error":"Screensaver manager not available"})");
        return;
    }

    QString mediaDir = m_screensaverManager->personalMediaDirectory();
    QString filePath = mediaDir + "/" + filename;
    QFileInfo fi(filePath);

    // Security check: ensure file is within expected directory
    if (!fi.absoluteFilePath().startsWith(mediaDir) || !fi.exists()) {
        sendResponse(socket, 404, "application/json", R"({"error":"Media file not found"})");
        return;
    }

    // Determine content type based on extension
    QString ext = fi.suffix().toLower();
    QString contentType = "application/octet-stream";
    if (ext == "jpg" || ext == "jpeg") contentType = "image/jpeg";
    else if (ext == "png") contentType = "image/png";
    else if (ext == "gif") contentType = "image/gif";
    else if (ext == "mp4") contentType = "video/mp4";
    else if (ext == "mov") contentType = "video/quicktime";
    else if (ext == "webm") contentType = "video/webm";

    sendFile(socket, filePath, contentType);
}

// ============================================================================
// Full Backup Download/Restore
// ============================================================================

void ShotServer::handleBackupFull(QTcpSocket* socket)
{
    struct Entry {
        QByteArray name;
        QByteArray data;
    };
    QList<Entry> entries;

    // 1. Settings
    if (m_settings) {
        QJsonObject settingsJson = SettingsSerializer::exportToJson(m_settings, true);
        QByteArray settingsData = QJsonDocument(settingsJson).toJson(QJsonDocument::Indented);
        entries.append({"settings.json", settingsData});
    }

    // 2. Shots database
    if (m_storage) {
        m_storage->checkpoint();
        QString dbPath = m_storage->databasePath();
        QFile dbFile(dbPath);
        if (dbFile.open(QIODevice::ReadOnly)) {
            entries.append({"shots.db", dbFile.readAll()});
        }
    }

    // 3. Profiles (from both external and fallback paths)
    if (m_profileStorage) {
        QSet<QString> seenFiles;
        auto addProfilesFrom = [&](const QString& dirPath) {
            if (dirPath.isEmpty()) return;
            QDir dir(dirPath);
            if (!dir.exists()) return;
            QFileInfoList files = dir.entryInfoList(QStringList() << "*.json", QDir::Files);
            for (const QFileInfo& fi : files) {
                if (fi.fileName().startsWith("_")) continue;
                if (seenFiles.contains(fi.fileName())) continue;
                seenFiles.insert(fi.fileName());
                QFile f(fi.absoluteFilePath());
                if (f.open(QIODevice::ReadOnly)) {
                    QByteArray name = ("profiles/" + fi.fileName()).toUtf8();
                    entries.append({name, f.readAll()});
                }
            }
        };
        addProfilesFrom(m_profileStorage->externalProfilesPath());
        addProfilesFrom(m_profileStorage->fallbackPath());
    }

    // 4. Media files
    if (m_screensaverManager) {
        QString mediaDir = m_screensaverManager->personalMediaDirectory();
        QDir dir(mediaDir);
        if (dir.exists()) {
            QFileInfoList files = dir.entryInfoList(QDir::Files);
            for (const QFileInfo& fi : files) {
                if (fi.fileName() == "index.json") continue;
                QFile f(fi.absoluteFilePath());
                if (f.open(QIODevice::ReadOnly)) {
                    QByteArray name = ("media/" + fi.fileName()).toUtf8();
                    entries.append({name, f.readAll()});
                }
            }
        }
    }

    // Build binary archive
    QByteArray archiveData;
    QBuffer buffer(&archiveData);
    buffer.open(QIODevice::WriteOnly);

    // Magic "DCBK"
    buffer.write("DCBK", 4);

    // Version (uint32 LE)
    quint32 version = 1;
    buffer.write(reinterpret_cast<const char*>(&version), 4);

    // Entry count (uint32 LE)
    quint32 count = static_cast<quint32>(entries.size());
    buffer.write(reinterpret_cast<const char*>(&count), 4);

    // Each entry
    for (const Entry& e : entries) {
        quint32 nameLen = static_cast<quint32>(e.name.size());
        buffer.write(reinterpret_cast<const char*>(&nameLen), 4);
        buffer.write(e.name);
        quint64 dataLen = static_cast<quint64>(e.data.size());
        buffer.write(reinterpret_cast<const char*>(&dataLen), 8);
        buffer.write(e.data);
    }

    buffer.close();

    qDebug() << "ShotServer: Created backup archive with" << entries.size() << "entries,"
             << archiveData.size() << "bytes";

    // Send as download
    QString filename = QString("decenza_backup_%1.dcbackup")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd"));
    QByteArray extraHeaders = QString("Content-Disposition: attachment; filename=\"%1\"\r\n").arg(filename).toUtf8();
    sendResponse(socket, 200, "application/octet-stream", archiveData, extraHeaders);
}

QString ShotServer::generateRestorePage() const
{
    QString html;

    // Part 1: Head and base CSS
    html += R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Restore Backup - Decenza DE1</title>
    <style>
)HTML";
    html += WEB_CSS_VARIABLES;
    html += WEB_CSS_HEADER;
    html += WEB_CSS_MENU;

    // Part 2: Page-specific CSS
    html += R"HTML(
        :root {
            --success: #18c37e;
            --error: #f85149;
        }
        .upload-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 2rem;
            margin-bottom: 1.5rem;
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
        .upload-icon { font-size: 3rem; margin-bottom: 1rem; }
        .upload-text { color: var(--text-secondary); margin-bottom: 0.5rem; }
        .upload-hint { color: var(--text-secondary); font-size: 0.875rem; }
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
            height: 100%%;
            background: var(--accent);
            width: 0%%;
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
        .info-box {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1.5rem;
        }
        .info-box h4 {
            margin-bottom: 0.75rem;
            color: var(--accent);
        }
        .info-box ul {
            list-style: none;
            padding: 0;
        }
        .info-box li {
            padding: 0.25rem 0;
            color: var(--text-secondary);
            font-size: 0.875rem;
        }
        .info-box li::before {
            content: "\2022 ";
            color: var(--accent);
        }
    </style>
</head>
<body>
)HTML";

    // Part 3: Header with back button and menu
    html += R"HTML(
    <header class="header">
        <div class="header-content">
            <div style="display:flex;align-items:center;gap:1rem">
                <a href="/" class="back-btn">&larr;</a>
                <h1>Restore Backup</h1>
            </div>
            <div class="header-right">
)HTML";
    html += generateMenuHtml();
    html += R"HTML(
            </div>
        </div>
    </header>
)HTML";

    // Part 4: Main content
    html += R"HTML(
    <main class="container" style="max-width:800px">
        <div class="upload-card">
            <div class="upload-zone" id="uploadZone" onclick="document.getElementById('fileInput').click()">
                <div class="upload-icon">&#128229;</div>
                <div class="upload-text">Click or drag a .dcbackup file here</div>
                <div class="upload-hint">Restores settings, profiles, shots, and media</div>
            </div>
            <input type="file" id="fileInput" accept=".dcbackup" onchange="handleFile(this.files[0])">
            <div class="progress-bar" id="progressBar">
                <div class="progress-fill" id="progressFill"></div>
            </div>
            <div class="status-message" id="statusMessage"></div>
        </div>

        <div class="info-box">
            <h4>&#9432; How restore works</h4>
            <ul>
                <li>Settings will be overwritten with backup values</li>
                <li>Shot history will be merged (no duplicates)</li>
                <li>Profiles with the same name are skipped (not overwritten)</li>
                <li>Media with the same name is skipped (not overwritten)</li>
                <li>The app may need a restart for some settings to take effect</li>
            </ul>
        </div>
    </main>
)HTML";

    // Part 5: JavaScript
    html += R"HTML(
    <script>
)HTML";
    html += WEB_JS_MENU;
    html += R"HTML(
        var uploadZone = document.getElementById("uploadZone");
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

        function handleFile(file) {
            if (!file) return;
            if (!file.name.endsWith(".dcbackup")) {
                showStatus("error", "Please select a .dcbackup file");
                return;
            }

            uploadZone.classList.add("uploading");
            progressBar.style.display = "block";
            progressFill.style.width = "0%";
            showStatus("processing", "Uploading backup (" + formatSize(file.size) + ")...");

            var xhr = new XMLHttpRequest();
            xhr.open("POST", "/api/backup/restore", true);
            xhr.timeout = 600000;

            xhr.upload.onprogress = function(e) {
                if (e.lengthComputable) {
                    var pct = (e.loaded / e.total) * 100;
                    progressFill.style.width = pct + "%";
                    if (pct >= 100) {
                        showStatus("processing", "Processing backup... this may take a moment");
                    }
                }
            };

            xhr.onload = function() {
                uploadZone.classList.remove("uploading");
                if (xhr.status === 200) {
                    try {
                        var r = JSON.parse(xhr.responseText);
                        var parts = [];
                        if (r.settings) parts.push("Settings restored");
                        if (r.shotsImported) parts.push("Shots merged");
                        if (r.profilesImported > 0) parts.push(r.profilesImported + " profiles imported");
                        if (r.profilesSkipped > 0) parts.push(r.profilesSkipped + " profiles already existed");
                        if (r.mediaImported > 0) parts.push(r.mediaImported + " media imported");
                        if (r.mediaSkipped > 0) parts.push(r.mediaSkipped + " media already existed");
                        if (parts.length === 0) parts.push("Nothing to restore");
                        showStatus("success", "Restore complete: " + parts.join(", "));
                    } catch (e) {
                        showStatus("success", "Restore complete");
                    }
                } else {
                    try {
                        var err = JSON.parse(xhr.responseText);
                        showStatus("error", "Restore failed: " + (err.error || "Unknown error"));
                    } catch (e) {
                        showStatus("error", "Restore failed: " + (xhr.responseText || "Unknown error"));
                    }
                }
            };

            xhr.onerror = function() {
                uploadZone.classList.remove("uploading");
                showStatus("error", "Connection error. Check that the server is running.");
            };

            xhr.ontimeout = function() {
                uploadZone.classList.remove("uploading");
                showStatus("error", "Upload timed out. The backup file may be too large.");
            };

            xhr.setRequestHeader("Content-Type", "application/octet-stream");
            xhr.setRequestHeader("X-Filename", encodeURIComponent(file.name));
            xhr.send(file);
        }

        function formatSize(bytes) {
            if (bytes < 1024) return bytes + " B";
            if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
            return (bytes / (1024 * 1024)).toFixed(1) + " MB";
        }

        function showStatus(type, message) {
            statusMessage.className = "status-message " + type;
            statusMessage.textContent = message;
            statusMessage.style.display = "block";
        }
    </script>
</body>
</html>
)HTML";

    return html;
}

void ShotServer::handleBackupRestore(QTcpSocket* socket, const QString& tempFilePath, const QString& headers)
{
    Q_UNUSED(headers);

    QString tempPathToCleanup = tempFilePath;
    auto cleanupTempFile = [&tempPathToCleanup]() {
        if (!tempPathToCleanup.isEmpty() && QFile::exists(tempPathToCleanup)) {
            QFile::remove(tempPathToCleanup);
        }
    };

    QFile file(tempFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        sendResponse(socket, 500, "application/json", R"({"error":"Failed to open uploaded file"})");
        cleanupTempFile();
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    // Validate magic and minimum size
    if (data.size() < 12 || data.left(4) != "DCBK") {
        sendResponse(socket, 400, "application/json", R"({"error":"Invalid backup file. Expected a .dcbackup file."})");
        cleanupTempFile();
        return;
    }

    // Parse header
    const char* ptr = data.constData();
    quint32 version;
    memcpy(&version, ptr + 4, 4);
    quint32 entryCount;
    memcpy(&entryCount, ptr + 8, 4);

    if (version != 1) {
        sendResponse(socket, 400, "application/json",
            QString(R"({"error":"Unsupported backup version: %1"})").arg(version).toUtf8());
        cleanupTempFile();
        return;
    }

    if (entryCount > 100000) {
        sendResponse(socket, 400, "application/json", R"json({"error":"Backup file appears corrupt (too many entries)"})json");
        cleanupTempFile();
        return;
    }

    qDebug() << "ShotServer: Restoring backup with" << entryCount << "entries," << data.size() << "bytes";

    qint64 offset = 12;
    bool settingsRestored = false;
    bool shotsRestored = false;
    int profilesImported = 0;
    int profilesSkipped = 0;
    int mediaImported = 0;
    int mediaSkipped = 0;

    for (quint32 i = 0; i < entryCount; i++) {
        // Read name length
        if (offset + 4 > data.size()) {
            qWarning() << "ShotServer: Backup truncated at entry" << i << "(name length)";
            break;
        }
        quint32 nameLen;
        memcpy(&nameLen, ptr + offset, 4);
        offset += 4;

        if (nameLen > 10000 || offset + nameLen > data.size()) {
            qWarning() << "ShotServer: Backup truncated at entry" << i << "(name)";
            break;
        }
        QString name = QString::fromUtf8(ptr + offset, nameLen);
        offset += nameLen;

        // Read data length
        if (offset + 8 > data.size()) {
            qWarning() << "ShotServer: Backup truncated at entry" << i << "(data length)";
            break;
        }
        quint64 dataLen;
        memcpy(&dataLen, ptr + offset, 8);
        offset += 8;

        if (offset + static_cast<qint64>(dataLen) > data.size()) {
            qWarning() << "ShotServer: Backup truncated at entry" << i << "(data)";
            break;
        }
        QByteArray entryData(ptr + offset, static_cast<qsizetype>(dataLen));
        offset += static_cast<qint64>(dataLen);

        // Process entry by type
        if (name == "settings.json") {
            if (m_settings) {
                QJsonDocument doc = QJsonDocument::fromJson(entryData);
                if (!doc.isNull() && doc.isObject()) {
                    SettingsSerializer::importFromJson(m_settings, doc.object());
                    settingsRestored = true;
                    qDebug() << "ShotServer: Restored settings";
                }
            }
        }
        else if (name == "shots.db") {
            if (m_storage) {
                QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
                QString dbTempPath = tempDir + "/restore_shots_" +
                    QString::number(QDateTime::currentMSecsSinceEpoch()) + ".db";
                QFile dbFile(dbTempPath);
                if (dbFile.open(QIODevice::WriteOnly)) {
                    dbFile.write(entryData);
                    dbFile.close();
                    int beforeCount = m_storage->totalShots();
                    bool success = m_storage->importDatabase(dbTempPath, true);
                    if (success) {
                        m_storage->refreshTotalShots();
                        int imported = m_storage->totalShots() - beforeCount;
                        qDebug() << "ShotServer: Imported" << imported << "new shots";
                        shotsRestored = true;
                    }
                }
                QFile::remove(dbTempPath);
            }
        }
        else if (name.startsWith("profiles/")) {
            if (m_profileStorage) {
                QString filename = name.mid(9);  // Remove "profiles/"
                // Strip .json extension since profileExists/writeProfile add it
                QString profileName = filename;
                if (profileName.endsWith(".json", Qt::CaseInsensitive)) {
                    profileName = profileName.left(profileName.length() - 5);
                }
                if (m_profileStorage->profileExists(profileName)) {
                    profilesSkipped++;
                } else {
                    QString content = QString::fromUtf8(entryData);
                    if (m_profileStorage->writeProfile(profileName, content)) {
                        profilesImported++;
                        qDebug() << "ShotServer: Imported profile:" << profileName;
                    }
                }
            }
        }
        else if (name.startsWith("media/")) {
            if (m_screensaverManager) {
                QString filename = name.mid(6);  // Remove "media/"
                if (filename == "index.json") continue;
                if (m_screensaverManager->hasPersonalMediaWithName(filename)) {
                    mediaSkipped++;
                } else {
                    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
                    QString mediaTempPath = tempDir + "/restore_media_" +
                        QString::number(QDateTime::currentMSecsSinceEpoch()) + "_" + filename;
                    QFile mediaFile(mediaTempPath);
                    if (mediaFile.open(QIODevice::WriteOnly)) {
                        mediaFile.write(entryData);
                        mediaFile.close();
                        if (m_screensaverManager->addPersonalMedia(mediaTempPath, filename)) {
                            mediaImported++;
                            qDebug() << "ShotServer: Imported media:" << filename;
                        }
                    }
                    QFile::remove(mediaTempPath);
                }
            }
        }
    }

    qDebug() << "ShotServer: Restore complete - settings:" << settingsRestored
             << "shots:" << shotsRestored
             << "profiles:" << profilesImported << "(skipped:" << profilesSkipped << ")"
             << "media:" << mediaImported << "(skipped:" << mediaSkipped << ")";

    // Build response
    QJsonObject result;
    result["success"] = true;
    result["settings"] = settingsRestored;
    result["shotsImported"] = shotsRestored;
    result["profilesImported"] = profilesImported;
    result["profilesSkipped"] = profilesSkipped;
    result["mediaImported"] = mediaImported;
    result["mediaSkipped"] = mediaSkipped;

    sendJson(socket, QJsonDocument(result).toJson(QJsonDocument::Compact));
    cleanupTempFile();
}

// ============================================================================
// Layout Editor Web UI
// ============================================================================

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

    // All remaining endpoints are POST
    if (method != "POST") {
        sendResponse(socket, 405, "application/json", R"({"error":"Method not allowed"})");
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
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
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            margin-bottom: 1rem;
        }
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
            background: var(--accent);
            color: #000;
            border-color: var(--accent);
        }
        .chip.special { color: orange; }
        .chip.selected.special { color: #000; }
        .chip-arrow {
            cursor: pointer;
            font-size: 1rem;
            opacity: 0.8;
        }
        .chip-arrow:hover { opacity: 1; }
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
        .html-input {
            width: 100%;
            min-height: 80px;
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: 6px;
            color: var(--text);
            font-family: monospace;
            font-size: 0.8rem;
            padding: 0.5rem;
            resize: vertical;
            box-sizing: border-box;
        }
        .html-input:focus { border-color: var(--accent); outline: none; }
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
        .color-dot {
            width: 22px;
            height: 22px;
            border-radius: 50%;
            border: 1px solid var(--border);
            cursor: pointer;
            display: inline-block;
        }
        .color-dot:hover { border-color: white; }
        .color-grid { display: flex; flex-wrap: wrap; gap: 4px; margin: 0.5rem 0; }
        .section-label {
            font-size: 0.75rem;
            color: var(--text-secondary);
            margin: 0.5rem 0 0.25rem;
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .var-list, .action-list {
            max-height: 180px;
            overflow-y: auto;
            border: 1px solid var(--border);
            border-radius: 6px;
            background: var(--bg);
        }
        .var-item, .action-item {
            padding: 0.375rem 0.5rem;
            cursor: pointer;
            font-size: 0.8rem;
            color: var(--accent);
            border-bottom: 1px solid var(--border);
        }
        .var-item:last-child, .action-item:last-child { border-bottom: none; }
        .var-item:hover, .action-item:hover { background: var(--surface-hover); }
        .action-item { color: var(--text); }
        .action-item.selected { background: var(--accent); color: #000; }
        .preview-box {
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: 6px;
            padding: 0.75rem;
            margin: 0.75rem 0;
            min-height: 40px;
            color: var(--text);
        }
        .preview-box.has-action { border-color: var(--accent); border-width: 2px; }
        .editor-buttons {
            display: flex;
            gap: 0.5rem;
            justify-content: flex-end;
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
        .two-col { display: flex; gap: 0.75rem; }
        .two-col > div { flex: 1; }
        .editor-inner { display: flex; gap: 1.25rem; }
        .editor-left { flex: 3; min-width: 300px; }
        .editor-right { flex: 1; min-width: 220px; max-width: 340px; }
        @media (max-width: 700px) {
            .editor-inner { flex-direction: column; }
            .editor-right { flex: none; }
        }
    </style>
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
    <div class="main-layout">
        <div class="zones-panel" id="zonesPanel"></div>
        <div class="editor-panel editor-hidden" id="editorPanel">
            <div class="editor-card">
                <h3>Edit Text Widget</h3>
                <div class="editor-inner">
                    <div class="editor-left">
                        <textarea class="html-input" id="htmlInput" placeholder="Enter text or HTML..." style="min-height:120px"></textarea>

                        <div class="toolbar" id="formatToolbar">
                            <button class="tool-btn" onclick="insertTag('&lt;b&gt;','&lt;/b&gt;')" title="Bold"><b>B</b></button>
                            <button class="tool-btn" onclick="insertTag('&lt;i&gt;','&lt;/i&gt;')" title="Italic"><i>I</i></button>
                            <div class="tool-sep"></div>
                            <button class="tool-btn" onclick="insertFontSize(12)" title="Small">S</button>
                            <button class="tool-btn" onclick="insertFontSize(18)" title="Medium">M</button>
                            <button class="tool-btn" onclick="insertFontSize(28)" title="Large">L</button>
                            <button class="tool-btn" onclick="insertFontSize(48)" title="Extra Large">XL</button>
                            <div class="tool-sep"></div>
                            <button class="tool-btn" id="alignLeft" onclick="setAlign('left')" title="Left">&#9664;</button>
                            <button class="tool-btn active" id="alignCenter" onclick="setAlign('center')" title="Center">&#9679;</button>
                            <button class="tool-btn" id="alignRight" onclick="setAlign('right')" title="Right">&#9654;</button>
                        </div>

                        <div class="section-label">Color</div>
                        <div class="color-grid">
                            <span class="color-dot" style="background:#ffffff" onclick="insertColor('#ffffff')"></span>
                            <span class="color-dot" style="background:#a0a8b8" onclick="insertColor('#a0a8b8')"></span>
                            <span class="color-dot" style="background:#4e85f4" onclick="insertColor('#4e85f4')"></span>
                            <span class="color-dot" style="background:#e94560" onclick="insertColor('#e94560')"></span>
                            <span class="color-dot" style="background:#00cc6d" onclick="insertColor('#00cc6d')"></span>
                            <span class="color-dot" style="background:#ffaa00" onclick="insertColor('#ffaa00')"></span>
                            <span class="color-dot" style="background:#a2693d" onclick="insertColor('#a2693d')"></span>
                            <span class="color-dot" style="background:#c0c5e3" onclick="insertColor('#c0c5e3')"></span>
                            <span class="color-dot" style="background:#e73249" onclick="insertColor('#e73249')"></span>
                            <span class="color-dot" style="background:#18c37e" onclick="insertColor('#18c37e')"></span>
                            <span class="color-dot" style="background:#ff4444" onclick="insertColor('#ff4444')"></span>
                            <span class="color-dot" style="background:#9C27B0" onclick="insertColor('#9C27B0')"></span>
                        </div>

                        <div class="section-label">Preview</div>
                        <div class="preview-box" id="previewBox"></div>

                        <div class="editor-buttons" style="margin-top:0.75rem">
                            <button class="btn btn-cancel" onclick="closeEditor()">Cancel</button>
                            <button class="btn btn-save" onclick="saveText()">Save</button>
                        </div>
                    </div>
                    <div class="editor-right">
                        <div class="section-label">Variables (click to insert)</div>
                        <div class="var-list">
                            <div class="var-item" onclick="insertVar('%TEMP%')">Temp (&deg;C)</div>
                            <div class="var-item" onclick="insertVar('%STEAM_TEMP%')">Steam (&deg;C)</div>
                            <div class="var-item" onclick="insertVar('%PRESSURE%')">Pressure (bar)</div>
                            <div class="var-item" onclick="insertVar('%FLOW%')">Flow (ml/s)</div>
                            <div class="var-item" onclick="insertVar('%WATER%')">Water (%)</div>
                            <div class="var-item" onclick="insertVar('%WATER_ML%')">Water (ml)</div>
                            <div class="var-item" onclick="insertVar('%WEIGHT%')">Weight (g)</div>
                            <div class="var-item" onclick="insertVar('%SHOT_TIME%')">Shot Time (s)</div>
                            <div class="var-item" onclick="insertVar('%TARGET_WEIGHT%')">Target Wt (g)</div>
                            <div class="var-item" onclick="insertVar('%VOLUME%')">Volume (ml)</div>
                            <div class="var-item" onclick="insertVar('%PROFILE%')">Profile Name</div>
                            <div class="var-item" onclick="insertVar('%STATE%')">Machine State</div>
                            <div class="var-item" onclick="insertVar('%TARGET_TEMP%')">Target Temp</div>
                            <div class="var-item" onclick="insertVar('%SCALE%')">Scale Name</div>
                            <div class="var-item" onclick="insertVar('%TIME%')">Time (HH:MM)</div>
                            <div class="var-item" onclick="insertVar('%DATE%')">Date</div>
                            <div class="var-item" onclick="insertVar('%RATIO%')">Brew Ratio</div>
                            <div class="var-item" onclick="insertVar('%DOSE%')">Dose (g)</div>
                            <div class="var-item" onclick="insertVar('%CONNECTED%')">Online/Offline</div>
                            <div class="var-item" onclick="insertVar('%CONNECTED_COLOR%')">Status Color</div>
                            <div class="var-item" onclick="insertVar('%DEVICES%')">Devices</div>
                        </div>
                        <div class="section-label" style="margin-top:0.75rem">Action (on tap)</div>
                        <div class="action-list" id="actionList"></div>
                    </div>
                </div>
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

    var ZONES = [
        {key: "topLeft", label: "Top Bar (Left)", hasOffset: false},
        {key: "topRight", label: "Top Bar (Right)", hasOffset: false},
        {key: "centerStatus", label: "Center - Top", hasOffset: true},
        {key: "centerTop", label: "Center - Action Buttons", hasOffset: true},
        {key: "centerMiddle", label: "Center - Info", hasOffset: true},
        {key: "bottomLeft", label: "Bottom Bar (Left)", hasOffset: false},
        {key: "bottomRight", label: "Bottom Bar (Right)", hasOffset: false}
    ];

    var WIDGET_TYPES = [
        {type:"espresso",label:"Espresso"},{type:"steam",label:"Steam"},
        {type:"hotwater",label:"Hot Water"},{type:"flush",label:"Flush"},
        {type:"beans",label:"Beans"},{type:"history",label:"History"},
        {type:"autofavorites",label:"Favorites"},{type:"sleep",label:"Sleep"},
        {type:"settings",label:"Settings"},{type:"temperature",label:"Temperature"},
        {type:"waterLevel",label:"Water Level"},{type:"connectionStatus",label:"Connection"},
        {type:"scaleWeight",label:"Scale Weight"},{type:"shotPlan",label:"Shot Plan"},
        {type:"spacer",label:"Spacer",special:true},{type:"text",label:"Text",special:true}
    ];

    var DISPLAY_NAMES = {
        espresso:"Espresso",steam:"Steam",hotwater:"Hot Water",flush:"Flush",
        beans:"Beans",history:"History",autofavorites:"Favorites",sleep:"Sleep",
        settings:"Settings",temperature:"Temp",waterLevel:"Water",
        connectionStatus:"Connection",scaleWeight:"Scale",shotPlan:"Shot Plan",
        spacer:"Spacer",text:"Text"
    };

    var ACTIONS = [
        {id:"",label:"None"},
        {id:"navigate:settings",label:"Go to Settings"},
        {id:"navigate:history",label:"Go to History"},
        {id:"navigate:profiles",label:"Go to Profiles"},
        {id:"navigate:profileEditor",label:"Go to Profile Editor"},
        {id:"navigate:recipes",label:"Go to Recipes"},
        {id:"navigate:descaling",label:"Go to Descaling"},
        {id:"navigate:ai",label:"Go to AI Settings"},
        {id:"navigate:visualizer",label:"Go to Visualizer"},
        {id:"command:sleep",label:"Sleep"},
        {id:"command:startEspresso",label:"Start Espresso"},
        {id:"command:startSteam",label:"Start Steam"},
        {id:"command:startHotWater",label:"Start Hot Water"},
        {id:"command:startFlush",label:"Start Flush"},
        {id:"command:idle",label:"Stop (Idle)"},
        {id:"command:tare",label:"Tare Scale"}
    ];

    function loadLayout() {
        fetch("/api/layout").then(function(r){return r.json()}).then(function(data) {
            layoutData = data;
            renderZones();
        });
    }

    function renderZones() {
        var panel = document.getElementById("zonesPanel");
        var html = "";
        for (var z = 0; z < ZONES.length; z++) {
            var zone = ZONES[z];
            var items = (layoutData && layoutData.zones && layoutData.zones[zone.key]) || [];

            // Pair top and bottom zones side by side
            var isPairStart = (z === 0 || z === 5);
            var isPairEnd = (z === 1 || z === 6);
            if (isPairStart) html += '<div class="zone-row">';

            html += '<div class="zone-card" style="' + (isPairStart || isPairEnd ? 'flex:1' : '') + '">';
            html += '<div class="zone-header"><span class="zone-title">' + zone.label + '</span>';

            if (zone.hasOffset) {
                var offset = 0;
                if (layoutData && layoutData.offsets && layoutData.offsets[zone.key] !== undefined)
                    offset = layoutData.offsets[zone.key];
                html += '<div class="zone-offset-controls">';
                html += '<button class="offset-btn" onclick="changeOffset(\'' + zone.key + '\',-5)">&#9650;</button>';
                html += '<span class="offset-val">' + (offset !== 0 ? (offset > 0 ? "+" : "") + offset : "0") + '</span>';
                html += '<button class="offset-btn" onclick="changeOffset(\'' + zone.key + '\',5)">&#9660;</button>';
                html += '</div>';
            }
            html += '</div>';

            html += '<div class="chips-area">';
            for (var i = 0; i < items.length; i++) {
                var item = items[i];
                var isSpecial = item.type === "spacer" || item.type === "text";
                var isSel = selectedChip && selectedChip.id === item.id;
                var cls = "chip" + (isSel ? " selected" : "") + (isSpecial ? " special" : "");
                html += '<span class="' + cls + '" onclick="chipClick(\'' + item.id + '\',\'' + zone.key + '\',\'' + item.type + '\')">';

                if (isSel && i > 0) {
                    html += '<span class="chip-arrow" onclick="event.stopPropagation();reorder(\'' + zone.key + '\',' + i + ',' + (i-1) + ')">&#9664;</span>';
                }
                html += DISPLAY_NAMES[item.type] || item.type;
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
                html += '<div class="add-dropdown-item' + (wt.special ? ' special' : '') + '" ';
                html += 'onclick="event.stopPropagation();addItem(\'' + wt.type + '\',\'' + zone.key + '\');this.parentElement.classList.remove(\'open\')">';
                html += wt.label + '</div>';
            }
            html += '</div></div>';

            html += '</div></div>';

            if (isPairEnd) html += '</div>';
        }
        panel.innerHTML = html;
    }

    function chipClick(itemId, zone, type) {
        if (selectedChip && selectedChip.id === itemId) {
            // Deselect
            selectedChip = null;
        } else if (selectedChip && selectedChip.zone !== zone) {
            // Move to different zone
            apiPost("/api/layout/move", {itemId: selectedChip.id, fromZone: selectedChip.zone, toZone: zone, toIndex: -1}, function() {
                selectedChip = null;
                loadLayout();
            });
            return;
        } else {
            selectedChip = {id: itemId, zone: zone};
            if (type === "text") {
                openEditor(itemId, zone);
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
        dropdown.classList.toggle("open");
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

    function resetLayout() {
        if (!confirm("Reset layout to default?")) return;
        apiPost("/api/layout/reset", {}, function() {
            selectedChip = null;
            closeEditor();
            loadLayout();
        });
    }

    // ---- Text Editor ----

    function openEditor(itemId, zone) {
        editingItem = {id: itemId, zone: zone};
        fetch("/api/layout/item?id=" + encodeURIComponent(itemId))
            .then(function(r){return r.json()})
            .then(function(props) {
                document.getElementById("htmlInput").value = props.content || "Text";
                currentAlign = props.align || "center";
                currentAction = props.action || "";
                updateAlignButtons();
                renderActions();
                updatePreview();
                document.getElementById("editorPanel").classList.remove("editor-hidden");
            });
    }

    function closeEditor() {
        editingItem = null;
        document.getElementById("editorPanel").classList.add("editor-hidden");
    }

    function saveText() {
        if (!editingItem) return;
        var content = document.getElementById("htmlInput").value || "Text";
        var id = editingItem.id;
        var done = 0;
        var total = 3;
        function check() { done++; if (done >= total) loadLayout(); }
        apiPost("/api/layout/item", {itemId: id, key: "content", value: content}, check);
        apiPost("/api/layout/item", {itemId: id, key: "align", value: currentAlign}, check);
        apiPost("/api/layout/item", {itemId: id, key: "action", value: currentAction}, check);
    }

    function insertTag(open, close) {
        var el = document.getElementById("htmlInput");
        var start = el.selectionStart, end = el.selectionEnd;
        // Decode HTML entities for actual insertion
        var tmp = document.createElement("span");
        tmp.innerHTML = open; var openT = tmp.textContent;
        tmp.innerHTML = close; var closeT = tmp.textContent;
        var txt = el.value;
        if (start !== end) {
            var sel = txt.substring(start, end);
            el.value = txt.substring(0, start) + openT + sel + closeT + txt.substring(end);
            el.selectionStart = el.selectionEnd = end + openT.length + closeT.length;
        } else {
            el.value = txt.substring(0, start) + openT + closeT + txt.substring(start);
            el.selectionStart = el.selectionEnd = start + openT.length;
        }
        el.focus();
        updatePreview();
    }

    function insertFontSize(size) {
        insertTag('<span style="font-size:' + size + 'px">', '</span>');
    }

    function insertColor(color) {
        insertTag('<span style="color:' + color + '">', '</span>');
    }

    function insertVar(token) {
        var el = document.getElementById("htmlInput");
        var pos = el.selectionStart;
        var txt = el.value;
        el.value = txt.substring(0, pos) + token + txt.substring(pos);
        el.selectionStart = el.selectionEnd = pos + token.length;
        el.focus();
        updatePreview();
    }

    function setAlign(a) {
        currentAlign = a;
        updateAlignButtons();
        updatePreview();
    }

    function updateAlignButtons() {
        ["Left","Center","Right"].forEach(function(d) {
            var btn = document.getElementById("align" + d);
            btn.classList.toggle("active", currentAlign === d.toLowerCase());
        });
    }

    function renderActions() {
        var html = "";
        for (var i = 0; i < ACTIONS.length; i++) {
            var a = ACTIONS[i];
            var cls = "action-item" + (currentAction === a.id ? " selected" : "");
            html += '<div class="' + cls + '" onclick="selectAction(\'' + a.id + '\')">' + a.label + '</div>';
        }
        document.getElementById("actionList").innerHTML = html;
    }

    function selectAction(id) {
        currentAction = id;
        renderActions();
        updatePreview();
    }

    function updatePreview() {
        var text = document.getElementById("htmlInput").value || "";
        text = substitutePreview(text);
        var box = document.getElementById("previewBox");
        box.innerHTML = text;
        box.style.textAlign = currentAlign;
        box.className = "preview-box" + (currentAction ? " has-action" : "");
    }

    function substitutePreview(t) {
        var now = new Date();
        var hh = String(now.getHours()).padStart(2,"0");
        var mm = String(now.getMinutes()).padStart(2,"0");
        return t
            .replace(/%TEMP%/g,"92.3").replace(/%STEAM_TEMP%/g,"155.0")
            .replace(/%PRESSURE%/g,"9.0").replace(/%FLOW%/g,"2.1")
            .replace(/%WATER%/g,"78").replace(/%WATER_ML%/g,"850")
            .replace(/%STATE%/g,"Idle").replace(/%WEIGHT%/g,"36.2")
            .replace(/%SHOT_TIME%/g,"28.5").replace(/%VOLUME%/g,"42")
            .replace(/%TARGET_WEIGHT%/g,"36.0").replace(/%PROFILE%/g,"Adaptive v2")
            .replace(/%TARGET_TEMP%/g,"93.0").replace(/%RATIO%/g,"2.0")
            .replace(/%DOSE%/g,"18.0").replace(/%SCALE%/g,"Lunar")
            .replace(/%CONNECTED%/g,"Online").replace(/%CONNECTED_COLOR%/g,"#18c37e")
            .replace(/%DEVICES%/g,"Machine + Scale")
            .replace(/%TIME%/g,hh+":"+mm)
            .replace(/%DATE%/g,now.toISOString().split("T")[0]);
    }

    // Listen for input changes to update preview
    document.getElementById("htmlInput").addEventListener("input", updatePreview);

    function apiPost(url, data, cb) {
        fetch(url, {
            method: "POST",
            headers: {"Content-Type": "application/json"},
            body: JSON.stringify(data)
        }).then(function(r){return r.json()}).then(function(result) {
            if (cb) cb(result);
        });
    }

    // Initial load
    loadLayout();

    </script>
</body>
</html>
)HTML";

    return html;
}

// ============================================================================
// Settings Web UI
// ============================================================================

QString ShotServer::generateSettingsPage() const
{
    return QString(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>API Keys & Settings - Decenza DE1</title>
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --surface-hover: #1f2937;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
            --success: #18c37e;
            --error: #e73249;
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
        h1 { font-size: 1.125rem; font-weight: 600; flex: 1; }
        .container { max-width: 800px; margin: 0 auto; padding: 1.5rem; }
        .section {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            margin-bottom: 1.5rem;
            overflow: hidden;
        }
        .section-header {
            padding: 1rem 1.25rem;
            border-bottom: 1px solid var(--border);
            display: flex;
            align-items: center;
            gap: 0.75rem;
        }
        .section-header h2 {
            font-size: 1rem;
            font-weight: 600;
        }
        .section-icon { font-size: 1.25rem; }
        .section-body { padding: 1.25rem; }
        .form-group {
            margin-bottom: 1rem;
        }
        .form-group:last-child { margin-bottom: 0; }
        .form-label {
            display: block;
            font-size: 0.875rem;
            color: var(--text-secondary);
            margin-bottom: 0.375rem;
        }
        .form-input {
            width: 100%;
            padding: 0.625rem 0.875rem;
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: 6px;
            color: var(--text);
            font-size: 0.9375rem;
            font-family: inherit;
        }
        .form-input:focus {
            outline: none;
            border-color: var(--accent);
        }
        .form-input::placeholder { color: var(--text-secondary); }
        .form-row {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 1rem;
        }
)HTML" R"HTML(
        @media (max-width: 600px) {
            .form-row { grid-template-columns: 1fr; }
        }
        .form-checkbox {
            display: flex;
            align-items: center;
            gap: 0.5rem;
            cursor: pointer;
        }
        .form-checkbox input {
            width: 1.125rem;
            height: 1.125rem;
            accent-color: var(--accent);
        }
        .btn {
            padding: 0.75rem 1.5rem;
            border: none;
            border-radius: 6px;
            font-size: 0.9375rem;
            font-weight: 500;
            cursor: pointer;
            transition: all 0.15s;
        }
        .btn-primary {
            background: var(--accent);
            color: var(--bg);
        }
        .btn-primary:hover { filter: brightness(1.1); }
        .btn-primary:disabled {
            opacity: 0.5;
            cursor: not-allowed;
        }
        .save-bar {
            position: sticky;
            bottom: 0;
            background: var(--surface);
            border-top: 1px solid var(--border);
            padding: 1rem 1.5rem;
            display: flex;
            justify-content: flex-end;
            gap: 1rem;
            align-items: center;
        }
        .status-msg {
            font-size: 0.875rem;
            padding: 0.5rem 0.75rem;
            border-radius: 4px;
        }
        .status-success {
            background: rgba(24, 195, 126, 0.15);
            color: var(--success);
        }
        .status-error {
            background: rgba(231, 50, 73, 0.15);
            color: var(--error);
        }
        .help-text {
            font-size: 0.75rem;
            color: var(--text-secondary);
            margin-top: 0.25rem;
        }
        .password-wrapper {
            position: relative;
        }
        .password-toggle {
            position: absolute;
            right: 0.75rem;
            top: 50%;
            transform: translateY(-50%);
            background: none;
            border: none;
            color: var(--text-secondary);
            cursor: pointer;
            font-size: 1rem;
            padding: 0.25rem;
        }
        .password-toggle:hover { color: var(--text); }
    </style>
</head>)HTML" R"HTML(
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&larr;</a>
            <h1>API Keys & Settings</h1>
        </div>
    </header>

    <div class="container">
        <!-- Visualizer Section -->
        <div class="section">
            <div class="section-header">
                <span class="section-icon">&#9749;</span>
                <h2>Visualizer.coffee</h2>
            </div>
            <div class="section-body">
                <div class="form-group">
                    <label class="form-label">Username / Email</label>
                    <input type="text" class="form-input" id="visualizerUsername" placeholder="your@email.com">
                </div>
                <div class="form-group">
                    <label class="form-label">Password</label>
                    <div class="password-wrapper">
                        <input type="password" class="form-input" id="visualizerPassword" placeholder="Enter password">
                        <button type="button" class="password-toggle" onclick="togglePassword('visualizerPassword')">&#128065;</button>
                    </div>
                </div>
            </div>
        </div>

        <!-- AI Section -->
        <div class="section">
            <div class="section-header">
                <span class="section-icon">&#129302;</span>
                <h2>AI Dialing Assistant</h2>
            </div>
            <div class="section-body">
                <div class="form-group">
                    <label class="form-label">Provider</label>
                    <select class="form-input" id="aiProvider" onchange="updateAiFields()">
                        <option value="">Disabled</option>
                        <option value="openai">OpenAI (GPT-4)</option>
                        <option value="anthropic">Anthropic (Claude)</option>
                        <option value="gemini">Google (Gemini)</option>
                        <option value="openrouter">OpenRouter (Multi)</option>
                        <option value="ollama">Ollama (Local)</option>
                    </select>
                </div>
                <div class="form-group" id="openaiGroup" style="display:none;">
                    <label class="form-label">OpenAI API Key</label>
                    <div class="password-wrapper">
                        <input type="password" class="form-input" id="openaiApiKey" placeholder="sk-...">
                        <button type="button" class="password-toggle" onclick="togglePassword('openaiApiKey')">&#128065;</button>
                    </div>
                    <div class="help-text">Get your API key from <a href="https://platform.openai.com/api-keys" target="_blank" style="color:var(--accent)">platform.openai.com</a></div>
                </div>
                <div class="form-group" id="anthropicGroup" style="display:none;">
                    <label class="form-label">Anthropic API Key</label>
                    <div class="password-wrapper">
                        <input type="password" class="form-input" id="anthropicApiKey" placeholder="sk-ant-...">
                        <button type="button" class="password-toggle" onclick="togglePassword('anthropicApiKey')">&#128065;</button>
                    </div>
                    <div class="help-text">Get your API key from <a href="https://console.anthropic.com/settings/keys" target="_blank" style="color:var(--accent)">console.anthropic.com</a></div>
                </div>
                <div class="form-group" id="geminiGroup" style="display:none;">
                    <label class="form-label">Google Gemini API Key</label>
                    <div class="password-wrapper">
                        <input type="password" class="form-input" id="geminiApiKey" placeholder="AI...">
                        <button type="button" class="password-toggle" onclick="togglePassword('geminiApiKey')">&#128065;</button>
                    </div>
                    <div class="help-text">Get your API key from <a href="https://aistudio.google.com/apikey" target="_blank" style="color:var(--accent)">aistudio.google.com</a></div>
                </div>
                <div id="openrouterGroup" style="display:none;">
                    <div class="form-group">
                        <label class="form-label">OpenRouter API Key</label>
                        <div class="password-wrapper">
                            <input type="password" class="form-input" id="openrouterApiKey" placeholder="sk-or-...">
                            <button type="button" class="password-toggle" onclick="togglePassword('openrouterApiKey')">&#128065;</button>
                        </div>
                        <div class="help-text">Get your API key from <a href="https://openrouter.ai/keys" target="_blank" style="color:var(--accent)">openrouter.ai</a></div>
                    </div>
                    <div class="form-group">
                        <label class="form-label">Model</label>
                        <input type="text" class="form-input" id="openrouterModel" placeholder="anthropic/claude-sonnet-4">
                        <div class="help-text">Enter model ID from <a href="https://openrouter.ai/models" target="_blank" style="color:var(--accent)">openrouter.ai/models</a></div>
                    </div>
                </div>
                <div id="ollamaGroup" style="display:none;">
                    <div class="form-row">
                        <div class="form-group">
                            <label class="form-label">Ollama Endpoint</label>
                            <input type="text" class="form-input" id="ollamaEndpoint" placeholder="http://localhost:11434">
                        </div>
                        <div class="form-group">
                            <label class="form-label">Model</label>
                            <input type="text" class="form-input" id="ollamaModel" placeholder="llama3.2">
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <!-- MQTT Section -->
        <div class="section">
            <div class="section-header">
                <span class="section-icon">&#127968;</span>
                <h2>MQTT (Home Automation)</h2>
            </div>
            <div class="section-body">
                <div class="form-group">
                    <label class="form-checkbox">
                        <input type="checkbox" id="mqttEnabled" onchange="updateMqttFields()">
                        <span>Enable MQTT</span>
                    </label>
                </div>
                <div id="mqttFields" style="display:none;">
                    <div class="form-row">
                        <div class="form-group">
                            <label class="form-label">Broker Host</label>
                            <input type="text" class="form-input" id="mqttBrokerHost" placeholder="192.168.1.100">
                        </div>
                        <div class="form-group">
                            <label class="form-label">Port</label>
                            <input type="number" class="form-input" id="mqttBrokerPort" placeholder="1883">
                        </div>
                    </div>
                    <div class="form-row">
                        <div class="form-group">
                            <label class="form-label">Username (optional)</label>
                            <input type="text" class="form-input" id="mqttUsername" placeholder="mqtt_user">
                        </div>
                        <div class="form-group">
                            <label class="form-label">Password (optional)</label>
                            <div class="password-wrapper">
                                <input type="password" class="form-input" id="mqttPassword" placeholder="Enter password">
                                <button type="button" class="password-toggle" onclick="togglePassword('mqttPassword')">&#128065;</button>
                            </div>
                        </div>
                    </div>
                    <div class="form-group">
                        <label class="form-label">Base Topic</label>
                        <input type="text" class="form-input" id="mqttBaseTopic" placeholder="decenza">
                    </div>
                    <div class="form-row">
                        <div class="form-group">
                            <label class="form-label">Publish Interval (seconds)</label>
                            <input type="number" class="form-input" id="mqttPublishInterval" placeholder="5">
                        </div>
                        <div class="form-group">
                            <label class="form-label">Client ID (optional)</label>
                            <input type="text" class="form-input" id="mqttClientId" placeholder="decenza_de1">
                        </div>
                    </div>
                    <div class="form-group">
                        <label class="form-checkbox">
                            <input type="checkbox" id="mqttRetainMessages">
                            <span>Retain messages</span>
                        </label>
                    </div>
                    <div class="form-group">
                        <label class="form-checkbox">
                            <input type="checkbox" id="mqttHomeAssistantDiscovery">
                            <span>Home Assistant auto-discovery</span>
                        </label>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <div class="save-bar">
        <span id="statusMsg"></span>
        <button class="btn btn-primary" id="saveBtn" onclick="saveSettings()">Save Settings</button>
    </div>
)HTML" R"HTML(
    <script>
        // Load current settings on page load
        async function loadSettings() {
            try {
                const resp = await fetch('/api/settings');
                const data = await resp.json();

                // Visualizer
                document.getElementById('visualizerUsername').value = data.visualizerUsername || '';
                document.getElementById('visualizerPassword').value = data.visualizerPassword || '';

                // AI
                document.getElementById('aiProvider').value = data.aiProvider || '';
                document.getElementById('openaiApiKey').value = data.openaiApiKey || '';
                document.getElementById('anthropicApiKey').value = data.anthropicApiKey || '';
                document.getElementById('geminiApiKey').value = data.geminiApiKey || '';
                document.getElementById('openrouterApiKey').value = data.openrouterApiKey || '';
                document.getElementById('openrouterModel').value = data.openrouterModel || '';
                document.getElementById('ollamaEndpoint').value = data.ollamaEndpoint || 'http://localhost:11434';
                document.getElementById('ollamaModel').value = data.ollamaModel || 'llama3.2';
                updateAiFields();

                // MQTT
                document.getElementById('mqttEnabled').checked = data.mqttEnabled || false;
                document.getElementById('mqttBrokerHost').value = data.mqttBrokerHost || '';
                document.getElementById('mqttBrokerPort').value = data.mqttBrokerPort || 1883;
                document.getElementById('mqttUsername').value = data.mqttUsername || '';
                document.getElementById('mqttPassword').value = data.mqttPassword || '';
                document.getElementById('mqttBaseTopic').value = data.mqttBaseTopic || 'decenza';
                document.getElementById('mqttPublishInterval').value = data.mqttPublishInterval || 5;
                document.getElementById('mqttClientId').value = data.mqttClientId || '';
                document.getElementById('mqttRetainMessages').checked = data.mqttRetainMessages || false;
                document.getElementById('mqttHomeAssistantDiscovery').checked = data.mqttHomeAssistantDiscovery || false;
                updateMqttFields();
            } catch (e) {
                showStatus('Failed to load settings', true);
            }
        }

        function updateAiFields() {
            const provider = document.getElementById('aiProvider').value;
            document.getElementById('openaiGroup').style.display = provider === 'openai' ? 'block' : 'none';
            document.getElementById('anthropicGroup').style.display = provider === 'anthropic' ? 'block' : 'none';
            document.getElementById('geminiGroup').style.display = provider === 'gemini' ? 'block' : 'none';
            document.getElementById('openrouterGroup').style.display = provider === 'openrouter' ? 'block' : 'none';
            document.getElementById('ollamaGroup').style.display = provider === 'ollama' ? 'block' : 'none';
        }

        function updateMqttFields() {
            const enabled = document.getElementById('mqttEnabled').checked;
            document.getElementById('mqttFields').style.display = enabled ? 'block' : 'none';
        }

        function togglePassword(id) {
            const input = document.getElementById(id);
            input.type = input.type === 'password' ? 'text' : 'password';
        }

        async function saveSettings() {
            const btn = document.getElementById('saveBtn');
            btn.disabled = true;
            btn.textContent = 'Saving...';

            const data = {
                // Visualizer
                visualizerUsername: document.getElementById('visualizerUsername').value,
                visualizerPassword: document.getElementById('visualizerPassword').value,

                // AI
                aiProvider: document.getElementById('aiProvider').value,
                openaiApiKey: document.getElementById('openaiApiKey').value,
                anthropicApiKey: document.getElementById('anthropicApiKey').value,
                geminiApiKey: document.getElementById('geminiApiKey').value,
                openrouterApiKey: document.getElementById('openrouterApiKey').value,
                openrouterModel: document.getElementById('openrouterModel').value,
                ollamaEndpoint: document.getElementById('ollamaEndpoint').value,
                ollamaModel: document.getElementById('ollamaModel').value,

                // MQTT
                mqttEnabled: document.getElementById('mqttEnabled').checked,
                mqttBrokerHost: document.getElementById('mqttBrokerHost').value,
                mqttBrokerPort: parseInt(document.getElementById('mqttBrokerPort').value) || 1883,
                mqttUsername: document.getElementById('mqttUsername').value,
                mqttPassword: document.getElementById('mqttPassword').value,
                mqttBaseTopic: document.getElementById('mqttBaseTopic').value,
                mqttPublishInterval: parseInt(document.getElementById('mqttPublishInterval').value) || 5,
                mqttClientId: document.getElementById('mqttClientId').value,
                mqttRetainMessages: document.getElementById('mqttRetainMessages').checked,
                mqttHomeAssistantDiscovery: document.getElementById('mqttHomeAssistantDiscovery').checked
            };

            try {
                const resp = await fetch('/api/settings', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(data)
                });
                const result = await resp.json();
                if (result.success) {
                    showStatus('Settings saved successfully!', false);
                } else {
                    showStatus(result.error || 'Failed to save', true);
                }
            } catch (e) {
                showStatus('Network error', true);
            }

            btn.disabled = false;
            btn.textContent = 'Save Settings';
        }

        function showStatus(msg, isError) {
            const el = document.getElementById('statusMsg');
            el.textContent = msg;
            el.className = 'status-msg ' + (isError ? 'status-error' : 'status-success');
            setTimeout(() => { el.textContent = ''; el.className = ''; }, 4000);
        }

        loadSettings();
    </script>
</body>
</html>
)HTML");
}

void ShotServer::handleGetSettings(QTcpSocket* socket)
{
    if (!m_settings) {
        sendJson(socket, R"({"error": "Settings not available"})");
        return;
    }

    QJsonObject obj;

    // Visualizer
    obj["visualizerUsername"] = m_settings->visualizerUsername();
    obj["visualizerPassword"] = m_settings->visualizerPassword();

    // AI
    obj["aiProvider"] = m_settings->aiProvider();
    obj["openaiApiKey"] = m_settings->openaiApiKey();
    obj["anthropicApiKey"] = m_settings->anthropicApiKey();
    obj["geminiApiKey"] = m_settings->geminiApiKey();
    obj["openrouterApiKey"] = m_settings->openrouterApiKey();
    obj["openrouterModel"] = m_settings->openrouterModel();
    obj["ollamaEndpoint"] = m_settings->ollamaEndpoint();
    obj["ollamaModel"] = m_settings->ollamaModel();

    // MQTT
    obj["mqttEnabled"] = m_settings->mqttEnabled();
    obj["mqttBrokerHost"] = m_settings->mqttBrokerHost();
    obj["mqttBrokerPort"] = m_settings->mqttBrokerPort();
    obj["mqttUsername"] = m_settings->mqttUsername();
    obj["mqttPassword"] = m_settings->mqttPassword();
    obj["mqttBaseTopic"] = m_settings->mqttBaseTopic();
    obj["mqttPublishInterval"] = m_settings->mqttPublishInterval();
    obj["mqttClientId"] = m_settings->mqttClientId();
    obj["mqttRetainMessages"] = m_settings->mqttRetainMessages();
    obj["mqttHomeAssistantDiscovery"] = m_settings->mqttHomeAssistantDiscovery();

    sendJson(socket, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void ShotServer::handleSaveSettings(QTcpSocket* socket, const QByteArray& body)
{
    if (!m_settings) {
        sendJson(socket, R"({"error": "Settings not available"})");
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError) {
        sendJson(socket, R"({"error": "Invalid JSON"})");
        return;
    }

    QJsonObject obj = doc.object();

    // Visualizer
    if (obj.contains("visualizerUsername"))
        m_settings->setVisualizerUsername(obj["visualizerUsername"].toString());
    if (obj.contains("visualizerPassword"))
        m_settings->setVisualizerPassword(obj["visualizerPassword"].toString());

    // AI
    if (obj.contains("aiProvider"))
        m_settings->setAiProvider(obj["aiProvider"].toString());
    if (obj.contains("openaiApiKey"))
        m_settings->setOpenaiApiKey(obj["openaiApiKey"].toString());
    if (obj.contains("anthropicApiKey"))
        m_settings->setAnthropicApiKey(obj["anthropicApiKey"].toString());
    if (obj.contains("geminiApiKey"))
        m_settings->setGeminiApiKey(obj["geminiApiKey"].toString());
    if (obj.contains("openrouterApiKey"))
        m_settings->setOpenrouterApiKey(obj["openrouterApiKey"].toString());
    if (obj.contains("openrouterModel"))
        m_settings->setOpenrouterModel(obj["openrouterModel"].toString());
    if (obj.contains("ollamaEndpoint"))
        m_settings->setOllamaEndpoint(obj["ollamaEndpoint"].toString());
    if (obj.contains("ollamaModel"))
        m_settings->setOllamaModel(obj["ollamaModel"].toString());

    // MQTT
    if (obj.contains("mqttEnabled"))
        m_settings->setMqttEnabled(obj["mqttEnabled"].toBool());
    if (obj.contains("mqttBrokerHost"))
        m_settings->setMqttBrokerHost(obj["mqttBrokerHost"].toString());
    if (obj.contains("mqttBrokerPort"))
        m_settings->setMqttBrokerPort(obj["mqttBrokerPort"].toInt());
    if (obj.contains("mqttUsername"))
        m_settings->setMqttUsername(obj["mqttUsername"].toString());
    if (obj.contains("mqttPassword"))
        m_settings->setMqttPassword(obj["mqttPassword"].toString());
    if (obj.contains("mqttBaseTopic"))
        m_settings->setMqttBaseTopic(obj["mqttBaseTopic"].toString());
    if (obj.contains("mqttPublishInterval"))
        m_settings->setMqttPublishInterval(obj["mqttPublishInterval"].toInt());
    if (obj.contains("mqttClientId"))
        m_settings->setMqttClientId(obj["mqttClientId"].toString());
    if (obj.contains("mqttRetainMessages"))
        m_settings->setMqttRetainMessages(obj["mqttRetainMessages"].toBool());
    if (obj.contains("mqttHomeAssistantDiscovery"))
        m_settings->setMqttHomeAssistantDiscovery(obj["mqttHomeAssistantDiscovery"].toBool());

    sendJson(socket, R"({"success": true})");
}

