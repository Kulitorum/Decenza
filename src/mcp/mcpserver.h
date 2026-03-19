#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QPointer>
#include <QSet>

class McpSession;
class McpToolRegistry;
class McpResourceRegistry;
class DE1Device;
class MachineState;
class MainController;
class ShotHistoryStorage;
class BLEManager;
class Settings;

class McpServer : public QObject {
    Q_OBJECT
    Q_PROPERTY(int activeSessionCount READ activeSessionCount NOTIFY activeSessionCountChanged)

public:
    explicit McpServer(QObject* parent = nullptr);
    ~McpServer();

    // Dependency injection
    void setDE1Device(DE1Device* device) { m_device = device; }
    void setMachineState(MachineState* state) { m_machineState = state; }
    void setMainController(MainController* controller) { m_mainController = controller; }
    void setShotHistoryStorage(ShotHistoryStorage* storage) { m_shotHistory = storage; }
    void setBLEManager(BLEManager* ble) { m_bleManager = ble; }
    void setSettings(Settings* settings) { m_settings = settings; }

    // Called by ShotServer for /mcp routes
    void handleHttpRequest(QTcpSocket* socket, const QString& method,
                           const QString& path, const QByteArray& headers,
                           const QByteArray& body);

    int activeSessionCount() const { return static_cast<int>(m_sessions.size()); }

    // Register all tools and resources — called after dependencies are set
    void registerAllTools();
    void registerAllResources();
    void connectSseNotifications();

    // Registries (accessible for tool/resource registration in later phases)
    McpToolRegistry* toolRegistry() const { return m_toolRegistry; }
    McpResourceRegistry* resourceRegistry() const { return m_resourceRegistry; }

signals:
    void activeSessionCountChanged();
    void confirmationRequested(const QString& toolName, const QString& toolDescription,
                               const QString& sessionId);

public slots:
    void confirmationResolved(const QString& sessionId, bool accepted);

private:
    // JSON-RPC dispatch
    QJsonObject handleJsonRpc(const QJsonObject& request, McpSession* session);
    QJsonObject handleInitialize(const QJsonObject& params, McpSession* session);
    QJsonObject handleToolsList(const QJsonObject& params, McpSession* session);
    QJsonObject handleToolsCall(const QJsonObject& params, McpSession* session);
    QJsonObject handleResourcesList(const QJsonObject& params, McpSession* session);
    QJsonObject handleResourcesRead(const QJsonObject& params, McpSession* session);

    // Session management
    McpSession* findOrCreateSession(const QString& sessionHeader);
    McpSession* findSession(const QString& sessionId);
    void cleanupExpiredSessions();

    // Response helpers
    void sendJsonRpcResponse(QTcpSocket* socket, const QJsonObject& result,
                             const QVariant& id, const QString& sessionId);
    void sendJsonRpcError(QTcpSocket* socket, int code, const QString& message,
                          const QVariant& id, const QString& sessionId = QString());
    void sendHttpResponse(QTcpSocket* socket, int statusCode,
                          const QByteArray& body, const QString& contentType,
                          const QString& sessionId = QString());

    // Dependencies
    DE1Device* m_device = nullptr;
    MachineState* m_machineState = nullptr;
    MainController* m_mainController = nullptr;
    ShotHistoryStorage* m_shotHistory = nullptr;
    BLEManager* m_bleManager = nullptr;
    Settings* m_settings = nullptr;

    // Registries
    McpToolRegistry* m_toolRegistry;
    McpResourceRegistry* m_resourceRegistry;

    // Sessions
    QHash<QString, McpSession*> m_sessions;
    QTimer* m_cleanupTimer;

    // Rate limiting
    QTimer* m_rateLimitTimer;

    // SSE clients
    QSet<QTcpSocket*> m_sseClients;
    void broadcastSseNotification(const QString& resourceUri);

    // Limits
    static constexpr int MaxSessions = 8;
    static constexpr int MaxSseConnections = 4;
    static constexpr int SessionTimeoutMinutes = 30;
    static constexpr int RateLimitPerMinute = 10;
};
