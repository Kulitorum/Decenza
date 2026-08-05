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
class VisualizerUploader;

struct PendingConfirmation {
    QPointer<QTcpSocket> socket;
    QVariant requestId;
    QString sessionId;
    QString toolName;
    QJsonObject arguments;
    int accessLevel;
    QString protocolVersion = QStringLiteral("2024-11-05");  // captured at request time; default to legacy gating so a missed assignment never silently emits 2025-spec fields
};

// The MCP server's own version, reported as `serverInfo.version` at initialize.
//
// BUMP THIS WHENEVER THE TOOL SURFACE CHANGES — a tool added, removed or renamed, an
// action added to a merged tool, an argument that changes meaning. It is not the app
// version and does not follow it: 2.0.2 shipped a 97-tool server and then a 66-tool
// one, and a client reporting the app version would have shown the same string for
// both. `serverInfo.appVersion` carries the build alongside it.
//
// This is a correctness obligation before it is anything else. The handshake is
// where a server states what it is, and a server whose surface has changed while
// its version has not is making a false statement on the wire — independently of
// whether any particular client acts on it. A client that DOES key on the version
// (today, or under the 2026-07-28 caching semantics) can only behave correctly if
// we tell it the truth; pinning "1.0.0" forever defeated exactly those clients.
//
// It does not, on its own, invalidate an existing cache: a client caches the tool
// list it fetched at initialize and refreshes only on RECONNECT, some not even
// then. This server declares no `tools.listChanged` and could not usefully send
// one, since tools are registered once at startup and never change while the app
// runs. The useful side effect is that a stale session becomes VISIBLE — a
// connector still reporting an old version is talking to a session that predates
// the change, which otherwise can only be inferred by counting tools.
//
// scripts/check_mcp_tool_budget.py fingerprints the registered tools and their
// actions and fails the PR if the surface moved without this string moving, so the
// rule above is enforced rather than remembered.
inline constexpr const char* McpSurfaceVersion = "1.0.1";
// Fingerprint of the tool surface this version was recorded against. Update it in
// the same edit as the version; the check prints the value to paste.
inline constexpr const char* McpSurfaceFingerprint = "8ada4d203b66";

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

    // Called by ShotServer for /mcp routes, and by McpRemoteAccess for the
    // tokenized remote connector route. When `remote` is true the session that
    // handles this request is flagged remote (informational only — the same
    // access-level and confirmation gates apply either way).
    void handleHttpRequest(QTcpSocket* socket, const QString& method,
                           const QString& path, const QByteArray& headers,
                           const QByteArray& body, bool remote = false);

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

    // Control/settings calls allowed per session per minute. Public because it is a
    // policy a test asserts against: the per-action rate limiting added with merged
    // tools is only meaningful if a read verb can be shown NOT to spend this budget
    // and a write verb can be shown to.
    static constexpr int RateLimitPerMinute = 60;

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
    // A POST body that is a JSON array. Answers with an array of the responses
    // for `id`-bearing elements, or 202 when the batch is all notifications.
    // See handleHttpRequest's POST branch for which revisions require this — it
    // is exactly one of the four we negotiate, not the two it first claimed.
    void handleJsonRpcBatch(QTcpSocket* socket, const QJsonArray& batch,
                            const QString& sessionHeader, const QString& protocolHeader,
                            bool remote);
    QJsonObject handleInitialize(const QJsonObject& params, McpSession* session);
    QJsonObject handleToolsList(const QJsonObject& params, McpSession* session);
    QJsonObject handleToolsCall(const QJsonObject& params, McpSession* session,
                                QTcpSocket* socket, const QVariant& requestId);
    QJsonObject handleResourcesList(const QJsonObject& params, McpSession* session);
    QJsonObject handleResourcesRead(const QJsonObject& params, McpSession* session,
                                    QTcpSocket* socket, const QVariant& requestId);
    QJsonObject handleResourcesSubscribe(const QJsonObject& params, McpSession* session);
    QJsonObject handleResourcesUnsubscribe(const QJsonObject& params, McpSession* session);

    // Which session serves one JSON-RPC message, and why it can't be served if
    // it can't. The helper deliberately writes NOTHING to the socket: a
    // single-message POST and one element of a batch reach the same decision but
    // emit it differently — an HTTP status answers the whole request, a JSON-RPC
    // error fills one array slot — so the two callers do the emitting.
    struct SessionResolution {
        McpSession* session = nullptr;
        int httpStatus = 0;        // non-zero → answer at the HTTP level, no JSON-RPC body
        QByteArray httpBody;
        int rpcErrorCode = 0;      // non-zero → this message gets a JSON-RPC error
        QString rpcErrorMessage;
    };
    SessionResolution resolveSessionForMessage(const QJsonObject& request,
                                               const QString& sessionHeader,
                                               const QString& protocolHeader);

    // Whether this server ended `sessionId` itself. One rule, consulted from
    // every verb — see the definition.
    bool isTerminatedSession(const QString& sessionId) const;

    // Whether handling this message would defer its response (in-app
    // confirmation, or an async tool/resource). Answerable WITHOUT dispatching,
    // which is the point — see the definition.
    bool willDeferResponse(const QJsonObject& request) const;

    // Session management
    McpSession* findOrCreateSession(const QString& sessionHeader);
    McpSession* findSession(const QString& sessionId);
    void cleanupExpiredSessions();
    // Remember a session ID the server itself ended, so later requests carrying
    // it get 404 rather than the auto-recovery path.
    void recordTerminatedSession(const QString& sessionId);
    // Count of stateful (live-SSE) sessions. Only these occupy a durable slot,
    // so the MaxSessions cap is measured against this — not m_sessions.size() —
    // to keep ephemeral per-request clients (cloud connectors) from exhausting
    // the pool. See McpSession::isStateful().
    int statefulSessionCount() const;

    // Confirmation helpers
    // End a pending confirmation that will never be answered, ANSWERING the
    // client holding the open request. See the definition.
    void abandonPendingConfirmation(const QString& reason);
    // All three take the call's arguments, not just its name: a merged tool has one
    // name and several verbs, and only the arguments say which one is being asked
    // for. For an unmerged tool the arguments are ignored and the name list below
    // decides, exactly as before.
    bool needsInAppConfirmation(const QString& toolName, const QJsonObject& arguments) const;
    bool needsChatConfirmation(const QString& toolName, const QJsonObject& arguments) const;
    QString confirmationDescription(const QString& toolName, const QJsonObject& arguments) const;
    // "steam_pitcher.delete" for a merged tool, the bare name otherwise — what the
    // confirmation payload and the in-app dialog report as the pending action.
    QString confirmationActionId(const QString& toolName, const QJsonObject& arguments) const;

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

    // Session IDs this server ended on an explicit DELETE. A later request
    // carrying one gets HTTP 404, which is what tells a client to re-initialize
    // (MUST, 2025-03-26 onward). Oldest first.
    //
    // DELETE ONLY, deliberately. The server ends sessions three other ways — the
    // idle-expiry reaper, the orphan reaper inside findOrCreateSession, and
    // MaxTotalSessions eviction — and none of them records here. Each targets a
    // client that is expected to come back, which is what the auto-recovery path
    // exists for; 404ing those is the "permanently broken until restart" outcome
    // that path's own comment warns about. See cleanupExpiredSessions for the
    // full reasoning and what would have to be verified to tighten it.
    //
    // Bounded because the alternative grows with every session the app ever had,
    // for the whole process lifetime. Eviction is safe in a way that is worth
    // stating: an evicted ID stops being "terminated" and becomes merely
    // unrecognized, so it falls back to the auto-recovery path — more permissive
    // than the spec, but the behaviour that shipped for a year, not a new
    // failure mode.
    QList<QString> m_terminatedSessions;

    // Monotonic SSE event ID. Attaching one is a 2025-11-25 **MAY**, not a
    // SHOULD — the SHOULDs alongside it are the `retry` field and the priming
    // event. One counter for the whole server run: unique across the process is
    // unique within every session at once. It does NOT satisfy the separate
    // 2025-11-25 SHOULD that IDs encode their originating stream, which only
    // buys something once Last-Event-ID replay exists, and that is out of scope.
    quint64 m_sseEventId = 0;

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

    // In-app confirmation (the machine_start tool)
    std::optional<PendingConfirmation> m_pendingConfirmation;

    // Async tool response helper — sends the tool result back on the held HTTP connection.
    // protocolVersion is captured at dispatch time so the deferred response
    // matches the originating session's negotiated spec.
    void sendAsyncToolResponse(QPointer<QTcpSocket> socket, const QVariant& requestId,
                               const QString& sessionId, const QString& protocolVersion,
                               const QJsonObject& toolResult);

    // Limits
    static constexpr int MaxSessions = 8;         // ceiling on *stateful* (live-SSE) sessions
    // Absolute backstop on *total* retained sessions (stateful + ephemeral).
    // Since MaxSessions counts only stateful sessions, ephemeral POST-only
    // sessions have no other ceiling; this bounds memory against a client that
    // POSTs `initialize` in a tight loop (which is not rate-limited). Set far
    // above any legitimate churn so it only ever trims a runaway/malicious peer.
    static constexpr int MaxTotalSessions = 128;
    static constexpr int MaxSseConnections = 4;
    // Ceiling on remembered terminated session IDs. Only explicit DELETEs land
    // here, and a client sends at most one per session it finishes with, so 256
    // is two full session pools (MaxTotalSessions is 128) — comfortably above any
    // real churn between app restarts. At 36-char UUIDs that is roughly 26 KB
    // including the QString and QList overhead, not the ~10 KB a
    // one-byte-per-character estimate suggests.
    static constexpr int MaxTerminatedSessions = 256;
    static constexpr int SessionTimeoutMinutes = 30;  // idle-session cleanup; runs every 60s on m_cleanupTimer and again opportunistically when a new session is created

};
