#include "remoteaccess.h"
#include "../core/networklogging.h"

#include "../mcp/mcpserver.h"
#include "../mcp/mcptunnel_tsnet.h"
#include "../core/settings_mcp.h"
#include "../core/settings_network.h"
#include "../core/capabilitytoken.h"
#include "remotewebroute.h"
#include "webrequestguard.h"
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

// The listener and the Tailscale node serve both remote routes (MCP and the web
// interface), so their story is [Network], not [MCP]. Stderr forms: this class
// has no logMessage signal.
#define RA_LOG(msg)  NETWORK_LOG_STDERR("RemoteAccess", msg)
#define RA_INFO(msg) NETWORK_INFO_STDERR("RemoteAccess", msg)
#define RA_WARN(msg) NETWORK_WARN_STDERR("RemoteAccess", msg)

RemoteAccess::RemoteAccess(QObject* parent)
    : QObject(parent)
    , m_reaper(new QTimer(this))
{
    // Periodic reaper: closes idle keep-alive sockets and re-listens if the
    // listener dropped (network flap). A genuinely periodic housekeeping task,
    // not an event guard.
    m_reaper->setInterval(ReaperIntervalMs);
    connect(m_reaper, &QTimer::timeout, this, &RemoteAccess::onReaperTick);
}

RemoteAccess::~RemoteAccess()
{
    stopTunnel();   // brings the embedded node down (joins its worker) if running
    stopListener();
}

void RemoteAccess::setSettings(SettingsMcp* settings)
{
    m_settings = settings;
    if (!m_settings)
        return;

    // Any change that affects reachability re-evaluates the listener.
    connect(m_settings, &SettingsMcp::remoteMcpEnabledChanged, this, &RemoteAccess::refresh);
    connect(m_settings, &SettingsMcp::remoteMcpModeChanged, this, &RemoteAccess::refresh);
    connect(m_settings, &SettingsMcp::remoteMcpPortChanged, this, &RemoteAccess::refresh);

    // Changes that only affect the composed URL shown in the UI.
    connect(m_settings, &SettingsMcp::remoteMcpCustomBaseUrlChanged, this,
            &RemoteAccess::connectorUrlChanged);
    connect(m_settings, &SettingsMcp::remoteMcpModeChanged, this,
            &RemoteAccess::connectorUrlChanged);
    connect(m_settings, &SettingsMcp::remoteMcpTokenChanged, this,
            &RemoteAccess::connectorUrlChanged);
}

void RemoteAccess::setNetworkSettings(SettingsNetwork* settings)
{
    m_networkSettings = settings;
    if (!m_networkSettings)
        return;
    // The web route needs the server it hands connections to.
    connect(m_networkSettings, &SettingsNetwork::shotServerEnabledChanged, this, &RemoteAccess::refresh);
    connect(m_networkSettings, &SettingsNetwork::remoteWebEnabledChanged, this, &RemoteAccess::refresh);
    connect(m_networkSettings, &SettingsNetwork::remoteWebTokenChanged, this, &RemoteAccess::webUrlChanged);
}

// Remote Access carries MCP on its own: "Enable MCP Server" (mcpEnabled) opens
// only the local endpoint, which is open to the LAN. Enable Network is the master
// switch for everything the device serves, although this route itself would not
// need ShotServer running.
bool RemoteAccess::mcpRouteWanted() const
{
    return m_settings && m_settings->remoteMcpEnabled()
           && m_networkSettings && m_networkSettings->shotServerEnabled();
}

bool RemoteAccess::webRouteWanted() const
{
    return m_networkSettings && m_networkSettings->shotServerEnabled()
           && m_networkSettings->remoteWebEnabled();
}

bool RemoteAccess::tunnelWanted() const
{
    return webRouteWanted()
           || (mcpRouteWanted() && m_settings->remoteMcpMode() == QString::fromLatin1(SettingsMcp::ModeTailscale));
}

bool RemoteAccess::lanBindWanted() const
{
    return mcpRouteWanted() && m_settings->remoteMcpMode() == QString::fromLatin1(SettingsMcp::ModeCustom);
}

// The embedded node proxies from loopback, and it runs whenever either route
// needs it, so a Custom-URL connector's LAN listener carries the Funnel too when
// Remote Web Access is on.
bool RemoteAccess::viaFunnel(const QTcpSocket* socket) const
{
    return socket && socket->peerAddress().isLoopback()
           && (m_tunnelProxiedListener || tunnelWanted());
}

bool RemoteAccess::webPeer(const QTcpSocket* socket) const
{
    return webRouteWanted() && socket && socket->peerAddress().isLoopback();
}

QString RemoteAccess::statusString() const
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

int RemoteAccess::listenPort() const
{
    return (m_listener && m_listener->isListening())
        ? static_cast<int>(m_listener->serverPort()) : 0;
}

QString RemoteAccess::connectorUrl() const
{
    // With the route off, refresh() stops serving it, so a composed URL would
    // point at nothing.
    if (!mcpRouteWanted())
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

QString RemoteAccess::webUrl() const
{
    // Only once the probe has confirmed the public URL answers, as for the connector.
    if (!webRouteWanted() || !m_tunnel || m_tunnel->certDomain().isEmpty() || !m_funnelReachable)
        return QString();
    return QStringLiteral("https://") + m_tunnel->certDomain()
           + QLatin1String(RemoteWebRoute::kEntryPrefix) + m_networkSettings->remoteWebToken();
}

QString RemoteAccess::loginUrl() const
{
    return m_tunnel ? m_tunnel->authUrl() : QString();
}

bool RemoteAccess::tunnelAvailable()
{
    return McpTunnelTsnet::isAvailable();
}

void RemoteAccess::refresh()
{
    const bool wantMcp = mcpRouteWanted();
    const bool wantWeb = webRouteWanted();
    if (m_webWasWanted && !wantWeb)
        endWebSessions();
    m_webWasWanted = wantWeb;

    const auto urlsChanged = [this] {
        emit connectorUrlChanged();
        emit webUrlChanged();
    };

    if (!wantMcp && !wantWeb) {
        stopTunnel();
        stopListener();
        setStatus(Off);
        urlsChanged();
        return;
    }

    const QString mode = m_settings ? m_settings->remoteMcpMode() : QString();
    const bool mcpKnownMode = mode == QString::fromLatin1(SettingsMcp::ModeCustom)
                              || mode == QString::fromLatin1(SettingsMcp::ModeTailscale);
    if (wantMcp && !mcpKnownMode && !wantWeb) {
        // ngrok (Mode B) not implemented yet.
        stopTunnel();
        stopListener();
        setStatus(Error, QStringLiteral("Mode '%1' is not available in this build yet").arg(mode));
        urlsChanged();
        return;
    }

    // Mode C (BYO URL) needs a LAN-routable listener for the off-box proxy; the
    // embedded node proxies from 127.0.0.1, which a LAN bind also covers.
    const bool lanBind = lanBindWanted();
    if (tunnelWanted() && !McpTunnelTsnet::isAvailable() && !lanBind) {
        stopListener();
        setStatus(Error, QStringLiteral("This build was not compiled with Tailscale support"));
        urlsChanged();
        return;
    }
    startListener(/*bindLoopbackOnly=*/!lanBind);
    // Asked of the listener, not the status: an Error the tunnel left behind
    // (Funnel not granted, say) must not keep the node from being started.
    if (!m_listener || !m_listener->isListening()) {
        urlsChanged();
        return;  // listener failed to bind
    }
    if (tunnelWanted() && McpTunnelTsnet::isAvailable())
        startTunnel();
    else
        stopTunnel();
    urlsChanged();
}

void RemoteAccess::startListener(bool bindLoopbackOnly)
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
        connect(m_listener, &QTcpServer::newConnection, this, &RemoteAccess::onNewConnection);
        connect(m_listener, &QTcpServer::acceptError, this, &RemoteAccess::onAcceptError);
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

void RemoteAccess::startTunnel()
{
    if (!McpTunnelTsnet::isAvailable() || !m_settings)
        return;
    if (!m_tunnel) {
        m_tunnel = new McpTunnelTsnet(this);
        connect(m_tunnel, &McpTunnelTsnet::stateChanged, this, &RemoteAccess::onTunnelStateChanged);
        connect(m_tunnel, &McpTunnelTsnet::certDomainChanged, this, &RemoteAccess::connectorUrlChanged);
        connect(m_tunnel, &McpTunnelTsnet::certDomainChanged, this, &RemoteAccess::webUrlChanged);
        connect(m_tunnel, &McpTunnelTsnet::authUrlChanged, this, &RemoteAccess::loginUrlChanged);
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

void RemoteAccess::stopTunnel()
{
    stopReachabilityProbe();
    m_funnelReachable = false;
    if (m_tunnel)
        m_tunnel->stop();
}

void RemoteAccess::onTunnelStateChanged()
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
    emit webUrlChanged();
    updateTailscaleSetupNeeded();
}

void RemoteAccess::startReachabilityProbe()
{
    if (!m_reachProbe) {
        m_reachProbe = new QNetworkAccessManager(this);
        m_reachTimer = new QTimer(this);
        m_reachTimer->setInterval(6000);
        connect(m_reachTimer, &QTimer::timeout, this, &RemoteAccess::doReachabilityProbe);
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

void RemoteAccess::stopReachabilityProbe()
{
    ++m_probeGeneration;   // drop any in-flight reply
    if (m_reachTimer)
        m_reachTimer->stop();
    m_probeInFlight = false;
}

void RemoteAccess::doReachabilityProbe()
{
    if (m_probeInFlight || !m_reachProbe || !m_tunnel || !m_settings)
        return;
    const QString domain = m_tunnel->certDomain();
    if (domain.isEmpty())
        return;

    // Fetch a tokenized URL over the public internet. It leaves this machine,
    // hits the Funnel edge, and routes back to the loopback listener — so any
    // HTTP response proves the whole public path works. The tokenized path
    // avoids the failed-token limiter. A GET on the connector (no SSE) creates
    // no MCP session; with only the web route on, HEAD /web/<token> is answered
    // with a bare 404 and admits nobody.
    const bool probeMcp = mcpRouteWanted()
        && m_settings->remoteMcpMode() == QString::fromLatin1(SettingsMcp::ModeTailscale);
    if (!probeMcp && !webRouteWanted())
        return;
    const QString path = probeMcp
        ? QString(QStringLiteral("/mcp/") + m_settings->remoteMcpToken())
        : QString(QLatin1String(RemoteWebRoute::kEntryPrefix) + m_networkSettings->remoteWebToken());
    const QString url = QStringLiteral("https://") + domain + path;
    QNetworkRequest req{QUrl(url)};
    req.setTransferTimeout(10000);
    m_probeInFlight = true;
    const quint64 gen = m_probeGeneration;
    QNetworkReply* reply = probeMcp ? m_reachProbe->get(req) : m_reachProbe->head(req);
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
                RA_INFO(QStringLiteral("Funnel reachable at %1 — remote access Active").arg(domain));
                setStatus(Active);
                emit connectorUrlChanged();
                emit webUrlChanged();
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
            RA_LOG(probeLine);
            return;
        case ProbeFailure::WaitingForTailscale:
            RA_LOG(probeLine);
            if (m_status != Publishing) {
                RA_INFO(hostNotFound
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
            RA_WARN(probeLine);
        setStatus(Error, errorDetail);
    });
}

RemoteAccess::ProbeFailure RemoteAccess::classifyProbeFailure(
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

void RemoteAccess::stopListener()
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

void RemoteAccess::closeAllSockets()
{
    const auto sockets = m_sockets;
    for (QTcpSocket* socket : sockets) {
        if (socket)
            socket->close();  // onSocketDisconnected() removes and deletes it
    }
    m_sockets.clear();
    m_pending.clear();
}

void RemoteAccess::setStatus(Status status, const QString& detail)
{
    if (m_status == status && m_statusDetail == detail)
        return;
    m_status = status;
    m_statusDetail = detail;
    if (status == Error && !detail.isEmpty())
        RA_WARN(detail);
    emit statusChanged();
    updateTailscaleSetupNeeded();
}

bool RemoteAccess::setupNeeded(McpTunnelTsnet::FunnelGrant grant,
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

void RemoteAccess::updateTailscaleSetupNeeded()
{
    const bool needed = m_tunnel && setupNeeded(m_tunnel->funnelGrant(), m_tunnel->state(), m_status);
    if (needed == m_tailscaleSetupNeeded)
        return;
    m_tailscaleSetupNeeded = needed;
    emit tailscaleSetupNeededChanged();
}

void RemoteAccess::onNewConnection()
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
        connect(socket, &QTcpSocket::readyRead, this, &RemoteAccess::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &RemoteAccess::onSocketDisconnected);
        // Data may already have arrived before readyRead was wired.
        if (socket->bytesAvailable() > 0)
            readFromSocket(socket);
    }
}

void RemoteAccess::onAcceptError(QAbstractSocket::SocketError error)
{
    // QTcpServer pauses itself on any accept() failure but EAGAIN and stays
    // isListening() (qtcpserver.cpp:185-193), so every later client would connect
    // and hang. Closing it hands recovery to the reaper's rebind.
    if (!m_listener)
        return;
    RA_WARN(QStringLiteral("listener stopped accepting connections (error %1: %2) — "
                           "rebinding").arg(int(error)).arg(m_listener->errorString()));
    m_listener->close();
    setStatus(Reconnecting);
}

void RemoteAccess::onSocketDisconnected()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket)
        return;
    m_sockets.remove(socket);
    m_pending.remove(socket);
    socket->deleteLater();
}

void RemoteAccess::onReadyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket)
        readFromSocket(socket);
}

void RemoteAccess::readFromSocket(QTcpSocket* socket)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;

    // Once a socket is upgraded to an MCP SSE stream, McpServer owns it and
    // pushes server-initiated notifications; we must not parse its bytes.
    if (m_mcpServer && m_mcpServer->isSseClient(socket))
        return;

    PendingRequest& pending = m_pending[socket];
    pending.lastActivity = QDateTime::currentDateTimeUtc();
    if (pending.buffer.isEmpty() && tryHandOffWebConnection(socket))
        return;
    pending.buffer.append(socket->readAll());

    if (pending.buffer.size() > MaxHeaderSize + MaxBodySize) {
        refuseRequest(socket, bufferedRequestCarriesToken(pending.buffer));
        socket->close();
        return;
    }

    processBuffer(socket);
}

void RemoteAccess::processBuffer(QTcpSocket* socket)
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

void RemoteAccess::routeRequest(QTcpSocket* socket, const QString& method,
                                const QString& path, const QByteArray& headerBlock,
                                const QByteArray& body)
{
    const QString source = describeSource(socket);

    // Each token opens only its own route, and only while that route is on.
    const bool webEntry = path.startsWith(QLatin1String(RemoteWebRoute::kEntryPrefix));
    const bool authorized = webEntry ? webPeer(socket) && pathCarriesWebToken(path)
                                     : mcpRouteWanted() && pathCarriesMcpToken(path);

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
            RA_WARN(QStringLiteral("rejected unauthorized request from %1").arg(source));
            refuseRequest(socket, /*callerHoldsToken=*/false);
        } else {
            // Over the failed-attempt budget for this source this minute. Drop the
            // keep-alive connection so a scanner must reconnect (bounded by
            // MaxConnections) instead of pipelining guesses on one socket, and log
            // the transition exactly once per window rather than going silent.
            if (m_failedAttempts.takeSuppressionLogSlot(source)) {
                RA_WARN(QStringLiteral("further unauthorized requests from %1 will be "
                                       "dropped for the rest of this minute").arg(source));
            } else if (isUnauthorizedLogMilestone(seen)) {
                RA_WARN(QStringLiteral("%1 unauthorized requests from %2 this minute — "
                                       "still being dropped").arg(seen).arg(source));
            }
            socket->close();
        }
        return;
    }

    if (webEntry) {
        routeWebEntry(socket, method, headerBlock);
        return;
    }

    // Authorized. Only the three MCP methods are served; anything else is a bare
    // 404 (the token was valid, so no rate-limit penalty).
    if (method != QStringLiteral("POST") && method != QStringLiteral("GET")
        && method != QStringLiteral("DELETE")) {
        sendBare404(socket);
        return;
    }

    if (!m_mcpServer) {
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

void RemoteAccess::refuseRequest(QTcpSocket* socket, bool callerHoldsToken)
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
    // Mode C keeps the 404 and the keep-alive either way for its LAN peers:
    // there the reply goes to the user's own reverse proxy, where a silent drop
    // reads as a broken backend.
    //
    // One function because four sites refuse a request — unterminated headers,
    // oversized request, malformed Content-Length, bad token — and a policy
    // about whether we answer strangers must not be free to drift between them.
    if (viaFunnel(socket) && !callerHoldsToken)
        socket->close();
    else
        sendBare404(socket);
}

bool RemoteAccess::pathCarriesToken(const QString& path) const
{
    return (mcpRouteWanted() && pathCarriesMcpToken(path))
           || (webRouteWanted() && pathCarriesWebToken(path));
}

namespace {
// The single segment after `prefix`, ignoring a query string; empty when the path
// is not exactly `<prefix><segment>`.
QString tokenSegment(const QString& path, QLatin1String prefix)
{
    QString cleanPath = path;
    const qsizetype q = cleanPath.indexOf('?');
    if (q >= 0)
        cleanPath = cleanPath.left(q);
    if (!cleanPath.startsWith(prefix))
        return QString();
    const QString candidate = cleanPath.mid(prefix.size());
    return candidate.contains('/') ? QString() : candidate;
}
}  // namespace

bool RemoteAccess::pathCarriesMcpToken(const QString& path) const
{
    const QString candidate = tokenSegment(path, QLatin1String("/mcp/"));
    return !candidate.isEmpty() && m_settings
           && CapabilityToken::matches(candidate.toUtf8(), m_settings->remoteMcpToken().toUtf8());
}

bool RemoteAccess::pathCarriesWebToken(const QString& path) const
{
    const QString candidate = tokenSegment(path, QLatin1String(RemoteWebRoute::kEntryPrefix));
    return !candidate.isEmpty() && m_networkSettings
           && CapabilityToken::matches(candidate.toUtf8(), m_networkSettings->remoteWebToken().toUtf8());
}

bool RemoteAccess::tryHandOffWebConnection(QTcpSocket* socket)
{
    if (!m_webHost || !webPeer(socket) || !m_webHost->remoteWebServing())
        return false;
    // Peek, never read: the host's parser, and a WebSocket upgrade above all,
    // must find the request still in the socket.
    const QByteArray peeked = socket->peek(MaxHeaderSize + 1);
    const qsizetype lineEnd = peeked.indexOf("\r\n");
    if (lineEnd >= 0) {
        const QList<QByteArray> parts = peeked.left(lineEnd).split(' ');
        const QString path = parts.size() > 1 ? QString::fromLatin1(parts[1]) : QString();
        if (path.startsWith(QLatin1String("/mcp/"))
            || path.startsWith(QLatin1String(RemoteWebRoute::kEntryPrefix)))
            return false;  // a tokenized route: the normal path checks the token
    }
    const qsizetype headerEnd = peeked.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        // Not decidable yet: leave the bytes in the socket until more arrive. Past
        // the header cap the normal path reads and refuses it.
        return peeked.size() <= MaxHeaderSize;
    }
    const QString admission = RemoteWebRoute::admissionId(peeked.left(headerEnd));
    if (admission.isEmpty() || !m_webHost->remoteWebAdmissionValid(admission))
        return false;  // refused as unauthorized by the normal path

    disconnect(socket, nullptr, this, nullptr);
    m_sockets.remove(socket);
    m_pending.remove(socket);
    // The listener is the socket's parent; stopping it must not delete a
    // connection the host now owns.
    socket->setParent(nullptr);
    m_webHost->adoptRemoteWebConnection(socket);
    return true;
}

void RemoteAccess::routeWebEntry(QTcpSocket* socket, const QString& method,
                                 const QByteArray& headerBlock)
{
    if (method != QStringLiteral("GET") || !m_webHost || !m_webHost->remoteWebServing()) {
        // HEAD is the reachability probe: answered, but nobody is admitted.
        sendBare404(socket);
        return;
    }
    const QString admission = m_webHost->createRemoteWebAdmission(
        WebRequestGuard::headerValue(headerBlock, "User-Agent"));
    if (admission.isEmpty()) {
        sendBare404(socket);
        return;
    }
    // Off the token URL at once, so it stays out of history and Referer.
    const QByteArray response =
        "HTTP/1.1 303 See Other\r\n"
        "Location: /\r\n"
        "Set-Cookie: " + QByteArray(RemoteWebRoute::kAdmissionCookie) + '=' + admission.toLatin1()
        + "; Max-Age=" + QByteArray::number(RemoteWebRoute::kAdmissionLifetimeDays * 24 * 60 * 60)
        + "; Path=/; Secure; HttpOnly; SameSite=Lax\r\n"
        "Cache-Control: no-store\r\n"
        "Referrer-Policy: no-referrer\r\n"
        "Content-Length: 0\r\n\r\n";
    socket->write(response);
    socket->flush();
    RA_INFO(QStringLiteral("admitted a browser to Remote Web Access from %1").arg(describeSource(socket)));
}

void RemoteAccess::endWebSessions()
{
    if (m_webHost)
        m_webHost->endRemoteWebSessions();
}

bool RemoteAccess::bufferedRequestCarriesToken(const QByteArray& buffer) const
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

QString RemoteAccess::describeSource(const QTcpSocket* socket) const
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
    if (viaFunnel(socket))
        return QStringLiteral("Funnel (public internet)");

    return peer.toString();
}

bool RemoteAccess::failedTokenOverLimit(const QString& source)
{
    return m_failedAttempts.recordAndCheckOverLimit(source, MaxFailedPerMinute);
}

void RemoteAccess::sendBare404(QTcpSocket* socket)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;
    // Bare 404 with no body — indistinguishable from "no server here".
    static const QByteArray kResponse =
        "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    socket->write(kResponse);
    socket->flush();
}

void RemoteAccess::rotateToken()
{
    if (!m_settings)
        return;
    m_settings->rotateRemoteMcpToken();
    // The old capability URL must die immediately: drop every live remote
    // connection so any in-flight session on the previous token is severed.
    closeAllSockets();
    emit connectorUrlChanged();
}

void RemoteAccess::rotateWebToken()
{
    if (!m_networkSettings)
        return;
    m_networkSettings->rotateRemoteWebToken();
    endWebSessions();
    emit webUrlChanged();
}

bool RemoteAccess::forgetTailscale()
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
        RA_WARN(QStringLiteral("failed to wipe tsnet state dir %1").arg(dir));

    // Bring a fresh node up (new identity → new login URL) if either route still
    // needs it (tunnelWanted()); otherwise refresh() just settles to Off.
    refresh();
    return wiped;
}

QString RemoteAccess::tsnetStateDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/tsnet");
}

void RemoteAccess::onReaperTick()
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
    if ((mcpRouteWanted() || webRouteWanted()) && m_listener && !m_listener->isListening()) {
        setStatus(Reconnecting);
        startListener(/*bindLoopbackOnly=*/!lanBindWanted());
        if (m_listener->isListening())
            RA_INFO(QStringLiteral("listener rebound on port %1").arg(m_listener->serverPort()));
    }
}
