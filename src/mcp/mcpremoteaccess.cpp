#include "mcpremoteaccess.h"

#include "mcpserver.h"
#include "mcptunnel_tsnet.h"
#include "../core/settings_mcp.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QHostAddress>
#include <QUrl>
#include <QStandardPaths>
#include <QDir>
#include <QSysInfo>

McpRemoteAccess::McpRemoteAccess(QObject* parent)
    : QObject(parent)
    , m_reaper(new QTimer(this))
{
    // Periodic reaper: closes idle keep-alive sockets and re-listens if the
    // listener dropped (network flap). A genuinely periodic housekeeping task,
    // not an event guard.
    m_reaper->setInterval(ReaperIntervalMs);
    connect(m_reaper, &QTimer::timeout, this, &McpRemoteAccess::onReaperTick);
}

McpRemoteAccess::~McpRemoteAccess()
{
    stopTunnel();   // brings the embedded node down (joins its worker) if running
    stopListener();
}

void McpRemoteAccess::setSettings(SettingsMcp* settings)
{
    m_settings = settings;
    if (!m_settings)
        return;

    // Any change that affects reachability re-evaluates the listener.
    connect(m_settings, &SettingsMcp::remoteMcpEnabledChanged, this, &McpRemoteAccess::refresh);
    connect(m_settings, &SettingsMcp::remoteMcpModeChanged, this, &McpRemoteAccess::refresh);
    connect(m_settings, &SettingsMcp::remoteMcpPortChanged, this, &McpRemoteAccess::refresh);
    // Master MCP toggle also gates remote access — no point serving when the
    // MCP server itself is off.
    connect(m_settings, &SettingsMcp::mcpEnabledChanged, this, &McpRemoteAccess::refresh);

    // Changes that only affect the composed URL shown in the UI.
    connect(m_settings, &SettingsMcp::remoteMcpCustomBaseUrlChanged, this,
            &McpRemoteAccess::connectorUrlChanged);
    connect(m_settings, &SettingsMcp::remoteMcpModeChanged, this,
            &McpRemoteAccess::connectorUrlChanged);
    connect(m_settings, &SettingsMcp::remoteMcpTokenChanged, this,
            &McpRemoteAccess::connectorUrlChanged);
}

QString McpRemoteAccess::statusString() const
{
    switch (m_status) {
    case Off:          return QStringLiteral("off");
    case Starting:     return QStringLiteral("starting");
    case Active:       return QStringLiteral("active");
    case Reconnecting: return QStringLiteral("reconnecting");
    case Error:        return QStringLiteral("error");
    }
    return QStringLiteral("off");
}

int McpRemoteAccess::listenPort() const
{
    return (m_listener && m_listener->isListening())
        ? static_cast<int>(m_listener->serverPort()) : 0;
}

QString McpRemoteAccess::connectorUrl() const
{
    // Both the master MCP toggle and the remote toggle must be on — with MCP off,
    // refresh() stops the listener, so a composed URL would point at nothing.
    if (!m_settings || !m_settings->mcpEnabled() || !m_settings->remoteMcpEnabled())
        return QString();

    const QString mode = m_settings->remoteMcpMode();
    if (mode == QString::fromLatin1(SettingsMcp::ModeCustom)) {
        QString base = m_settings->remoteMcpCustomBaseUrl().trimmed();
        if (base.isEmpty())
            return QString();
        // Only https bases produce a working connector (the vendor backend
        // requires TLS). Reject anything else rather than compose a dead URL.
        const QUrl url(base);
        if (url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0)
            return QString();
        while (base.endsWith('/'))
            base.chop(1);
        return base + QStringLiteral("/mcp/") + m_settings->remoteMcpToken();
    }

    // Mode A (Tailscale): the Funnel FQDN comes from the embedded node once it is
    // up. Empty until then (login pending / Funnel not yet approved).
    if (mode == QString::fromLatin1(SettingsMcp::ModeTailscale)) {
        if (!m_tunnel || m_tunnel->certDomain().isEmpty())
            return QString();
        return QStringLiteral("https://") + m_tunnel->certDomain()
               + QStringLiteral("/mcp/") + m_settings->remoteMcpToken();
    }

    // ngrok (Mode B) not implemented yet.
    return QString();
}

QString McpRemoteAccess::loginUrl() const
{
    return m_tunnel ? m_tunnel->authUrl() : QString();
}

bool McpRemoteAccess::tunnelAvailable()
{
    return McpTunnelTsnet::isAvailable();
}

void McpRemoteAccess::refresh()
{
    if (!m_settings) {
        stopTunnel();
        stopListener();
        setStatus(Off);
        return;
    }

    const bool wantOn = m_settings->mcpEnabled() && m_settings->remoteMcpEnabled();
    if (!wantOn) {
        stopTunnel();
        stopListener();
        setStatus(Off);
        emit connectorUrlChanged();
        return;
    }

    const QString mode = m_settings->remoteMcpMode();

    // Mode C (BYO URL): LAN-routable listener, an off-box proxy fronts it.
    if (mode == QString::fromLatin1(SettingsMcp::ModeCustom)) {
        stopTunnel();
        startListener(/*bindLoopbackOnly=*/false);
        emit connectorUrlChanged();
        return;
    }

    // Mode A (Tailscale): loopback listener + embedded node that Funnels to it.
    if (mode == QString::fromLatin1(SettingsMcp::ModeTailscale)) {
        if (!McpTunnelTsnet::isAvailable()) {
            stopListener();
            setStatus(Error, QStringLiteral("This build was not compiled with Tailscale support"));
            emit connectorUrlChanged();
            return;
        }
        startListener(/*bindLoopbackOnly=*/true);
        if (m_status == Error)
            return;  // listener failed to bind
        startTunnel();
        emit connectorUrlChanged();
        return;
    }

    // ngrok (Mode B) not implemented yet.
    stopTunnel();
    stopListener();
    setStatus(Error, QStringLiteral("Mode '%1' is not available in this build yet").arg(mode));
    emit connectorUrlChanged();
}

void McpRemoteAccess::startListener(bool bindLoopbackOnly)
{
    const quint16 port = m_settings ? static_cast<quint16>(m_settings->remoteMcpPort()) : 8890;
    const QHostAddress bindAddr = bindLoopbackOnly ? QHostAddress(QHostAddress::LocalHost)
                                                   : QHostAddress(QHostAddress::Any);

    if (m_listener && m_listener->isListening()) {
        if (m_listener->serverPort() == port && m_listener->serverAddress() == bindAddr)
            return;  // already serving the right port + interface
        stopListener();  // port or bind changed — rebind
    }

    setStatus(Starting);

    if (!m_listener) {
        m_listener = new QTcpServer(this);
        connect(m_listener, &QTcpServer::newConnection, this, &McpRemoteAccess::onNewConnection);
    }

    // Start the reaper before attempting to bind: if the bind fails now (e.g. the
    // port is transiently in use at boot), the reaper's recovery branch retries it
    // on the next tick. Without this, a failed initial bind would stay Error until
    // the next settings change.
    m_reaper->start();

    // Mode C binds a routable interface so an off-box reverse proxy can reach it;
    // Mode A binds loopback (the embedded tsnet node proxies from 127.0.0.1). The
    // listener still serves only the tokenized MCP route (route gating in
    // routeRequest), so no other ShotServer surface is ever exposed.
    if (!m_listener->listen(bindAddr, port)) {
        setStatus(Error, QStringLiteral("Could not listen on port %1: %2")
                            .arg(port).arg(m_listener->errorString()));
        return;
    }

    // Mode C: listener up is as "active" as we can verify (the off-box proxy is
    // the user's responsibility). Mode A: stay Starting — the embedded tunnel's
    // state (login → running) drives the real status via onTunnelStateChanged().
    if (!bindLoopbackOnly)
        setStatus(Active);
}

void McpRemoteAccess::startTunnel()
{
    if (!McpTunnelTsnet::isAvailable() || !m_settings)
        return;
    if (!m_tunnel) {
        m_tunnel = new McpTunnelTsnet(this);
        connect(m_tunnel, &McpTunnelTsnet::stateChanged, this, &McpRemoteAccess::onTunnelStateChanged);
        connect(m_tunnel, &McpTunnelTsnet::certDomainChanged, this, &McpRemoteAccess::connectorUrlChanged);
        connect(m_tunnel, &McpTunnelTsnet::authUrlChanged, this, &McpRemoteAccess::loginUrlChanged);
    }
    const QString stateDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                             + QStringLiteral("/tsnet");
    // Node name → Funnel subdomain. Include the machine hostname so multiple
    // Decenza instances on one tailnet get distinct, recognisable node names
    // (and distinct Funnel URLs). Tailscale node names allow only [a-z0-9-];
    // sanitise the host and trim stray hyphens.
    QString host = QSysInfo::machineHostName().toLower();
    // Drop any domain suffix (e.g. macOS returns "name.local") — keep just the
    // first label so the node is "decenza-<hostname>", not "…-local".
    const qsizetype dot = host.indexOf('.');
    if (dot > 0)
        host = host.left(dot);
    for (QChar& c : host) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            c = '-';
    }
    while (host.startsWith('-')) host.remove(0, 1);
    while (host.endsWith('-')) host.chop(1);
    const QString nodeName = host.isEmpty() ? QStringLiteral("decenza")
                                            : QStringLiteral("decenza-") + host;
    m_tunnel->start(stateDir, nodeName,
                    static_cast<quint16>(m_settings->remoteMcpPort()));
}

void McpRemoteAccess::stopTunnel()
{
    if (m_tunnel)
        m_tunnel->stop();
}

void McpRemoteAccess::onTunnelStateChanged()
{
    if (!m_tunnel)
        return;
    switch (m_tunnel->state()) {
    case McpTunnelTsnet::NeedsLogin:
        // Surface the login URL (loginUrl property) and keep status "starting".
        setStatus(Starting, QStringLiteral("Waiting for Tailscale login"));
        break;
    case McpTunnelTsnet::Starting:
        setStatus(Starting, QStringLiteral("Connecting to Tailscale"));
        break;
    case McpTunnelTsnet::Running:
        setStatus(Active);
        break;
    case McpTunnelTsnet::Error:
        setStatus(Error, m_tunnel->lastError());
        break;
    case McpTunnelTsnet::Stopped:
        break;
    }
    emit connectorUrlChanged();
}

void McpRemoteAccess::stopListener()
{
    m_reaper->stop();
    closeAllSockets();
    if (m_listener) {
        m_listener->close();
        m_listener->deleteLater();
        m_listener = nullptr;
    }
    m_failedAttempts.clear();
}

void McpRemoteAccess::closeAllSockets()
{
    const auto sockets = m_sockets;
    for (QTcpSocket* socket : sockets) {
        if (socket)
            socket->close();  // onSocketDisconnected() removes and deletes it
    }
    m_sockets.clear();
    m_pending.clear();
}

void McpRemoteAccess::setStatus(Status status, const QString& detail)
{
    if (m_status == status && m_statusDetail == detail)
        return;
    m_status = status;
    m_statusDetail = detail;
    if (status == Error && !detail.isEmpty())
        qWarning() << "McpRemoteAccess:" << detail;
    emit statusChanged();
}

void McpRemoteAccess::onNewConnection()
{
    if (!m_listener)
        return;
    while (QTcpSocket* socket = m_listener->nextPendingConnection()) {
        if (m_sockets.size() >= MaxConnections) {
            // Fail closed under connection pressure — nothing leaked.
            socket->close();
            socket->deleteLater();
            continue;
        }
        m_sockets.insert(socket);
        PendingRequest& pending = m_pending[socket];
        pending.lastActivity = QDateTime::currentDateTimeUtc();
        connect(socket, &QTcpSocket::readyRead, this, &McpRemoteAccess::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &McpRemoteAccess::onSocketDisconnected);
        // Data may already have arrived before readyRead was wired.
        if (socket->bytesAvailable() > 0)
            readFromSocket(socket);
    }
}

void McpRemoteAccess::onSocketDisconnected()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket)
        return;
    m_sockets.remove(socket);
    m_pending.remove(socket);
    socket->deleteLater();
}

void McpRemoteAccess::onReadyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket)
        readFromSocket(socket);
}

void McpRemoteAccess::readFromSocket(QTcpSocket* socket)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;

    // Once a socket is upgraded to an MCP SSE stream, McpServer owns it and
    // pushes server-initiated notifications; we must not parse its bytes.
    if (m_mcpServer && m_mcpServer->isSseClient(socket))
        return;

    PendingRequest& pending = m_pending[socket];
    pending.lastActivity = QDateTime::currentDateTimeUtc();
    pending.buffer.append(socket->readAll());

    if (pending.buffer.size() > MaxHeaderSize + MaxBodySize) {
        sendBare404(socket);
        socket->close();
        return;
    }

    processBuffer(socket);
}

void McpRemoteAccess::processBuffer(QTcpSocket* socket)
{
    // Drain every complete request the buffer holds (handles keep-alive
    // pipelining). Stops early if the socket is taken over for SSE or closed.
    for (;;) {
        PendingRequest& pending = m_pending[socket];

        if (pending.headerEnd < 0) {
            pending.headerEnd = static_cast<int>(pending.buffer.indexOf("\r\n\r\n"));
            if (pending.headerEnd < 0) {
                // Headers incomplete. Cap the header section independently of the
                // body so a client that never terminates the headers can't buffer
                // unbounded data on one connection.
                if (pending.buffer.size() > MaxHeaderSize) {
                    sendBare404(socket);
                    socket->close();
                    return;
                }
                return;  // wait for more header bytes
            }

            pending.contentLength = 0;
            const QByteArray headerBlock = pending.buffer.left(pending.headerEnd);
            for (const QByteArray& line : headerBlock.split('\n')) {
                if (line.trimmed().toLower().startsWith("content-length:")) {
                    bool ok = false;
                    pending.contentLength =
                        line.mid(line.indexOf(':') + 1).trimmed().toLongLong(&ok);
                    // A malformed Content-Length (non-numeric) must not silently
                    // parse as 0 — that desyncs keep-alive framing (the real body
                    // bytes would be read as the next request). Reject and close.
                    if (!ok)
                        pending.contentLength = -1;
                    break;
                }
            }
            if (pending.contentLength < 0 || pending.contentLength > MaxBodySize) {
                sendBare404(socket);
                socket->close();
                return;
            }
        }

        const qint64 total = pending.headerEnd + 4 + pending.contentLength;
        if (pending.buffer.size() < total)
            return;  // body incomplete

        const QByteArray headerBlock = pending.buffer.left(pending.headerEnd);
        const QByteArray body = pending.buffer.mid(pending.headerEnd + 4,
                                                   static_cast<int>(pending.contentLength));

        // Parse the request line (first line of the header block).
        const int lineEnd = headerBlock.indexOf("\r\n");
        const QByteArray requestLine = lineEnd >= 0 ? headerBlock.left(lineEnd) : headerBlock;
        const QList<QByteArray> parts = requestLine.split(' ');
        const QString method = parts.size() > 0 ? QString::fromLatin1(parts[0]) : QString();
        const QString path = parts.size() > 1 ? QString::fromLatin1(parts[1]) : QString();

        // Consume this request from the buffer BEFORE dispatch: forwarding a GET
        // hands the socket to McpServer (SSE), after which m_pending[socket] may
        // be stale to touch.
        QByteArray remainder = pending.buffer.mid(static_cast<int>(total));
        pending.buffer = remainder;
        pending.headerEnd = -1;
        pending.contentLength = -1;

        routeRequest(socket, method, path, headerBlock, body);

        // If the socket became an SSE stream or was closed, stop draining.
        if (socket->state() != QAbstractSocket::ConnectedState)
            return;
        if (m_mcpServer && m_mcpServer->isSseClient(socket))
            return;
        if (remainder.isEmpty())
            return;
    }
}

void McpRemoteAccess::routeRequest(QTcpSocket* socket, const QString& method,
                                   const QString& path, const QByteArray& headerBlock,
                                   const QByteArray& body)
{
    const QString source = socket->peerAddress().toString();

    // Strip any query string, then require an exact `/mcp/<token>` path with no
    // trailing segments.
    QString cleanPath = path;
    const int q = cleanPath.indexOf('?');
    if (q >= 0)
        cleanPath = cleanPath.left(q);

    bool authorized = false;
    if (cleanPath.startsWith(QStringLiteral("/mcp/"))) {
        const QString candidate = cleanPath.mid(5);
        if (!candidate.isEmpty() && !candidate.contains('/'))
            authorized = tokenMatches(candidate.toUtf8());
    }

    if (!authorized) {
        // Never echo the attempted path; just note the source and count it.
        if (!failedTokenOverLimit(source)) {
            qWarning() << "McpRemoteAccess: rejected unauthorized request from" << source;
            sendBare404(socket);
        } else {
            // Over the failed-attempt budget for this source this minute. Drop the
            // keep-alive connection so a scanner must reconnect (bounded by
            // MaxConnections) instead of pipelining guesses on one socket, and log
            // the transition exactly once per window rather than going silent.
            FailWindow& window = m_failedAttempts[source];
            if (!window.suppressionLogged) {
                window.suppressionLogged = true;
                qWarning() << "McpRemoteAccess: further unauthorized requests from" << source
                           << "will be dropped for the rest of this minute";
            }
            socket->close();
        }
        return;
    }

    // Authorized. Only the three MCP methods are served; anything else is a bare
    // 404 (the token was valid, so no rate-limit penalty).
    if (method != QStringLiteral("POST") && method != QStringLiteral("GET")
        && method != QStringLiteral("DELETE")) {
        sendBare404(socket);
        return;
    }

    if (!m_mcpServer || !m_settings || !m_settings->mcpEnabled()) {
        sendBare404(socket);
        return;
    }

    // Forward in-process. Path is rewritten to the canonical `/mcp` the LAN path
    // uses (McpServer ignores the path); the session is flagged remote.
    m_mcpServer->handleHttpRequest(socket, method, QStringLiteral("/mcp"),
                                   headerBlock, body, /*remote=*/true);
}

bool McpRemoteAccess::tokenMatches(const QByteArray& candidate) const
{
    const QByteArray token = m_settings ? m_settings->remoteMcpToken().toUtf8() : QByteArray();
    if (token.isEmpty())
        return false;
    // Token length is fixed (22 base64url chars); comparing lengths up front
    // leaks nothing useful. The byte loop is constant-time over the token.
    if (candidate.size() != token.size())
        return false;
    quint8 diff = 0;
    for (int i = 0; i < token.size(); ++i)
        diff |= static_cast<quint8>(candidate[i] ^ token[i]);
    return diff == 0;
}

bool McpRemoteAccess::failedTokenOverLimit(const QString& source)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    FailWindow& window = m_failedAttempts[source];
    if (!window.windowStart.isValid() || window.windowStart.secsTo(now) >= 60) {
        window.windowStart = now;
        window.count = 0;
        window.suppressionLogged = false;
    }
    window.count++;
    return window.count > MaxFailedPerMinute;
}

void McpRemoteAccess::sendBare404(QTcpSocket* socket)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;
    // Bare 404 with no body — indistinguishable from "no server here".
    static const QByteArray kResponse =
        "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    socket->write(kResponse);
    socket->flush();
}

void McpRemoteAccess::rotateToken()
{
    if (!m_settings)
        return;
    m_settings->rotateRemoteMcpToken();
    // The old capability URL must die immediately: drop every live remote
    // connection so any in-flight session on the previous token is severed.
    closeAllSockets();
    emit connectorUrlChanged();
}

void McpRemoteAccess::onReaperTick()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();

    // Close idle keep-alive sockets (skip SSE streams — McpServer keeps those
    // alive with its own keepalives).
    const auto sockets = m_sockets;
    for (QTcpSocket* socket : sockets) {
        if (!socket)
            continue;
        if (m_mcpServer && m_mcpServer->isSseClient(socket))
            continue;
        const QDateTime last = m_pending.value(socket).lastActivity;
        if (last.isValid() && last.secsTo(now) >= IdleTimeoutSeconds)
            socket->close();
    }

    // Prune expired failed-attempt windows so the per-source map can't grow
    // unbounded from many distinct (or spoofed) source addresses over uptime.
    for (auto it = m_failedAttempts.begin(); it != m_failedAttempts.end();) {
        if (!it.value().windowStart.isValid() || it.value().windowStart.secsTo(now) >= 60)
            it = m_failedAttempts.erase(it);
        else
            ++it;
    }

    // Recover from a dropped listener (e.g. interface flap) while still enabled,
    // rebinding with the correct interface for the active mode.
    if (m_settings && m_settings->mcpEnabled() && m_settings->remoteMcpEnabled()
        && m_listener && !m_listener->isListening()) {
        const QString mode = m_settings->remoteMcpMode();
        const bool loopback = (mode == QString::fromLatin1(SettingsMcp::ModeTailscale));
        if (loopback || mode == QString::fromLatin1(SettingsMcp::ModeCustom)) {
            setStatus(Reconnecting);
            startListener(loopback);
        }
    }
}
