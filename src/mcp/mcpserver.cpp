#include "mcpserver.h"
#include "mcpsession.h"
#include "mcptoolregistry.h"
#include "mcpresourceregistry.h"
#include "../core/settings.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../controllers/maincontroller.h"
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
                          MachineState* machineState, MainController* mainController);
void registerShotTools(McpToolRegistry* registry, ShotHistoryStorage* shotHistory);
void registerProfileTools(McpToolRegistry* registry, MainController* mainController);
void registerSettingsReadTools(McpToolRegistry* registry, Settings* settings);
void registerDialingTools(McpToolRegistry* registry, MainController* mainController,
                          ShotHistoryStorage* shotHistory, Settings* settings);
void registerControlTools(McpToolRegistry* registry, DE1Device* device, MachineState* machineState);
void registerWriteTools(McpToolRegistry* registry, MainController* mainController,
                        ShotHistoryStorage* shotHistory, Settings* settings);
void registerScaleTools(McpToolRegistry* registry, MachineState* machineState);
void registerDeviceTools(McpToolRegistry* registry, BLEManager* bleManager, DE1Device* device);
class MemoryMonitor;
void registerDebugTools(McpToolRegistry* registry, MemoryMonitor* memoryMonitor);
void registerMcpResources(McpResourceRegistry* registry, DE1Device* device,
                          MachineState* machineState, MainController* mainController,
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
    registerMachineTools(m_toolRegistry, m_device, m_machineState, m_mainController);
    registerShotTools(m_toolRegistry, m_shotHistory);
    registerProfileTools(m_toolRegistry, m_mainController);
    registerSettingsReadTools(m_toolRegistry, m_settings);
    registerDialingTools(m_toolRegistry, m_mainController, m_shotHistory, m_settings);
    registerControlTools(m_toolRegistry, m_device, m_machineState);
    registerWriteTools(m_toolRegistry, m_mainController, m_shotHistory, m_settings);
    registerScaleTools(m_toolRegistry, m_machineState);
    registerDeviceTools(m_toolRegistry, m_bleManager, m_device);
    registerDebugTools(m_toolRegistry, m_memoryMonitor);
    qDebug() << "McpServer: Registered" << m_toolRegistry->listTools(2).size() << "tools";
}

void McpServer::registerAllResources()
{
    registerMcpResources(m_resourceRegistry, m_device, m_machineState, m_mainController, m_shotHistory, m_memoryMonitor);
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
    if (m_mainController) {
        connect(m_mainController, &MainController::currentProfileChanged, this, [this]() {
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

    QList<QTcpSocket*> dead;
    for (QTcpSocket* client : std::as_const(m_sseClients)) {
        if (client->state() != QAbstractSocket::ConnectedState) {
            dead.append(client);
            continue;
        }
        client->write(event);
        client->flush();
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

        // Initialize can come without a session — creates one
        McpSession* session = nullptr;
        if (rpcMethod == "initialize") {
            session = findOrCreateSession(QString()); // always create new
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
            if (!session) {
                sendJsonRpcError(socket, -32600, "Invalid session",
                                 request["id"].toVariant(), sessionHeader);
                return;
            }
            if (!session->initialized() && rpcMethod != "notifications/initialized") {
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

        QJsonObject result = handleJsonRpc(request, session);
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
            // Not an SSE request — return server info (used by mcp-remote for transport discovery)
            sendHttpResponse(socket, 405, "Method not allowed. Use POST for JSON-RPC.", "text/plain");
            return;
        }

        // SSE stream for server-initiated notifications
        if (static_cast<int>(m_sseClients.size()) >= MaxSseConnections) {
            sendHttpResponse(socket, 429, "Too many SSE connections", "text/plain");
            return;
        }

        // Send SSE headers
        QByteArray response;
        response.append("HTTP/1.1 200 OK\r\n");
        response.append("Content-Type: text/event-stream\r\n");
        response.append("Cache-Control: no-cache\r\n");
        response.append("Connection: keep-alive\r\n");
        response.append("Access-Control-Allow-Origin: *\r\n");
        response.append("Access-Control-Expose-Headers: Mcp-Session-Id, Mcp-Session\r\n");
        response.append("\r\n");
        socket->write(response);
        socket->flush();

        m_sseClients.insert(socket);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_sseClients.remove(socket);
            qDebug() << "McpServer: SSE client disconnected, remaining:" << m_sseClients.size();
        });
        qDebug() << "McpServer: SSE client connected, total:" << m_sseClients.size();

    } else if (method == "DELETE") {
        // Terminate session
        McpSession* session = findSession(sessionHeader);
        if (session) {
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

QJsonObject McpServer::handleJsonRpc(const QJsonObject& request, McpSession* session)
{
    QString method = request["method"].toString();
    QJsonObject params = request["params"].toObject();

    if (method == "initialize")
        return handleInitialize(params, session);
    if (method == "tools/list")
        return handleToolsList(params, session);
    if (method == "tools/call")
        return handleToolsCall(params, session);
    if (method == "resources/list")
        return handleResourcesList(params, session);
    if (method == "resources/read")
        return handleResourcesRead(params, session);

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

QJsonObject McpServer::handleToolsCall(const QJsonObject& params, McpSession* session)
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

    // Count successful control/settings calls
    if (category == "control" || category == "settings")
        session->incrementControlCalls();

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

QJsonObject McpServer::handleResourcesRead(const QJsonObject& params, McpSession* session)
{
    Q_UNUSED(session)

    QString uri = params["uri"].toString();
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

McpSession* McpServer::findOrCreateSession(const QString& sessionHeader)
{
    Q_UNUSED(sessionHeader)

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
        delete m_sessions.take(id);
    }

    if (!expired.isEmpty())
        emit activeSessionCountChanged();
}

void McpServer::confirmationResolved(const QString& sessionId, bool accepted)
{
    Q_UNUSED(sessionId)
    Q_UNUSED(accepted)
    // Placeholder — will be implemented with machine control tools (Phase 7)
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
