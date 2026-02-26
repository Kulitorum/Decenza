#include "shotserver.h"
#include "webdebuglogger.h"
#include "webtemplates.h"
#include "webtemplates/auth_page.h"
#include "../history/shothistorystorage.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../screensaver/screensavervideomanager.h"
#include "../core/settings.h"
#include "../core/profilestorage.h"
#include "../core/settingsserializer.h"
#include "../ai/aimanager.h"
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
#include <QRandomGenerator>
#include <QSslServer>
#include <QSocketNotifier>

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifndef Q_OS_IOS
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/rand.h>
#endif

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

// ---------------------------------------------------------------------------
// HttpRedirectSslServer – QSslServer subclass that detects plain HTTP
// connections and replies with a 301 redirect to https://.
// ---------------------------------------------------------------------------
class HttpRedirectSslServer : public QSslServer {
public:
    explicit HttpRedirectSslServer(int port, QObject* parent = nullptr)
        : QSslServer(parent), m_port(port) {}

    ~HttpRedirectSslServer() override {
        // Close any file descriptors still waiting for data
        for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
            delete it.value().notifier;
            delete it.value().timer;
            closeFd(it.key());
        }
        m_pending.clear();
    }

protected:
    void incomingConnection(qintptr fd) override {
        // We can't peek yet – data may not have arrived. Use QSocketNotifier
        // to wait asynchronously for the first byte.
        auto* notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        auto* timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(5000);

        PendingConn pc;
        pc.notifier = notifier;
        pc.timer = timer;
        m_pending.insert(fd, pc);

        connect(notifier, &QSocketNotifier::activated, this, [this, fd]() {
            handleFirstByte(fd);
        });

        connect(timer, &QTimer::timeout, this, [this, fd]() {
            // Timeout – no data arrived; clean up
            cleanupPending(fd);
            closeFd(fd);
        });

        timer->start();
    }

private:
    struct PendingConn {
        QSocketNotifier* notifier = nullptr;
        QTimer* timer = nullptr;
    };
    QHash<qintptr, PendingConn> m_pending;
    int m_port;

    // Platform-appropriate socket type from qintptr
#ifdef Q_OS_WIN
    static SOCKET toNativeFd(qintptr fd) { return static_cast<SOCKET>(fd); }
#else
    static int toNativeFd(qintptr fd) { return static_cast<int>(fd); }
#endif

    void handleFirstByte(qintptr fd) {
        auto it = m_pending.find(fd);
        if (it == m_pending.end()) return;

        // Disable notifier immediately to avoid re-entry
        it.value().notifier->setEnabled(false);

        unsigned char peek = 0;
        int n = ::recv(toNativeFd(fd), reinterpret_cast<char*>(&peek), 1, MSG_PEEK);

        if (n <= 0) {
            cleanupPending(fd);
            closeFd(fd);
            return;
        }

        cleanupPending(fd);

        if (peek == 0x16) {
            // TLS ClientHello – hand off to QSslServer for normal processing
            QSslServer::incomingConnection(fd);
        } else {
            // Plain HTTP – read request, send redirect, close
            sendHttpRedirect(fd);
        }
    }

    void sendHttpRedirect(qintptr fd) {
        // Read whatever HTTP data is available (up to 4 KB is plenty for a request line + headers)
        char buf[4096];
        int n = ::recv(toNativeFd(fd), buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            closeFd(fd);
            return;
        }
        buf[n] = '\0';

        // Parse path from "GET /path HTTP/1.x"
        QString path = "/";
        QByteArray request(buf, n);
        int firstSpace = request.indexOf(' ');
        if (firstSpace > 0) {
            int secondSpace = request.indexOf(' ', firstSpace + 1);
            if (secondSpace > firstSpace + 1) {
                path = QString::fromLatin1(request.mid(firstSpace + 1, secondSpace - firstSpace - 1));
            }
        }

        // Parse Host header
        QString host = "localhost";
        int hostIdx = request.indexOf("Host:");
        if (hostIdx < 0) hostIdx = request.indexOf("host:");
        if (hostIdx >= 0) {
            int start = hostIdx + 5;
            while (start < request.size() && request.at(start) == ' ') ++start;
            int end = request.indexOf('\r', start);
            if (end < 0) end = request.indexOf('\n', start);
            if (end > start) {
                host = QString::fromLatin1(request.mid(start, end - start));
                // Strip port from host if present (we'll add our own)
                int colonIdx = host.lastIndexOf(':');
                if (colonIdx > 0) host = host.left(colonIdx);
            }
        }

        // Sanitize host and path to prevent header injection via CRLF
        static QRegularExpression validHost(QStringLiteral("^[a-zA-Z0-9.\\-]+$"));
        if (!validHost.match(host).hasMatch())
            host = QStringLiteral("localhost");
        path.remove(QChar('\r'));
        path.remove(QChar('\n'));

        QString location = QString("https://%1:%2%3").arg(host).arg(m_port).arg(path);
        QByteArray response =
            "HTTP/1.1 301 Moved Permanently\r\n"
            "Location: " + location.toLatin1() + "\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n";

        ::send(toNativeFd(fd), response.constData(), response.size(), 0);
        closeFd(fd);
    }

    void cleanupPending(qintptr fd) {
        auto it = m_pending.find(fd);
        if (it != m_pending.end()) {
            delete it.value().notifier;
            delete it.value().timer;
            m_pending.erase(it);
        }
    }

    static void closeFd(qintptr fd) {
#ifdef Q_OS_WIN
        ::closesocket(toNativeFd(fd));
#else
        ::close(toNativeFd(fd));
#endif
    }
};

// ---------------------------------------------------------------------------

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

void ShotServer::setSettings(Settings* settings)
{
    m_settings = settings;
    if (m_settings) {
        connect(m_settings, &Settings::layoutConfigurationChanged,
                this, &ShotServer::onLayoutChanged);
        connect(m_settings, &Settings::customThemeColorsChanged,
                this, &ShotServer::onThemeChanged);
        connect(m_settings, &Settings::customFontSizesChanged,
                this, &ShotServer::onThemeChanged);
        connect(m_settings, &Settings::activeThemeNameChanged,
                this, &ShotServer::onThemeChanged);
        connect(m_settings, &Settings::currentPageColorsChanged,
                this, &ShotServer::onThemeChanged);
    }
}

void ShotServer::onLayoutChanged()
{
    // Notify all SSE clients that the layout has changed
    QByteArray event = "event: layout-changed\ndata: {}\n\n";
    QList<QTcpSocket*> dead;
    for (QTcpSocket* client : m_sseLayoutClients) {
        if (client->state() != QAbstractSocket::ConnectedState) {
            dead.append(client);
            continue;
        }
        client->write(event);
        client->flush();
    }
    for (QTcpSocket* s : dead) {
        m_sseLayoutClients.remove(s);
    }
}

void ShotServer::onThemeChanged()
{
    // Notify all SSE clients that the theme has changed
    QByteArray event = "event: theme-changed\ndata: {}\n\n";
    QList<QTcpSocket*> dead;
    for (QTcpSocket* client : m_sseThemeClients) {
        if (client->state() != QAbstractSocket::ConnectedState) {
            dead.append(client);
            continue;
        }
        client->write(event);
        client->flush();
    }
    for (QTcpSocket* s : dead) {
        m_sseThemeClients.remove(s);
    }
}

QString ShotServer::url() const
{
    if (!isRunning()) return QString();
    QString scheme = isSecurityEnabled() ? "https" : "http";
    return QString("%1://%2:%3").arg(scheme, getLocalIpAddress()).arg(m_port);
}

bool ShotServer::isSecurityEnabled() const
{
    return m_settings && m_settings->webSecurityEnabled();
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

    if (isSecurityEnabled()) {
        // Set up TLS with self-signed certificate
        if (!setupTls()) {
            qWarning() << "ShotServer: TLS setup failed, cannot start secure server";
            return false;
        } else {
            auto* sslServer = new HttpRedirectSslServer(m_port, this);
            QSslConfiguration sslConfig;
            sslConfig.setLocalCertificate(m_sslCert);
            sslConfig.setPrivateKey(m_sslKey);
            sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
            sslServer->setSslConfiguration(sslConfig);
            m_server = sslServer;
            // Use pendingConnectionAvailable (not newConnection) for QSslServer:
            // newConnection fires on TCP accept (before SSL handshake), when hasPendingConnections() is still false.
            // pendingConnectionAvailable fires after handshake completes and socket is added to the pending queue.
            connect(m_server, &QTcpServer::pendingConnectionAvailable, this, &ShotServer::onNewConnection);
            connect(sslServer, &QSslServer::sslErrors, this, [](QSslSocket* socket, const QList<QSslError>& errors) {
                for (const auto& err : errors)
                    qWarning() << "ShotServer: SSL error:" << err.errorString();
            });
            connect(sslServer, &QSslServer::handshakeInterruptedOnError, this, [](QSslSocket* socket, const QSslError& error) {
                qWarning() << "ShotServer: SSL handshake interrupted:" << error.errorString();
            });
            connect(sslServer, &QSslServer::peerVerifyError, this, [](QSslSocket* socket, const QSslError& error) {
                qWarning() << "ShotServer: SSL peer verify error:" << error.errorString();
            });


            if (!m_server->listen(QHostAddress::Any, m_port)) {
                qWarning() << "ShotServer: Failed to start TLS on port" << m_port << m_server->errorString();
                delete m_server;
                m_server = nullptr;
                return false;
            }

            // Load persisted sessions
            loadSessions();

            qDebug() << "ShotServer: HTTPS mode enabled";
        }
    }

    if (!m_server) {
        // Plain HTTP mode (security disabled)
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, &ShotServer::onNewConnection);

        if (!m_server->listen(QHostAddress::Any, m_port)) {
            qWarning() << "ShotServer: Failed to start on port" << m_port << m_server->errorString();
            delete m_server;
            m_server = nullptr;
            return false;
        }
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
        m_sseLayoutClients.clear();
        m_sseThemeClients.clear();
        m_keepAliveTimers.clear();
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

    // SSE clients keep connections open — ignore further data from them
    if (m_sseLayoutClients.contains(socket)) return;
    if (m_sseThemeClients.contains(socket)) return;

    // Stop keep-alive idle timer while processing incoming request data
    if (QTimer* t = m_keepAliveTimers.value(socket))
        t->stop();

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
        m_sseLayoutClients.remove(socket);
        m_sseThemeClients.remove(socket);
        m_keepAliveTimers.remove(socket);
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
        QString addr = (socket->state() != QAbstractSocket::UnconnectedState)
            ? socket->peerAddress().toString() : "unknown";
        qWarning() << "ShotServer: Cleaning up stale connection from" << addr;
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
            response["secure"] = isSecurityEnabled();

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

    // Don't log polling requests (too noisy)
    if (!path.startsWith("/api/debug") && path != "/api/settings/mqtt/status" && path != "/api/telemetry") {
        qDebug() << "ShotServer:" << method << path;
    }

    // Auth middleware: when security is enabled, check session before routing
    if (isSecurityEnabled()) {
        bool isAuthRoute = path.startsWith("/auth/") || path.startsWith("/api/auth/");
        bool exempt = isAuthRoute || path == "/favicon.ico";

        if (!exempt && !checkSession(request)) {
            if (hasStoredTotpSecret()) {
                sendResponse(socket, 401, "text/html; charset=utf-8", QByteArray(WEB_AUTH_LOGIN_PAGE));
            } else {
                sendResponse(socket, 401, "text/html; charset=utf-8", QByteArray(WEB_AUTH_SETUP_REQUIRED_PAGE));
            }
            return;
        }

        // Handle auth API routes
        if (isAuthRoute) {
            // Extract body for POST requests
            QByteArray body;
            int bodyStart = request.indexOf("\r\n\r\n");
            if (bodyStart != -1) {
                body = request.mid(bodyStart + 4);
            }
            // Store full request on socket for auth handlers that need cookie access
            socket->setProperty("fullRequest", request);
            handleAuthRoute(socket, method, path, body);
            return;
        }
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
    else if (path.startsWith("/api/shot/") && path.endsWith("/metadata") && method == "POST") {
        // POST /api/shot/123/metadata - update shot metadata
        QString idPart = path.mid(10); // Remove "/api/shot/"
        idPart = idPart.left(idPart.indexOf("/metadata"));
        bool ok;
        qint64 shotId = idPart.toLongLong(&ok);
        if (!ok) {
            sendResponse(socket, 400, "application/json", R"({"error":"Invalid shot ID"})");
            return;
        }
        int bodyStart = request.indexOf("\r\n\r\n");
        if (bodyStart == -1) {
            sendResponse(socket, 400, "application/json", R"({"error":"Invalid request"})");
            return;
        }
        QByteArray body = request.mid(bodyStart + 4);
        QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Invalid JSON"})");
            return;
        }
        QVariantMap metadata = doc.object().toVariantMap();
        if (m_storage->updateShotMetadata(shotId, metadata)) {
            sendJson(socket, R"({"success":true})");
        } else {
            sendResponse(socket, 500, "application/json", R"({"error":"Failed to update metadata"})");
        }
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
    else if (path == "/api/shots/delete" && method == "POST") {
        int bodyStart = request.indexOf("\r\n\r\n");
        if (bodyStart == -1) {
            sendResponse(socket, 400, "application/json", R"({"error":"Invalid request"})");
            return;
        }
        QByteArray body = request.mid(bodyStart + 4);
        QJsonDocument doc = QJsonDocument::fromJson(body);
        QJsonArray ids = doc.object().value("ids").toArray();
        int deleted = 0;
        for (const QJsonValue& v : std::as_const(ids)) {
            qint64 id = v.toInteger();
            if (id > 0 && m_storage->deleteShot(id))
                deleted++;
        }
        sendJson(socket, QString(R"({"deleted":%1})").arg(deleted).toUtf8());
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
    else if (path == "/api/settings/visualizer/test" && method == "POST") {
        int bodyStart = request.indexOf("\r\n\r\n");
        if (bodyStart != -1) {
            handleVisualizerTest(socket, request.mid(bodyStart + 4));
        } else {
            sendJson(socket, R"({"success": false, "message": "Invalid request"})");
        }
    }
    else if (path == "/api/settings/ai/test" && method == "POST") {
        int bodyStart = request.indexOf("\r\n\r\n");
        if (bodyStart != -1) {
            handleAiTest(socket, request.mid(bodyStart + 4));
        } else {
            sendJson(socket, R"({"success": false, "message": "Invalid request"})");
        }
    }
    else if (path == "/api/settings/mqtt/connect" && method == "POST") {
        int bodyStart = request.indexOf("\r\n\r\n");
        QByteArray body = (bodyStart != -1) ? request.mid(bodyStart + 4) : QByteArray();
        handleMqttConnect(socket, body);
    }
    else if (path == "/api/settings/mqtt/disconnect" && method == "POST") {
        handleMqttDisconnect(socket);
    }
    else if (path == "/api/settings/mqtt/status") {
        handleMqttStatus(socket);
    }
    else if (path == "/api/settings/mqtt/publish-discovery" && method == "POST") {
        handleMqttPublishDiscovery(socket);
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
    else if (path == "/api/saved-searches") {
        if (method == "GET") {
            QJsonArray arr;
            if (m_settings) {
                for (const QString& s : m_settings->savedSearches()) {
                    arr.append(s);
                }
            }
            QJsonObject result;
            result["searches"] = arr;
            sendJson(socket, QJsonDocument(result).toJson(QJsonDocument::Compact));
        } else if (method == "POST") {
            int bodyStart = request.indexOf("\r\n\r\n");
            QJsonObject result;
            bool hasError = false;
            if (bodyStart != -1 && m_settings) {
                QByteArray body = request.mid(bodyStart + 4);
                QJsonObject obj = QJsonDocument::fromJson(body).object();
                QString search = obj["search"].toString().trimmed();
                if (!search.isEmpty()) {
                    m_settings->addSavedSearch(search);
                    result["success"] = true;
                } else {
                    result["error"] = "Empty search";
                    hasError = true;
                }
            } else {
                result["error"] = "Invalid request";
                hasError = true;
            }
            QByteArray json = QJsonDocument(result).toJson(QJsonDocument::Compact);
            if (hasError)
                sendResponse(socket, 400, "application/json", json);
            else
                sendJson(socket, json);
        } else if (method == "DELETE") {
            int bodyStart = request.indexOf("\r\n\r\n");
            QJsonObject result;
            bool hasError = false;
            if (bodyStart != -1 && m_settings) {
                QByteArray body = request.mid(bodyStart + 4);
                QJsonObject obj = QJsonDocument::fromJson(body).object();
                QString search = obj["search"].toString().trimmed();
                if (!search.isEmpty()) {
                    m_settings->removeSavedSearch(search);
                    result["success"] = true;
                } else {
                    result["error"] = "Empty search";
                    hasError = true;
                }
            } else {
                result["error"] = "Invalid request";
                hasError = true;
            }
            QByteArray json = QJsonDocument(result).toJson(QJsonDocument::Compact);
            if (hasError)
                sendResponse(socket, 400, "application/json", json);
            else
                sendJson(socket, json);
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
        // Category can be compound (e.g., "external/user"), so split on LAST slash
        QString remainder = path.mid(20);  // After "/api/backup/profile/"
        int slashIdx = remainder.lastIndexOf('/');
        if (slashIdx > 0) {
            QString category = remainder.left(slashIdx);
            QString filename = QUrl::fromPercentEncoding(remainder.mid(slashIdx + 1).toUtf8());
            handleBackupProfileFile(socket, category, filename);
        } else {
            sendResponse(socket, 400, "application/json", R"({"error":"Invalid profile path"})");
        }
    }
    else if (path == "/api/backup/shots") {
        // Create a safe backup copy in temp directory, then send it
        // This ensures database is properly checkpointed and closed during copy
        QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QString tempPath = tempDir + "/backup_web_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".db";

        QString result = m_storage->createBackup(tempPath);
        if (!result.isEmpty()) {
            sendFile(socket, tempPath, "application/x-sqlite3");  // Reads file fully into memory
            QFile::remove(tempPath);
        } else {
            // Clean up temp file if it was partially created
            if (QFile::exists(tempPath)) {
                QFile::remove(tempPath);
            }
            sendResponse(socket, 500, "application/json", R"({"error":"Failed to create backup"})");
        }
    }
    else if (path == "/api/backup/media") {
        handleBackupMediaList(socket);
    }
    else if (path.startsWith("/api/backup/media/")) {
        // /api/backup/media/{filename} - download individual media file
        QString filename = QUrl::fromPercentEncoding(path.mid(18).toUtf8());
        handleBackupMediaFile(socket, filename);
    }
    else if (path == "/api/backup/ai-conversations") {
        handleBackupAIConversations(socket);
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
    // Theme editor
    else if (path == "/theme") {
        sendHtml(socket, generateThemePage());
    }
    // SSE endpoint for theme change notifications
    else if (path == "/api/theme/subscribe" && method == "GET") {
        QByteArray sseHeaders = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/event-stream\r\n"
                             "Cache-Control: no-cache\r\n"
                             "Connection: keep-alive\r\n"
                             "Access-Control-Allow-Origin: *\r\n\r\n";
        socket->write(sseHeaders);
        socket->flush();
        m_sseThemeClients.insert(socket);
        if (QTimer* t = m_keepAliveTimers.take(socket))
            t->stop();
    }
    // Theme API endpoints
    else if (path == "/api/theme" || path.startsWith("/api/theme/")) {
        qsizetype headerEndPos = request.indexOf("\r\n\r\n");
        QByteArray body = (headerEndPos >= 0) ? request.mid(headerEndPos + 4) : QByteArray();
        handleThemeApi(socket, method, path, body);
    }
    // Layout editor
    else if (path == "/layout") {
        sendHtml(socket, generateLayoutPage());
    }
    // SSE endpoint for layout change notifications
    else if (path == "/api/layout/events" && method == "GET") {
        QByteArray headers = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/event-stream\r\n"
                             "Cache-Control: no-cache\r\n"
                             "Connection: keep-alive\r\n"
                             "Access-Control-Allow-Origin: *\r\n\r\n";
        socket->write(headers);
        socket->flush();
        m_sseLayoutClients.insert(socket);
        if (QTimer* t = m_keepAliveTimers.take(socket))
            t->stop();
    }
    else if (path == "/api/layout" || path.startsWith("/api/layout/") || path.startsWith("/api/layout?")
             || path.startsWith("/api/library") || path.startsWith("/api/community")) {
        qsizetype headerEndPos = request.indexOf("\r\n\r\n");
        QByteArray body = (headerEndPos >= 0) ? request.mid(headerEndPos + 4) : QByteArray();
        handleLayoutApi(socket, method, path, body);
    }
    else if (path == "/ai-conversations") {
        sendHtml(socket, generateAIConversationsPage());
    }
    else if (path.startsWith("/api/ai-conversation/")) {
        // /api/ai-conversation/<key>/download?format=json|text
        QString remainder = path.mid(QString("/api/ai-conversation/").length());
        int slashIdx = remainder.indexOf('/');
        QString key = (slashIdx >= 0) ? remainder.left(slashIdx) : remainder;
        // Parse format from query string
        QString format = "json";
        if (path.contains("?")) {
            QUrlQuery query(path.mid(path.indexOf("?") + 1));
            if (query.queryItemValue("format") == "text") format = "text";
        }
        handleAIConversationDownload(socket, key, format);
    }
    else if (path.startsWith("/icons/") && path.endsWith(".svg")) {
        // Serve SVG icons from Qt resources for web layout editor
        QString resourcePath = ":/" + path.mid(1); // ":/icons/espresso.svg"
        QFile iconFile(resourcePath);
        if (iconFile.open(QIODevice::ReadOnly)) {
            QByteArray svgData = iconFile.readAll();
            sendResponse(socket, 200, "image/svg+xml", svgData);
        } else {
            sendResponse(socket, 404, "text/plain", "Icon not found");
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
        case 302: statusText = "Found"; break;
        case 400: statusText = "Bad Request"; break;
        case 401: statusText = "Unauthorized"; break;
        case 404: statusText = "Not Found"; break;
        case 413: statusText = "Payload Too Large"; break;
        case 429: statusText = "Too Many Requests"; break;
        case 503: statusText = "Service Unavailable"; break;
        default: statusText = "Unknown"; break;
    }

    QByteArray response;
    response.append(QString("HTTP/1.1 %1 %2\r\n").arg(statusCode).arg(statusText).toUtf8());
    response.append(QString("Content-Type: %1\r\n").arg(contentType).toUtf8());
    response.append(QString("Content-Length: %1\r\n").arg(body.size()).toUtf8());
    if (!isSecurityEnabled()) {
        response.append("Access-Control-Allow-Origin: *\r\n");
    }
    response.append("Connection: keep-alive\r\n");
    response.append(QString("Keep-Alive: timeout=%1\r\n").arg(KEEPALIVE_TIMEOUT_S).toUtf8());
    if (!extraHeaders.isEmpty()) {
        response.append(extraHeaders);
    }
    response.append("\r\n");
    response.append(body);

    socket->write(response);
    socket->flush();

    // Reset idle timer — close connection if no new request arrives
    resetKeepAliveTimer(socket);
}

void ShotServer::resetKeepAliveTimer(QTcpSocket* socket)
{
    QTimer* timer = m_keepAliveTimers.value(socket);
    if (!timer) {
        timer = new QTimer(socket);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, socket, [this, socket]() {
            m_keepAliveTimers.remove(socket);
            socket->disconnectFromHost();
        });
        m_keepAliveTimers[socket] = timer;
    }
    timer->start(KEEPALIVE_TIMEOUT_S * 1000);
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

    qint64 fileSize = file.size();
    QString filename = QFileInfo(path).fileName();
    filename.replace(QRegularExpression("[^a-zA-Z0-9_.-]"), "_");

    // Send headers
    QByteArray headers = QString(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %1\r\n"
        "Content-Length: %2\r\n"
        "Content-Disposition: attachment; filename=\"%3\"\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n"
    ).arg(contentType).arg(fileSize).arg(filename).toUtf8();

    if (socket->write(headers) == -1) {
        qWarning() << "ShotServer::sendFile: Failed to write headers -" << socket->errorString();
        socket->abort();
        return;
    }

    // Stream file in 64KB chunks to avoid OOM on large databases
    const qint64 chunkSize = 64 * 1024;
    bool success = true;
    while (!file.atEnd()) {
        QByteArray chunk = file.read(chunkSize);
        if (chunk.isEmpty() && !file.atEnd()) {
            qWarning() << "ShotServer::sendFile: File read error -" << file.errorString();
            success = false;
            break;
        }
        if (socket->write(chunk) == -1) {
            qWarning() << "ShotServer::sendFile: Socket write failed -" << socket->errorString();
            success = false;
            break;
        }
        if (!socket->waitForBytesWritten(5000)) {
            qWarning() << "ShotServer::sendFile: Write timed out";
            success = false;
            break;
        }
    }

    if (success) {
        socket->flush();
        socket->disconnectFromHost();
    } else {
        socket->abort();
    }
}

void ShotServer::sendRedirect(QTcpSocket* socket, const QString& location, const QString& setCookie)
{
    QByteArray response;
    response.append("HTTP/1.1 302 Found\r\n");
    response.append(QString("Location: %1\r\n").arg(location).toUtf8());
    if (!setCookie.isEmpty()) {
        response.append(QString("Set-Cookie: %1\r\n").arg(setCookie).toUtf8());
    }
    response.append("Content-Length: 0\r\n");
    response.append("Connection: keep-alive\r\n");
    response.append(QString("Keep-Alive: timeout=%1\r\n").arg(KEEPALIVE_TIMEOUT_S).toUtf8());
    response.append("\r\n");
    socket->write(response);
    socket->flush();
    resetKeepAliveTimer(socket);
}

#ifdef Q_OS_IOS
bool ShotServer::setupTls()
{
    qWarning() << "ShotServer: TLS not available on iOS (OpenSSL not linked)";
    return false;
}

bool ShotServer::generateSelfSignedCert(const QString&, const QString&)
{
    return false;
}
#else
bool ShotServer::setupTls()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    QString certPath = dataDir + "/server.crt";
    QString keyPath = dataDir + "/server.key";

    // Check if cert/key already exist
    if (QFile::exists(certPath) && QFile::exists(keyPath)) {
        QFile certFile(certPath);
        QFile keyFile(keyPath);
        if (certFile.open(QIODevice::ReadOnly) && keyFile.open(QIODevice::ReadOnly)) {
            m_sslCert = QSslCertificate(certFile.readAll(), QSsl::Pem);
            m_sslKey = QSslKey(keyFile.readAll(), QSsl::Ec, QSsl::Pem);
            if (!m_sslCert.isNull() && !m_sslKey.isNull()) {
                if (m_sslCert.expiryDate() <= QDateTime::currentDateTime()) {
                    qDebug() << "ShotServer: TLS certificate expired, regenerating";
                } else if (m_sslCert.subjectAlternativeNames().isEmpty()) {
                    qDebug() << "ShotServer: TLS certificate missing SANs (required by browsers), regenerating";
                } else {
                    qDebug() << "ShotServer: Loaded existing TLS certificate, expires" << m_sslCert.expiryDate().toString();
                    return true;
                }
            }
        }
    }

    // Generate new self-signed certificate
    if (!generateSelfSignedCert(certPath, keyPath)) {
        qWarning() << "ShotServer: Failed to generate self-signed certificate";
        return false;
    }

    // Load the newly generated cert/key
    QFile certFile(certPath);
    QFile keyFile(keyPath);
    if (!certFile.open(QIODevice::ReadOnly) || !keyFile.open(QIODevice::ReadOnly)) {
        qWarning() << "ShotServer: Failed to read generated certificate files";
        return false;
    }

    m_sslCert = QSslCertificate(certFile.readAll(), QSsl::Pem);
    m_sslKey = QSslKey(keyFile.readAll(), QSsl::Ec, QSsl::Pem);

    if (m_sslCert.isNull() || m_sslKey.isNull()) {
        qWarning() << "ShotServer: Generated certificate or key is invalid";
        return false;
    }

    qDebug() << "ShotServer: Generated new TLS certificate, expires" << m_sslCert.expiryDate().toString();
    return true;
}

bool ShotServer::generateSelfSignedCert(const QString& certPath, const QString& keyPath)
{
    // Generate EC P-256 key pair using OpenSSL EVP API
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (!pctx) return false;

    bool success = false;
    X509* x509 = nullptr;

    do {
        if (EVP_PKEY_keygen_init(pctx) <= 0) break;
        if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, NID_X9_62_prime256v1) <= 0) break;
        if (EVP_PKEY_keygen(pctx, &pkey) <= 0) break;

        // Create X509 certificate
        x509 = X509_new();
        if (!x509) break;

        // Set version to v3
        X509_set_version(x509, 2);

        // Set serial number (random)
        ASN1_INTEGER* serialNumber = X509_get_serialNumber(x509);
        unsigned char serial_bytes[16];
        RAND_bytes(serial_bytes, sizeof(serial_bytes));
        BIGNUM* bn = BN_bin2bn(serial_bytes, sizeof(serial_bytes), nullptr);
        BN_to_ASN1_INTEGER(bn, serialNumber);
        BN_free(bn);

        // Set validity: now to 10 years from now
        X509_gmtime_adj(X509_get_notBefore(x509), 0);
        X509_gmtime_adj(X509_get_notAfter(x509), 10L * 365 * 24 * 60 * 60);

        // Set subject and issuer (self-signed)
        X509_NAME* name = X509_get_subject_name(x509);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                    reinterpret_cast<const unsigned char*>("Decenza DE1"), -1, -1, 0);
        X509_set_issuer_name(x509, name);

        // Set public key
        X509_set_pubkey(x509, pkey);

        // Add Subject Alternative Names (required by modern browsers)
        // Include all local IP addresses so the cert works regardless of which interface is used
        QStringList sanEntries;
        sanEntries << "IP:127.0.0.1" << "DNS:localhost";
        for (const QHostAddress& addr : QNetworkInterface::allAddresses()) {
            if (!addr.isLoopback() && addr.protocol() == QAbstractSocket::IPv4Protocol) {
                sanEntries << QString("IP:%1").arg(addr.toString());
            }
        }
        QByteArray sanStr = sanEntries.join(",").toUtf8();

        X509V3_CTX ctx;
        X509V3_set_ctx(&ctx, x509, x509, nullptr, nullptr, 0);
        X509_EXTENSION* sanExt = X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_alt_name, sanStr.constData());
        if (sanExt) {
            X509_add_ext(x509, sanExt, -1);
            X509_EXTENSION_free(sanExt);
        }

        // Sign with our private key
        if (X509_sign(x509, pkey, EVP_sha256()) <= 0) break;

        // Write certificate to PEM file
        FILE* certFile = fopen(certPath.toLocal8Bit().constData(), "wb");
        if (!certFile) break;
        PEM_write_X509(certFile, x509);
        fclose(certFile);

        // Write private key to PEM file
        FILE* keyFile = fopen(keyPath.toLocal8Bit().constData(), "wb");
        if (!keyFile) break;
        PEM_write_PrivateKey(keyFile, pkey, nullptr, nullptr, 0, nullptr, nullptr);
        fclose(keyFile);

        // Restrict private key file permissions (owner-only)
        QFile::setPermissions(keyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

        success = true;
    } while (false);

    if (x509) X509_free(x509);
    if (pkey) EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(pctx);

    return success;
}
#endif // Q_OS_IOS

// Session management, extractCookie, checkSession, createSession, hasStoredTotpSecret,
// loadSessions, saveSessions are all in shotserver_auth.cpp

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
