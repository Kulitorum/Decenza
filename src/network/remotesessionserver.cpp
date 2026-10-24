#include "remotesessionserver.h"

#include "../remote/qtrowebsocketdevice.h"

#include <QJsonDocument>
#include <QTcpSocket>
#include <QWebSocket>
#include <QWebSocketCorsAuthenticator>
#include <QWebSocketServer>
#include "webrequestguard.h"

#include <QUrl>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace {
constexpr int kSnapshotIntervalMs = 250;
constexpr qint64 kMaximumSocketReadBuffer = 8 * 1024;
// Retain a small fixed output budget (roughly two maximum snapshots) so a
// client that stops reading cannot accumulate an unbounded write queue.
constexpr qint64 kMaximumQueuedOutput = 64 * 1024;
// Bound each complete state/telemetry snapshot, including appearance inputs.
constexpr qsizetype kMaximumSnapshotBytes = 32 * 1024;
constexpr qint64 kMaximumTextFrameOverhead = 16;
constexpr quint64 kMaximumIncomingMessageBytes = 4 * 1024;
// JSON encodes sequence as a number. Keep it within the exact integer range
// shared by Qt's JSON double and JavaScript's Number.
constexpr quint64 kMaximumExactJsonInteger = 9'007'199'254'740'991ULL;
constexpr auto kSessionUpgradePath = "/api/session/ws";
constexpr auto kRemoteObjectsUpgradePath = "/api/session/objects";

RemoteSessionServer::UpgradeInspection inspection(
    RemoteSessionServer::UpgradeDisposition disposition, int status = 400,
    const QByteArray& body = {})
{
    RemoteSessionServer::UpgradeInspection result;
    result.disposition = disposition;
    result.statusCode = status;
    result.responseBody = body;
    return result;
}

bool isPotentialSessionUpgradePrefix(const QByteArray& bytes)
{
    static const QList<QByteArray> routePaths{
        QByteArrayLiteral("/api/session/ws"),
        QByteArrayLiteral("/api/session/objects")
    };
    if (bytes.isEmpty())
        return false;

    const qsizetype lineEnd = bytes.indexOf("\r\n");
    if (lineEnd >= 0) {
        const QList<QByteArray> parts = bytes.left(lineEnd).split(' ');
        if (parts.size() < 2)
            return false;
        for (const QByteArray& path : routePaths) {
            if (parts.at(1) == path || parts.at(1).startsWith(path + '?'))
                return true;
        }
        return false;
    }

    // Keep a possible GET request-line prefix buffered until its target can be
    // distinguished from other HTTP routes.
    for (const QByteArray& path : routePaths) {
        const QByteArray prefix = QByteArrayLiteral("GET ") + path;
        if (prefix.startsWith(bytes) || bytes.startsWith(prefix))
            return true;
    }
    return false;
}

bool parseRemoteObjectTarget(const QByteArray& target, QString* packageId, QString* hostId,
                             QString* clientId = nullptr)
{
    static const QRegularExpression packagePattern(QStringLiteral("^[0-9a-f]{64}$"));
    static const QRegularExpression hostPattern(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    const QUrl url(QString::fromLatin1(target), QUrl::StrictMode);
    if (!url.isValid() || url.path() != QString::fromLatin1(kRemoteObjectsUpgradePath)
        || url.hasFragment() || !url.hasQuery()) {
        return false;
    }
    const QUrlQuery query(url);
    const auto items = query.queryItems(QUrl::FullyDecoded);
    if (items.size() != 3)
        return false;
    int packageCount = 0;
    int hostCount = 0;
    int clientCount = 0;
    QString parsedPackage;
    QString parsedHost;
    QString parsedClient;
    for (const auto& item : items) {
        if (item.first == QStringLiteral("packageId")) {
            ++packageCount;
            parsedPackage = item.second;
        } else if (item.first == QStringLiteral("hostId")) {
            ++hostCount;
            parsedHost = item.second;
        } else if (item.first == QStringLiteral("clientId")) {
            ++clientCount;
            parsedClient = item.second;
        } else {
            return false;
        }
    }
    if (packageCount != 1 || hostCount != 1 || clientCount != 1
        || !packagePattern.match(parsedPackage).hasMatch()
        || !hostPattern.match(parsedHost).hasMatch()
        || !hostPattern.match(parsedClient).hasMatch() || QUuid(parsedClient).isNull()) {
        return false;
    }
    *packageId = parsedPackage;
    *hostId = parsedHost;
    if (clientId) *clientId = parsedClient;
    return true;
}

QByteArray authenticatedRequestForSocket(const QWebSocket* socket)
{
    const QUrl url = socket->request().url();
    QByteArray request = QByteArrayLiteral("GET ") + url.path(QUrl::FullyEncoded).toLatin1();
    if (url.hasQuery())
        request += QByteArrayLiteral("?") + url.query(QUrl::FullyEncoded).toLatin1();
    request += QByteArrayLiteral(" HTTP/1.1\r\n");
    const QByteArray cookie = socket->request().rawHeader(QByteArrayLiteral("Cookie"));
    if (!cookie.isEmpty())
        request += QByteArrayLiteral("Cookie: ") + cookie + QByteArrayLiteral("\r\n");
    request += QByteArrayLiteral("\r\n");
    return request;
}

QByteArray requestTargetForSocket(const QWebSocket* socket)
{
    const QUrl url = socket->request().url();
    QByteArray target = url.path(QUrl::FullyEncoded).toLatin1();
    if (url.hasQuery())
        target += QByteArrayLiteral("?") + url.query(QUrl::FullyEncoded).toLatin1();
    return target;
}

bool sameOriginForListener(const QString& origin, const QByteArray& headerBlock, bool secure)
{
    const QUrl originUrl(origin, QUrl::StrictMode);
    const QString expectedScheme = secure ? QStringLiteral("https") : QStringLiteral("http");
    if (!originUrl.isValid() || originUrl.scheme().compare(expectedScheme, Qt::CaseInsensitive) != 0
        || originUrl.host().isEmpty() || !originUrl.userInfo().isEmpty()
        || (!originUrl.path().isEmpty() && originUrl.path() != QStringLiteral("/"))
        || originUrl.hasQuery() || originUrl.hasFragment()) {
        return false;
    }

    const QString hostHeader = WebRequestGuard::headerValue(headerBlock, "Host");
    const QUrl hostUrl(expectedScheme + QStringLiteral("://") + hostHeader, QUrl::StrictMode);
    if (!hostUrl.isValid() || hostUrl.host().isEmpty())
        return false;

    const int defaultPort = secure ? 443 : 80;
    return originUrl.host().compare(hostUrl.host(), Qt::CaseInsensitive) == 0
        && originUrl.port(defaultPort) == hostUrl.port(defaultPort);
}

bool strictHeaders(const QByteArray& headerBlock, QString* error)
{
    const QList<QByteArray> lines = headerBlock.split('\n');
    QSet<QByteArray> names;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        QByteArray line = lines.at(i);
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty())
            continue;
        if (line.startsWith(' ') || line.startsWith('\t')) {
            *error = QStringLiteral("Obsolete folded headers are not accepted");
            return false;
        }
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) {
            *error = QStringLiteral("Malformed HTTP header");
            return false;
        }
        const QByteArray name = line.left(colon).toLower();
        const auto isTokenCharacter = [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9')
                || QByteArrayLiteral("!#$%&'*+-.^_`|~").contains(c);
        };
        const bool validName = !name.isEmpty()
            && std::all_of(name.cbegin(), name.cend(), isTokenCharacter);
        if (!validName || names.contains(name)) {
            *error = QStringLiteral("Invalid or duplicate HTTP header name");
            return false;
        }
        names.insert(name);
        if (name == QByteArrayLiteral("content-length")
            || name == QByteArrayLiteral("transfer-encoding")) {
            *error = QStringLiteral("WebSocket upgrade requests must not have an HTTP body");
            return false;
        }
    }
    return true;
}
}

RemoteSessionServer::UpgradeInspection RemoteSessionServer::inspectUpgradeBuffer(
    const QByteArray& peekedBytes, qint64 bytesAvailable, qint64 maximumHeaderSize,
    bool secureOrigin, bool requireSession, const SessionValidator& validateSession,
    const QString& expectedPackageId, const QString& expectedHostId)
{
    if (!isPotentialSessionUpgradePrefix(peekedBytes))
        return inspection(UpgradeDisposition::NotTarget);

    if (bytesAvailable > maximumHeaderSize || peekedBytes.size() > maximumHeaderSize) {
        return inspection(UpgradeDisposition::Rejected, 413,
                          QByteArrayLiteral("{\"error\":\"Headers too large\"}"));
    }

    const qsizetype lineEnd = peekedBytes.indexOf("\r\n");
    if (lineEnd < 0)
        return inspection(UpgradeDisposition::NeedMore);

    const QList<QByteArray> parts = peekedBytes.left(lineEnd).split(' ');
    if (parts.size() != 3) {
        return inspection(UpgradeDisposition::Rejected, 400,
                          QByteArrayLiteral("{\"error\":\"Malformed request line\"}"));
    }
    const QByteArray& method = parts.at(0);
    const QByteArray& target = parts.at(1);
    const QByteArray& version = parts.at(2);
    if (target.startsWith(QByteArrayLiteral("/api/session/ws?")))
        return inspection(UpgradeDisposition::Rejected, 400,
                          QByteArrayLiteral("{\"error\":\"Invalid WebSocket route\"}"));
    const bool snapshotRoute = target == kSessionUpgradePath;
    const bool objectsRoute = target == kRemoteObjectsUpgradePath
        || target.startsWith(QByteArrayLiteral("/api/session/objects?"));
    if (!snapshotRoute && !objectsRoute) {
        return inspection(UpgradeDisposition::NotTarget);
    }
    if (method != QByteArrayLiteral("GET")) {
        return inspection(UpgradeDisposition::Rejected, 405,
                          QByteArrayLiteral("{\"error\":\"GET required\"}"));
    }
    if (version != QByteArrayLiteral("HTTP/1.1")) {
        return inspection(UpgradeDisposition::Rejected, 400,
                          QByteArrayLiteral("{\"error\":\"Invalid WebSocket route\"}"));
    }
    const qsizetype headerEnd = peekedBytes.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return inspection(UpgradeDisposition::NeedMore);

    const qsizetype fullHeaderSize = headerEnd + 4;
    if (fullHeaderSize > maximumHeaderSize) {
        return inspection(UpgradeDisposition::Rejected, 413,
                          QByteArrayLiteral("{\"error\":\"Headers too large\"}"));
    }
    // Per RFC 6455, a client must wait for the HTTP 101 before sending frames.
    // Treat bytes after the header as an invalid/pipelined request boundary.
    if (bytesAvailable != fullHeaderSize) {
        return inspection(UpgradeDisposition::Rejected, 400,
                          QByteArrayLiteral("{\"error\":\"Unexpected bytes after headers\"}"));
    }

    const QByteArray headerBlock = peekedBytes.left(headerEnd);
    QString headerError;
    if (!strictHeaders(headerBlock, &headerError)) {
        const QByteArray body = QJsonDocument(
            QJsonObject{{QStringLiteral("error"), headerError}})
                                    .toJson(QJsonDocument::Compact);
        return inspection(UpgradeDisposition::Rejected, 400, body);
    }

    const QString origin = WebRequestGuard::headerValue(headerBlock, "Origin");
    if (origin.isEmpty()) {
        return inspection(UpgradeDisposition::Rejected, 403,
                          QByteArrayLiteral("{\"error\":\"Origin required\"}"));
    }
    const QString crossSite = WebRequestGuard::crossSiteReason(
        QStringLiteral("GET"), QString::fromLatin1(target.left(target.indexOf('?') < 0
                                                                  ? target.size()
                                                                  : target.indexOf('?'))),
        headerBlock);
    if (!crossSite.isEmpty() || !sameOriginForListener(origin, headerBlock, secureOrigin)) {
        return inspection(UpgradeDisposition::Rejected, 403,
                          QByteArrayLiteral("{\"error\":\"Same-origin request required\"}"));
    }
    if (requireSession && (!validateSession || !validateSession(peekedBytes.left(fullHeaderSize)))) {
        return inspection(UpgradeDisposition::Rejected, 401,
                          QByteArrayLiteral("{\"error\":\"Session required\"}"));
    }
    if (objectsRoute) {
        if (expectedPackageId.isEmpty() || expectedHostId.isEmpty()) {
            return inspection(UpgradeDisposition::Rejected, 503,
                              QByteArrayLiteral("{\"error\":\"QtRO session is unavailable\"}"));
        }
        QString requestedPackageId;
        QString requestedHostId;
        if (!parseRemoteObjectTarget(target, &requestedPackageId, &requestedHostId)) {
            return inspection(UpgradeDisposition::Rejected, 400,
                              QByteArrayLiteral("{\"error\":\"Invalid QtRO session identity\"}"));
        }
        if (requestedPackageId != expectedPackageId || requestedHostId != expectedHostId) {
            return inspection(UpgradeDisposition::Rejected, 409,
                              QByteArrayLiteral("{\"error\":\"QtRO session identity mismatch\"}"));
        }
    }

    UpgradeInspection result = inspection(UpgradeDisposition::Accepted);
    result.requestHeaders = peekedBytes.left(fullHeaderSize);
    return result;
}

RemoteSessionServer::RemoteSessionServer(SnapshotProvider snapshotProvider,
                                         SessionValidator sessionValidator,
                                         bool secureOrigin,
                                         QObject* parent)
    : QObject(parent)
    , m_server(new QWebSocketServer(
          QStringLiteral("Decenza read-only session"),
          secureOrigin ? QWebSocketServer::SecureMode : QWebSocketServer::NonSecureMode,
          this))
    , m_snapshotProvider(std::move(snapshotProvider))
    , m_sessionValidator(std::move(sessionValidator))
{
    m_server->setMaxPendingConnections(64);
    // Match ShotServer's existing idle-handshake deadline. Once the HTTP
    // upgrade is handed off, Qt owns the handshake timeout and parser.
    // Qt 6.12 qtwebsockets/src/websockets/qwebsocketserver_p.cpp:544-561.
    m_server->setHandshakeTimeout(30'000);

    connect(m_server, &QWebSocketServer::newConnection,
            this, &RemoteSessionServer::takePendingConnections);
    // ShotServer has already checked Host, explicit Origin, fetch metadata,
    // and (when enabled) the cookie session before calling adoptConnection().
    // Only this preflighted route can enter this server.
    connect(m_server, &QWebSocketServer::originAuthenticationRequired,
            this, [](QWebSocketCorsAuthenticator* authenticator) {
                authenticator->setAllowed(true);
            });

    m_publishTimer.setInterval(kSnapshotIntervalMs);
    connect(&m_publishTimer, &QTimer::timeout,
            this, &RemoteSessionServer::publishSnapshots);
}

RemoteSessionServer::~RemoteSessionServer()
{
    closeAll();
}

void RemoteSessionServer::adoptConnection(QTcpSocket* socket)
{
    if (!socket)
        return;

    for (auto it = m_handedOffSockets.begin(); it != m_handedOffSockets.end();) {
        if (it.value().isNull())
            it = m_handedOffSockets.erase(it);
        else
            ++it;
    }
    if (ownsSocket(socket))
        return;
    m_handedOffSockets.insert(socket, socket);
    m_server->handleConnection(socket);
}

void RemoteSessionServer::setRemoteObjectHandler(const QString& packageId,
                                                 const QString& hostId,
                                                 RemoteObjectHandler handler)
{
    static const QRegularExpression packagePattern(QStringLiteral("^[0-9a-f]{64}$"));
    static const QRegularExpression hostPattern(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    if (!handler || !packagePattern.match(packageId).hasMatch()
        || !hostPattern.match(hostId).hasMatch()) {
        const auto sockets = m_remoteObjectClients.keys();
        for (QWebSocket* socket : sockets) {
            if (socket)
                closeClient(socket, QWebSocketProtocol::CloseCodePolicyViolated,
                            QStringLiteral("QtRO session is unavailable"));
        }
        m_remoteObjectHandler = {};
        m_remoteObjectPackageId.clear();
        m_remoteObjectHostId.clear();
        return;
    }
    if (packageId != m_remoteObjectPackageId || hostId != m_remoteObjectHostId) {
        const auto sockets = m_remoteObjectClients.keys();
        for (QWebSocket* socket : sockets) {
            if (socket)
                closeClient(socket, QWebSocketProtocol::CloseCodePolicyViolated,
                            QStringLiteral("QtRO session identity changed"));
        }
    }
    m_remoteObjectPackageId = packageId;
    m_remoteObjectHostId = hostId;
    m_remoteObjectHandler = std::move(handler);
}

QString RemoteSessionServer::remoteObjectPackageId() const
{
    return m_remoteObjectHandler ? m_remoteObjectPackageId : QString();
}

QString RemoteSessionServer::remoteObjectHostId() const
{
    return m_remoteObjectHandler ? m_remoteObjectHostId : QString();
}

bool RemoteSessionServer::ownsSocket(const QTcpSocket* socket) const
{
    if (!socket)
        return false;
    const auto it = m_handedOffSockets.constFind(const_cast<QTcpSocket*>(socket));
    return it != m_handedOffSockets.cend() && !it.value().isNull();
}

void RemoteSessionServer::forgetSocket(QTcpSocket* socket)
{
    m_handedOffSockets.remove(socket);
}

void RemoteSessionServer::closeAll()
{
    m_publishTimer.stop();

    const auto clients = m_clients.keys();
    for (QWebSocket* socket : clients) {
        if (!socket)
            continue;
        socket->abort();
        socket->deleteLater();
    }
    m_clients.clear();

    const auto remoteClients = m_remoteObjectClients.keys();
    for (QWebSocket* socket : remoteClients) {
        if (!socket)
            continue;
        removeRemoteObjectClient(socket);
        socket->abort();
        socket->deleteLater();
    }
    m_remoteObjectClients.clear();

    const auto pendingSockets = m_handedOffSockets.values();
    for (const QPointer<QTcpSocket>& guardedSocket : pendingSockets) {
        QTcpSocket* socket = guardedSocket.data();
        if (socket)
            socket->abort();
    }
    for (auto it = m_handedOffSockets.begin(); it != m_handedOffSockets.end();) {
        if (it.value().isNull())
            it = m_handedOffSockets.erase(it);
        else
            ++it;
    }
}

void RemoteSessionServer::takePendingConnections()
{
    for (auto it = m_handedOffSockets.begin(); it != m_handedOffSockets.end();) {
        if (it.value().isNull())
            it = m_handedOffSockets.erase(it);
        else
            ++it;
    }
    while (m_server->hasPendingConnections()) {
        QWebSocket* socket = m_server->nextPendingConnection();
        if (!socket)
            continue;

        QTcpSocket* socketIdentity = nullptr;
        for (auto it = m_handedOffSockets.cbegin(); it != m_handedOffSockets.cend(); ++it) {
            QTcpSocket* rawSocket = it.value().data();
            if (rawSocket && rawSocket->peerAddress() == socket->peerAddress()
                && rawSocket->peerPort() == socket->peerPort()) {
                socketIdentity = it.key();
                break;
            }
        }

        socket->setParent(this);
        socket->setMaxAllowedIncomingFrameSize(kMaximumIncomingMessageBytes);
        socket->setMaxAllowedIncomingMessageSize(kMaximumIncomingMessageBytes);
        socket->setReadBufferSize(kMaximumSocketReadBuffer);

        const QByteArray authenticatedRequest = authenticatedRequestForSocket(socket);

        if (m_sessionValidator && !m_sessionValidator(authenticatedRequest)) {
            if (socketIdentity)
                emit connectionClosed(socketIdentity);
            socket->close(QWebSocketProtocol::CloseCodePolicyViolated,
                          QStringLiteral("Session expired"));
            socket->deleteLater();
            continue;
        }

        const QUrl requestUrl = socket->request().url();
        if (requestUrl.path() == QString::fromLatin1(kRemoteObjectsUpgradePath)) {
            QString requestedPackageId;
            QString requestedHostId;
            if (!m_remoteObjectHandler
                || !parseRemoteObjectTarget(requestTargetForSocket(socket),
                                            &requestedPackageId, &requestedHostId)
                || requestedPackageId != m_remoteObjectPackageId
                || requestedHostId != m_remoteObjectHostId) {
                if (socketIdentity)
                    emit connectionClosed(socketIdentity);
                socket->close(QWebSocketProtocol::CloseCodePolicyViolated,
                              QStringLiteral("QtRO session identity mismatch"));
                socket->deleteLater();
                continue;
            }
            acceptRemoteObjectClient(socket, socketIdentity);
            continue;
        }
        if (requestUrl.path() != QString::fromLatin1(kSessionUpgradePath)) {
            if (socketIdentity)
                emit connectionClosed(socketIdentity);
            socket->close(QWebSocketProtocol::CloseCodePolicyViolated,
                          QStringLiteral("Unknown session WebSocket route"));
            socket->deleteLater();
            continue;
        }

        Client client;
        client.socket = socket;
        client.socketIdentity = socketIdentity;
        client.authenticatedRequest = std::move(authenticatedRequest);
        m_clients.insert(socket, std::move(client));

        connect(socket, &QWebSocket::textMessageReceived, this,
                [this, socket](const QString&) {
                    closeClient(socket, QWebSocketProtocol::CloseCodePolicyViolated,
                                QStringLiteral("This session is read-only"));
                });
        connect(socket, &QWebSocket::binaryMessageReceived, this,
                [this, socket](const QByteArray&) {
                    closeClient(socket, QWebSocketProtocol::CloseCodePolicyViolated,
                                QStringLiteral("This session is read-only"));
                });
        connect(socket, &QWebSocket::disconnected, this, [this, socket] {
            removeClient(socket);
            socket->deleteLater();
        });
        connect(socket, &QObject::destroyed, this, [this, socket] {
            removeClient(socket);
        });

        Client& acceptedClient = m_clients[socket];
        if (!sendSnapshot(socket, acceptedClient))
            continue;
        if (!m_publishTimer.isActive())
            m_publishTimer.start();
    }
}

bool RemoteSessionServer::acceptRemoteObjectClient(QWebSocket* socket,
                                                    QTcpSocket* socketIdentity)
{
    if (!socket || !m_remoteObjectHandler)
        return false;

    RemoteObjectClient client;
    QString packageId, hostId;
    if (!parseRemoteObjectTarget(requestTargetForSocket(socket), &packageId, &hostId,
                                 &client.clientId))
        return false;
    for (const auto& existing : std::as_const(m_remoteObjectClients)) {
        if (existing.clientId == client.clientId) {
            if (socketIdentity) emit connectionClosed(socketIdentity);
            socket->close(QWebSocketProtocol::CloseCodePolicyViolated,
                          QStringLiteral("QtRO client identity already connected"));
            socket->deleteLater();
            return false;
        }
    }
    client.socket = socket;
    client.socketIdentity = socketIdentity;
    client.authenticatedRequest = authenticatedRequestForSocket(socket);
    client.device = new QtRoWebSocketDevice(
        socket,
        [this, request = client.authenticatedRequest] {
            return !m_sessionValidator || m_sessionValidator(request);
        },
        QtRoWebSocketDevice::MaximumIncomingMessageBytes,
        QtRoWebSocketDevice::MaximumBufferedIncomingBytes,
        QtRoWebSocketDevice::MaximumOutgoingQueuedBytes,
        this);
    m_remoteObjectClients.insert(socket, client);

    connect(socket, &QWebSocket::disconnected, this, [this, socket] {
        removeRemoteObjectClient(socket);
        socket->deleteLater();
    });
    connect(socket, &QObject::destroyed, this, [this, socket] {
        removeRemoteObjectClient(socket);
    });
    connect(client.device, &QtRoWebSocketDevice::transportClosed, this, [this, socket] {
        removeRemoteObjectClient(socket);
    });

    emit remoteObjectClientOpened(client.clientId);
    m_remoteObjectHandler(client.device.data());
    if (!m_publishTimer.isActive())
        m_publishTimer.start();
    return true;
}

void RemoteSessionServer::removeRemoteObjectClient(QWebSocket* socket)
{
    const auto it = m_remoteObjectClients.find(socket);
    if (it == m_remoteObjectClients.end())
        return;
    QTcpSocket* socketIdentity = it->socketIdentity;
    QtRoWebSocketDevice* device = it->device.data();
    const QString clientId = it->clientId;
    m_remoteObjectClients.erase(it);
    emit remoteObjectClientClosed(clientId);
    if (device)
        device->deleteLater();
    if (socketIdentity)
        emit connectionClosed(socketIdentity);
    if (m_clients.isEmpty() && m_remoteObjectClients.isEmpty())
        m_publishTimer.stop();
}

bool RemoteSessionServer::validateRemoteObjectClient(RemoteObjectClient& client)
{
    QWebSocket* socket = client.socket.data();
    if (!socket || socket->state() == QAbstractSocket::UnconnectedState)
        return false;
    if (socket->state() != QAbstractSocket::ConnectedState)
        return true;
    if (m_sessionValidator && !m_sessionValidator(client.authenticatedRequest)) {
        socket->close(QWebSocketProtocol::CloseCodePolicyViolated,
                      QStringLiteral("Session expired"));
    }
    return true;
}

void RemoteSessionServer::removeClient(QWebSocket* socket)
{
    const auto it = m_clients.find(socket);
    if (it == m_clients.end())
        return;

    QTcpSocket* socketIdentity = it->socketIdentity;
    m_clients.erase(it);
    if (socketIdentity)
        emit connectionClosed(socketIdentity);
    if (m_clients.isEmpty() && m_remoteObjectClients.isEmpty())
        m_publishTimer.stop();
}

void RemoteSessionServer::publishSnapshots()
{
    for (auto it = m_handedOffSockets.begin(); it != m_handedOffSockets.end();) {
        if (it.value().isNull())
            it = m_handedOffSockets.erase(it);
        else
            ++it;
    }

    const auto sockets = m_clients.keys();
    for (QWebSocket* socket : sockets) {
        auto it = m_clients.find(socket);
        if (it == m_clients.end())
            continue;
        QWebSocket* liveSocket = it->socket.data();
        if (!liveSocket) {
            removeClient(socket);
            continue;
        }
        if (liveSocket->state() == QAbstractSocket::UnconnectedState) {
            removeClient(socket);
            continue;
        }
        // ClosingState (and any brief non-connected transition) is still an
        // accounted connection. Keep its entry until disconnected.
        if (liveSocket->state() != QAbstractSocket::ConnectedState)
            continue;
        sendSnapshot(liveSocket, it.value());
    }

    const auto remoteSockets = m_remoteObjectClients.keys();
    for (QWebSocket* socket : remoteSockets) {
        auto it = m_remoteObjectClients.find(socket);
        if (it == m_remoteObjectClients.end())
            continue;
        if (!validateRemoteObjectClient(it.value()))
            removeRemoteObjectClient(socket);
    }

    if (m_clients.isEmpty() && m_remoteObjectClients.isEmpty())
        m_publishTimer.stop();
}

bool RemoteSessionServer::sendSnapshot(QWebSocket* socket, Client& client)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return false;

    if (m_sessionValidator && !m_sessionValidator(client.authenticatedRequest)) {
        closeClient(socket, QWebSocketProtocol::CloseCodePolicyViolated,
                    QStringLiteral("Session expired"));
        return false;
    }

    if (client.sequence >= kMaximumExactJsonInteger || !m_snapshotProvider) {
        closeClient(socket, QWebSocketProtocol::CloseCodeBadOperation,
                    QStringLiteral("Session snapshot unavailable"));
        return false;
    }
    const quint64 nextSequence = client.sequence + 1;

    const QByteArray json = QJsonDocument(m_snapshotProvider(nextSequence))
                                .toJson(QJsonDocument::Compact);
    if (json.isEmpty() || json.size() > kMaximumSnapshotBytes) {
        closeClient(socket, QWebSocketProtocol::CloseCodeBadOperation,
                    QStringLiteral("Session snapshot exceeds limit"));
        return false;
    }
    if (socket->bytesToWrite() + json.size() + kMaximumTextFrameOverhead
        > kMaximumQueuedOutput) {
        // A close frame could sit behind the same full write queue. Abort to
        // release this bounded connection immediately.
        socket->abort();
        return false;
    }

    if (socket->sendTextMessage(QString::fromUtf8(json)) < 0) {
        closeClient(socket, QWebSocketProtocol::CloseCodeAbnormalDisconnection,
                    QStringLiteral("Unable to send session snapshot"));
        return false;
    }
    client.sequence = nextSequence;
    return true;
}

void RemoteSessionServer::closeClient(QWebSocket* socket,
                                      QWebSocketProtocol::CloseCode closeCode,
                                      const QString& reason)
{
    if (socket && socket->state() == QAbstractSocket::ConnectedState)
        socket->close(closeCode, reason);
}
