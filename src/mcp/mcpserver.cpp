#include "mcpserver.h"
#include "mcpsession.h"
#include "mcptoolregistry.h"
#include "mcpresourceregistry.h"
#include "../core/settings.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../controllers/maincontroller.h"
#include "../history/shothistorystorage.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>

// Tool registration functions (implemented in mcptools_*.cpp)
void registerMachineTools(McpToolRegistry* registry, DE1Device* device,
                          MachineState* machineState, MainController* mainController);
void registerShotTools(McpToolRegistry* registry, ShotHistoryStorage* shotHistory);
void registerProfileTools(McpToolRegistry* registry, MainController* mainController);
void registerSettingsReadTools(McpToolRegistry* registry, Settings* settings);

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
    qDebug() << "McpServer: Registered" << m_toolRegistry->listTools(2).size() << "tools";
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

    // Extract Mcp-Session header
    QString sessionHeader;
    for (const QByteArray& line : headers.split('\n')) {
        if (line.trimmed().toLower().startsWith("mcp-session:")) {
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

        // Notifications (no id, no response expected)
        if (!request.contains("id")) {
            // Handle notifications like "notifications/initialized"
            if (rpcMethod == "notifications/initialized") {
                // Client acknowledged initialization — nothing to do
            }
            return;
        }

        QJsonObject result = handleJsonRpc(request, session);
        sendJsonRpcResponse(socket, result, request["id"].toVariant(), session->id());

    } else if (method == "GET") {
        // SSE stream — not implemented yet (Phase 8)
        sendHttpResponse(socket, 501, "SSE not implemented yet", "text/plain");

    } else if (method == "DELETE") {
        // Terminate session
        McpSession* session = findSession(sessionHeader);
        if (session) {
            m_sessions.remove(session->id());
            delete session;
            emit activeSessionCountChanged();
        }
        sendHttpResponse(socket, 200, "{}", "application/json");

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

    QJsonObject result;
    result["protocolVersion"] = "2025-03-26";
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

void McpServer::sendHttpResponse(QTcpSocket* socket, int statusCode,
                                  const QByteArray& body, const QString& contentType,
                                  const QString& sessionId)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;

    QByteArray response;
    response.append("HTTP/1.1 ");
    response.append(QByteArray::number(statusCode));
    response.append(" OK\r\n");
    response.append("Content-Type: " + contentType.toUtf8() + "\r\n");
    response.append("Content-Length: " + QByteArray::number(body.size()) + "\r\n");
    if (!sessionId.isEmpty())
        response.append("Mcp-Session: " + sessionId.toUtf8() + "\r\n");
    response.append("Access-Control-Allow-Origin: *\r\n");
    response.append("\r\n");
    response.append(body);

    socket->write(response);
    socket->flush();
}
