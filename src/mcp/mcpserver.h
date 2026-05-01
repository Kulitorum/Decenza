#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QPair>
#include <QPointer>
#include <QList>
#include <QSet>
#include <optional>

class McpSession;
class McpToolRegistry;
class McpResourceRegistry;
class DE1Device;
class MachineState;
class MainController;
class ProfileManager;
class ShotHistoryStorage;
class BLEManager;
class Settings;
class MemoryMonitor;
class AccessibilityManager;
class ScreensaverVideoManager;
class TranslationManager;
class BatteryManager;

struct PendingConfirmation {
    QPointer<QTcpSocket> socket;
    QVariant requestId;
    QString sessionId;
    QString toolName;
    QJsonObject arguments;
    int accessLevel;
    QString protocolVersion = QStringLiteral("2024-11-05");  // captured at request time; default to legacy gating so a missed assignment never silently emits 2025-spec fields
};

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
    void setProfileManager(ProfileManager* mgr) { m_profileManager = mgr; }
    void setShotHistoryStorage(ShotHistoryStorage* storage) { m_shotHistory = storage; }
    void setBLEManager(BLEManager* ble) { m_bleManager = ble; }
    void setSettings(Settings* settings) { m_settings = settings; }
    void setMemoryMonitor(MemoryMonitor* monitor) { m_memoryMonitor = monitor; }
    void setAccessibilityManager(AccessibilityManager* mgr) { m_accessibilityManager = mgr; }
    void setScreensaverVideoManager(ScreensaverVideoManager* mgr) { m_screensaverManager = mgr; }
    void setTranslationManager(TranslationManager* mgr) { m_translationManager = mgr; }
    void setBatteryManager(BatteryManager* mgr) { m_batteryManager = mgr; }

    // Called by ShotServer for /mcp routes
    void handleHttpRequest(QTcpSocket* socket, const QString& method,
                           const QString& path, const QByteArray& headers,
                           const QByteArray& body);

    // Called by ShotServer to keep SSE-aware code paths in sync with raw HTTP
    // socket handling. ShotServer owns the QTcpSocket; McpServer just tracks
    // which of those sockets are upgraded to SSE.
    bool isSseClient(QTcpSocket* socket) const;
    void probeSseKeepalives();

    int activeSessionCount() const { return static_cast<int>(m_sessions.size()); }

    // Register all tools and resources — called after dependencies are set
    void registerAllTools();
    void registerAllResources();
    void connectSseNotifications();

    // Registries (accessible for tool/resource registration in later phases)
    McpToolRegistry* toolRegistry() const { return m_toolRegistry; }
    McpResourceRegistry* resourceRegistry() const { return m_resourceRegistry; }

    // Protocol versions this server can negotiate. First entry is preferred.
    static const QStringList& supportedProtocolVersions();

signals:
    void activeSessionCountChanged();
    void confirmationRequested(const QString& toolName, const QString& toolDescription,
                               const QString& sessionId);

public slots:
    void confirmationResolved(const QString& sessionId, bool accepted);

private:
    // JSON-RPC dispatch
    QJsonObject handleJsonRpc(const QJsonObject& request, McpSession* session,
                              QTcpSocket* socket, const QVariant& requestId);
    QJsonObject handleInitialize(const QJsonObject& params, McpSession* session);
    QJsonObject handleToolsList(const QJsonObject& params, McpSession* session);
    QJsonObject handleToolsCall(const QJsonObject& params, McpSession* session,
                                QTcpSocket* socket, const QVariant& requestId);
    QJsonObject handleResourcesList(const QJsonObject& params, McpSession* session);
    QJsonObject handleResourcesRead(const QJsonObject& params, McpSession* session,
                                    QTcpSocket* socket, const QVariant& requestId);
    QJsonObject handleResourcesSubscribe(const QJsonObject& params, McpSession* session);
    QJsonObject handleResourcesUnsubscribe(const QJsonObject& params, McpSession* session);

    // Session management
    McpSession* findOrCreateSession(const QString& sessionHeader);
    McpSession* findSession(const QString& sessionId);
    void cleanupExpiredSessions();

    // Confirmation helpers
    bool needsInAppConfirmation(const QString& toolName) const;
    bool needsChatConfirmation(const QString& toolName) const;
    QString confirmationDescription(const QString& toolName) const;

    // Response helpers
    void sendJsonRpcResponse(QTcpSocket* socket, const QJsonObject& result,
                             const QVariant& id, const QString& sessionId);
    void sendJsonRpcError(QTcpSocket* socket, int code, const QString& message,
                          const QVariant& id, const QString& sessionId = QString());
    void sendHttpResponse(QTcpSocket* socket, int statusCode,
                          const QByteArray& body, const QString& contentType,
                          const QString& sessionId = QString(),
                          const QList<QPair<QByteArray, QByteArray>>& extraHeaders = {});

    // Tool result construction. Always emits a `content[]` text block (works
    // for every protocol version). Spec-versioned additions are gated on the
    // negotiated protocol version: `structuredContent` and `resource_link`
    // content blocks are 2025-06-18 features, so 2024-11-05 clients see only
    // the text block. If the tool result carries a `_resourceLinks` array,
    // those entries are stripped from `structuredContent` and (when the
    // version permits) emitted as `resource_link` blocks.
    QJsonObject buildToolCallResponse(const QJsonObject& toolResult,
                                       const QString& protocolVersion) const;

    // Origin allowlist. Empty Origin header is always accepted; loopback and
    // the host's own LAN IPs (computed from QNetworkInterface at construction)
    // are the only browser origins that match.
    bool isOriginAllowed(const QString& origin) const;

    // Dependencies
    DE1Device* m_device = nullptr;
    MachineState* m_machineState = nullptr;
    MainController* m_mainController = nullptr;
    ProfileManager* m_profileManager = nullptr;
    ShotHistoryStorage* m_shotHistory = nullptr;
    BLEManager* m_bleManager = nullptr;
    Settings* m_settings = nullptr;
    MemoryMonitor* m_memoryMonitor = nullptr;
    AccessibilityManager* m_accessibilityManager = nullptr;
    ScreensaverVideoManager* m_screensaverManager = nullptr;
    TranslationManager* m_translationManager = nullptr;
    BatteryManager* m_batteryManager = nullptr;

    // Registries
    McpToolRegistry* m_toolRegistry;
    McpResourceRegistry* m_resourceRegistry;

    // Sessions
    QHash<QString, McpSession*> m_sessions;
    QTimer* m_cleanupTimer;

    // Rate limiting
    QTimer* m_rateLimitTimer;

    // SSE clients. Stored as QPointer so that if ShotServer destroys the
    // underlying socket without us seeing the disconnected signal first
    // (e.g. teardown ordering on macOS), iteration goes to nullptr instead
    // of dangling — the macOS QCFSocketNotifier crash on shutdown was use-
    // after-free of exactly this kind of raw socket pointer. QList rather
    // than QSet because QPointer has no qHash overload and the list is
    // bounded by MaxSseConnections (4) — linear scans are trivial.
    QList<QPointer<QTcpSocket>> m_sseClients;
    void broadcastSseNotification(const QString& resourceUri);

    // Cached set of allowed Origin values, populated once at construction
    // from loopback addresses and the host's LAN IPs. Each entry is a
    // lowercase scheme://host[:port] string with no trailing slash; entries
    // ending in `:*` match any port.
    QSet<QString> m_allowedOrigins;

    // In-app confirmation (machine_start_* tools)
    std::optional<PendingConfirmation> m_pendingConfirmation;

    // Async tool response helper — sends the tool result back on the held HTTP connection.
    // protocolVersion is captured at dispatch time so the deferred response
    // matches the originating session's negotiated spec.
    void sendAsyncToolResponse(QPointer<QTcpSocket> socket, const QVariant& requestId,
                               const QString& sessionId, const QString& protocolVersion,
                               const QJsonObject& toolResult);

    // Limits
    static constexpr int MaxSessions = 8;
    static constexpr int MaxSseConnections = 4;
    static constexpr int SessionTimeoutMinutes = 30;  // idle-session cleanup; runs every 60s on m_cleanupTimer and again opportunistically when a new session is created
    static constexpr int RateLimitPerMinute = 60;
};
