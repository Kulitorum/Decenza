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
#include "../core/dbutils.h"
#include "../ai/aimanager.h"
#include "../core/batterymanager.h"
#include "../core/memorymonitor.h"
#include "../mcp/mcpserver.h"
#include "../mcp/mcptoolregistry.h"
#include "version.h"

#include <QThread>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QNetworkInterface>
#include <QUdpSocket>
#include <QSet>
#include <QFile>
#include <QBuffer>
#include <private/qzipwriter_p.h>
#include <algorithm>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QLocale>
#include <QUrl>
#include <QUrlQuery>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
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
#include <cerrno>
#endif

#ifdef Q_OS_IOS
#include <Security/Security.h>
#import <Foundation/Foundation.h>
#else
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
//
// When a new connection arrives, we peek at the first byte using raw recv():
//   0x16 = TLS ClientHello -> hand off to QSslServer for normal TLS flow
//   anything else          -> parse HTTP request, send 301, close
//
// Host and path are sanitized against CRLF header injection.
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
            qDebug() << "HttpRedirectSslServer: Timeout waiting for first byte, fd:" << fd;
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
        auto n = ::recv(toNativeFd(fd), reinterpret_cast<char*>(&peek), 1, MSG_PEEK);

        if (n <= 0) {
            if (n < 0) {
#ifdef Q_OS_WIN
                qWarning() << "HttpRedirectSslServer: recv(MSG_PEEK) failed, fd:" << fd
                           << "WSA error:" << WSAGetLastError();
#else
                qWarning() << "HttpRedirectSslServer: recv(MSG_PEEK) failed, fd:" << fd
                           << "errno:" << errno << strerror(errno);
#endif
            }
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
        auto n = ::recv(toNativeFd(fd), buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            closeFd(fd);
            return;
        }
        buf[n] = '\0';

        // Parse path from "GET /path HTTP/1.x"
        QString path = "/";
        QByteArray request(buf, n);
        qsizetype firstSpace = request.indexOf(' ');
        if (firstSpace > 0) {
            qsizetype secondSpace = request.indexOf(' ', firstSpace + 1);
            if (secondSpace > firstSpace + 1) {
                path = QString::fromLatin1(request.mid(firstSpace + 1, secondSpace - firstSpace - 1));
            }
        }

        // Parse Host header
        QString host = "localhost";
        qsizetype hostIdx = request.indexOf("Host:");
        if (hostIdx < 0) hostIdx = request.indexOf("host:");
        if (hostIdx >= 0) {
            qsizetype start = hostIdx + 5;
            while (start < request.size() && request.at(start) == ' ') ++start;
            qsizetype end = request.indexOf('\r', start);
            if (end < 0) end = request.indexOf('\n', start);
            if (end > start) {
                host = QString::fromLatin1(request.mid(start, end - start));
                // Strip port from host if present (we'll add our own)
                if (host.startsWith('[')) {
                    // IPv6 literal: [::1]:port – strip after closing bracket
                    qsizetype closeBracket = host.indexOf(']');
                    if (closeBracket > 0)
                        host = host.left(closeBracket + 1);
                } else {
                    qsizetype colonIdx = host.lastIndexOf(':');
                    if (colonIdx > 0) host = host.left(colonIdx);
                }
            }
        }

        // Sanitize host and path to prevent header injection via CRLF
        static QRegularExpression validHost(QStringLiteral("^[a-zA-Z0-9.\\-]+$|^\\[[0-9a-fA-F:]+\\]$"));
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

        auto sent = ::send(toNativeFd(fd), response.constData(), response.size(), 0);
        if (sent < 0)
            qWarning() << "HttpRedirectSslServer: send() failed, fd:" << fd;
        else if (sent < response.size())
            qWarning() << "HttpRedirectSslServer: partial send" << sent << "/" << response.size() << "fd:" << fd;
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

// Query shot list from DB. Returns true on success, false on query failure.
static bool queryShotList(QSqlDatabase& db, QVariantList& result) {
    QSqlQuery query(db);
    if (!query.prepare(R"(
        SELECT id, uuid, timestamp, profile_name, duration_seconds,
               final_weight, dose_weight, bean_brand, bean_type,
               enjoyment, visualizer_id, grinder_setting,
               temperature_override, yield_override, beverage_type,
               drink_tds, drink_ey
        FROM shots ORDER BY timestamp DESC LIMIT 1000
    )") || !query.exec()) {
        qWarning() << "ShotServer: Shot list query failed:" << query.lastError().text();
        return false;
    }
    while (query.next()) {
        QVariantMap row;
        row["id"] = query.value(0).toLongLong();
        row["uuid"] = query.value(1).toString();
        row["timestamp"] = query.value(2).toLongLong();
        row["profileName"] = query.value(3).toString();
        row["duration"] = query.value(4).toDouble();
        row["finalWeight"] = query.value(5).toDouble();
        row["doseWeight"] = query.value(6).toDouble();
        row["beanBrand"] = query.value(7).toString();
        row["beanType"] = query.value(8).toString();
        row["enjoyment"] = query.value(9).toDouble();
        row["hasVisualizerUpload"] = !query.value(10).toString().isEmpty();
        row["grinderSetting"] = query.value(11).toString();
        row["temperatureOverride"] = query.value(12).toDouble();
        row["yieldOverride"] = query.value(13).toDouble();
        row["beverageType"] = query.value(14).toString();
        row["drinkTds"] = query.value(15).toDouble();
        row["drinkEy"] = query.value(16).toDouble();
        QDateTime dt = QDateTime::fromSecsSinceEpoch(query.value(2).toLongLong());
        static const bool use12h = QLocale::system().timeFormat(QLocale::ShortFormat).contains("AP", Qt::CaseInsensitive);
        row["dateTime"] = dt.toString(use12h ? "yyyy-MM-dd h:mm AP" : "yyyy-MM-dd HH:mm");
        result.append(row);
    }
    return true;
}

// ---------------------------------------------------------------------------

ShotServer::ShotServer(ShotHistoryStorage* storage, DE1Device* device, QObject* parent)
    : QObject(parent)
    , m_storage(storage)
    , m_device(device)
{
    // Timer to cleanup stale connections
    m_cleanupTimer = new QTimer(this);
    m_cleanupTimer->setInterval(30000);  // Check every 30 seconds
    connect(m_cleanupTimer, &QTimer::timeout, this, &ShotServer::onCleanupTimerTick);
}

ShotServer::~ShotServer()
{
    *m_destroyed = true;
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
        connect(m_settings, &Settings::themeModeChanged,
                this, &ShotServer::onThemeChanged);
        connect(m_settings, &Settings::editingPaletteChanged,
                this, &ShotServer::onThemeChanged);
    }
}

// Precondition: no socket in `clients` has an entry in m_keepAliveTimers.
// This is enforced at each SSE registration site by calling take() on the map:
//   /api/theme/subscribe  — see handleRequest(), "m_sseThemeClients.insert"
//   /api/layout/events    — see handleRequest(), "m_sseLayoutClients.insert"
// If a new SSE endpoint is added WITHOUT calling take(), this function will call
// deleteLater() on the socket while its timer lambda still holds a raw pointer to
// it — causing use-after-free when the timer fires. This static function has no
// access to m_keepAliveTimers, so callers must maintain the invariant.
static void broadcastSseEvent(QSet<QTcpSocket*>& clients, const QByteArray& event)
{
    QList<QTcpSocket*> dead;
    for (QTcpSocket* client : clients) {
        if (client->state() != QAbstractSocket::ConnectedState || client->write(event) == -1) {
            dead.append(client);
            continue;
        }
        client->flush();
    }
    for (QTcpSocket* s : dead) {
        clients.remove(s);
        s->close();
        s->deleteLater();
    }
}

void ShotServer::onLayoutChanged()
{
    broadcastSseEvent(m_sseLayoutClients, "event: layout-changed\ndata: {}\n\n");
}

void ShotServer::onThemeChanged()
{
    broadcastSseEvent(m_sseThemeClients, "event: theme-changed\ndata: {}\n\n");
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
    cancelAllLibraryRequests();

    if (m_discoverySocket) {
        m_discoverySocket->close();
        delete m_discoverySocket;
        m_discoverySocket = nullptr;
    }

    if (m_server) {
        m_cleanupTimer->stop();
        // Stop all keep-alive timers BEFORE closing any sockets. This ensures
        // every timer pointer is still valid (sockets haven't been destroyed yet).
        // Closing sockets afterwards may trigger onDisconnected() → deleteLater(),
        // which could destroy timers; clearing the map first avoids dangling pointers.
        for (QTimer* t : std::as_const(m_keepAliveTimers))
            t->stop();
        m_keepAliveTimers.clear();
        // Copy sets before closing — s->close() can synchronously trigger
        // onDisconnected() which calls m_sseLayoutClients.remove() during iteration.
        auto layoutCopy = m_sseLayoutClients;
        m_sseLayoutClients.clear();
        for (QTcpSocket* s : layoutCopy) s->close();
        auto themeCopy = m_sseThemeClients;
        m_sseThemeClients.clear();
        for (QTcpSocket* s : themeCopy) s->close();
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

    // After sleep/wake or network changes, readyRead can fire for sockets that
    // are closing or already disconnected. Processing such a socket would create
    // a new keep-alive timer (via sendResponse → resetKeepAliveTimer) whose map
    // entry becomes dangling once deleteLater destroys the socket and its child timer.
    if (socket->state() != QAbstractSocket::ConnectedState) return;

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
            qint64& last = m_uploadProgressLog[socket];
            if (pending.bodyReceived - last > 5 * 1024 * 1024) {
                qDebug() << "Upload progress:" << pending.bodyReceived / (1024*1024) << "MB /" << pending.contentLength / (1024*1024) << "MB";
                last = pending.bodyReceived;
            }
        }

        // Check if we have all the body data
        if (pending.bodyReceived < pending.contentLength) {
            return;  // Still waiting for more data
        }

        // Clean up upload progress tracking for this socket
        m_uploadProgressLog.remove(socket);

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
        m_uploadProgressLog.remove(socket);
        cleanupPendingRequest(socket);
        m_pendingRequests.remove(socket);
        socket->close();
    } catch (...) {
        qWarning() << "ShotServer: Unknown exception in onReadyRead";
        m_uploadProgressLog.remove(socket);
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
        // Stop the keep-alive timer if one is active. The timer is a child of
        // `socket` and will be destroyed with it when deleteLater() processes —
        // no explicit delete needed. Stopping it here prevents the timeout lambda
        // from firing in the window between now and socket destruction.
        if (QTimer* t = m_keepAliveTimers.take(socket))
            t->stop();
        cleanupPendingRequest(socket);
        m_pendingRequests.remove(socket);

        // Clean up any pending library requests for this socket (or already-destroyed sockets)
        QList<int> toRemove;
        for (auto it = m_pendingLibraryRequests.begin(); it != m_pendingLibraryRequests.end(); ++it) {
            if (it.value().socket == socket || it.value().socket.isNull()) {
                qDebug() << "ShotServer: Cleaning up pending library request" << it.key() << "- socket disconnected";
                invalidateLibraryRequest(it.value());
                toRemove.append(it.key());
            }
        }
        for (int id : toRemove) {
            m_pendingLibraryRequests.remove(id);
        }

        socket->deleteLater();
    }
}

void ShotServer::invalidateLibraryRequest(PendingLibraryRequest& req)
{
    if (req.fired) *req.fired = true;
    for (auto& conn : req.connections) disconnect(conn);
    if (req.timeoutTimer) {
        req.timeoutTimer->stop();
        req.timeoutTimer->deleteLater();
    }
}

void ShotServer::cancelAllLibraryRequests()
{
    for (auto it = m_pendingLibraryRequests.begin(); it != m_pendingLibraryRequests.end(); ++it) {
        invalidateLibraryRequest(it.value());
    }
    m_pendingLibraryRequests.clear();
}

void ShotServer::completeLibraryRequest(int reqId, const QJsonObject& resp)
{
    if (!m_pendingLibraryRequests.contains(reqId)) return;
    auto& req = m_pendingLibraryRequests[reqId];
    if (*req.fired) {
        qDebug() << "ShotServer: Library request" << reqId << "already handled, ignoring duplicate callback";
        return;
    }
    invalidateLibraryRequest(req);

    if (req.socket && req.socket->state() == QAbstractSocket::ConnectedState) {
        sendJson(req.socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
    } else {
        qDebug() << "ShotServer: Dropping response for library request" << reqId << "- socket disconnected";
    }
    m_pendingLibraryRequests.remove(reqId);
}

void ShotServer::cleanupPendingRequest(QTcpSocket* socket)
{
    if (!m_pendingRequests.contains(socket)) return;

    m_uploadProgressLog.remove(socket);
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
    // Only decrement if this was a large upload that was actually counted
    // (small uploads < MAX_SMALL_BODY_SIZE don't increment m_activeMediaUploads)
    if ((pending.isMediaUpload || pending.isBackupRestore) && !pending.tempFilePath.isEmpty() && m_activeMediaUploads > 0) {
        m_activeMediaUploads--;
    }
}

void ShotServer::onCleanupTimerTick()
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
        // Remove the keep-alive timer from the map before scheduling deletion.
        // If disconnected() doesn't fire synchronously from close(), the socket
        // and its child timer will be destroyed by deleteLater() while the pointer
        // still sits in m_keepAliveTimers — causing a dangling-pointer crash in stop().
        if (QTimer* t = m_keepAliveTimers.take(socket))
            t->stop();
        cleanupPendingRequest(socket);
        m_pendingRequests.remove(socket);
        socket->close();
        socket->deleteLater();
    }

    // SSE clients are long-lived and never appear in m_pendingRequests, so the
    // stale-connection loop above won't catch them. Probe each client with an SSE
    // keepalive comment every 30 s to detect silently-dropped connections.
    // Inlined rather than delegating to broadcastSseEvent() so we can defensively
    // call m_keepAliveTimers.take() before deleteLater() — the static helper has
    // no access to the map and cannot enforce the no-timer-in-map invariant itself.
    for (QSet<QTcpSocket*>* clientSet : {&m_sseLayoutClients, &m_sseThemeClients}) {
        QList<QTcpSocket*> dead;
        for (QTcpSocket* c : *clientSet) {
            if (c->state() != QAbstractSocket::ConnectedState || c->write(": keepalive\n\n") == -1)
                dead.append(c);
            else
                c->flush();
        }
        for (QTcpSocket* c : dead) {
            clientSet->remove(c);
            if (QTimer* t = m_keepAliveTimers.take(c)) t->stop();
            c->close();
            c->deleteLater();
        }
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

        // MCP routes can authenticate via API key (Bearer token) instead of TOTP
        if (!exempt && (path == "/mcp" || path.startsWith("/mcp/")) && m_settings) {
            QString authHeader;
            for (const auto& line : requestStr.split("\r\n")) {
                if (line.startsWith("Authorization:", Qt::CaseInsensitive)) {
                    authHeader = line.mid(line.indexOf(':') + 1).trimmed();
                    break;
                }
            }
            if (authHeader.startsWith("Bearer ", Qt::CaseInsensitive)) {
                QString token = authHeader.mid(7).trimmed();
                if (!token.isEmpty() && token == m_settings->mcpApiKey()) {
                    exempt = true;  // Valid API key — skip TOTP check
                }
            }
        }

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
            qsizetype bodyStart = request.indexOf("\r\n\r\n");
            if (bodyStart != -1) {
                body = request.mid(bodyStart + 4);
            }
            // Store full request on socket for auth handlers that need cookie access
            socket->setProperty("fullRequest", request);
            handleAuthRoute(socket, method, path, body);
            return;
        }
    }

    // MCP Server routes
    if (path == "/mcp" || path.startsWith("/mcp/")) {
        // Install scripts (platform-specific, auto-configure everything)
        if (path == "/mcp/install.sh") {
            QString serverUrl = url();
            QString mcpUrl = serverUrl + "/mcp";
            QString script = QString(R"BASH(#!/bin/bash
set -e
echo "Installing Decenza MCP bridge..."

# Create directory
BRIDGE_DIR="$HOME/Library/Application Support/Decenza"
if [ ! -d "$HOME/Library" ]; then
    BRIDGE_DIR="$HOME/.local/share/Decenza"
fi
mkdir -p "$BRIDGE_DIR"

# Download bridge script
BRIDGE_PATH="$BRIDGE_DIR/mcp_bridge.py"
curl -fsSL "%1/mcp/bridge.py" -o "$BRIDGE_PATH"
chmod +x "$BRIDGE_PATH"
echo "Bridge script installed: $BRIDGE_PATH"

# Check for Python
if ! command -v python3 &>/dev/null; then
    echo "WARNING: python3 not found. Install Python from python.org"
    exit 1
fi

# Configure Claude Desktop
CONFIG_DIR="$HOME/Library/Application Support/Claude"
if [ ! -d "$CONFIG_DIR" ]; then
    echo "Claude Desktop config directory not found."
    echo "Install Claude Desktop from claude.ai/download, then re-run this script."
    exit 1
fi
CONFIG="$CONFIG_DIR/claude_desktop_config.json"

# Read existing config or create new
if [ -f "$CONFIG" ]; then
    # Merge: add decenza server to existing config
    python3 -c "
import json, sys
with open('$CONFIG') as f:
    config = json.load(f)
config.setdefault('mcpServers', {})
config['mcpServers']['decenza'] = {
    'command': '/usr/bin/python3',
    'args': ['$BRIDGE_PATH', '%2']
}
with open('$CONFIG', 'w') as f:
    json.dump(config, f, indent=2)
print('Updated:', '$CONFIG')
"
else
    cat > "$CONFIG" << 'ENDJSON'
{
  "mcpServers": {
    "decenza": {
      "command": "/usr/bin/python3",
      "args": ["BRIDGE_PATH_PLACEHOLDER", "%2"]
    }
  }
}
ENDJSON
    sed -i '' "s|BRIDGE_PATH_PLACEHOLDER|$BRIDGE_PATH|g" "$CONFIG"
    echo "Created: $CONFIG"
fi

echo ""
echo "Done! Restart Claude Desktop and ask:"
echo '  "What is the state of my espresso machine?"'
)BASH").arg(serverUrl, mcpUrl);

            sendResponse(socket, 200, "text/plain; charset=utf-8", script.toUtf8());
            return;
        }

        if (path == "/mcp/install.ps1") {
            QString serverUrl = url();
            QString mcpUrl = serverUrl + "/mcp";
            QString script = QString(R"PS1(
Write-Host "Installing Decenza MCP bridge..."

$bridgeDir = "$env:APPDATA\Decenza"
New-Item -ItemType Directory -Force -Path $bridgeDir | Out-Null

$bridgePath = "$bridgeDir\mcp_bridge.py"
Invoke-WebRequest -Uri "%1/mcp/bridge.py" -OutFile $bridgePath
Write-Host "Bridge script installed: $bridgePath"

# Check for Python
$py = Get-Command python -ErrorAction SilentlyContinue
if (-not $py) {
    Write-Host "WARNING: Python not found. Install from python.org" -ForegroundColor Yellow
    exit 1
}

$configDir = "$env:APPDATA\Claude"
if (-not (Test-Path $configDir)) {
    Write-Host "Claude Desktop config directory not found."
    Write-Host "Install Claude Desktop from claude.ai/download, then re-run."
    exit 1
}
$configPath = "$configDir\claude_desktop_config.json"

if (Test-Path $configPath) {
    $config = Get-Content $configPath | ConvertFrom-Json
} else {
    $config = @{}
}
if (-not $config.mcpServers) { $config | Add-Member -NotePropertyName mcpServers -NotePropertyValue @{} }
$config.mcpServers.decenza = @{
    command = "python"
    args = @($bridgePath, "%2")
}
$config | ConvertTo-Json -Depth 10 | Set-Content $configPath
Write-Host "Config updated: $configPath"
Write-Host ""
Write-Host "Done! Restart Claude Desktop and ask:"
Write-Host '  "What is the state of my espresso machine?"'
)PS1").arg(serverUrl, mcpUrl);

            sendResponse(socket, 200, "text/plain; charset=utf-8", script.toUtf8());
            return;
        }

        // Serve bridge script inline
        if (path == "/mcp/bridge.py") {
            QByteArray script = R"PY(#!/usr/bin/env python3
"""Decenza MCP Bridge - connects Claude Desktop (stdio) to Decenza's HTTP MCP server."""
import sys, json, urllib.request, urllib.error

def main():
    if len(sys.argv) < 2:
        print("Usage: mcp_bridge.py <server-url> [api-key]", file=sys.stderr)
        sys.exit(1)
    server_url = sys.argv[1]
    api_key = sys.argv[2] if len(sys.argv) > 2 else None
    session = [None]

    def make_headers():
        h = {"Content-Type": "application/json"}
        if session[0]: h["Mcp-Session"] = session[0]
        if api_key: h["Authorization"] = f"Bearer {api_key}"
        return h

    while True:
        try:
            line = sys.stdin.readline()
        except EOFError:
            break
        if not line: break
        line = line.strip()
        if not line: continue
        try:
            request = json.loads(line)
        except json.JSONDecodeError as e:
            print(json.dumps({"jsonrpc":"2.0","id":None,"error":{"code":-32700,"message":f"Parse error: {e}"}}), flush=True)
            continue
        is_notification = "id" not in request
        try:
            data = json.dumps(request).encode("utf-8")
            req = urllib.request.Request(server_url, data=data, headers=make_headers(), method="POST")
            if is_notification:
                try:
                    with urllib.request.urlopen(req, timeout=2) as resp: resp.read()
                except Exception: pass
                continue
            with urllib.request.urlopen(req, timeout=30) as resp:
                new_session = resp.headers.get("Mcp-Session")
                if new_session: session[0] = new_session
                body = resp.read().decode("utf-8")
                if body.strip(): print(body, flush=True)
        except urllib.error.URLError as e:
            msg = str(e.reason) if hasattr(e,'reason') else str(e)
            rid = request.get("id")
            if rid is not None:
                print(json.dumps({"jsonrpc":"2.0","id":rid,"error":{"code":-32000,"message":f"Server error: {msg}"}}), flush=True)
        except Exception as e:
            rid = request.get("id")
            if rid is not None:
                print(json.dumps({"jsonrpc":"2.0","id":rid,"error":{"code":-32000,"message":f"Bridge error: {e}"}}), flush=True)

if __name__ == "__main__":
    main()
)PY";
            sendResponse(socket, 200, "text/plain; charset=utf-8", script,
                "Content-Disposition: attachment; filename=\"decenza_mcp_bridge.py\"\r\n");
            return;
        }

        // Setup guide page (available even when MCP is disabled)
        if (path == "/mcp/setup") {
            QString serverUrl = url();
            QString mcpUrl = serverUrl + "/mcp";
            QString html = QString::fromUtf8(R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Decenza MCP Setup</title>
<style>
body{font-family:-apple-system,system-ui,sans-serif;max-width:640px;margin:40px auto;padding:0 20px;
background:#1a1a2e;color:#e0e0e0;line-height:1.6}
h1{color:#6c8cff}h2{color:#8ca0ff;margin-top:32px}
pre{background:#0d0d1a;padding:16px;border-radius:8px;overflow-x:auto;border:1px solid #333;
font-size:14px;position:relative}
code{color:#a0cfff}
.copy-btn{position:absolute;top:8px;right:8px;background:#6c8cff;color:white;border:none;
padding:6px 12px;border-radius:4px;cursor:pointer;font-size:12px}
.copy-btn:hover{background:#8ca0ff}
.step{background:#0d0d1a;padding:16px;border-radius:8px;margin:12px 0;border-left:3px solid #6c8cff}
.step-num{color:#6c8cff;font-weight:bold;font-size:18px}
.status{padding:12px;border-radius:8px;margin:16px 0;font-weight:bold}
.enabled{background:rgba(46,125,50,0.2);border:1px solid #2e7d32;color:#81c784}
.disabled{background:rgba(198,40,40,0.2);border:1px solid #c62828;color:#ef9a9a}
a{color:#6c8cff}
</style></head><body>
<h1>Decenza MCP Server Setup</h1>
<div class="status %1">MCP Server: %2</div>
<p>Connect Claude Desktop to your DE1 espresso machine for AI-powered shot analysis, dial-in advice, and remote control.</p>

<h2>How It Works</h2>
<p>The MCP server runs inside Decenza on <strong>any platform</strong> (Android tablet, iOS, Windows, macOS, Linux). Claude Desktop on your computer connects to it over your local WiFi network.</p>

<h2>Platform Compatibility</h2>
<table style="width:100%%;border-collapse:collapse;margin:12px 0">
<tr style="border-bottom:1px solid #333"><td style="padding:8px"><strong>Decenza runs on</strong></td><td style="padding:8px">Any platform &mdash; tablet, phone, or desktop</td></tr>
<tr style="border-bottom:1px solid #333"><td style="padding:8px"><strong>Claude Desktop connects from</strong></td><td style="padding:8px">macOS or Windows (same WiFi network)</td></tr>
<tr style="border-bottom:1px solid #333"><td style="padding:8px"><strong>Does NOT work with</strong></td><td style="padding:8px">claude.ai web, Claude iOS/Android apps</td></tr>
</table>
<p><strong>Tip:</strong> You can also use the <em>Discuss</em> button on any shot review page to open any AI (Claude Web, ChatGPT, Gemini, Grok) with your shot data copied to clipboard &mdash; this works on all platforms without MCP.</p>

<h2>Available Tools (%4)</h2>
<p>At <strong>Full Automation</strong> access level, Claude can:</p>
<ul>
<li>Read machine state, telemetry, water level</li>
<li>Browse and analyze shot history</li>
<li>Get dial-in context and suggest improvements</li>
<li>Start/stop espresso, steam, hot water, flush (DE1 v1.0 headless machines only &mdash; most machines with GHC require physical button press)</li>
<li>Change profiles, settings, and grinder parameters</li>
</ul>

<h2>Access Levels</h2>
<ul>
<li><strong>Monitor Only</strong> &mdash; read state, history, profiles (no control)</li>
<li><strong>Control</strong> &mdash; + start/stop operations, wake/sleep</li>
<li><strong>Full Automation</strong> &mdash; + change profiles, settings, parameters</li>
</ul>

<h2>Setup Steps</h2>
<div class="step"><span class="step-num">1.</span> Enable MCP in Decenza: Settings &gt; AI &gt; MCP Server &gt; toggle ON</div>
<div class="step"><span class="step-num">2.</span> On your computer, install <a href="https://claude.ai/download">Claude Desktop</a></div>
<div class="step"><span class="step-num">3.</span> Run this command in your terminal to install the MCP bridge:
<pre id="installCmd"><code id="installCmdText">Loading...</code><button class="copy-btn" onclick="copyInstallCmd(this)">Copy</button></pre></div>
<div class="step"><span class="step-num">4.</span> Restart Claude Desktop (Quit &amp; reopen)</div>
<div class="step"><span class="step-num">5.</span> Ask Claude: <em>"What's the current state of my espresso machine?"</em></div>

<script>
(function(){
var url='%3';
var p=navigator.platform||navigator.userAgent||'';
var isMac=p.indexOf('Mac')>=0;
var isWin=p.indexOf('Win')>=0;

if(isMac){
document.getElementById('installCmdText').textContent=
'curl -fsSL '+location.origin+'/mcp/install.sh | bash';
}else if(isWin){
document.getElementById('installCmdText').textContent=
'powershell -c "irm '+location.origin+'/mcp/install.ps1 | iex"';
}else{
document.getElementById('installCmdText').textContent=
'curl -fsSL '+location.origin+'/mcp/install.sh | bash';
}
})();
</script>

<script>
function copyInstallCmd(btn){
var text=document.getElementById('installCmdText').textContent;
if(navigator.clipboard&&window.isSecureContext){
navigator.clipboard.writeText(text).then(function(){btn.textContent='Copied!';setTimeout(function(){btn.textContent='Copy'},2000)});
}else{
var ta=document.createElement('textarea');ta.value=text;ta.style.position='fixed';ta.style.left='-9999px';
document.body.appendChild(ta);ta.select();document.execCommand('copy');document.body.removeChild(ta);
btn.textContent='Copied!';setTimeout(function(){btn.textContent='Copy'},2000);
}
}
</script>
</body></html>)HTML");

            bool mcpOn = m_settings && m_settings->mcpEnabled();
            // %3 is used in the JS install script URL (the MCP endpoint URL)
            QString configJson = mcpUrl;
            int toolCount = m_mcpServer ? m_mcpServer->toolRegistry()->listTools(2).size() : 0;

            html = html.arg(mcpOn ? "enabled" : "disabled",
                           mcpOn ? "Enabled" : "Disabled",
                           configJson,
                           QString::number(toolCount));

            sendHtml(socket, html);
            return;
        }

        if (!m_mcpServer || !m_settings || !m_settings->mcpEnabled()) {
            sendResponse(socket, 404, "text/plain", "Not Found");
            return;
        }
        // Extract headers from the raw request
        int headerEnd = request.indexOf("\r\n\r\n");
        QByteArray headers = (headerEnd > 0) ? request.left(headerEnd) : QByteArray();
        QByteArray body;
        if (headerEnd > 0 && headerEnd + 4 < request.size())
            body = request.mid(headerEnd + 4);
        m_mcpServer->handleHttpRequest(socket, method, path, headers, body);
        return;
    }

    // Route requests
    if (path == "/" || path == "/index.html" || path == "/shots" || path == "/shots/") {
        QPointer<QTcpSocket> socketGuard(socket);
        QString dbPath = m_storage->databasePath();
        auto destroyed = m_destroyed;
        QThread* thread = QThread::create([this, socketGuard, dbPath, destroyed]() {
            QVariantList shots;
            bool success = false;
            withTempDb(dbPath, "shs_web_list", [&](QSqlDatabase& db) {
                success = queryShotList(db, shots);
            });

            if (*destroyed) return;
            QMetaObject::invokeMethod(this, [this, socketGuard, destroyed, success,
                                             shots = std::move(shots)]() {
                if (*destroyed || !socketGuard) return;
                if (!success) {
                    sendResponse(socketGuard, 500, "text/plain", "Database unavailable");
                } else {
                    sendHtml(socketGuard, generateShotListPage(shots));
                }
            }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
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
        if (ids.size() < 2) {
            sendResponse(socket, 400, "text/plain", "Need at least 2 shot IDs to compare");
            return;
        }
        QPointer<QTcpSocket> socketGuard(socket);
        QString dbPath = m_storage->databasePath();
        auto destroyed = m_destroyed;
        QThread* thread = QThread::create([this, socketGuard, dbPath, ids, destroyed]() {
            QList<ShotRecord> shots;
            bool dbOpened = withTempDb(dbPath, "shs_web_cmp", [&](QSqlDatabase& db) {
                for (qint64 id : ids) {
                    ShotRecord r = ShotHistoryStorage::loadShotRecordStatic(db, id);
                    if (r.summary.id > 0) shots.append(std::move(r));
                }
            });

            if (*destroyed) return;
            QMetaObject::invokeMethod(this, [this, socketGuard, destroyed, dbOpened,
                                             shots = std::move(shots)]() {
                if (*destroyed || !socketGuard) return;
                if (!dbOpened) {
                    sendResponse(socketGuard, 500, "text/plain", "Database unavailable");
                } else {
                    sendHtml(socketGuard, generateComparisonPage(shots));
                }
            }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
    }
    else if (path.startsWith("/shot/") && path.endsWith("/profile.json")) {
        // /shot/123/profile.json - download profile JSON for a shot
        QString idPart = path.mid(6);  // Remove "/shot/"
        idPart = idPart.left(idPart.indexOf("/profile.json"));
        bool ok;
        qint64 shotId = idPart.toLongLong(&ok);
        if (!ok) {
            sendResponse(socket, 400, "application/json", R"({"error":"Invalid shot ID"})");
            return;
        }
        QPointer<QTcpSocket> socketGuard(socket);
        QString dbPath = m_storage->databasePath();
        auto destroyed = m_destroyed;
        QThread* thread = QThread::create([this, socketGuard, dbPath, shotId, destroyed]() {
            ShotRecord record;
            bool dbOpened = withTempDb(dbPath, "shs_web_prof", [&](QSqlDatabase& db) {
                record = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
            });

            if (*destroyed) return;
            QMetaObject::invokeMethod(this, [this, socketGuard, destroyed, dbOpened, record]() {
                if (*destroyed || !socketGuard) return;
                if (!dbOpened) {
                    sendResponse(socketGuard, 500, "application/json", R"({"error":"Database unavailable"})");
                } else if (!record.profileJson.isEmpty()) {
                    QJsonDocument doc = QJsonDocument::fromJson(record.profileJson.toUtf8());
                    QByteArray prettyJson = doc.toJson(QJsonDocument::Indented);
                    QString filename = record.summary.profileName.isEmpty() ? "profile" : record.summary.profileName;
                    filename = filename.replace(QRegularExpression("[^a-zA-Z0-9_-]"), "_");
                    QByteArray headers = QString("Content-Disposition: attachment; filename=\"%1.json\"\r\n").arg(filename).toUtf8();
                    sendResponse(socketGuard, 200, "application/json", prettyJson, headers);
                } else {
                    sendResponse(socketGuard, 404, "application/json", R"({"error":"No profile data for this shot"})");
                }
            }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
    }
    else if (path.startsWith("/shot/")) {
        bool ok;
        qint64 shotId = path.mid(6).split("?").first().toLongLong(&ok);
        if (!ok) {
            sendResponse(socket, 400, "text/plain", "Invalid shot ID");
            return;
        }
        QPointer<QTcpSocket> socketGuard(socket);
        QString dbPath = m_storage->databasePath();
        auto destroyed = m_destroyed;
        QThread* thread = QThread::create([this, socketGuard, dbPath, shotId, destroyed]() {
            QVariantMap shot;
            bool dbOpened = withTempDb(dbPath, "shs_web_det", [&](QSqlDatabase& db) {
                ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
                shot = ShotHistoryStorage::convertShotRecord(record);
            });

            if (*destroyed) return;
            QMetaObject::invokeMethod(this, [this, socketGuard, destroyed, dbOpened, shotId,
                                             shot = std::move(shot)]() {
                if (*destroyed || !socketGuard) return;
                if (!dbOpened) {
                    sendResponse(socketGuard, 500, "text/plain", "Database unavailable");
                    return;
                }
                sendHtml(socketGuard, generateShotDetailPage(shotId, shot));
            }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
    }
    else if (path == "/api/shots") {
        QPointer<QTcpSocket> socketGuard(socket);
        QString dbPath = m_storage->databasePath();
        auto destroyed = m_destroyed;
        QThread* thread = QThread::create([this, socketGuard, dbPath, destroyed]() {
            QVariantList shots;
            bool success = false;
            withTempDb(dbPath, "shs_web_api", [&](QSqlDatabase& db) {
                success = queryShotList(db, shots);
            });

            if (*destroyed) return;
            QMetaObject::invokeMethod(this, [this, socketGuard, destroyed, success,
                                             shots = std::move(shots)]() {
                if (*destroyed || !socketGuard) return;
                if (!success) {
                    sendResponse(socketGuard, 500, "application/json", R"({"error":"Database unavailable"})");
                } else {
                    QJsonArray arr;
                    for (const QVariant& v : shots)
                        arr.append(QJsonObject::fromVariantMap(v.toMap()));
                    sendJson(socketGuard, QJsonDocument(arr).toJson(QJsonDocument::Compact));
                }
            }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
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
        qsizetype bodyStart = request.indexOf("\r\n\r\n");
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
        QPointer<QTcpSocket> socketGuard(socket);
        QString dbPath = m_storage->databasePath();
        auto destroyed = m_destroyed;
        QThread* thread = QThread::create([this, socketGuard, dbPath, shotId, metadata, destroyed]() {
            bool success = false;
            bool dbOpened = withTempDb(dbPath, "shs_web_upd", [&](QSqlDatabase& db) {
                success = ShotHistoryStorage::updateShotMetadataStatic(db, shotId, metadata);
            });

            if (*destroyed) return;
            QMetaObject::invokeMethod(this, [this, socketGuard, destroyed, shotId, success, dbOpened]() {
                if (*destroyed) return;
                if (success) {
                    m_storage->invalidateDistinctCache();
                    emit m_storage->shotMetadataUpdated(shotId, true);
                }
                if (!socketGuard) return;
                if (!dbOpened) {
                    sendResponse(socketGuard, 500, "application/json", R"({"error":"Database unavailable"})");
                } else if (success) {
                    sendJson(socketGuard, R"({"success":true})");
                } else {
                    sendResponse(socketGuard, 500, "application/json", R"({"error":"Failed to update metadata"})");
                }
            }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
    }
    else if (path.startsWith("/api/shot/")) {
        bool ok;
        qint64 shotId = path.mid(10).toLongLong(&ok);
        if (!ok) {
            sendResponse(socket, 400, "application/json", R"({"error":"Invalid shot ID"})");
            return;
        }
        QPointer<QTcpSocket> socketGuard(socket);
        QString dbPath = m_storage->databasePath();
        auto destroyed = m_destroyed;
        QThread* thread = QThread::create([this, socketGuard, dbPath, shotId, destroyed]() {
            QVariantMap shot;
            bool dbOpened = withTempDb(dbPath, "shs_web_get", [&](QSqlDatabase& db) {
                ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
                shot = ShotHistoryStorage::convertShotRecord(record);
            });

            if (*destroyed) return;
            QMetaObject::invokeMethod(this, [this, socketGuard, destroyed, dbOpened,
                                             shot = std::move(shot)]() {
                if (*destroyed || !socketGuard) return;
                if (!dbOpened) {
                    sendResponse(socketGuard, 500, "application/json", R"({"error":"Database unavailable"})");
                } else {
                    sendJson(socketGuard, QJsonDocument(QJsonObject::fromVariantMap(shot)).toJson());
                }
            }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
    }
    else if (path == "/api/shots/delete" && method == "POST") {
        qsizetype bodyStart = request.indexOf("\r\n\r\n");
        if (bodyStart == -1) {
            sendResponse(socket, 400, "application/json", R"({"error":"Invalid request"})");
            return;
        }
        QByteArray body = request.mid(bodyStart + 4);
        QJsonDocument doc = QJsonDocument::fromJson(body);
        QJsonArray ids = doc.object().value("ids").toArray();
        QList<qint64> shotIds;
        for (const QJsonValue& v : std::as_const(ids)) {
            qint64 id = v.toInteger();
            if (id > 0) shotIds << id;
        }
        if (shotIds.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"No valid shot IDs provided"})");
            return;
        }
        QPointer<QTcpSocket> socketGuard(socket);
        QString dbPath = m_storage->databasePath();
        auto destroyed = m_destroyed;
        QThread* thread = QThread::create([this, socketGuard, dbPath, shotIds, destroyed]() {
            int deleted = 0;
            QList<qint64> deletedIds;
            bool dbOpened = withTempDb(dbPath, "shs_web_del", [&](QSqlDatabase& db) {
                QSqlQuery query(db);
                if (!query.prepare("DELETE FROM shots WHERE id = ?")) {
                    qWarning() << "ShotServer: Batch delete prepare failed:" << query.lastError().text();
                    return;
                }
                for (qint64 id : shotIds) {
                    query.bindValue(0, id);
                    if (query.exec()) {
                        if (query.numRowsAffected() > 0) {
                            deleted++;
                            deletedIds << id;
                        }
                    } else {
                        qWarning() << "ShotServer: Failed to delete shot" << id << ":" << query.lastError().text();
                    }
                }
            });

            if (*destroyed) return;
            QMetaObject::invokeMethod(this, [this, socketGuard, destroyed, deleted, deletedIds, dbOpened]() {
                if (*destroyed) return;
                if (deleted > 0) {
                    m_storage->invalidateDistinctCache();
                    m_storage->refreshTotalShots();
                    for (qint64 id : deletedIds)
                        emit m_storage->shotDeleted(id);
                }
                if (!socketGuard) return;
                if (!dbOpened) {
                    sendResponse(socketGuard, 500, "application/json", R"({"error":"Database unavailable"})");
                } else {
                    sendJson(socketGuard, QString(R"({"deleted":%1})").arg(deleted).toUtf8());
                }
            }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
    }
    else if (path == "/api/database" || path == "/database.db") {
        // Checkpoint WAL and send DB file from background thread
        QPointer<QTcpSocket> socketGuard(socket);
        QString dbPath = m_storage->databasePath();
        auto destroyed = m_destroyed;
        QThread* thread = QThread::create([this, socketGuard, dbPath, destroyed]() {
            bool checkpointOk = false;
            withTempDb(dbPath, "shs_web_db", [&](QSqlDatabase& db) {
                QSqlQuery walQuery(db);
                if (!walQuery.exec("PRAGMA wal_checkpoint(FULL)")) {
                    qWarning() << "ShotServer: WAL checkpoint failed:" << walQuery.lastError().text();
                } else {
                    checkpointOk = true;
                }
            });

            QByteArray fileData;
            if (checkpointOk) {
                QFile dbFile(dbPath);
                if (dbFile.open(QIODevice::ReadOnly)) {
                    fileData = dbFile.readAll();
                } else {
                    qWarning() << "ShotServer: Failed to read DB file for download:" << dbPath << dbFile.errorString();
                }
            }

            if (*destroyed) return;
            QMetaObject::invokeMethod(this, [this, socketGuard, destroyed,
                                             fileData = std::move(fileData)]() {
                if (*destroyed || !socketGuard) return;
                if (!fileData.isEmpty()) {
                    sendResponse(socketGuard, 200, "application/x-sqlite3", fileData);
                } else {
                    sendResponse(socketGuard, 500, "application/json", R"({"error":"Database checkpoint failed - download may be incomplete"})");
                }
            }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
    }
    else if (path == "/api/memory") {
        if (m_memoryMonitor) {
            QJsonDocument doc(m_memoryMonitor->toJson());
            sendJson(socket, doc.toJson(QJsonDocument::Compact));
        } else {
            sendResponse(socket, 503, "application/json", R"({"error":"Memory monitor not available"})");
        }
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
        qsizetype bodyStart = request.indexOf("\r\n\r\n");
        if (bodyStart != -1) {
            handleVisualizerTest(socket, request.mid(bodyStart + 4));
        } else {
            sendJson(socket, R"({"success": false, "message": "Invalid request"})");
        }
    }
    else if (path == "/api/settings/ai/test" && method == "POST") {
        qsizetype bodyStart = request.indexOf("\r\n\r\n");
        if (bodyStart != -1) {
            handleAiTest(socket, request.mid(bodyStart + 4));
        } else {
            sendJson(socket, R"({"success": false, "message": "Invalid request"})");
        }
    }
    else if (path == "/api/settings/mqtt/connect" && method == "POST") {
        qsizetype bodyStart = request.indexOf("\r\n\r\n");
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
            qsizetype bodyStart = request.indexOf("\r\n\r\n");
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
            qsizetype bodyStart = request.indexOf("\r\n\r\n");
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
            qsizetype bodyStart = request.indexOf("\r\n\r\n");
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
        // Return persisted log file content (survives crashes), with memory snapshot appended
        QJsonObject result;
        QString log = WebDebugLogger::instance() ? WebDebugLogger::instance()->getPersistedLog() : QString();
        if (m_memoryMonitor)
            log += m_memoryMonitor->toSummaryString();
        result["log"] = log;
        result["path"] = WebDebugLogger::instance() ? WebDebugLogger::instance()->logFilePath() : QString();
        sendJson(socket, QJsonDocument(result).toJson(QJsonDocument::Compact));
    }
    else if (path == "/api/debug/file/zip") {
        // Return persisted log as a ZIP file for smaller downloads
        QString log = WebDebugLogger::instance() ? WebDebugLogger::instance()->getPersistedLog() : QString();
        if (m_memoryMonitor)
            log += m_memoryMonitor->toSummaryString();

        QBuffer zipBuffer;
        zipBuffer.open(QIODevice::WriteOnly);
        bool zipOk = false;
        {
            QZipWriter writer(&zipBuffer);
            writer.setCompressionPolicy(QZipWriter::AlwaysCompress);
            writer.addFile("debug.log", log.toUtf8());
            zipOk = (writer.status() == QZipWriter::NoError);
            writer.close();
        }

        if (zipOk) {
            sendResponse(socket, 200, "application/zip", zipBuffer.data(),
                         "Content-Disposition: attachment; filename=\"debug.zip\"\r\n");
        } else {
            qWarning() << "ShotServer: QZipWriter failed to create debug ZIP";
            sendResponse(socket, 500, "text/plain", "Failed to create ZIP file");
        }
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
        if (m_batteryManager) {
            result["batteryPercent"] = m_batteryManager->batteryPercent();
        }
        if (m_settings) {
            result["waterLevelDisplayUnit"] = m_settings->waterLevelDisplayUnit();
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
    else if (path == "/api/media/personal" && method == "GET") {
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
        qsizetype slashIdx = remainder.lastIndexOf('/');
        if (slashIdx > 0) {
            QString category = remainder.left(slashIdx);
            QString filename = QUrl::fromPercentEncoding(remainder.mid(slashIdx + 1).toUtf8());
            handleBackupProfileFile(socket, category, filename);
        } else {
            sendResponse(socket, 400, "application/json", R"({"error":"Invalid profile path"})");
        }
    }
    else if (path == "/api/backup/shots") {
        // Create a safe backup copy on background thread, then send it
        QPointer<QTcpSocket> socketGuard(socket);
        QString dbPath = m_storage->databasePath();
        auto destroyed = m_destroyed;
        QThread* thread = QThread::create([this, socketGuard, dbPath, destroyed]() {
            QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
            QString tempPath = tempDir + "/backup_web_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".db";

            QString result = ShotHistoryStorage::createBackupStatic(dbPath, tempPath);
            QByteArray fileData;
            if (!result.isEmpty()) {
                QFile f(tempPath);
                if (f.open(QIODevice::ReadOnly)) {
                    fileData = f.readAll();
                } else {
                    qWarning() << "ShotServer: Failed to read backup file:" << f.errorString();
                }
            } else {
                qWarning() << "ShotServer: createBackupStatic failed for" << dbPath << "->" << tempPath;
            }
            if (QFile::exists(tempPath))
                QFile::remove(tempPath);

            if (*destroyed) return;
            QMetaObject::invokeMethod(this, [this, socketGuard, destroyed,
                                             fileData = std::move(fileData)]() {
                if (*destroyed || !socketGuard) return;
                if (!fileData.isEmpty()) {
                    sendResponse(socketGuard, 200, "application/x-sqlite3", fileData);
                } else {
                    sendResponse(socketGuard, 500, "application/json", R"({"error":"Failed to create backup"})");
                }
            }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
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
        qsizetype slashIdx = remainder.indexOf('/');
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
    // Don't create timers for sockets that are already disconnecting/closed —
    // the timer (parented to the socket) would be destroyed by deleteLater()
    // while the pointer remains in m_keepAliveTimers, causing a dangling access.
    if (socket->state() != QAbstractSocket::ConnectedState) return;

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

bool ShotServer::setupTls()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    QString certPath = dataDir + "/server.crt";
    QString keyPath = dataDir + "/server.key";

    // iOS uses RSA keys (SecureTransport requires RSA/DSA for PKCS12), other platforms use EC
#ifdef Q_OS_IOS
    const auto keyAlgo = QSsl::Rsa;
#else
    const auto keyAlgo = QSsl::Ec;
#endif

    // Check if cert/key already exist
    if (QFile::exists(certPath) && QFile::exists(keyPath)) {
        QFile certFile(certPath);
        QFile keyFile(keyPath);
        if (certFile.open(QIODevice::ReadOnly) && keyFile.open(QIODevice::ReadOnly)) {
            m_sslCert = QSslCertificate(certFile.readAll(), QSsl::Pem);
            m_sslKey = QSslKey(keyFile.readAll(), keyAlgo, QSsl::Pem);
            if (!m_sslCert.isNull() && !m_sslKey.isNull()) {
                if (m_sslCert.expiryDate() <= QDateTime::currentDateTime()) {
                    qDebug() << "ShotServer: TLS certificate expired, regenerating";
                } else if (m_sslCert.subjectAlternativeNames().isEmpty()) {
                    qDebug() << "ShotServer: TLS certificate missing SANs (required by browsers), regenerating";
                } else {
                    qDebug() << "ShotServer: Loaded existing TLS certificate, expires" << m_sslCert.expiryDate().toString();
                    return true;
                }
            } else {
                qDebug() << "ShotServer: Existing cert/key invalid or wrong type, regenerating";
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
    m_sslKey = QSslKey(keyFile.readAll(), keyAlgo, QSsl::Pem);

    if (m_sslCert.isNull() || m_sslKey.isNull()) {
        qWarning() << "ShotServer: Generated certificate or key is invalid";
        return false;
    }

    qDebug() << "ShotServer: Generated new TLS certificate, expires" << m_sslCert.expiryDate().toString();
    return true;
}

#ifdef Q_OS_IOS
// ---------------------------------------------------------------------------
// iOS: Self-signed certificate generation using Apple Security.framework
// Uses SecKeyCreateRandomKey for RSA-2048 and manual DER/ASN.1 encoding
// for X509v3 with Subject Alternative Names. No OpenSSL dependency.
// ---------------------------------------------------------------------------

// ASN.1 DER encoding helpers
static QByteArray derLength(int len)
{
    QByteArray out;
    if (len < 128) {
        out.append(static_cast<char>(len));
    } else if (len < 256) {
        out.append(static_cast<char>(0x81));
        out.append(static_cast<char>(len));
    } else {
        out.append(static_cast<char>(0x82));
        out.append(static_cast<char>((len >> 8) & 0xFF));
        out.append(static_cast<char>(len & 0xFF));
    }
    return out;
}

static QByteArray derTag(unsigned char tag, const QByteArray& content)
{
    QByteArray out;
    out.append(static_cast<char>(tag));
    out.append(derLength(content.size()));
    out.append(content);
    return out;
}

static QByteArray derSequence(const QByteArray& content) { return derTag(0x30, content); }
static QByteArray derSet(const QByteArray& content) { return derTag(0x31, content); }
static QByteArray derOctetString(const QByteArray& content) { return derTag(0x04, content); }

static QByteArray derInteger(const QByteArray& val)
{
    QByteArray v = val;
    // Ensure positive by prepending 0x00 if high bit is set
    if (!v.isEmpty() && (static_cast<unsigned char>(v[0]) & 0x80))
        v.prepend(static_cast<char>(0x00));
    return derTag(0x02, v);
}

static QByteArray derBitString(const QByteArray& content)
{
    QByteArray v;
    v.append(static_cast<char>(0x00)); // no unused bits
    v.append(content);
    return derTag(0x03, v);
}

static QByteArray derOid(const char* bytes, int len)
{
    return derTag(0x06, QByteArray(bytes, len));
}

static QByteArray derUtf8String(const QByteArray& s) { return derTag(0x0C, s); }

static QByteArray derTime(const QDateTime& dt)
{
    // RFC 5280 §4.1.2.5: UTCTime for years through 2049, GeneralizedTime for 2050+
    QDateTime utc = dt.toUTC();
    if (utc.date().year() >= 2050) {
        // GeneralizedTime format: YYYYMMDDHHMMSSZ
        QByteArray s = utc.toString("yyyyMMddHHmmss").toLatin1() + "Z";
        return derTag(0x18, s);
    }
    // UTCTime format: YYMMDDHHMMSSZ
    QByteArray s = utc.toString("yyMMddHHmmss").toLatin1() + "Z";
    return derTag(0x17, s);
}

static QByteArray derExplicitTag(int tagNum, const QByteArray& content)
{
    return derTag(static_cast<unsigned char>(0xA0 | tagNum), content);
}

// OID constants
static const char OID_SHA256_RSA[] = "\x2a\x86\x48\x86\xf7\x0d\x01\x01\x0b"; // 1.2.840.113549.1.1.11
static const char OID_RSA[]        = "\x2a\x86\x48\x86\xf7\x0d\x01\x01\x01"; // 1.2.840.113549.1.1.1
static const char OID_CN[]         = "\x55\x04\x03";                           // 2.5.4.3
static const char OID_SAN[]        = "\x55\x1d\x11";                           // 2.5.29.17

static QByteArray sha256RsaAlgorithm()
{
    return derSequence(derOid(OID_SHA256_RSA, 9) + derTag(0x05, QByteArray())); // AlgorithmIdentifier { sha256WithRSA, NULL }
}

static QByteArray buildSanExtension(const QStringList& sanEntries)
{
    QByteArray generalNames;
    for (const QString& entry : sanEntries) {
        if (entry.startsWith("DNS:")) {
            QByteArray dns = entry.mid(4).toLatin1();
            generalNames.append(derTag(0x82, dns)); // [2] dNSName
        } else if (entry.startsWith("IP:")) {
            QHostAddress addr(entry.mid(3));
            quint32 ip4 = addr.toIPv4Address();
            QByteArray ipBytes;
            ipBytes.append(static_cast<char>((ip4 >> 24) & 0xFF));
            ipBytes.append(static_cast<char>((ip4 >> 16) & 0xFF));
            ipBytes.append(static_cast<char>((ip4 >> 8) & 0xFF));
            ipBytes.append(static_cast<char>(ip4 & 0xFF));
            generalNames.append(derTag(0x87, ipBytes)); // [7] iPAddress
        }
    }
    // Extension: OID + critical=false + OCTET STRING wrapping the GeneralNames SEQUENCE
    return derSequence(derOid(OID_SAN, 3) + derOctetString(derSequence(generalNames)));
}

bool ShotServer::generateSelfSignedCert(const QString& certPath, const QString& keyPath)
{
    // Generate RSA-2048 key pair using Security.framework
    NSDictionary* keyAttrs = @{
        (id)kSecAttrKeyType: (id)kSecAttrKeyTypeRSA,
        (id)kSecAttrKeySizeInBits: @2048,
    };
    CFErrorRef error = nullptr;
    SecKeyRef privateKey = SecKeyCreateRandomKey((__bridge CFDictionaryRef)keyAttrs, &error);
    if (!privateKey) {
        if (error) {
            qWarning() << "ShotServer: SecKeyCreateRandomKey failed:" << CFBridgingRelease(error);
        }
        return false;
    }

    SecKeyRef publicKey = SecKeyCopyPublicKey(privateKey);
    if (!publicKey) {
        CFRelease(privateKey);
        qWarning() << "ShotServer: Failed to extract public key";
        return false;
    }

    // Export public key (PKCS#1 DER format)
    CFDataRef pubKeyData = SecKeyCopyExternalRepresentation(publicKey, &error);
    CFRelease(publicKey);
    if (!pubKeyData) {
        CFRelease(privateKey);
        qWarning() << "ShotServer: Failed to export public key:" << CFBridgingRelease(error);
        return false;
    }
    QByteArray pubKeyDer(reinterpret_cast<const char*>(CFDataGetBytePtr(pubKeyData)),
                          static_cast<int>(CFDataGetLength(pubKeyData)));
    CFRelease(pubKeyData);

    // Export private key (PKCS#1 DER format)
    CFDataRef privKeyData = SecKeyCopyExternalRepresentation(privateKey, &error);
    if (!privKeyData) {
        CFRelease(privateKey);
        qWarning() << "ShotServer: Failed to export private key:" << CFBridgingRelease(error);
        return false;
    }
    QByteArray privKeyDer(reinterpret_cast<const char*>(CFDataGetBytePtr(privKeyData)),
                           static_cast<int>(CFDataGetLength(privKeyData)));
    CFRelease(privKeyData);

    // Build SubjectPublicKeyInfo: SEQUENCE { AlgorithmIdentifier, BIT STRING { pubkey } }
    QByteArray spki = derSequence(
        derSequence(derOid(OID_RSA, 9) + derTag(0x05, QByteArray())) +
        derBitString(pubKeyDer)
    );

    // Build issuer/subject: RDNSequence with CN=Decenza
    QByteArray rdnSeq = derSequence(
        derSet(derSequence(derOid(OID_CN, 3) + derUtf8String("Decenza")))
    );

    // Serial number (16 random bytes)
    QByteArray serialBytes(16, Qt::Uninitialized);
    SecRandomCopyBytes(kSecRandomDefault, serialBytes.size(),
                        reinterpret_cast<uint8_t*>(serialBytes.data()));
    // Ensure positive
    serialBytes[0] = serialBytes[0] & 0x7F;

    // Validity: now to 10 years
    QDateTime notBefore = QDateTime::currentDateTimeUtc();
    QDateTime notAfter = notBefore.addYears(10);

    // Subject Alternative Names
    QStringList sanEntries;
    sanEntries << "IP:127.0.0.1" << "DNS:localhost";
    for (const QHostAddress& addr : QNetworkInterface::allAddresses()) {
        if (!addr.isLoopback() && addr.protocol() == QAbstractSocket::IPv4Protocol) {
            sanEntries << QString("IP:%1").arg(addr.toString());
        }
    }

    // Extensions: SAN only
    QByteArray extensions = derSequence(buildSanExtension(sanEntries));

    // Build TBSCertificate
    QByteArray tbs = derSequence(
        derExplicitTag(0, derInteger(QByteArray(1, 0x02))) +  // version v3
        derInteger(serialBytes) +
        sha256RsaAlgorithm() +
        rdnSeq +                                               // issuer
        derSequence(derTime(notBefore) + derTime(notAfter)) +
        rdnSeq +                                               // subject (self-signed)
        spki +
        derExplicitTag(3, extensions)
    );

    // Sign TBSCertificate with private key
    CFDataRef tbsData = CFDataCreate(kCFAllocatorDefault,
                                      reinterpret_cast<const UInt8*>(tbs.constData()),
                                      tbs.size());
    CFDataRef signature = SecKeyCreateSignature(privateKey,
                                                 kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA256,
                                                 tbsData, &error);
    CFRelease(tbsData);
    CFRelease(privateKey);

    if (!signature) {
        qWarning() << "ShotServer: SecKeyCreateSignature failed:" << CFBridgingRelease(error);
        return false;
    }

    QByteArray sigBytes(reinterpret_cast<const char*>(CFDataGetBytePtr(signature)),
                         static_cast<int>(CFDataGetLength(signature)));
    CFRelease(signature);

    // Build final Certificate DER
    QByteArray certDer = derSequence(tbs + sha256RsaAlgorithm() + derBitString(sigBytes));

    // Write certificate as PEM (RFC 7468 requires 64-char line wrapping)
    auto base64Wrapped = [](const QByteArray& der) {
        QByteArray b64 = der.toBase64(QByteArray::Base64Encoding);
        QByteArray wrapped;
        for (qsizetype i = 0; i < b64.size(); i += 64) {
            wrapped.append(b64.mid(i, 64));
            wrapped.append('\n');
        }
        return wrapped;
    };
    QByteArray certPem = "-----BEGIN CERTIFICATE-----\n" +
                          base64Wrapped(certDer) +
                          "-----END CERTIFICATE-----\n";

    // Write private key as PEM (PKCS#1 RSA format)
    QByteArray keyPem = "-----BEGIN RSA PRIVATE KEY-----\n" +
                         base64Wrapped(privKeyDer) +
                         "-----END RSA PRIVATE KEY-----\n";

    QFile certFile(certPath);
    if (!certFile.open(QIODevice::WriteOnly)) return false;
    certFile.write(certPem);
    certFile.close();

    QFile keyFile(keyPath);
    if (!keyFile.open(QIODevice::WriteOnly)) return false;
    keyFile.write(keyPem);
    keyFile.close();

    QFile::setPermissions(keyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

#else // !Q_OS_IOS
// ---------------------------------------------------------------------------
// Desktop/Android: Self-signed certificate generation using OpenSSL
// ---------------------------------------------------------------------------

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
                                    reinterpret_cast<const unsigned char*>("Decenza"), -1, -1, 0);
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
