#include "mcpserver.h"
#include "mcpsession.h"
#include "mcptoolregistry.h"
#include "mcpresourceregistry.h"
#include "mcplogging.h"
#include "../core/settings.h"
#include "../core/settings_mcp.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../controllers/maincontroller.h"
#include "../controllers/profilemanager.h"
#include "../history/shothistorystorage.h"
#include "../ble/blemanager.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QUrl>

// Tool registration functions (implemented in mcptools_*.cpp)
void registerMachineTools(McpToolRegistry* registry, DE1Device* device,
                          MachineState* machineState, MainController* mainController,
                          ProfileManager* profileManager);
void registerShotTools(McpToolRegistry* registry, ShotHistoryStorage* shotHistory);
class ProfileManager;
void registerProfileTools(McpToolRegistry* registry, ProfileManager* profileManager, Settings* settings);
void registerPresetsTools(McpToolRegistry* registry, Settings* settings, MainController* mainController,
                          MachineState* machineState);
class RecipeStorage;
void registerRecipeTools(McpToolRegistry* registry, ShotHistoryStorage* shotHistory,
                         RecipeStorage* recipeStorage, MainController* mainController,
                         Settings* settings);
class AccessibilityManager;
class ScreensaverVideoManager;
class TranslationManager;
class BatteryManager;
class CoffeeBagStorage;
class AIManager;
void registerSettingsReadTools(McpToolRegistry* registry, Settings* settings,
                               AccessibilityManager* accessibility,
                               ScreensaverVideoManager* screensaver,
                               TranslationManager* translation,
                               BatteryManager* battery,
                               AIManager* aiManager);
void registerDialingTools(McpToolRegistry* registry, MainController* mainController,
                          ProfileManager* profileManager,
                          ShotHistoryStorage* shotHistory, Settings* settings);
void registerControlTools(McpToolRegistry* registry, DE1Device* device, MachineState* machineState,
                          ProfileManager* profileManager, MainController* mainController,
                          Settings* settings);
void registerWriteTools(McpToolRegistry* registry, ProfileManager* profileManager,
                        ShotHistoryStorage* shotHistory, Settings* settings,
                        VisualizerUploader* visualizerUploader,
                        CoffeeBagStorage* bagStorage,
                        AccessibilityManager* accessibility,
                        ScreensaverVideoManager* screensaver,
                        TranslationManager* translation,
                        BatteryManager* battery,
                        AIManager* aiManager,
                        BeanBaseClient* beanbase);
void registerScaleTools(McpToolRegistry* registry, MachineState* machineState);
void registerDeviceTools(McpToolRegistry* registry, BLEManager* bleManager, DE1Device* device);
class MemoryMonitor;
void registerDebugTools(McpToolRegistry* registry, MemoryMonitor* memoryMonitor);
void registerAgentTools(McpToolRegistry* registry);
void registerAITools(McpToolRegistry* registry, MainController* mainController);
void registerMcpResources(McpResourceRegistry* registry, DE1Device* device,
                          MachineState* machineState, ProfileManager* profileManager,
                          ShotHistoryStorage* shotHistory, MemoryMonitor* memoryMonitor,
                          Settings* settings);

McpServer::McpServer(QObject* parent)
    : QObject(parent)
    , m_toolRegistry(new McpToolRegistry(this))
    , m_resourceRegistry(new McpResourceRegistry(this))
    , m_cleanupTimer(new QTimer(this))
    , m_rateLimitTimer(new QTimer(this))
{
    // Session cleanup every 60 seconds
    m_cleanupTimer->setInterval(60000);
    connect(m_cleanupTimer, &QTimer::timeout, this, &McpServer::cleanupExpiredSessions);
    m_cleanupTimer->start();

    // Rate limit reset every 60 seconds
    m_rateLimitTimer->setInterval(60000);
    connect(m_rateLimitTimer, &QTimer::timeout, this, [this]() {
        for (auto* session : std::as_const(m_sessions))
            session->resetControlCalls();
    });
    m_rateLimitTimer->start();

    // Loopback. Match any port on these hosts since LAN browsers often
    // pick an ephemeral dev-server port.
    m_allowedOrigins.insert(QStringLiteral("http://localhost:*"));
    m_allowedOrigins.insert(QStringLiteral("https://localhost:*"));
    m_allowedOrigins.insert(QStringLiteral("http://127.0.0.1:*"));
    m_allowedOrigins.insert(QStringLiteral("https://127.0.0.1:*"));
    m_allowedOrigins.insert(QStringLiteral("http://[::1]:*"));
    m_allowedOrigins.insert(QStringLiteral("https://[::1]:*"));

    // Host's own LAN IPs — same machine, any port.
    for (const QHostAddress& addr : QNetworkInterface::allAddresses()) {
        if (addr.isLoopback() || addr.isNull()) continue;
        if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
            const QString host = addr.toString();
            m_allowedOrigins.insert(QStringLiteral("http://%1:*").arg(host));
            m_allowedOrigins.insert(QStringLiteral("https://%1:*").arg(host));
        } else if (addr.protocol() == QAbstractSocket::IPv6Protocol) {
            QString host = addr.toString();
            const qsizetype pct = host.indexOf(QLatin1Char('%'));
            if (pct >= 0) host.truncate(pct);  // strip zone id
            m_allowedOrigins.insert(QStringLiteral("http://[%1]:*").arg(host));
            m_allowedOrigins.insert(QStringLiteral("https://[%1]:*").arg(host));
        }
    }
}

// Authoritative list of MCP protocol versions this server will accept. First
// entry is also the preferred version returned when a client requests an
// unrecognized one. Order matters: keep newest first so `supportedVersions.first()`
// is the latest spec.
const QStringList& McpServer::supportedProtocolVersions()
{
    static const QStringList versions = {
        QStringLiteral("2025-11-25"),
        QStringLiteral("2025-06-18"),
        QStringLiteral("2025-03-26"),
        QStringLiteral("2024-11-05"),
    };
    return versions;
}

bool McpServer::isOriginAllowed(const QString& origin) const
{
    // Empty Origin (CLI clients, mcp-remote, MCP Inspector CLI) is always allowed.
    if (origin.isEmpty()) return true;

    const QString lower = origin.toLower();
    if (m_allowedOrigins.contains(lower)) return true;

    // Wildcard-port match: compare scheme://host[:any port] against entries
    // ending in ":*".
    const QUrl url(origin);
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty())
        return false;

    const QString host = url.host().toLower();
    const QString scheme = url.scheme().toLower();
    const QString hostBracketed = host.contains(QLatin1Char(':'))
        ? QStringLiteral("[%1]").arg(host)  // bracket IPv6 literals
        : host;
    const QString anyPort = QStringLiteral("%1://%2:*").arg(scheme, hostBracketed);
    return m_allowedOrigins.contains(anyPort);
}

void McpServer::registerAllTools()
{
    registerMachineTools(m_toolRegistry, m_device, m_machineState, m_mainController, m_profileManager);
    registerShotTools(m_toolRegistry, m_shotHistory);
    registerProfileTools(m_toolRegistry, m_profileManager, m_settings);
    registerPresetsTools(m_toolRegistry, m_settings, m_mainController, m_machineState);
    registerRecipeTools(m_toolRegistry, m_shotHistory,
                        m_mainController ? m_mainController->recipeStorage() : nullptr,
                        m_mainController, m_settings);
    registerSettingsReadTools(m_toolRegistry, m_settings, m_accessibilityManager,
                              m_screensaverManager, m_translationManager, m_batteryManager,
                              m_mainController ? m_mainController->aiManager() : nullptr);
    registerDialingTools(m_toolRegistry, m_mainController, m_profileManager, m_shotHistory, m_settings);
    registerControlTools(m_toolRegistry, m_device, m_machineState, m_profileManager,
                         m_mainController, m_settings);
    registerWriteTools(m_toolRegistry, m_profileManager, m_shotHistory, m_settings,
                       m_mainController ? m_mainController->visualizer() : nullptr,
                       m_mainController ? m_mainController->bagStorage() : nullptr,
                       m_accessibilityManager, m_screensaverManager,
                       m_translationManager, m_batteryManager,
                       m_mainController ? m_mainController->aiManager() : nullptr,
                       m_mainController ? m_mainController->beanbase() : nullptr);
    registerScaleTools(m_toolRegistry, m_machineState);
    registerDeviceTools(m_toolRegistry, m_bleManager, m_device);
    registerDebugTools(m_toolRegistry, m_memoryMonitor);
    registerAgentTools(m_toolRegistry);
    registerAITools(m_toolRegistry, m_mainController);
    MCP_LOG_TAGGED("Server", QStringLiteral("Registered %1 tools")
                       .arg(m_toolRegistry->listTools(2, QStringLiteral("2025-11-25")).size()));
}

void McpServer::registerAllResources()
{
    registerMcpResources(m_resourceRegistry, m_device, m_machineState, m_profileManager, m_shotHistory, m_memoryMonitor, m_settings);
    MCP_LOG_TAGGED("Server", QStringLiteral("Registered %1 resources")
                       .arg(m_resourceRegistry->listResources(QStringLiteral("2025-11-25")).size()));
}

void McpServer::connectSseNotifications()
{
    // Phase change → decenza://machine/state
    if (m_machineState) {
        connect(m_machineState, &MachineState::phaseChanged, this, [this]() {
            broadcastSseNotification("decenza://machine/state");
        });
    }

    // Profile changed → decenza://profiles/active
    if (m_profileManager) {
        connect(m_profileManager, &ProfileManager::currentProfileChanged, this, [this]() {
            broadcastSseNotification("decenza://profiles/active");
        });
    }

    // Shot saved → decenza://shots/recent
    if (m_shotHistory) {
        connect(m_shotHistory, &ShotHistoryStorage::shotSaved, this, [this]() {
            broadcastSseNotification("decenza://shots/recent");
        });
    }
}

void McpServer::broadcastSseNotification(const QString& resourceUri)
{
    if (m_sseClients.isEmpty()) return;

    QJsonObject notification;
    notification["jsonrpc"] = "2.0";
    notification["method"] = "notifications/resources/updated";
    QJsonObject params;
    params["uri"] = resourceUri;
    notification["params"] = params;

    // Every event carries an ID (2025-11-25 SHOULD) so a reconnecting client can
    // say what it last saw. One ID per broadcast, not per recipient: the spec
    // asks for uniqueness within a session, and each session sees this event at
    // most once.
    QByteArray event;
    event.append("id: " + QByteArray::number(++m_sseEventId) + "\n");
    event.append("event: message\n");
    event.append("data: ");
    event.append(QJsonDocument(notification).toJson(QJsonDocument::Compact));
    event.append("\n\n");

    // Send only to sessions that subscribed to this resource URI.
    // Sessions without any subscriptions receive all notifications (backward compat).
    QList<QPointer<QTcpSocket>> dead;
    for (const QPointer<QTcpSocket>& clientPtr : std::as_const(m_sseClients)) {
        QTcpSocket* client = clientPtr.data();
        if (!client || client->state() != QAbstractSocket::ConnectedState) {
            dead.append(clientPtr);
            continue;
        }

        // Check if the SSE client's session has subscribed to this URI
        bool shouldSend = true;
        for (auto* session : std::as_const(m_sessions)) {
            if (session->sseSocket() == client) {
                // Session has subscriptions — only send if URI is in the set
                if (!session->subscribedResources().isEmpty())
                    shouldSend = session->subscribedResources().contains(resourceUri);
                break;
            }
        }

        if (shouldSend) {
            client->write(event);
            client->flush();
        }
    }
    for (const QPointer<QTcpSocket>& p : dead)
        m_sseClients.removeAll(p);
}

bool McpServer::isSseClient(QTcpSocket* socket) const
{
    if (!socket) return false;
    return m_sseClients.contains(QPointer<QTcpSocket>(socket));
}

void McpServer::probeSseKeepalives()
{
    QList<QPointer<QTcpSocket>> dead;
    for (const QPointer<QTcpSocket>& clientPtr : std::as_const(m_sseClients)) {
        QTcpSocket* client = clientPtr.data();
        if (!client || client->state() != QAbstractSocket::ConnectedState
                    || client->write(": keepalive\n\n") == -1) {
            dead.append(clientPtr);
            continue;
        }
        client->flush();
    }
    // ShotServer owns the QTcpSocket lifetime; we just unsubscribe and let
    // its onDisconnected drive deleteLater. close() emits disconnected
    // synchronously, which fires our own lambda and removes from m_sseClients
    // again (no-op once removed).
    for (const QPointer<QTcpSocket>& p : dead) {
        m_sseClients.removeAll(p);
        if (QTcpSocket* c = p.data())
            c->close();
    }
}

McpServer::~McpServer()
{
    qDeleteAll(m_sessions);
}

QJsonObject McpServer::buildToolCallResponse(const QJsonObject& toolResult,
                                              const QString& protocolVersion) const
{
    // `structuredContent` and the `resource_link` content block type were
    // introduced in 2025-06-18. Strict 2024-11-05 clients reject the response
    // when either appears, so both are gated on the negotiated version.
    const bool emitStructured = protocolVersion >= QStringLiteral("2025-06-18");
    const bool emitResourceLinks = protocolVersion >= QStringLiteral("2025-06-18");

    // Pull out optional `_resourceLinks` array — tools that want to attach
    // resource_link content blocks declare them as a side-channel here so the
    // structured payload itself stays clean. Each entry is
    // { "uri": "...", "title": "...", "mimeType": "..." (optional) }.
    QJsonObject sanitized = toolResult;
    QJsonArray resourceLinks = sanitized.take(QStringLiteral("_resourceLinks")).toArray();

    QJsonArray content;

    if (emitResourceLinks) {
        // Resource link blocks first — they're cheap to render and let clients
        // that subscribe to resource updates correlate the result with a URI.
        for (const QJsonValue& v : std::as_const(resourceLinks)) {
            QJsonObject src = v.toObject();
            QJsonObject block;
            block["type"] = "resource_link";
            const QString uri = src.value("uri").toString();
            block["uri"] = uri;
            // MCP 2025-06-18: `resource_link` carries the same shape as a
            // `Resource`, where `name` is REQUIRED. Strict clients reject the
            // whole content[] entry when it's missing. Prefer a side-channel
            // `name` when the emitter supplied one; otherwise fall back to the
            // uri's last path segment (e.g. decenza://shots/884 → "884",
            // decenza://machine/state → "state") so we never ship an entry
            // without `name`.
            QString name = src.value("name").toString();
            if (name.isEmpty() && !uri.isEmpty()) {
                const qsizetype slash = uri.lastIndexOf('/');
                const QString tail = slash >= 0 ? uri.mid(slash + 1) : uri;
                name = tail.isEmpty() ? uri : tail;
            }
            if (name.isEmpty()) {
                // Empty uri AND no provided name — emitter bug. Skip the block
                // entirely rather than ship a payload that fails strict zod
                // validation downstream.
                MCP_WARN_TAGGED("Server", QStringLiteral("dropping resource_link with empty name and uri"));
                continue;
            }
            block["name"] = name;
            const QString lt = src.value("title").toString();
            if (!lt.isEmpty()) block["title"] = lt;
            const QString mt = src.value("mimeType").toString();
            block["mimeType"] = mt.isEmpty() ? QStringLiteral("application/json") : mt;
            const QString ld = src.value("description").toString();
            if (!ld.isEmpty()) block["description"] = ld;
            content.append(block);
        }
    }

    // Text content block is always emitted: it's the only payload that
    // 2024-11-05 / 2025-03-26 clients read, and 2025-06-18+ clients ignore it
    // once they consume `structuredContent` below.
    QJsonObject textBlock;
    textBlock["type"] = "text";
    textBlock["text"] = QString::fromUtf8(QJsonDocument(sanitized).toJson(QJsonDocument::Compact));
    content.append(textBlock);

    QJsonObject result;
    result["content"] = content;
    if (emitStructured)
        result["structuredContent"] = sanitized;

    // A tool reports failure by returning a top-level `error` key — ~283 sites
    // across src/mcp/mcptools_*.cpp do exactly that, and none uses a different
    // spelling (measure with: grep -rhoE '\["error"\] *=|\{"error"'
    // src/mcp/mcptools_*.cpp | wc -l). Wrapping buries that key one level down,
    // where sendJsonRpcResponse's top-level contains("error") test can never see
    // it — so before this branch existed, tool failures shipped as unmarked
    // successes, the single exception being the confirmation denial below, which
    // set `isError` by hand and no longer needs to.
    //
    // What this does NOT cover: a failure signalled any other way. A payload with
    // `success: false` and no `error`, a `warning`, an `available: false`, an
    // empty result, or the registry's `errorOut` (access denied, unknown tool —
    // those become JSON-RPC errors upstream) are all invisible here. See
    // MCP_SERVER.md; the rule is "an `error` key is marked", not "every failure is
    // marked".
    //
    // Transferring it HERE is what makes it work for all of them at once: this is
    // the only place that sees both the tool's own `error` key and the envelope it
    // is about to become. MCP's CallToolResult (schema 2025-11-25) is explicit that this is
    // the right shape — "Any errors that originate from the tool SHOULD be
    // reported inside the result object, with `isError` set to true, _not_ as an
    // MCP protocol-level error response. Otherwise, the LLM would not be able to
    // see that an error occurred and self-correct." The error text therefore stays
    // in content[] rather than moving.
    //
    // Sparse-emit: `isError?: boolean`, "If not set, this is assumed to be false",
    // so a successful call carries no key at all rather than `isError: false`.
    if (sanitized.contains(QStringLiteral("error")))
        result["isError"] = true;

    return result;
}

void McpServer::handleHttpRequest(QTcpSocket* socket, const QString& method,
                                   const QString& path, const QByteArray& headers,
                                   const QByteArray& body, bool remote)
{
    Q_UNUSED(path)

    // Extract relevant request headers in a single pass:
    //   - Mcp-Session-Id / Mcp-Session: session identifier
    //   - MCP-Protocol-Version:        negotiated protocol version (per 2025-06-18)
    //   - Origin:                      browser-supplied origin (per 2025-11-25)
    QString sessionHeader;
    QString protocolHeader;
    QString originHeader;
    for (const QByteArray& line : headers.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        const QByteArray lower = trimmed.toLower();
        if (sessionHeader.isEmpty() &&
            (lower.startsWith("mcp-session-id:") || lower.startsWith("mcp-session:"))) {
            sessionHeader = QString::fromUtf8(trimmed.mid(trimmed.indexOf(':') + 1).trimmed());
        } else if (protocolHeader.isEmpty() && lower.startsWith("mcp-protocol-version:")) {
            protocolHeader = QString::fromUtf8(trimmed.mid(trimmed.indexOf(':') + 1).trimmed());
        } else if (originHeader.isEmpty() && lower.startsWith("origin:")) {
            originHeader = QString::fromUtf8(trimmed.mid(trimmed.indexOf(':') + 1).trimmed());
        }
    }

    // Origin allowlist check (DNS-rebinding protection per 2025-11-25). Done
    // before any JSON-RPC parsing so a foreign Origin can't even reach the
    // dispatcher. Stash the validated origin on the socket so sendHttpResponse
    // can echo it back via Access-Control-Allow-Origin.
    if (!isOriginAllowed(originHeader)) {
        MCP_WARN_TAGGED("Server", QStringLiteral("Rejecting request from disallowed Origin: %1")
                                      .arg(originHeader));
        sendHttpResponse(socket, 403, "Origin not allowed", "text/plain");
        return;
    }
    if (socket && !originHeader.isEmpty())
        socket->setProperty("mcpOrigin", originHeader);

    if (method == "POST") {
        // JSON-RPC request. The body is either a single message object or a
        // batch array — the 2024-11-05 and 2025-03-26 base protocols require
        // every server to accept the array form, and both are still negotiated.
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError
            || (!doc.isObject() && !doc.isArray())) {
            sendJsonRpcError(socket, -32700, "Parse error", QJsonValue::Null);
            return;
        }

        if (doc.isArray()) {
            handleJsonRpcBatch(socket, doc.array(), sessionHeader, protocolHeader, remote);
            return;
        }

        QJsonObject request = doc.object();
        const QString rpcMethod = request["method"].toString();

        const SessionResolution resolved =
            resolveSessionForMessage(request, sessionHeader, protocolHeader);
        if (resolved.httpStatus != 0) {
            sendHttpResponse(socket, resolved.httpStatus, resolved.httpBody, "text/plain",
                             resolved.session ? resolved.session->id() : QString());
            return;
        }
        if (resolved.rpcErrorCode != 0) {
            sendJsonRpcError(socket, resolved.rpcErrorCode, resolved.rpcErrorMessage,
                             request["id"].toVariant(),
                             resolved.session ? resolved.session->id() : sessionHeader);
            return;
        }
        McpSession* session = resolved.session;

        session->touch();
        if (remote)
            session->setRemote(true);

        // Notifications (no id, no response expected per JSON-RPC)
        // But HTTP still needs a response — send 202 Accepted
        if (!request.contains("id")) {
            if (rpcMethod == "notifications/initialized") {
                // Client acknowledged initialization — nothing to do
            }
            sendHttpResponse(socket, 202, "", "application/json", session->id());
            return;
        }

        QJsonObject result = handleJsonRpc(request, session, socket, request["id"].toVariant());

        // If in-app confirmation is pending, response will be sent later by confirmationResolved()
        if (result.contains("_deferred"))
            return;

        sendJsonRpcResponse(socket, result, request["id"].toVariant(), session->id());

    } else if (method == "GET") {
        // Check if client wants SSE (Accept: text/event-stream)
        bool wantsSse = false;
        for (const QByteArray& line : headers.split('\n')) {
            if (line.trimmed().toLower().startsWith("accept:") &&
                line.toLower().contains("text/event-stream")) {
                wantsSse = true;
                break;
            }
        }

        if (!wantsSse) {
            // GET without Accept: text/event-stream is invalid per MCP Streamable HTTP spec
            sendHttpResponse(socket, 405, "Method not allowed. Use POST for JSON-RPC.", "text/plain");
            return;
        }

        // SSE stream for server-initiated notifications. Count only live entries —
        // a QPointer that has gone null (socket destroyed before our disconnect
        // lambda ran) still occupies a slot until probeSseKeepalives() GCs it on
        // the next 30 s tick, and we don't want stale nulls to falsely trip the
        // limit and reject a legitimate client.
        int liveSseCount = 0;
        for (const QPointer<QTcpSocket>& p : std::as_const(m_sseClients))
            if (!p.isNull()) ++liveSseCount;
        if (liveSseCount >= MaxSseConnections) {
            sendHttpResponse(socket, 429, "Too many SSE connections", "text/plain");
            return;
        }

        // Associate SSE socket with session if the client sent a session header
        McpSession* sseSession = findSession(sessionHeader);
        if (remote && sseSession)
            sseSession->setRemote(true);

        // Send SSE headers (include session ID if known)
        QByteArray response;
        response.append("HTTP/1.1 200 OK\r\n");
        response.append("Content-Type: text/event-stream\r\n");
        response.append("Cache-Control: no-cache\r\n");
        response.append("Connection: keep-alive\r\n");
        if (!originHeader.isEmpty()) {
            response.append("Access-Control-Allow-Origin: " + originHeader.toUtf8() + "\r\n");
            response.append("Access-Control-Allow-Credentials: true\r\n");
            response.append("Vary: Origin\r\n");
        } else {
            response.append("Access-Control-Allow-Origin: *\r\n");
        }
        response.append("Access-Control-Expose-Headers: Mcp-Session-Id, Mcp-Session, MCP-Protocol-Version\r\n");
        if (sseSession) {
            response.append("Mcp-Session-Id: " + sseSession->id().toUtf8() + "\r\n");
            response.append("Mcp-Session: " + sseSession->id().toUtf8() + "\r\n");
            response.append("MCP-Protocol-Version: " + sseSession->protocolVersion().toUtf8() + "\r\n");
        }
        response.append("\r\n");

        // Prime the client for reconnection (2025-11-25 SHOULDs), before any
        // real event:
        //   - `retry` sets the reconnect delay. 3 s: fast enough that a dropped
        //     stream is back before the 30 s keepalive probe would have noticed
        //     it was gone, slow enough that a server that is actually down is not
        //     hammered. The two intervals are independent — the probe detects a
        //     dead socket from our side, `retry` paces the client's return — but
        //     they must not invert, or the probe would keep finding half-open
        //     streams a client had already given up on.
        //   - an opening event with an ID and empty `data` gives the client a
        //     `Last-Event-ID` to send back immediately, rather than only after
        //     the first notification (which may be minutes away, or never).
        // We do not replay from `Last-Event-ID` — that is a MAY, and a partial
        // replay is worse than none. A client that missed events re-reads the
        // resources it cares about.
        response.append("retry: 3000\n");
        response.append("id: " + QByteArray::number(++m_sseEventId) + "\n");
        response.append("data: \n\n");
        socket->write(response);
        socket->flush();

        m_sseClients.append(QPointer<QTcpSocket>(socket));
        if (sseSession)
            sseSession->setSseSocket(socket);

        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_sseClients.removeAll(QPointer<QTcpSocket>(socket));
            // Clear the session's SSE socket reference — the client may reconnect
            // SSE without re-initializing, so keep the session alive.
            for (auto* s : std::as_const(m_sessions)) {
                if (s->sseSocket() == socket) {
                    s->setSseSocket(nullptr);
                    break;
                }
            }
            MCP_INFO_TAGGED("Server", QStringLiteral("SSE client disconnected, remaining: %1")
                                          .arg(m_sseClients.size()));
        });
        MCP_INFO_TAGGED("Server", QStringLiteral("SSE client connected, total: %1")
                                  .arg(m_sseClients.size()));

    } else if (method == "DELETE") {
        // Terminate session
        McpSession* session = findSession(sessionHeader);
        if (session) {
            // Clear pending confirmation if it belongs to this session
            if (m_pendingConfirmation.has_value() && m_pendingConfirmation->sessionId == session->id())
                m_pendingConfirmation.reset();
            m_sessions.remove(session->id());
            // Recorded AFTER the pending-confirmation cleanup above, so nothing
            // can observe a tombstoned ID whose session is still half-alive.
            recordTerminatedSession(session->id());
            delete session;
            emit activeSessionCountChanged();
        }
        sendHttpResponse(socket, 200, "{}", "application/json");

    } else if (method == "OPTIONS") {
        // CORS preflight. MCP-Protocol-Version is required on every request
        // after `initialize` per 2025-06-18, so it must be in the allowlist.
        sendHttpResponse(socket, 204, "", "", QString(),
                         {{"Access-Control-Allow-Methods", "POST, GET, DELETE, OPTIONS"},
                          {"Access-Control-Allow-Headers", "Content-Type, Authorization, Mcp-Session, Mcp-Session-Id, MCP-Protocol-Version"},
                          {"Access-Control-Max-Age", "86400"}});

    } else {
        sendHttpResponse(socket, 405, "Method not allowed", "text/plain");
    }
}

// One place builds a JSON-RPC error object. sendJsonRpcError writes it to a
// socket; the batch path folds it into an array slot instead, and the two must
// not be free to drift into different shapes.
static QJsonObject makeJsonRpcError(int code, const QString& message, const QVariant& id)
{
    QJsonObject error;
    error["code"] = code;
    error["message"] = message;

    QJsonObject response;
    response["jsonrpc"] = "2.0";
    response["id"] = QJsonValue::fromVariant(id);
    response["error"] = error;
    return response;
}

// A `tools/call` that never reached a handler, as a JSON-RPC error result.
//
// An unregistered tool name is `-32602` Invalid params — the tools spec's own
// example returns exactly that for `Unknown tool: …`, and it is a bad request,
// not a server fault. Every other registry failure stays `-32603`: a tool
// dispatched on the wrong sync/async path is our wiring bug, and an
// access-level refusal is a server-side policy decision the caller cannot fix
// by changing its arguments.
static QJsonObject registryErrorResult(const QString& message, McpRegistryFailure failure)
{
    QJsonObject errorObj;
    errorObj["code"] = failure == McpRegistryFailure::NotFound ? -32602 : -32603;
    errorObj["message"] = message;
    QJsonObject result;
    result["error"] = errorObj;
    return result;
}

McpServer::SessionResolution McpServer::resolveSessionForMessage(const QJsonObject& request,
                                                                 const QString& sessionHeader,
                                                                 const QString& protocolHeader)
{
    SessionResolution out;
    const QString rpcMethod = request["method"].toString();

    // Initialize can come without a session — creates one.
    // Pass sessionHeader so reconnecting clients reuse their existing session.
    if (rpcMethod == "initialize") {
        // Deliberately reached WITHOUT the terminated-session check below:
        // re-initializing is the documented move after a 404, so 404ing the
        // client's attempt to recover would strand it for good.
        out.session = findOrCreateSession(sessionHeader);
        if (!out.session) {
            out.rpcErrorCode = -32000;
            out.rpcErrorMessage = QStringLiteral("Too many sessions");
        }
        return out;
    }

    // A session this server ended itself is gone for good, and 404 is what tells
    // the client to start a new one (MUST, 2025-03-26 onward). Checked BEFORE the
    // auto-recovery below, which must stay reachable for IDs we never issued —
    // a restart, another instance, or a garbage value. The server cannot tell
    // those apart from an ID it issued before a restart, which is why only IDs it
    // explicitly ended are rejected.
    if (!sessionHeader.isEmpty() && m_terminatedSessions.contains(sessionHeader)) {
        MCP_INFO_TAGGED("Server", QStringLiteral("Request for terminated session %1 — 404")
                                      .arg(sessionHeader));
        out.httpStatus = 404;
        out.httpBody = "Session terminated";
        return out;
    }

    McpSession* session = findSession(sessionHeader);
    // Fallback: if no session header provided, use the most recent session.
    // mcp-remote doesn't always send the Mcp-Session header after initialize.
    if (!session && sessionHeader.isEmpty() && m_sessions.size() == 1) {
        session = m_sessions.begin().value();
    }
    // Auto-recover: if session expired or ID is stale, reuse the sole
    // remaining session if possible, otherwise create a new one.
    // mcp-remote can't re-initialize on its own, so rejecting here
    // leaves the client permanently broken until restart.
    if (!session) {
        if (m_sessions.size() == 1) {
            // Only one session exists — the client almost certainly belongs
            // to it. Reuse it to avoid leaking a new session on every request.
            session = m_sessions.begin().value();
            MCP_INFO_TAGGED("Server", QStringLiteral("Stale session header, reusing sole session %1")
                          .arg(session->id()));
            // Adopt the client's MCP-Protocol-Version (when present and
            // supported) so the mismatch check below doesn't 400 a
            // recovered client whose prior negotiation differed from the
            // session's. Mirrors the auto-create branch.
            if (!protocolHeader.isEmpty()
                && supportedProtocolVersions().contains(protocolHeader)
                && protocolHeader != session->protocolVersion()) {
                session->setProtocolVersion(protocolHeader);
            }
        } else {
            MCP_INFO_TAGGED("Server", QStringLiteral("Session not found (expired or stale), "
                                     "auto-creating new session"));
            session = findOrCreateSession(QString());
            if (!session) {
                out.rpcErrorCode = -32000;
                out.rpcErrorMessage = QStringLiteral("Too many sessions");
                return out;
            }
            // Mark as initialized — the client already completed initialize
            // in a prior session, so skip the handshake requirement.
            // Adopt the client's MCP-Protocol-Version when present and
            // supported so the mismatch check below doesn't immediately
            // 400 a recovered client whose prior negotiation was newer
            // than our default. Reject unrecognized headers so an
            // attacker can't push the gate into an unspec'd state.
            session->setInitialized(true);
            if (!protocolHeader.isEmpty()
                && supportedProtocolVersions().contains(protocolHeader)) {
                session->setProtocolVersion(protocolHeader);
            }
        }
    }
    out.session = session;

    // MCP-Protocol-Version header check (required by 2025-06-18 for
    // every non-initialize HTTP request after the session is set up).
    // - Skip on `initialize` itself: handled by the early return above.
    // - Skip on uninitialized sessions: clients legitimately may not
    //   know the version yet (e.g. on `notifications/initialized`).
    // - When absent, the spec says assume `2025-03-26` — sessions
    //   default to that, so no action needed.
    if (!protocolHeader.isEmpty() && session->initialized()
        && protocolHeader != session->protocolVersion()) {
        MCP_WARN_TAGGED("Server", QStringLiteral("Protocol version mismatch — header %1, "
                                                 "session %2")
                                      .arg(protocolHeader, session->protocolVersion()));
        out.httpStatus = 400;
        out.httpBody = "Protocol version mismatch (negotiated "
                       + session->protocolVersion().toUtf8()
                       + ", header " + protocolHeader.toUtf8() + ")";
        return out;
    }

    if (!session->initialized() && rpcMethod != "notifications/initialized"
        && rpcMethod != "ping") {
        out.rpcErrorCode = -32600;
        out.rpcErrorMessage = QStringLiteral("Session not initialized");
    }
    return out;
}

void McpServer::handleJsonRpcBatch(QTcpSocket* socket, const QJsonArray& batch,
                                   const QString& sessionHeader, const QString& protocolHeader,
                                   bool remote)
{
    // JSON-RPC 2.0: an empty array is an Invalid Request, not an empty batch.
    if (batch.isEmpty()) {
        sendJsonRpcError(socket, -32600, "Invalid Request", QJsonValue::Null);
        return;
    }

    QJsonArray responses;
    QString sessionId;

    for (const QJsonValue& element : batch) {
        if (!element.isObject()) {
            responses.append(makeJsonRpcError(-32600, QStringLiteral("Invalid Request"),
                                              QVariant()));
            continue;
        }
        const QJsonObject request = element.toObject();
        const QVariant requestId = request["id"].toVariant();

        const SessionResolution resolved =
            resolveSessionForMessage(request, sessionHeader, protocolHeader);

        // An HTTP-level answer — a terminated session, a protocol-version
        // mismatch — is about the request as a whole, not about one element, so
        // it replaces the batch response entirely. Both depend only on headers,
        // so the first element to hit one would be followed by every other.
        if (resolved.httpStatus != 0) {
            sendHttpResponse(socket, resolved.httpStatus, resolved.httpBody, "text/plain",
                             resolved.session ? resolved.session->id() : QString());
            return;
        }
        if (resolved.rpcErrorCode != 0) {
            if (request.contains("id"))
                responses.append(makeJsonRpcError(resolved.rpcErrorCode,
                                                  resolved.rpcErrorMessage, requestId));
            continue;
        }

        McpSession* session = resolved.session;
        sessionId = session->id();
        session->touch();
        if (remote)
            session->setRemote(true);

        // Notifications produce no entry in the response array (JSON-RPC 2.0).
        if (!request.contains("id"))
            continue;

        QJsonObject result = handleJsonRpc(request, session, socket, requestId);

        // A deferred result — an in-app confirmation, or an async tool — is
        // written to the socket later as a COMPLETE HTTP response, which cannot
        // be folded into this array. Rather than leave the slot empty (and the
        // client waiting on a response that will arrive as a second, malformed
        // body), refuse the element and answer the rest normally.
        //
        // Logged because no client here has ever batched such a call: if this
        // line appears, a real client wants it and the refusal is worth
        // revisiting.
        if (result.contains("_deferred")) {
            MCP_WARN_TAGGED("Server",
                            QStringLiteral("Batched call to %1 refused — it defers its response")
                                .arg(request["params"].toObject()["name"].toString()));
            responses.append(makeJsonRpcError(
                -32600,
                QStringLiteral("This tool cannot be called in a batch: its response is "
                               "delivered separately. Send it as a single request."),
                requestId));
            continue;
        }

        QJsonObject response;
        response["jsonrpc"] = "2.0";
        response["id"] = QJsonValue::fromVariant(requestId);
        if (result.contains("error"))
            response["error"] = result["error"];
        else
            response["result"] = result;
        responses.append(response);
    }

    // A batch of nothing but notifications gets 202 with no body, matching what
    // a single notification gets.
    if (responses.isEmpty()) {
        sendHttpResponse(socket, 202, "", "application/json", sessionId);
        return;
    }

    sendHttpResponse(socket, 200, QJsonDocument(responses).toJson(QJsonDocument::Compact),
                     "application/json", sessionId);
}

QJsonObject McpServer::handleJsonRpc(const QJsonObject& request, McpSession* session,
                                     QTcpSocket* socket, const QVariant& requestId)
{
    QString method = request["method"].toString();
    QJsonObject params = request["params"].toObject();

    if (method == "initialize")
        return handleInitialize(params, session);
    if (method == "tools/list")
        return handleToolsList(params, session);
    if (method == "tools/call")
        return handleToolsCall(params, session, socket, requestId);
    if (method == "resources/list")
        return handleResourcesList(params, session);
    if (method == "resources/read")
        return handleResourcesRead(params, session, socket, requestId);
    if (method == "resources/subscribe")
        return handleResourcesSubscribe(params, session);
    if (method == "resources/unsubscribe")
        return handleResourcesUnsubscribe(params, session);
    if (method == "ping")
        return QJsonObject(); // empty result per spec

    // Unknown method
    QJsonObject error;
    error["code"] = -32601;
    error["message"] = "Method not found: " + method;
    QJsonObject result;
    result["error"] = error;
    return result;
}

QJsonObject McpServer::handleInitialize(const QJsonObject& params, McpSession* session)
{
    session->setClientCapabilities(params["capabilities"].toObject());
    session->setInitialized(true);

    QJsonObject serverCapabilities;

    // Declare tool support
    QJsonObject toolsCap;
    serverCapabilities["tools"] = toolsCap;

    // Declare resource support
    QJsonObject resourcesCap;
    resourcesCap["subscribe"] = true;
    serverCapabilities["resources"] = resourcesCap;

    QJsonObject serverInfo;
    serverInfo["name"] = "Decenza MCP Server";
    serverInfo["version"] = "1.0.0";

    // Negotiate protocol version — accept what the client requests if we support it,
    // otherwise return our preferred version (the first entry).
    QString clientVersion = params["protocolVersion"].toString();
    const QStringList& supportedVersions = supportedProtocolVersions();
    QString negotiatedVersion = supportedVersions.contains(clientVersion)
        ? clientVersion : supportedVersions.first();

    if (session)
        session->setProtocolVersion(negotiatedVersion);

    const QJsonObject clientInfo = params["clientInfo"].toObject();
    auto sanitizeForLog = [](QString s) {
        // Untrusted strings from the network — cap length and strip newlines so
        // a hostile or buggy client can't forge log lines or DoS log volume.
        if (s.size() > 64) s.truncate(64);
        s.replace(QChar('\n'), QChar(' '));
        s.replace(QChar('\r'), QChar(' '));
        return s;
    };
    MCP_INFO_TAGGED("Server", QStringLiteral("initialize — client=%1 v%2 requested=%3 "
                                             "negotiated=%4 session=%5")
                                  .arg(sanitizeForLog(clientInfo["name"].toString()),
                                       sanitizeForLog(clientInfo["version"].toString()),
                                       sanitizeForLog(clientVersion),
                                       negotiatedVersion,
                                       session ? session->id() : QStringLiteral("(none)")));

    QJsonObject result;
    result["protocolVersion"] = negotiatedVersion;
    result["capabilities"] = serverCapabilities;
    result["serverInfo"] = serverInfo;

    // MCP `instructions`: server-level guidance the client retains for the
    // whole session and typically folds into its system prompt. #1162: an
    // external AI kept citing the internal numeric shot id ("shot 5188"),
    // which the user cannot find anywhere — Shot History and every
    // user-facing surface key shots by date/time. State the rule once here
    // so it reaches MCP clients that never call ai_advisor_invoke (which
    // always carries the full system prompt) and never request
    // dialing_get_context with includeFullKnowledge (opt-in since #1164):
    // for those clients this handshake string is the only carrier of the
    // rule, and it costs nothing per call.
    //
    // The `instructions` field was introduced in MCP revision 2025-03-26
    // and is absent from the 2024-11-05 InitializeResult. Gate it on the
    // negotiated version so strict 2024-11-05 clients don't reject the
    // response — the same discipline buildToolCallResponse() applies to
    // structuredContent / resource_link.
    if (negotiatedVersion >= QStringLiteral("2025-03-26")) {
        result["instructions"] = QStringLiteral(
            "When you refer to one of the user's espresso shots in a reply, "
            "identify it by its local date and time — the handle shown in the "
            "app's Shot History — for example \"your May 10, 9:04 AM shot\". "
            "Never cite the numeric shot `id`: it is an internal database key "
            "with no user-facing counterpart, and a user told to look at "
            "\"shot 5188\" cannot find it anywhere. Shots appear in "
            "dialing_get_context (dialInSessions, bestRecentShot) and "
            "shots_list, each carrying a local ISO `timestamp` — render it "
            "the way a person reads a clock. Use the numeric `id` only as an "
            "opaque argument to other tools.");
    }
    return result;
}

QJsonObject McpServer::handleToolsList(const QJsonObject& params, McpSession* session)
{
    Q_UNUSED(params)

    int accessLevel = m_settings ? m_settings->mcp()->mcpAccessLevel() : 0;
    const QString protocolVersion = session ? session->protocolVersion()
                                            : QStringLiteral("2024-11-05");

    QJsonObject result;
    result["tools"] = m_toolRegistry->listTools(accessLevel, protocolVersion);
    return result;
}

QJsonObject McpServer::handleToolsCall(const QJsonObject& params, McpSession* session,
                                       QTcpSocket* socket, const QVariant& requestId)
{
    QString toolName = params["name"].toString();
    QJsonObject arguments = params["arguments"].toObject();

    int accessLevel = m_settings ? m_settings->mcp()->mcpAccessLevel() : 0;
    const QString protocolVersion = session ? session->protocolVersion()
                                            : QStringLiteral("2024-11-05");

    // Rate limiting for control + settings tools
    QString category = m_toolRegistry->toolCategory(toolName);
    if (category == "control" || category == "settings") {
        if (session->controlCallCount() >= RateLimitPerMinute) {
            // The assistant is told; the user is not. Without this line nothing
            // anywhere explains why the machine ignored a command it was asked
            // for — every other refusal in this file logs.
            MCP_WARN_TAGGED("Server", QStringLiteral("Rate limit exceeded (%1/min) — refusing %2 "
                                                     "for session %3")
                                          .arg(RateLimitPerMinute).arg(toolName, session->id()));
            QJsonObject error;
            error["code"] = -32000;
            error["message"] = "Rate limit exceeded";
            QJsonObject result;
            result["error"] = error;
            return result;
        }
    }

    // Count control/settings calls before execution so failed calls also count
    if (category == "control" || category == "settings")
        session->incrementControlCalls();

    // Chat-based confirmation: tool returns needs_confirmation, AI re-calls with confirmed:true
    if (needsChatConfirmation(toolName) && !arguments.contains("confirmed")) {
        QJsonObject confirmPayload;
        confirmPayload["needs_confirmation"] = true;
        confirmPayload["action"] = toolName;
        confirmPayload["description"] = confirmationDescription(toolName);
        confirmPayload["parameters"] = arguments;
        return buildToolCallResponse(confirmPayload, protocolVersion);
    }

    // Strip the confirmed key before passing to tool handler
    if (arguments.contains("confirmed"))
        arguments.remove("confirmed");

    // In-app confirmation: hold HTTP response, show QML dialog on machine screen
    if (needsInAppConfirmation(toolName)) {
        // Deny any existing pending confirmation
        if (m_pendingConfirmation.has_value()) {
            auto& old = m_pendingConfirmation.value();
            if (old.socket && old.socket->state() == QAbstractSocket::ConnectedState) {
                // Same event class as a denial: the tool did not run because of the
                // confirmation gate. So it takes the same shape — a failed tool
                // result, not a protocol fault. A JSON-RPC error delivers no
                // content[], so the model would never learn WHY its call died and
                // could not decide to ask again.
                QJsonObject supersededPayload;
                supersededPayload["error"] = "Confirmation superseded by a newer request for "
                                             + old.toolName;
                sendJsonRpcResponse(old.socket,
                                    buildToolCallResponse(supersededPayload, old.protocolVersion),
                                    old.requestId, old.sessionId);
                MCP_WARN_TAGGED("Server", QStringLiteral("Superseded pending confirmation for %1")
                                              .arg(old.toolName));
            }
            m_pendingConfirmation.reset();
        }

        PendingConfirmation pending;
        pending.socket = socket;
        pending.requestId = requestId;
        pending.sessionId = session->id();
        pending.toolName = toolName;
        pending.arguments = arguments;
        pending.accessLevel = accessLevel;
        pending.protocolVersion = protocolVersion;
        m_pendingConfirmation = pending;

        QString description = confirmationDescription(toolName);
        emit confirmationRequested(toolName, description, session->id());

        QJsonObject deferred;
        deferred["_deferred"] = true;
        return deferred;
    }

    // Async tool: dispatch to background thread, send response later
    if (m_toolRegistry->isAsyncTool(toolName)) {
        QPointer<QTcpSocket> socketPtr(socket);
        QVariant reqId = requestId;
        QString sessId = session->id();
        QString protoVer = protocolVersion;

        QString error;
        McpRegistryFailure failure = McpRegistryFailure::None;
        bool dispatched = m_toolRegistry->callAsyncTool(
            toolName, arguments, accessLevel, error,
            [this, socketPtr, reqId, sessId, protoVer](QJsonObject toolResult) {
                sendAsyncToolResponse(socketPtr, reqId, sessId, protoVer, toolResult);
            }, &failure);

        if (!dispatched)
            return registryErrorResult(error, failure);

        QJsonObject deferred;
        deferred["_deferred"] = true;
        return deferred;
    }

    // Synchronous tool
    QString error;
    McpRegistryFailure failure = McpRegistryFailure::None;
    QJsonObject toolResult = m_toolRegistry->callTool(toolName, arguments, accessLevel, error,
                                                      &failure);

    if (!error.isEmpty())
        return registryErrorResult(error, failure);

    return buildToolCallResponse(toolResult, protocolVersion);
}

QJsonObject McpServer::handleResourcesList(const QJsonObject& params, McpSession* session)
{
    Q_UNUSED(params)

    const QString protocolVersion = session ? session->protocolVersion()
                                            : QStringLiteral("2024-11-05");

    QJsonObject result;
    result["resources"] = m_resourceRegistry->listResources(protocolVersion);
    return result;
}

QJsonObject McpServer::handleResourcesRead(const QJsonObject& params, McpSession* session,
                                            QTcpSocket* socket, const QVariant& requestId)
{
    QString uri = params["uri"].toString();

    // A `resources/read` failure carries -32002 when the URI names nothing we
    // serve — the code the resources spec assigns to "Resource not found", with
    // the requested URI in `data` as its example shows. Other read failures stay
    // -32602: they describe a request that named a real resource wrongly.
    const auto readErrorResult = [&uri](const QString& message, McpRegistryFailure failure) {
        QJsonObject errorObj;
        errorObj["code"] = failure == McpRegistryFailure::NotFound ? -32002 : -32602;
        errorObj["message"] = message;
        if (failure == McpRegistryFailure::NotFound) {
            QJsonObject data;
            data["uri"] = uri;
            errorObj["data"] = data;
        }
        QJsonObject result;
        result["error"] = errorObj;
        return result;
    };

    // One shape for a resource content entry, both paths. Carries only fields the
    // MCP `ResourceContents` schema defines — `structuredContent` is NOT one of
    // them (it exists on `CallToolResult` alone), and the same JSON it used to
    // duplicate is already in `text`.
    const auto buildContents = [](const QString& resourceUri, const QJsonObject& resourceData) {
        QJsonObject content;
        content["uri"] = resourceUri;
        content["mimeType"] = "application/json";
        content["text"] = QString::fromUtf8(QJsonDocument(resourceData).toJson(QJsonDocument::Compact));
        QJsonArray contents;
        contents.append(content);
        QJsonObject result;
        result["contents"] = contents;
        return result;
    };

    // Async resources: dispatch to background, send response later
    if (m_resourceRegistry->isAsyncResource(uri)) {
        QPointer<QTcpSocket> socketPtr(socket);
        QVariant reqId = requestId;
        QString sessId = session->id();

        QString error;
        McpRegistryFailure failure = McpRegistryFailure::None;
        bool dispatched = m_resourceRegistry->readAsyncResource(uri, error,
            [this, socketPtr, reqId, sessId, uri, buildContents](QJsonObject resourceData) {
                if (!socketPtr || socketPtr->state() != QAbstractSocket::ConnectedState) {
                    MCP_WARN_TAGGED("Server", QStringLiteral("async resource response dropped "
                                                             "(socket disconnected)"));
                    return;
                }
                sendJsonRpcResponse(socketPtr, buildContents(uri, resourceData), reqId, sessId);
            }, &failure);

        if (!dispatched)
            return readErrorResult(error, failure);

        QJsonObject deferred;
        deferred["_deferred"] = true;
        return deferred;
    }

    QString error;
    McpRegistryFailure failure = McpRegistryFailure::None;
    QJsonObject resourceData = m_resourceRegistry->readResource(uri, error, &failure);

    if (!error.isEmpty())
        return readErrorResult(error, failure);

    return buildContents(uri, resourceData);
}

QJsonObject McpServer::handleResourcesSubscribe(const QJsonObject& params, McpSession* session)
{
    QString uri = params["uri"].toString();
    if (uri.isEmpty()) {
        QJsonObject error;
        error["code"] = -32602;
        error["message"] = "Missing required parameter: uri";
        QJsonObject result;
        result["error"] = error;
        return result;
    }

    session->subscribe(uri);
    MCP_LOG_TAGGED("Server", QStringLiteral("Session %1 subscribed to %2").arg(session->id(), uri));
    return QJsonObject(); // empty result per spec
}

QJsonObject McpServer::handleResourcesUnsubscribe(const QJsonObject& params, McpSession* session)
{
    QString uri = params["uri"].toString();
    if (uri.isEmpty()) {
        QJsonObject error;
        error["code"] = -32602;
        error["message"] = "Missing required parameter: uri";
        QJsonObject result;
        result["error"] = error;
        return result;
    }

    session->unsubscribe(uri);
    MCP_LOG_TAGGED("Server", QStringLiteral("Session %1 unsubscribed from %2").arg(session->id(), uri));
    return QJsonObject(); // empty result per spec
}

McpSession* McpServer::findOrCreateSession(const QString& sessionHeader)
{
    // If sessionHeader is non-empty and matches an existing session, reuse it.
    // This prevents session leaks when mcp-remote reconnects and re-initializes.
    // If sessionHeader is empty (or unknown), a new session is always created.
    if (!sessionHeader.isEmpty()) {
        McpSession* existing = m_sessions.value(sessionHeader, nullptr);
        if (existing) {
            MCP_LOG_TAGGED("Server", QStringLiteral("Reusing existing session %1").arg(sessionHeader));
            existing->touch();
            return existing;
        }
    }

    // Clean up orphaned sessions before creating a new one.
    // Two transports leak slots and need different signals:
    //
    //   1. SSE clients (mcp-remote, etc.) — their SSE stream drops and they
    //      re-initialize without sending a session header. The old session
    //      stays around with no SSE socket. Detect via hadSseSocket() so we
    //      don't kill freshly-created sessions still in the window between
    //      POST initialize and GET /mcp.
    //
    //   2. Pure-HTTP clients (Claude Code's `type: "http"` transport) — they
    //      never establish an SSE stream, so hadSseSocket() is always false
    //      and the SSE rule above never fires. Each reconnect leaks a slot
    //      until the 30-min idle timeout, wedging the pool at MaxSessions.
    //      Use idle time as the signal: an HTTP MCP client that hasn't sent
    //      a request in OrphanIdleSeconds is presumed gone.
    QDateTime now = QDateTime::currentDateTimeUtc();
    // 5 min: well above any reasonable client keep-alive cadence (Claude Code
    // pings far more often, mcp-remote reconnects within seconds), and well
    // above the longest expected synchronous tool runtime, so we don't reap a
    // session whose async tool call is still in flight. Long enough to not
    // misfire, short enough to keep the pool from wedging at MaxSessions.
    constexpr int OrphanIdleSeconds = 300;
    QStringList orphaned;
    for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it) {
        const auto* s = it.value();
        if (s->isStateful())  // holds a live SSE stream — not an orphan
            continue;
        // Never reap a session that is holding a machine-start confirmation open.
        // Two ways such a session reaches this sweep with no live SSE: a cloud
        // connector's momentary SSE closed (hadSseSocket), or a pure-POST connector
        // (claude.ai) crossed OrphanIdleSeconds while the user deliberates at the
        // machine. Reaping it would silently reset the pending confirmation and drop
        // the held HTTP response — so the removal loop below cannot see it.
        if (m_pendingConfirmation.has_value() && m_pendingConfirmation->sessionId == it.key())
            continue;
        if (s->hadSseSocket()) {
            orphaned.append(it.key());
        } else if (s->lastActivity().secsTo(now) > OrphanIdleSeconds) {
            orphaned.append(it.key());
        }
    }
    for (const QString& id : orphaned) {
        MCP_INFO_TAGGED("Server", QStringLiteral("Removing orphaned session %1").arg(id));
        // No m_pendingConfirmation reset here: the guard above excludes any
        // confirmation-holding session from `orphaned`, so it is unreachable.
        delete m_sessions.take(id);
    }
    if (!orphaned.isEmpty())
        emit activeSessionCountChanged();

    // Absolute backstop on total retained sessions. Ephemeral (non-SSE) sessions
    // are no longer bounded by MaxSessions (which now counts only stateful ones),
    // and `initialize` is not rate-limited, so a client that POSTs `initialize` in
    // a tight loop without echoing a session header — a faster version of the cloud
    // connector churn — would otherwise accumulate session objects up to
    // (request rate × OrphanIdleSeconds) with no ceiling, risking OOM on the tablet.
    // When the pool is full, evict the least-recently-active *ephemeral* session
    // (never a stateful one, never one holding a pending confirmation). Eviction,
    // not rejection, so a burst of churn can never deny service to another client:
    // the evicted client re-initializes anyway, and any in-flight async response is
    // decoupled from the session object (it captures the socket + session id by
    // value), so dropping the session cannot lose or misroute that response.
    while (static_cast<int>(m_sessions.size()) >= MaxTotalSessions) {
        McpSession* victim = nullptr;
        for (McpSession* s : std::as_const(m_sessions)) {
            if (s->isStateful())
                continue;
            if (m_pendingConfirmation.has_value() && m_pendingConfirmation->sessionId == s->id())
                continue;
            if (!victim || s->lastActivity() < victim->lastActivity())
                victim = s;
        }
        if (!victim)
            break;  // pool is all stateful / confirming — let the stateful cap decide
        MCP_WARN_TAGGED("Server", QStringLiteral("Session pool at MaxTotalSessions (%1) — evicting "
                                                 "least-recently-active ephemeral session %2")
                                      .arg(m_sessions.size()).arg(victim->id()));
        m_sessions.remove(victim->id());
        // Deliberately NOT recorded as terminated. Eviction is resource pressure
        // on our side, not the end of the client's session, and the victim is by
        // construction an ephemeral cloud-connector session — the exact client
        // the auto-recovery path exists for. 404ing it would break the case that
        // path was added to fix.
        delete victim;
        emit activeSessionCountChanged();
    }

    // Cap only the *stateful* (live-SSE) sessions — the ones that hold retained
    // server-side state. Ephemeral POST-only sessions (cloud connectors that
    // re-initialize per request and never hold an SSE stream) are not counted,
    // so they can never trip "Too many sessions" and block another client.
    // Stateful sessions are additionally bounded by MaxSseConnections (4) at the
    // SSE-establishment path, and MaxSseConnections < MaxSessions (8), so this is
    // a safety ceiling that is not reachable in normal operation.
    const int stateful = statefulSessionCount();
    if (stateful >= MaxSessions) {
        MCP_WARN_TAGGED("Server", QStringLiteral("Too many stateful sessions (%1 stateful, %2 total)")
                                      .arg(stateful).arg(m_sessions.size()));
        return nullptr;
    }

    auto* session = new McpSession(this);
    m_sessions[session->id()] = session;
    emit activeSessionCountChanged();
    MCP_INFO_TAGGED("Server", QStringLiteral("Created session %1").arg(session->id()));
    return session;
}

McpSession* McpServer::findSession(const QString& sessionId)
{
    return m_sessions.value(sessionId, nullptr);
}

int McpServer::statefulSessionCount() const
{
    int n = 0;
    for (const McpSession* s : std::as_const(m_sessions))
        if (s->isStateful())
            ++n;
    return n;
}

void McpServer::cleanupExpiredSessions()
{
    QDateTime now = QDateTime::currentDateTimeUtc();
    QStringList expired;

    for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it) {
        qint64 inactiveSecs = it.value()->lastActivity().secsTo(now);
        if (inactiveSecs > SessionTimeoutMinutes * 60)
            expired.append(it.key());
    }

    for (const QString& id : expired) {
        MCP_INFO_TAGGED("Server", QStringLiteral("Expiring session %1").arg(id));
        // Clear pending confirmation if it belongs to this expired session
        if (m_pendingConfirmation.has_value() && m_pendingConfirmation->sessionId == id) {
            MCP_WARN_TAGGED("Server", QStringLiteral("Cancelling pending confirmation for "
                                                     "expired session %1").arg(id));
            m_pendingConfirmation.reset();
        }
        delete m_sessions.take(id);
        recordTerminatedSession(id);
    }

    if (!expired.isEmpty())
        emit activeSessionCountChanged();
}

void McpServer::recordTerminatedSession(const QString& sessionId)
{
    if (sessionId.isEmpty())
        return;
    m_terminatedSessions.append(sessionId);
    while (m_terminatedSessions.size() > MaxTerminatedSessions)
        m_terminatedSessions.removeFirst();
}

void McpServer::confirmationResolved(const QString& sessionId, bool accepted)
{
    if (!m_pendingConfirmation.has_value()) {
        MCP_WARN_TAGGED("Server", QStringLiteral("confirmationResolved but no pending confirmation"));
        return;
    }

    auto pending = m_pendingConfirmation.value();

    if (pending.sessionId != sessionId) {
        MCP_WARN_TAGGED("Server", QStringLiteral("confirmation session mismatch, expected %1 got %2")
                                      .arg(pending.sessionId, sessionId));
        // Don't reset m_pendingConfirmation — a newer valid confirmation may be pending.
        // This can happen when a stale QML callback arrives after a superseded dialog.
        return;
    }

    m_pendingConfirmation.reset();

    if (!pending.socket || pending.socket->state() != QAbstractSocket::ConnectedState) {
        MCP_WARN_TAGGED("Server", QStringLiteral("confirmation socket disconnected, dropping "
                                                 "response for %1").arg(pending.toolName));
        return;
    }

    if (!accepted) {
        MCP_INFO_TAGGED("Server", QStringLiteral("User denied %1").arg(pending.toolName));
        QJsonObject deniedPayload;
        deniedPayload["error"] = "User denied confirmation for " + pending.toolName;

        // `isError` is set by buildToolCallResponse off the `error` key above.
        sendJsonRpcResponse(pending.socket,
                            buildToolCallResponse(deniedPayload, pending.protocolVersion),
                            pending.requestId, pending.sessionId);
        return;
    }

    MCP_INFO_TAGGED("Server", QStringLiteral("User confirmed %1").arg(pending.toolName));

    // Async tools: dispatch to background thread
    if (m_toolRegistry->isAsyncTool(pending.toolName)) {
        QPointer<QTcpSocket> socketPtr(pending.socket);
        QString error;
        McpRegistryFailure failure = McpRegistryFailure::None;
        bool dispatched = m_toolRegistry->callAsyncTool(
            pending.toolName, pending.arguments, pending.accessLevel, error,
            [this, socketPtr, reqId = pending.requestId, sessId = pending.sessionId,
             protoVer = pending.protocolVersion](QJsonObject toolResult) {
                sendAsyncToolResponse(socketPtr, reqId, sessId, protoVer, toolResult);
            }, &failure);
        if (!dispatched) {
            sendJsonRpcResponse(pending.socket, registryErrorResult(error, failure),
                                pending.requestId, pending.sessionId);
        }
        return;
    }

    // Synchronous tools
    QString error;
    McpRegistryFailure failure = McpRegistryFailure::None;
    QJsonObject toolResult = m_toolRegistry->callTool(
        pending.toolName, pending.arguments, pending.accessLevel, error, &failure);

    if (!error.isEmpty()) {
        sendJsonRpcResponse(pending.socket, registryErrorResult(error, failure),
                            pending.requestId, pending.sessionId);
        return;
    }

    sendJsonRpcResponse(pending.socket,
                        buildToolCallResponse(toolResult, pending.protocolVersion),
                        pending.requestId, pending.sessionId);
}

void McpServer::sendAsyncToolResponse(QPointer<QTcpSocket> socket, const QVariant& requestId,
                                       const QString& sessionId, const QString& protocolVersion,
                                       const QJsonObject& toolResult)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        MCP_WARN_TAGGED("Server", QStringLiteral("async tool response dropped (socket disconnected)"));
        return;
    }

    sendJsonRpcResponse(socket, buildToolCallResponse(toolResult, protocolVersion),
                        requestId, sessionId);
}

bool McpServer::needsInAppConfirmation(const QString& toolName) const
{
    if (!m_settings) return false;
    int level = m_settings->mcp()->mcpConfirmationLevel();
    if (level == 0) return false;
    // machine_start_* requires in-app confirmation at any non-zero confirmation level
    return toolName.startsWith("machine_start_");
}

bool McpServer::needsChatConfirmation(const QString& toolName) const
{
    if (!m_settings) return false;
    int level = m_settings->mcp()->mcpConfirmationLevel();
    if (level == 0) return false;

    // All non-zero levels: confirm tools that cause IRREVERSIBLE data loss or
    // change PERSISTENT device/startup state. (Reversible/routine ops —
    // tare, timers, scan, connect, theme, backup, mqtt — are deliberately
    // NOT here: prompt fatigue would erode the safety net's value.) The MCP
    // server is network-exposed, so an ungated destructive tool is remotely
    // invocable with no operator prompt — that is the threat model here.
    //
    // Confirmation is enforced HERE (server-side); handlers must NEVER check
    // `confirmed` themselves — McpServer strips it before the handler runs.
    // A handler-side check is unreachable-true and was the shipped #1219 bug.
    if (toolName == "profiles_set_active" || toolName == "profiles_edit_params" ||
        toolName == "profiles_save" || toolName == "profiles_delete" ||
        toolName == "profiles_create" || toolName == "shots_delete" ||
        toolName == "settings_set" ||
        toolName == "devices_set_scale_priority_mode" ||
        toolName == "devices_reset_scale_priority" ||
        // Irreversible learning/calibration wipes + forget-the-scale (the
        // last also advertises a `confirmed` arg that was never enforced —
        // same class as the #1219 bug above).
        toolName == "reset_saw_learning" ||
        toolName == "reset_saw_learning_for_profile" ||
        toolName == "clear_flow_calibration" ||
        // Overwrites the profile's learned calibration with a hand-picked number,
        // and it reaches the machine immediately — the old value is not recoverable.
        toolName == "set_flow_calibration" ||
        toolName == "devices_disconnect_scale")
        return true;

    // Level 2 (All Control): also non-start machine control ops
    if (level >= 2) {
        if (toolName == "machine_wake" || toolName == "machine_sleep" ||
            toolName == "machine_stop" || toolName == "machine_skip_frame")
            return true;
    }
    return false;
}

QString McpServer::confirmationDescription(const QString& toolName) const
{
    static const QHash<QString, QString> descriptions = {
        {"machine_start_espresso", "Start pulling an espresso shot"},
        {"machine_start_steam", "Start steaming milk"},
        {"machine_start_hot_water", "Dispense hot water"},
        {"machine_start_flush", "Flush the group head"},
        {"machine_wake", "Wake the machine from sleep"},
        {"machine_sleep", "Put the machine to sleep"},
        {"machine_stop", "Stop the current operation"},
        {"machine_skip_frame", "Skip to next profile frame"},
        {"profiles_set_active", "Activate a different profile"},
        {"profiles_edit_params", "Edit profile parameters"},
        {"profiles_save", "Save profile to disk"},
        {"profiles_delete", "Delete a profile"},
        {"profiles_create", "Create a new profile"},
        {"shots_delete", "Delete a shot permanently"},
        {"settings_set", "Change machine settings"},
        {"devices_set_scale_priority_mode",
         "Change the scale connection-priority backoff policy (enforce/observe)"},
        {"devices_reset_scale_priority",
         "Clear the scale connection-priority backoff latch"},
        {"reset_saw_learning",
         "Erase ALL stop-at-weight learning (global pool, every profile/scale "
         "history, bootstrap) — irreversible"},
        {"reset_saw_learning_for_profile",
         "Erase stop-at-weight learning for one profile/scale pair — irreversible"},
        {"clear_flow_calibration",
         "Clear the profile's flow calibration (re-learned over future shots)"},
        {"set_flow_calibration",
         "Overwrite the profile's flow calibration with a hand-set multiplier"},
        {"devices_disconnect_scale",
         "Disconnect and forget the saved scale (must be re-paired)"},
    };
    return descriptions.value(toolName, toolName);
}

void McpServer::sendJsonRpcResponse(QTcpSocket* socket, const QJsonObject& result,
                                     const QVariant& id, const QString& sessionId)
{
    QJsonObject response;
    response["jsonrpc"] = "2.0";
    response["id"] = QJsonValue::fromVariant(id);

    // A top-level `error` means a JSON-RPC error response. Two kinds of caller
    // hand back a raw {error: {code, message}} and land here:
    //   - plain methods — handleJsonRpc's unknown-method fallback, and the
    //     resources/read|subscribe|unsubscribe handlers;
    //   - tools/call faults that happen BEFORE dispatch — rate limit, async
    //     dispatch failure, tool-registry error (both in handleToolsCall and in
    //     confirmationResolved's confirmed-tool continuation).
    // The second group is what MCP means by "errors in _finding_ the tool … or
    // any other exceptional conditions" (schema 2025-11-25, CallToolResult.isError):
    // no tool ran, so there is no tool result to carry a failure.
    //
    // What CANNOT reach this branch is a WRAPPED tool payload. buildToolCallResponse
    // returns only {content, structuredContent, isError}, so once a tool has run,
    // its own `error` key is one level down — inside `structuredContent` at
    // 2025-06-18+, and at older versions surviving only as text inside the
    // serialized JSON of the text block. Do not "fix" that by unwrapping here: a
    // tool that ran and failed is a successful protocol exchange carrying a failed
    // tool result, so it must stay a JSON-RPC `result` with `isError: true` (set at
    // the wrap site). Emitting a JSON-RPC error instead would drop content[]
    // entirely and with it the error text the model needs to self-correct.
    if (result.contains("error")) {
        response["error"] = result["error"];
    } else {
        response["result"] = result;
    }

    QByteArray body = QJsonDocument(response).toJson(QJsonDocument::Compact);
    sendHttpResponse(socket, 200, body, "application/json", sessionId);
}

void McpServer::sendJsonRpcError(QTcpSocket* socket, int code, const QString& message,
                                  const QVariant& id, const QString& sessionId)
{
    const QByteArray body =
        QJsonDocument(makeJsonRpcError(code, message, id)).toJson(QJsonDocument::Compact);
    sendHttpResponse(socket, 200, body, "application/json", sessionId);
}

static const char* httpStatusText(int code)
{
    switch (code) {
    case 200: return "OK";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 429: return "Too Many Requests";
    default:  return "Unknown";
    }
}

void McpServer::sendHttpResponse(QTcpSocket* socket, int statusCode,
                                  const QByteArray& body, const QString& contentType,
                                  const QString& sessionId,
                                  const QList<QPair<QByteArray, QByteArray>>& extraHeaders)
{
    // Every response leaves through here, so a silent drop here is a request the
    // client never hears about at all — the async paths log their drops, this one
    // did not.
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        MCP_WARN_TAGGED("Server", QStringLiteral("response dropped, socket not connected "
                                                 "(status %1, %2 bytes)")
                                      .arg(statusCode).arg(body.size()));
        return;
    }

    QByteArray response;
    response.append("HTTP/1.1 ");
    response.append(QByteArray::number(statusCode));
    response.append(" ");
    response.append(httpStatusText(statusCode));
    response.append("\r\n");

    // RFC 7231: 204 must NOT include Content-Type or Content-Length
    if (statusCode != 204) {
        response.append("Content-Type: " + contentType.toUtf8() + "\r\n");
        response.append("Content-Length: " + QByteArray::number(body.size()) + "\r\n");
    }

    // Send both session header names for maximum client compatibility
    if (!sessionId.isEmpty()) {
        response.append("Mcp-Session-Id: " + sessionId.toUtf8() + "\r\n");
        response.append("Mcp-Session: " + sessionId.toUtf8() + "\r\n");
        // Echo the negotiated protocol version so clients can detect server
        // choice on raw HTTP debugging. Per 2025-06-18 the header is required
        // on requests; emitting it on responses is informational.
        if (auto* s = m_sessions.value(sessionId, nullptr))
            response.append("MCP-Protocol-Version: " + s->protocolVersion().toUtf8() + "\r\n");
    }

    // Echo the validated request Origin back if one was supplied; otherwise
    // fall back to `*` for non-browser clients (mcp-remote, curl, MCP Inspector
    // CLI). Echo-back lets browsers send credentials with `Allow-Credentials`.
    const QString reqOrigin = socket ? socket->property("mcpOrigin").toString() : QString();
    if (!reqOrigin.isEmpty()) {
        response.append("Access-Control-Allow-Origin: " + reqOrigin.toUtf8() + "\r\n");
        response.append("Access-Control-Allow-Credentials: true\r\n");
        response.append("Vary: Origin\r\n");
    } else {
        response.append("Access-Control-Allow-Origin: *\r\n");
    }
    response.append("Access-Control-Expose-Headers: Mcp-Session-Id, Mcp-Session, MCP-Protocol-Version\r\n");

    for (const auto& header : extraHeaders)
        response.append(header.first + ": " + header.second + "\r\n");

    response.append("\r\n");
    if (statusCode != 204)
        response.append(body);

    // QIODevice::write is not [[nodiscard]], so nothing forces this check. A short
    // write ships a truncated body the client reports as a parse error, with no
    // server-side counterpart to correlate it against.
    const qint64 written = socket->write(response);
    if (written != response.size()) {
        MCP_WARN_TAGGED("Server", QStringLiteral("short write: %1 of %2 bytes (status %3)")
                                      .arg(written).arg(response.size()).arg(statusCode));
    }
    socket->flush();
}
