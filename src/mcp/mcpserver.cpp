#include "mcpserver.h"
#include "mcpsession.h"
#include "mcptoolregistry.h"
#include "mcpresourceregistry.h"
#include "../core/settings.h"
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

// Tool registration functions (implemented in mcptools_*.cpp)
void registerMachineTools(McpToolRegistry* registry, DE1Device* device,
                          MachineState* machineState, MainController* mainController,
                          ProfileManager* profileManager);
void registerShotTools(McpToolRegistry* registry, ShotHistoryStorage* shotHistory);
class ProfileManager;
void registerProfileTools(McpToolRegistry* registry, ProfileManager* profileManager);
class AccessibilityManager;
class ScreensaverVideoManager;
class TranslationManager;
class BatteryManager;
void registerSettingsReadTools(McpToolRegistry* registry, Settings* settings,
                               AccessibilityManager* accessibility,
                               ScreensaverVideoManager* screensaver,
                               TranslationManager* translation,
                               BatteryManager* battery);
void registerDialingTools(McpToolRegistry* registry, MainController* mainController,
                          ProfileManager* profileManager,
                          ShotHistoryStorage* shotHistory, Settings* settings);
void registerControlTools(McpToolRegistry* registry, DE1Device* device, MachineState* machineState,
                          ProfileManager* profileManager);
void registerWriteTools(McpToolRegistry* registry, ProfileManager* profileManager,
                        ShotHistoryStorage* shotHistory, Settings* settings,
                        AccessibilityManager* accessibility,
                        ScreensaverVideoManager* screensaver,
                        TranslationManager* translation,
                        BatteryManager* battery);
void registerScaleTools(McpToolRegistry* registry, MachineState* machineState);
void registerDeviceTools(McpToolRegistry* registry, BLEManager* bleManager, DE1Device* device);
class MemoryMonitor;
void registerDebugTools(McpToolRegistry* registry, MemoryMonitor* memoryMonitor);
void registerMcpResources(McpResourceRegistry* registry, DE1Device* device,
                          MachineState* machineState, ProfileManager* profileManager,
                          ShotHistoryStorage* shotHistory, MemoryMonitor* memoryMonitor);

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
}

void McpServer::registerAllTools()
{
    registerMachineTools(m_toolRegistry, m_device, m_machineState, m_mainController, m_profileManager);
    registerShotTools(m_toolRegistry, m_shotHistory);
    registerProfileTools(m_toolRegistry, m_profileManager);
    registerSettingsReadTools(m_toolRegistry, m_settings, m_accessibilityManager,
                              m_screensaverManager, m_translationManager, m_batteryManager);
    registerDialingTools(m_toolRegistry, m_mainController, m_profileManager, m_shotHistory, m_settings);
    registerControlTools(m_toolRegistry, m_device, m_machineState, m_profileManager);
    registerWriteTools(m_toolRegistry, m_profileManager, m_shotHistory, m_settings,
                       m_accessibilityManager, m_screensaverManager,
                       m_translationManager, m_batteryManager);
    registerScaleTools(m_toolRegistry, m_machineState);
    registerDeviceTools(m_toolRegistry, m_bleManager, m_device);
    registerDebugTools(m_toolRegistry, m_memoryMonitor);
    qDebug() << "McpServer: Registered" << m_toolRegistry->listTools(2).size() << "tools";
}

void McpServer::registerAllResources()
{
    registerMcpResources(m_resourceRegistry, m_device, m_machineState, m_profileManager, m_shotHistory, m_memoryMonitor);
    qDebug() << "McpServer: Registered" << m_resourceRegistry->listResources().size() << "resources";
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

    QByteArray event;
    event.append("event: message\n");
    event.append("data: ");
    event.append(QJsonDocument(notification).toJson(QJsonDocument::Compact));
    event.append("\n\n");

    // Send only to sessions that subscribed to this resource URI.
    // Sessions without any subscriptions receive all notifications (backward compat).
    QList<QTcpSocket*> dead;
    for (QTcpSocket* client : std::as_const(m_sseClients)) {
        if (client->state() != QAbstractSocket::ConnectedState) {
            dead.append(client);
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
    for (QTcpSocket* client : dead)
        m_sseClients.remove(client);
}

McpServer::~McpServer()
{
    qDeleteAll(m_sessions);
}

void McpServer::handleHttpRequest(QTcpSocket* socket, const QString& method,
                                   const QString& path, const QByteArray& headers,
                                   const QByteArray& body)
{
    Q_UNUSED(path)

    // Extract Mcp-Session or Mcp-Session-Id header (spec uses Mcp-Session-Id,
    // but some clients send Mcp-Session — accept both for compatibility)
    QString sessionHeader;
    for (const QByteArray& line : headers.split('\n')) {
        QByteArray lower = line.trimmed().toLower();
        if (lower.startsWith("mcp-session-id:") || lower.startsWith("mcp-session:")) {
            sessionHeader = QString::fromUtf8(line.mid(line.indexOf(':') + 1).trimmed());
            break;
        }
    }

    if (method == "POST") {
        // JSON-RPC request
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            sendJsonRpcError(socket, -32700, "Parse error", QJsonValue::Null);
            return;
        }

        QJsonObject request = doc.object();
        QString rpcMethod = request["method"].toString();

        // Initialize can come without a session — creates one.
        // Pass sessionHeader so reconnecting clients reuse their existing session.
        McpSession* session = nullptr;
        if (rpcMethod == "initialize") {
            session = findOrCreateSession(sessionHeader);
            if (!session) {
                sendJsonRpcError(socket, -32000, "Too many sessions",
                                 request["id"].toVariant(), sessionHeader);
                return;
            }
        } else {
            session = findSession(sessionHeader);
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
                    qDebug() << "McpServer: Stale session header, reusing sole session" << session->id();
                } else {
                    qDebug() << "McpServer: Session not found (expired or stale), auto-creating new session";
                    session = findOrCreateSession(QString());
                    if (!session) {
                        sendJsonRpcError(socket, -32000, "Too many sessions",
                                         request["id"].toVariant(), sessionHeader);
                        return;
                    }
                    // Mark as initialized — the client already completed initialize
                    // in a prior session, so skip the handshake requirement
                    session->setInitialized(true);
                }
            }
            if (!session->initialized() && rpcMethod != "notifications/initialized"
                && rpcMethod != "ping") {
                sendJsonRpcError(socket, -32600, "Session not initialized",
                                 request["id"].toVariant(), session->id());
                return;
            }
        }

        session->touch();

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

        // SSE stream for server-initiated notifications
        if (static_cast<int>(m_sseClients.size()) >= MaxSseConnections) {
            sendHttpResponse(socket, 429, "Too many SSE connections", "text/plain");
            return;
        }

        // Associate SSE socket with session if the client sent a session header
        McpSession* sseSession = findSession(sessionHeader);

        // Send SSE headers (include session ID if known)
        QByteArray response;
        response.append("HTTP/1.1 200 OK\r\n");
        response.append("Content-Type: text/event-stream\r\n");
        response.append("Cache-Control: no-cache\r\n");
        response.append("Connection: keep-alive\r\n");
        response.append("Access-Control-Allow-Origin: *\r\n");
        response.append("Access-Control-Expose-Headers: Mcp-Session-Id, Mcp-Session\r\n");
        if (sseSession) {
            response.append("Mcp-Session-Id: " + sseSession->id().toUtf8() + "\r\n");
            response.append("Mcp-Session: " + sseSession->id().toUtf8() + "\r\n");
        }
        response.append("\r\n");
        socket->write(response);
        socket->flush();

        m_sseClients.insert(socket);
        if (sseSession)
            sseSession->setSseSocket(socket);

        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_sseClients.remove(socket);
            // Clear the session's SSE socket reference — the client may reconnect
            // SSE without re-initializing, so keep the session alive.
            for (auto* s : std::as_const(m_sessions)) {
                if (s->sseSocket() == socket) {
                    s->setSseSocket(nullptr);
                    break;
                }
            }
            qDebug() << "McpServer: SSE client disconnected, remaining:" << m_sseClients.size();
        });
        qDebug() << "McpServer: SSE client connected, total:" << m_sseClients.size();

    } else if (method == "DELETE") {
        // Terminate session
        McpSession* session = findSession(sessionHeader);
        if (session) {
            // Clear pending confirmation if it belongs to this session
            if (m_pendingConfirmation.has_value() && m_pendingConfirmation->sessionId == session->id())
                m_pendingConfirmation.reset();
            m_sessions.remove(session->id());
            delete session;
            emit activeSessionCountChanged();
        }
        sendHttpResponse(socket, 200, "{}", "application/json");

    } else if (method == "OPTIONS") {
        // CORS preflight
        sendHttpResponse(socket, 204, "", "", QString(),
                         {{"Access-Control-Allow-Methods", "POST, GET, DELETE, OPTIONS"},
                          {"Access-Control-Allow-Headers", "Content-Type, Authorization, Mcp-Session, Mcp-Session-Id"},
                          {"Access-Control-Max-Age", "86400"}});

    } else {
        sendHttpResponse(socket, 405, "Method not allowed", "text/plain");
    }
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

    // Negotiate protocol version — accept what the client requests if we support it
    QString clientVersion = params["protocolVersion"].toString();
    static const QStringList supportedVersions = {"2025-03-26", "2024-11-05"};
    QString negotiatedVersion = supportedVersions.contains(clientVersion)
        ? clientVersion : supportedVersions.first();

    QJsonObject result;
    result["protocolVersion"] = negotiatedVersion;
    result["capabilities"] = serverCapabilities;
    result["serverInfo"] = serverInfo;
    return result;
}

QJsonObject McpServer::handleToolsList(const QJsonObject& params, McpSession* session)
{
    Q_UNUSED(params)
    Q_UNUSED(session)

    int accessLevel = m_settings ? m_settings->mcpAccessLevel() : 0;

    QJsonObject result;
    result["tools"] = m_toolRegistry->listTools(accessLevel);
    return result;
}

QJsonObject McpServer::handleToolsCall(const QJsonObject& params, McpSession* session,
                                       QTcpSocket* socket, const QVariant& requestId)
{
    QString toolName = params["name"].toString();
    QJsonObject arguments = params["arguments"].toObject();

    int accessLevel = m_settings ? m_settings->mcpAccessLevel() : 0;

    // Rate limiting for control + settings tools
    QString category = m_toolRegistry->toolCategory(toolName);
    if (category == "control" || category == "settings") {
        if (session->controlCallCount() >= RateLimitPerMinute) {
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

        QJsonObject result;
        QJsonArray content;
        QJsonObject textContent;
        textContent["type"] = "text";
        textContent["text"] = QString::fromUtf8(QJsonDocument(confirmPayload).toJson(QJsonDocument::Compact));
        content.append(textContent);
        result["content"] = content;
        return result;
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
                sendJsonRpcError(old.socket, -32000, "Confirmation superseded by newer request",
                                 old.requestId, old.sessionId);
                qDebug() << "McpServer: Superseded pending confirmation for" << old.toolName;
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

        QString error;
        bool dispatched = m_toolRegistry->callAsyncTool(
            toolName, arguments, accessLevel, error,
            [this, socketPtr, reqId, sessId](QJsonObject toolResult) {
                sendAsyncToolResponse(socketPtr, reqId, sessId, toolResult);
            });

        if (!dispatched) {
            QJsonObject errorObj;
            errorObj["code"] = -32603;
            errorObj["message"] = error;
            QJsonObject result;
            result["error"] = errorObj;
            return result;
        }

        QJsonObject deferred;
        deferred["_deferred"] = true;
        return deferred;
    }

    // Synchronous tool
    QString error;
    QJsonObject toolResult = m_toolRegistry->callTool(toolName, arguments, accessLevel, error);

    if (!error.isEmpty()) {
        QJsonObject errorObj;
        errorObj["code"] = -32603;
        errorObj["message"] = error;
        QJsonObject result;
        result["error"] = errorObj;
        return result;
    }

    QJsonObject result;
    QJsonArray content;
    QJsonObject textContent;
    textContent["type"] = "text";
    textContent["text"] = QString::fromUtf8(QJsonDocument(toolResult).toJson(QJsonDocument::Compact));
    content.append(textContent);
    result["content"] = content;
    return result;
}

QJsonObject McpServer::handleResourcesList(const QJsonObject& params, McpSession* session)
{
    Q_UNUSED(params)
    Q_UNUSED(session)

    QJsonObject result;
    result["resources"] = m_resourceRegistry->listResources();
    return result;
}

QJsonObject McpServer::handleResourcesRead(const QJsonObject& params, McpSession* session,
                                            QTcpSocket* socket, const QVariant& requestId)
{
    Q_UNUSED(session)

    QString uri = params["uri"].toString();

    // Async resources: dispatch to background, send response later
    if (m_resourceRegistry->isAsyncResource(uri)) {
        QPointer<QTcpSocket> socketPtr(socket);
        QVariant reqId = requestId;
        QString sessId = session->id();

        QString error;
        bool dispatched = m_resourceRegistry->readAsyncResource(uri, error,
            [this, socketPtr, reqId, sessId, uri](QJsonObject resourceData) {
                if (!socketPtr || socketPtr->state() != QAbstractSocket::ConnectedState) {
                    qDebug() << "McpServer: async resource response dropped (socket disconnected)";
                    return;
                }

                QJsonObject result;
                QJsonArray contents;
                QJsonObject content;
                content["uri"] = uri;
                content["mimeType"] = "application/json";
                content["text"] = QString::fromUtf8(QJsonDocument(resourceData).toJson(QJsonDocument::Compact));
                contents.append(content);
                result["contents"] = contents;
                sendJsonRpcResponse(socketPtr, result, reqId, sessId);
            });

        if (!dispatched) {
            QJsonObject errorObj;
            errorObj["code"] = -32602;
            errorObj["message"] = error;
            QJsonObject result;
            result["error"] = errorObj;
            return result;
        }

        QJsonObject deferred;
        deferred["_deferred"] = true;
        return deferred;
    }

    QString error;
    QJsonObject resourceData = m_resourceRegistry->readResource(uri, error);

    if (!error.isEmpty()) {
        QJsonObject errorObj;
        errorObj["code"] = -32602;
        errorObj["message"] = error;
        QJsonObject result;
        result["error"] = errorObj;
        return result;
    }

    QJsonObject result;
    QJsonArray contents;
    QJsonObject content;
    content["uri"] = uri;
    content["mimeType"] = "application/json";
    content["text"] = QString::fromUtf8(QJsonDocument(resourceData).toJson(QJsonDocument::Compact));
    contents.append(content);
    result["contents"] = contents;
    return result;
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
    qDebug() << "McpServer: Session" << session->id() << "subscribed to" << uri;
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
    qDebug() << "McpServer: Session" << session->id() << "unsubscribed from" << uri;
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
            qDebug() << "McpServer: Reusing existing session" << sessionHeader;
            existing->touch();
            return existing;
        }
    }

    // Clean up orphaned sessions before creating a new one.
    // When mcp-remote's SSE connection drops and it re-initializes (without
    // sending a session header), the old session stays around with no SSE
    // socket. Over hours this fills the session quota. Remove sessions whose
    // SSE transport is gone — the client has already moved on.
    QStringList orphaned;
    for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it) {
        if (!it.value()->sseSocket())
            orphaned.append(it.key());
    }
    for (const QString& id : orphaned) {
        qDebug() << "McpServer: Removing orphaned session (no SSE socket)" << id;
        if (m_pendingConfirmation.has_value() && m_pendingConfirmation->sessionId == id)
            m_pendingConfirmation.reset();
        delete m_sessions.take(id);
    }
    if (!orphaned.isEmpty())
        emit activeSessionCountChanged();

    if (static_cast<int>(m_sessions.size()) >= MaxSessions) {
        qWarning() << "McpServer: Too many sessions (" << m_sessions.size() << ")";
        return nullptr;
    }

    auto* session = new McpSession(this);
    m_sessions[session->id()] = session;
    emit activeSessionCountChanged();
    qDebug() << "McpServer: Created session" << session->id();
    return session;
}

McpSession* McpServer::findSession(const QString& sessionId)
{
    return m_sessions.value(sessionId, nullptr);
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
        qDebug() << "McpServer: Expiring session" << id;
        // Clear pending confirmation if it belongs to this expired session
        if (m_pendingConfirmation.has_value() && m_pendingConfirmation->sessionId == id) {
            qDebug() << "McpServer: Cancelling pending confirmation for expired session" << id;
            m_pendingConfirmation.reset();
        }
        delete m_sessions.take(id);
    }

    if (!expired.isEmpty())
        emit activeSessionCountChanged();
}

void McpServer::confirmationResolved(const QString& sessionId, bool accepted)
{
    if (!m_pendingConfirmation.has_value()) {
        qWarning() << "McpServer: confirmationResolved but no pending confirmation";
        return;
    }

    auto pending = m_pendingConfirmation.value();

    if (pending.sessionId != sessionId) {
        qWarning() << "McpServer: confirmation session mismatch, expected"
                    << pending.sessionId << "got" << sessionId;
        // Don't reset m_pendingConfirmation — a newer valid confirmation may be pending.
        // This can happen when a stale QML callback arrives after a superseded dialog.
        return;
    }

    m_pendingConfirmation.reset();

    if (!pending.socket || pending.socket->state() != QAbstractSocket::ConnectedState) {
        qDebug() << "McpServer: confirmation socket disconnected, dropping response for"
                 << pending.toolName;
        return;
    }

    if (!accepted) {
        qDebug() << "McpServer: User denied" << pending.toolName;
        QJsonObject deniedPayload;
        deniedPayload["error"] = "User denied confirmation for " + pending.toolName;

        QJsonObject result;
        QJsonArray content;
        QJsonObject textContent;
        textContent["type"] = "text";
        textContent["text"] = QString::fromUtf8(QJsonDocument(deniedPayload).toJson(QJsonDocument::Compact));
        content.append(textContent);
        result["content"] = content;
        result["isError"] = true;
        sendJsonRpcResponse(pending.socket, result, pending.requestId, pending.sessionId);
        return;
    }

    qDebug() << "McpServer: User confirmed" << pending.toolName;

    // Async tools: dispatch to background thread
    if (m_toolRegistry->isAsyncTool(pending.toolName)) {
        QPointer<QTcpSocket> socketPtr(pending.socket);
        QString error;
        bool dispatched = m_toolRegistry->callAsyncTool(
            pending.toolName, pending.arguments, pending.accessLevel, error,
            [this, socketPtr, reqId = pending.requestId, sessId = pending.sessionId](QJsonObject toolResult) {
                sendAsyncToolResponse(socketPtr, reqId, sessId, toolResult);
            });
        if (!dispatched) {
            QJsonObject errorObj;
            errorObj["code"] = -32603;
            errorObj["message"] = error;
            QJsonObject result;
            result["error"] = errorObj;
            sendJsonRpcResponse(pending.socket, result, pending.requestId, pending.sessionId);
        }
        return;
    }

    // Synchronous tools
    QString error;
    QJsonObject toolResult = m_toolRegistry->callTool(
        pending.toolName, pending.arguments, pending.accessLevel, error);

    if (!error.isEmpty()) {
        QJsonObject errorObj;
        errorObj["code"] = -32603;
        errorObj["message"] = error;
        QJsonObject result;
        result["error"] = errorObj;
        sendJsonRpcResponse(pending.socket, result, pending.requestId, pending.sessionId);
        return;
    }

    QJsonObject result;
    QJsonArray content;
    QJsonObject textContent;
    textContent["type"] = "text";
    textContent["text"] = QString::fromUtf8(QJsonDocument(toolResult).toJson(QJsonDocument::Compact));
    content.append(textContent);
    result["content"] = content;
    sendJsonRpcResponse(pending.socket, result, pending.requestId, pending.sessionId);
}

void McpServer::sendAsyncToolResponse(QPointer<QTcpSocket> socket, const QVariant& requestId,
                                       const QString& sessionId, const QJsonObject& toolResult)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        qDebug() << "McpServer: async tool response dropped (socket disconnected)";
        return;
    }

    QJsonObject result;
    QJsonArray content;
    QJsonObject textContent;
    textContent["type"] = "text";
    textContent["text"] = QString::fromUtf8(QJsonDocument(toolResult).toJson(QJsonDocument::Compact));
    content.append(textContent);
    result["content"] = content;
    sendJsonRpcResponse(socket, result, requestId, sessionId);
}

bool McpServer::needsInAppConfirmation(const QString& toolName) const
{
    if (!m_settings) return false;
    int level = m_settings->mcpConfirmationLevel();
    if (level == 0) return false;
    // machine_start_* requires in-app confirmation at any non-zero confirmation level
    return toolName.startsWith("machine_start_");
}

bool McpServer::needsChatConfirmation(const QString& toolName) const
{
    if (!m_settings) return false;
    int level = m_settings->mcpConfirmationLevel();
    if (level == 0) return false;

    // All non-zero levels: settings/profile/dial-in write ops
    if (toolName == "profiles_set_active" || toolName == "profiles_edit_params" ||
        toolName == "profiles_save" || toolName == "profiles_delete" ||
        toolName == "profiles_create" || toolName == "shots_delete" ||
        toolName == "settings_set")
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
    };
    return descriptions.value(toolName, toolName);
}

void McpServer::sendJsonRpcResponse(QTcpSocket* socket, const QJsonObject& result,
                                     const QVariant& id, const QString& sessionId)
{
    QJsonObject response;
    response["jsonrpc"] = "2.0";
    response["id"] = QJsonValue::fromVariant(id);

    // Check if result contains an error
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
    QJsonObject response;
    response["jsonrpc"] = "2.0";
    response["id"] = QJsonValue::fromVariant(id);

    QJsonObject error;
    error["code"] = code;
    error["message"] = message;
    response["error"] = error;

    QByteArray body = QJsonDocument(response).toJson(QJsonDocument::Compact);
    sendHttpResponse(socket, 200, body, "application/json", sessionId);
}

static const char* httpStatusText(int code)
{
    switch (code) {
    case 200: return "OK";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 400: return "Bad Request";
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
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;

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
    }

    response.append("Access-Control-Allow-Origin: *\r\n");
    response.append("Access-Control-Expose-Headers: Mcp-Session-Id, Mcp-Session\r\n");

    for (const auto& header : extraHeaders)
        response.append(header.first + ": " + header.second + "\r\n");

    response.append("\r\n");
    if (statusCode != 204)
        response.append(body);

    socket->write(response);
    socket->flush();
}
