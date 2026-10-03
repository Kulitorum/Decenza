#include "mcpremoteaccess.h"
#include "mcplogging.h"

#include "mcpserver.h"
#include "mcptunnel_tsnet.h"
#include "../core/settings_mcp.h"
#include "../core/deviceinfo.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QHostAddress>
#include <QUrl>
#include <QStandardPaths>
#include <QDir>
#include <QSysInfo>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>

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
    case Publishing:   return QStringLiteral("publishing");
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
    // up — but only surface the URL once a real public-reachability probe has
    // confirmed it actually works (login done, HTTPS certs on, Funnel granted).
    if (mode == QString::fromLatin1(SettingsMcp::ModeTailscale)) {
        if (!m_tunnel || m_tunnel->certDomain().isEmpty() || !m_funnelReachable)
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
    // Recorded before the already-serving early return below, so a rebind that
    // changes mode cannot leave the previous mode's answer behind.
    m_tunnelProxiedListener = bindLoopbackOnly;

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
        connect(m_listener, &QTcpServer::acceptError, this, &McpRemoteAccess::onAcceptError);
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
        // HTTPS-off arrives while the tunnel stays Starting, so no stateChanged follows.
        connect(m_tunnel, &McpTunnelTsnet::funnelGrantChanged, this, [this] {
            // The probe windows are per grant: one granted after minutes in the admin console
            // starts its wait from now, not from when probing began.
            m_probeFailCount = 0;
            if (m_tunnel->state() == McpTunnelTsnet::Starting)
                onTunnelStateChanged();
            updateTailscaleSetupNeeded();
        });
    }
    const QString stateDir = tsnetStateDir();
    // Node name → Funnel subdomain. Include the device name so multiple Decenza
    // instances on one tailnet get distinct, recognisable node names (and
    // distinct Funnel URLs). Tailscale node names allow only [a-z0-9-];
    // sanitise the host and trim stray hyphens.
    QString host = QSysInfo::machineHostName().toLower();
    // Drop any domain suffix (e.g. macOS returns "name.local") — keep just the
    // first label so the node is "decenza-<hostname>", not "…-local".
    const qsizetype dot = host.indexOf('.');
    if (dot > 0)
        host = host.left(dot);
#ifdef Q_OS_ANDROID
    // Android doesn't expose a device hostname to apps — machineHostName()
    // returns "localhost", which would make every Android node "decenza-localhost".
    // Use the marketing model name (Build.MODEL, e.g. "SM-X210") instead so the
    // node is recognisable and distinct.
    if (host.isEmpty() || host == QLatin1String("localhost")) {
        const QString model = DeviceInfo::androidBuild().model;
        if (!model.isEmpty())
            host = model.toLower();
    }
#endif
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
    stopReachabilityProbe();
    m_funnelReachable = false;
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
        m_funnelReachable = false;
        stopReachabilityProbe();
        setStatus(Starting, QStringLiteral("Waiting for Tailscale login"));
        break;
    case McpTunnelTsnet::Starting:
        m_funnelReachable = false;
        stopReachabilityProbe();
        if (m_tunnel->funnelGrant() == McpTunnelTsnet::HttpsOff) {
            setStatus(Error, QStringLiteral(
                "HTTPS certificates are off for this tailnet. Turn them on in the Tailscale admin "
                "console (DNS page; see “Set up Tailscale Funnel”). This clears automatically "
                "once they're on."));
        } else {
            setStatus(Starting, QStringLiteral("Connecting to Tailscale"));
        }
        break;
    case McpTunnelTsnet::Running:
        // The node is up and Funnel is configured locally, but that is NOT proof
        // the public URL works. Stay "starting" and probe the real Funnel URL;
        // onReachability… flips to Active only once it responds.
        if (m_funnelReachable) {
            setStatus(Active);
        } else {
            setStatus(Starting, QStringLiteral("Verifying public reachability…"));
            startReachabilityProbe();
        }
        break;
    case McpTunnelTsnet::Error:
        m_funnelReachable = false;
        stopReachabilityProbe();
        setStatus(Error, m_tunnel->lastError());
        break;
    case McpTunnelTsnet::Stopped:
        m_funnelReachable = false;
        stopReachabilityProbe();
        break;
    }
    emit connectorUrlChanged();
    updateTailscaleSetupNeeded();
}

void McpRemoteAccess::startReachabilityProbe()
{
    if (!m_reachProbe) {
        m_reachProbe = new QNetworkAccessManager(this);
        m_reachTimer = new QTimer(this);
        m_reachTimer->setInterval(6000);
        connect(m_reachTimer, &QTimer::timeout, this, &McpRemoteAccess::doReachabilityProbe);
    }
    // New probing session: any in-flight reply from a previous session becomes
    // stale (its generation no longer matches) and is ignored on completion.
    ++m_probeGeneration;
    m_probeFailCount = 0;
    m_probeInFlight = false;
    if (!m_reachTimer->isActive())
        m_reachTimer->start();
    doReachabilityProbe();  // probe immediately, don't wait a full interval
}

void McpRemoteAccess::stopReachabilityProbe()
{
    ++m_probeGeneration;   // drop any in-flight reply
    if (m_reachTimer)
        m_reachTimer->stop();
    m_probeInFlight = false;
}

void McpRemoteAccess::doReachabilityProbe()
{
    if (m_probeInFlight || !m_reachProbe || !m_tunnel || !m_settings)
        return;
    const QString domain = m_tunnel->certDomain();
    if (domain.isEmpty())
        return;

    // Fetch the real connector URL over the public internet. It leaves this
    // machine, hits the Funnel edge, and routes back to the loopback listener —
    // so any HTTP response (a GET yields 405 from McpServer) proves the whole
    // public path works. Using the tokenized path avoids the failed-token
    // limiter. GET (no SSE) creates no MCP session.
    const QString url = QStringLiteral("https://") + domain
                        + QStringLiteral("/mcp/") + m_settings->remoteMcpToken();
    QNetworkRequest req{QUrl(url)};
    req.setTransferTimeout(10000);
    m_probeInFlight = true;
    const quint64 gen = m_probeGeneration;
    QNetworkReply* reply = m_reachProbe->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, domain]() {
        const bool gotHttpResponse =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid();
        const bool hostNotFound = reply->error() == QNetworkReply::HostNotFoundError;
        const QString errStr = reply->errorString();
        reply->deleteLater();
        // Ignore a reply from a superseded probing session (disable / mode
        // switch / tunnel restart happened while this was in flight) — it must
        // not flip status to Active.
        if (gen != m_probeGeneration)
            return;
        m_probeInFlight = false;

        if (gotHttpResponse) {
            m_probeFailCount = 0;
            if (!m_funnelReachable) {
                m_funnelReachable = true;
                stopReachabilityProbe();
                // Log the recovery edge. Success is what STOPS the probe
                // (stopReachabilityProbe() just above) and it used to do so
                // silently, so a log showing a few "probe failed" warnings and
                // then nothing was ambiguous — recovered, or still failing
                // off-screen? — and that is exactly how it read in practice.
                // (Failure never stops the probe; see the branch below.)
                MCP_INFO_TAGGED("RemoteAccess",
                                QStringLiteral("Funnel reachable at %1 — remote access Active")
                                    .arg(domain));
                setStatus(Active);
                emit connectorUrlChanged();
            }
            return;
        }

        // No HTTP response. Keep probing whatever the cause: each case below clears
        // by itself once the public URL answers.
        //
        // Rate-limited: a failure that never clears repeats at the 6 s probe
        // interval, 600 lines an hour against a 500-line in-memory ring
        // (WebDebugLogger), evicting the startup lines a reader needs. So DEBUG
        // while the outcome is open or expected, and WARN only alongside an Error
        // status: on entering it, then once a minute.
        ++m_probeFailCount;
        constexpr int kProbeWarnEveryNWhileError = 10;  // 10 x 6 s = once a minute
        const QString probeLine =
            QStringLiteral("Funnel reachability probe failed: %1 (attempt %2)")
                .arg(errStr).arg(m_probeFailCount);
        const ProbeFailure failure =
            classifyProbeFailure(m_tunnel->funnelGrant(), hostNotFound, m_probeFailCount);

        QString errorDetail;
        switch (failure) {
        case ProbeFailure::Verifying:
            MCP_LOG_TAGGED("RemoteAccess", probeLine);
            return;
        case ProbeFailure::WaitingForTailscale:
            MCP_LOG_TAGGED("RemoteAccess", probeLine);
            if (m_status != Publishing) {
                MCP_INFO_TAGGED("RemoteAccess",
                    hostNotFound
                        ? QStringLiteral("Funnel is enabled for this device; waiting for Tailscale "
                                         "to publish %1 in public DNS. A resolver that already "
                                         "answered \"not found\" keeps that answer for up to 5 "
                                         "minutes").arg(domain)
                        : QStringLiteral("Funnel is enabled for this device; waiting for %1 to "
                                         "answer (%2)").arg(domain, errStr));
            }
            setStatus(Publishing, QStringLiteral(
                "Waiting for Tailscale to bring up the public address, which can take a few "
                "minutes after starting."));
            return;
        case ProbeFailure::FunnelNotGranted:
            errorDetail = QStringLiteral(
                "Funnel isn't enabled for this device. Allow it in the Tailscale admin console "
                "(see “Set up Tailscale Funnel”). This clears automatically once it's allowed.");
            break;
        case ProbeFailure::DnsNotPublished:
            errorDetail = QStringLiteral(
                "Tailscale still hasn't published %1 after 15 minutes. Check in the Tailscale "
                "admin console that this device is connected and has Funnel. This clears "
                "automatically once the address is reachable.").arg(domain);
            break;
        case ProbeFailure::NotAnswering:
            errorDetail = QStringLiteral(
                "%1 still isn't answering after 2 minutes (%2). This clears automatically once "
                "it responds.").arg(domain, errStr);
            break;
        case ProbeFailure::Unreachable:
            errorDetail = QStringLiteral(
                "Public Funnel URL isn't reachable yet. Make sure Funnel is enabled for this "
                "device in the Tailscale admin console (see “Set up Tailscale Funnel”). "
                "This clears automatically once it's reachable.");
            break;
        }
        if (m_status != Error || m_probeFailCount % kProbeWarnEveryNWhileError == 0)
            MCP_WARN_TAGGED("RemoteAccess", probeLine);
        setStatus(Error, errorDetail);
    });
}

McpRemoteAccess::ProbeFailure McpRemoteAccess::classifyProbeFailure(
        McpTunnelTsnet::FunnelGrant grant, bool hostNotFound, int failCount)
{
    // A granted device is waiting on Tailscale, not on the user, so it gets a window
    // sized to how long Tailscale takes rather than the startup grace.
    if (grant == McpTunnelTsnet::Granted) {
        if (hostNotFound)
            return failCount >= kProbeFailuresBeforeDnsError ? ProbeFailure::DnsNotPublished
                                                             : ProbeFailure::WaitingForTailscale;
        return failCount >= kProbeFailuresBeforeNotAnsweringError ? ProbeFailure::NotAnswering
                                                                  : ProbeFailure::WaitingForTailscale;
    }
    if (failCount < kProbeFailuresBeforeError)
        return ProbeFailure::Verifying;
    return grant == McpTunnelTsnet::NotGranted ? ProbeFailure::FunnelNotGranted
                                               : ProbeFailure::Unreachable;
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
        MCP_WARN_TAGGED("RemoteAccess", detail);
    emit statusChanged();
    updateTailscaleSetupNeeded();
}

bool McpRemoteAccess::setupNeeded(McpTunnelTsnet::FunnelGrant grant,
                                  McpTunnelTsnet::State tunnelState, Status status)
{
    switch (grant) {
    case McpTunnelTsnet::Granted:    return false;
    case McpTunnelTsnet::NotGranted:
    case McpTunnelTsnet::HttpsOff:   return true;
    case McpTunnelTsnet::GrantUnknown: break;
    }
    return tunnelState == McpTunnelTsnet::NeedsLogin || status == Error;
}

void McpRemoteAccess::updateTailscaleSetupNeeded()
{
    const bool needed = m_tunnel && setupNeeded(m_tunnel->funnelGrant(), m_tunnel->state(), m_status);
    if (needed == m_tailscaleSetupNeeded)
        return;
    m_tailscaleSetupNeeded = needed;
    emit tailscaleSetupNeededChanged();
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

void McpRemoteAccess::onAcceptError(QAbstractSocket::SocketError error)
{
    // QTcpServer pauses itself on any accept() failure but EAGAIN and stays
    // isListening() (qtcpserver.cpp:185-193), so every later client would connect
    // and hang. Closing it hands recovery to the reaper's rebind.
    if (!m_listener)
        return;
    MCP_WARN_TAGGED("RemoteAccess",
                    QStringLiteral("listener stopped accepting connections (error %1: %2) — "
                                   "rebinding").arg(int(error)).arg(m_listener->errorString()));
    m_listener->close();
    setStatus(Reconnecting);
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
        refuseRequest(socket, bufferedRequestCarriesToken(pending.buffer));
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
                    refuseRequest(socket, bufferedRequestCarriesToken(pending.buffer));
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
                refuseRequest(socket, bufferedRequestCarriesToken(pending.buffer));
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
        const qsizetype lineEnd = headerBlock.indexOf("\r\n");
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

namespace {
// Counts at which a suppressed run of unauthorized requests still writes one
// line. Decimal rather than every-Nth so the cost is bounded no matter how hard
// the surface is hit: three extra lines a minute buys the difference between
// "somebody probed once" and "somebody is at ten thousand a minute", which is
// the only thing about a rejected request that a reader can act on.
bool isUnauthorizedLogMilestone(int count)
{
    return count == 100 || count == 1000 || count == 10000;
}
}  // namespace

void McpRemoteAccess::routeRequest(QTcpSocket* socket, const QString& method,
                                   const QString& path, const QByteArray& headerBlock,
                                   const QByteArray& body)
{
    const QString source = describeSource(socket);

    const bool authorized = pathCarriesToken(path);

    if (!authorized) {
        const bool overLimit = failedTokenOverLimit(source);
        const int seen = m_failedAttempts.countInWindow(source);

        // Never echo the attempted path; just note the source and count it.
        //
        // The line budget matters as much as the request budget: this surface is
        // reachable by anyone who finds the public URL, the debug log is a
        // fixed-size buffer, and one line per rejected request lets an
        // unauthenticated caller evict every other subsystem's evidence from it.
        // So log each of the first MaxFailedPerMinute, one transition line, and
        // after that only decimal milestones — at most seven lines a minute, and
        // each milestone carries the running count so a submitted log still
        // distinguishes a stray probe from sustained hammering.
        if (!overLimit) {
            MCP_WARN_TAGGED("RemoteAccess",
                            QStringLiteral("rejected unauthorized request from %1").arg(source));
            refuseRequest(socket, /*callerHoldsToken=*/false);
        } else {
            // Over the failed-attempt budget for this source this minute. Drop the
            // keep-alive connection so a scanner must reconnect (bounded by
            // MaxConnections) instead of pipelining guesses on one socket, and log
            // the transition exactly once per window rather than going silent.
            if (m_failedAttempts.takeSuppressionLogSlot(source)) {
                MCP_WARN_TAGGED("RemoteAccess",
                                QStringLiteral("further unauthorized requests from %1 will be "
                                               "dropped for the rest of this minute").arg(source));
            } else if (isUnauthorizedLogMilestone(seen)) {
                MCP_WARN_TAGGED("RemoteAccess",
                                QStringLiteral("%1 unauthorized requests from %2 this minute — "
                                               "still being dropped").arg(seen).arg(source));
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
    // uses (McpServer ignores the path); the session is flagged remote. `source`
    // travels with it so the stateless era's rate limiter buckets and reports
    // this caller by the same name this file logs it under — McpServer cannot
    // work it out, since only the listener knows whether it is tunnel-proxied.
    m_mcpServer->handleHttpRequest(socket, method, QStringLiteral("/mcp"),
                                   headerBlock, body, /*remote=*/true, source);
}

void McpRemoteAccess::refuseRequest(QTcpSocket* socket, bool callerHoldsToken)
{
    // Behind an embedded tunnel, answer a STRANGER nothing at all. A bare 404 is
    // still a reply, and a reply confirms that the URL fronts a live service
    // worth guessing at; a dropped connection tells a scanner nothing. The
    // socket is closed rather than left silently open, which would hold one of
    // MaxConnections until the idle reaper for a caller owed nothing.
    //
    // This does NOT make the endpoint invisible, and must not be sold as if it
    // did: the Funnel edge terminates TLS and serves its own error for a backend
    // that hangs up, so the hostname stays visibly configured. What stops is US
    // confirming anything about what is behind it.
    //
    // `callerHoldsToken` is why the framing refusals pass a flag rather than
    // inheriting the silence: those checks fire BEFORE any token is parsed, so
    // without it a legitimate client that trips the body cap gets a closed
    // socket — indistinguishable from a dropped network, and therefore retried
    // forever on a request that can never succeed. Someone who already knows the
    // token learns nothing from a 404, so there is no reason to withhold it.
    //
    // Mode C keeps the 404 and the keep-alive either way: there the reply goes
    // to the user's own reverse proxy, where a silent drop reads as a broken
    // backend.
    //
    // One function because four sites refuse a request — unterminated headers,
    // oversized request, malformed Content-Length, bad token — and a policy
    // about whether we answer strangers must not be free to drift between them.
    if (m_tunnelProxiedListener && !callerHoldsToken)
        socket->close();
    else
        sendBare404(socket);
}

bool McpRemoteAccess::pathCarriesToken(const QString& path) const
{
    // Strip any query string, then require an exact `/mcp/<token>` path with no
    // trailing segments.
    QString cleanPath = path;
    const qsizetype q = cleanPath.indexOf('?');
    if (q >= 0)
        cleanPath = cleanPath.left(q);

    if (!cleanPath.startsWith(QStringLiteral("/mcp/")))
        return false;
    const QString candidate = cleanPath.mid(5);
    if (candidate.isEmpty() || candidate.contains('/'))
        return false;
    return tokenMatches(candidate.toUtf8());
}

bool McpRemoteAccess::bufferedRequestCarriesToken(const QByteArray& buffer) const
{
    // The request line is the FIRST line, so it is readable long before the
    // header block terminates — which is what lets even the unterminated-header
    // refusal tell a token holder from a stranger. No complete line yet means no
    // claim to the token: the conservative answer, and the one that keeps a
    // scanner from buying a reply by sending a fragment.
    const qsizetype lineEnd = buffer.indexOf("\r\n");
    if (lineEnd < 0)
        return false;

    const QList<QByteArray> parts = buffer.left(lineEnd).split(' ');
    if (parts.size() < 2)
        return false;
    return pathCarriesToken(QString::fromLatin1(parts[1]));
}

QString McpRemoteAccess::describeSource(const QTcpSocket* socket) const
{
    const QHostAddress peer = socket->peerAddress();

    // Mode A puts the listener on loopback and lets the embedded tsnet node
    // proxy the public Funnel into it, so EVERY remote client arrives as
    // 127.0.0.1 and the peer address says nothing about who called. Logged
    // untagged it says something worse than nothing: a reader — or the field
    // AIs that read these logs — takes a loopback address for "this is me on
    // the tablet" and files a real scan as self-inflicted noise. That reading
    // is why an overnight burst on this line was nearly dismissed.
    //
    // The tag is a claim about the LISTENER's exposure, not about the individual
    // peer: an on-device process could also reach a loopback-bound listener, and
    // nothing at the socket layer separates the two. Treating anything that
    // reaches a publicly-proxied listener as public is the safe direction to be
    // wrong in. Mode C keeps the raw address, where it is a genuine peer (the
    // user's own reverse proxy, or the client itself).
    if (m_tunnelProxiedListener && peer.isLoopback())
        return QStringLiteral("Funnel (public internet)");

    return peer.toString();
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
    return m_failedAttempts.recordAndCheckOverLimit(source, MaxFailedPerMinute);
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

bool McpRemoteAccess::forgetTailscale()
{
    // Stop any running node first so its worker isn't touching the state dir we
    // are about to delete (stop() closes the handle and joins the worker; the
    // Stopped update clears authUrl/certDomain, firing the URL-changed signals).
    if (m_tunnel)
        m_tunnel->stop();

    // Wipe by path, not through the tunnel object: the stale identity must be
    // clearable even when no node is live (e.g. the loopback listener failed to
    // bind, so startTunnel() never ran and m_tunnel is null). "Already absent"
    // counts as success — there is nothing left to clear.
    const QString dir = tsnetStateDir();
    const bool wiped = !QDir(dir).exists() || QDir(dir).removeRecursively();
    if (!wiped)
        MCP_WARN_TAGGED("RemoteAccess",
                        QStringLiteral("failed to wipe tsnet state dir %1").arg(dir));

    // Bring a fresh node up (new identity → new login URL) if remote MCP is still
    // enabled in Tailscale mode; otherwise refresh() just settles to Off.
    refresh();
    return wiped;
}

QString McpRemoteAccess::tsnetStateDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/tsnet");
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

    // Expired failed-attempt windows. McpRateWindow prunes on every record, but
    // that only bounds the map while traffic CONTINUES — after a token spray
    // stops, whatever keys were live at the last attempt would stay resident for
    // the process lifetime. The reaper tick is what empties it, which is what the
    // hand-rolled loop here used to do before the mechanism was shared.
    m_failedAttempts.pruneNow();

    // Recover from a dropped listener (e.g. interface flap) while still enabled,
    // rebinding with the correct interface for the active mode.
    if (m_settings && m_settings->mcpEnabled() && m_settings->remoteMcpEnabled()
        && m_listener && !m_listener->isListening()) {
        const QString mode = m_settings->remoteMcpMode();
        const bool loopback = (mode == QString::fromLatin1(SettingsMcp::ModeTailscale));
        if (loopback || mode == QString::fromLatin1(SettingsMcp::ModeCustom)) {
            setStatus(Reconnecting);
            startListener(loopback);
            if (m_listener->isListening())
                MCP_INFO_TAGGED("RemoteAccess", QStringLiteral("listener rebound on port %1")
                                                    .arg(m_listener->serverPort()));
        }
    }
}
