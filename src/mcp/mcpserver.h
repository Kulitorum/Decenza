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

#include "mcpratewindow.h"

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
    // Captured at request time. Defaults to the LOWEST supported revision so a
    // missed assignment under-reports rather than emitting fields the client's
    // revision does not define.
    QString protocolVersion = QStringLiteral("2025-06-18");
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
inline constexpr const char* McpSurfaceVersion = "1.2.0";
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

    // Every protocol version this server can SERVE, newest legacy first.
    //
    // Not the same as "negotiable": `initialize` picks only among the legacy
    // entries (see handleInitialize), because the modern era has no handshake
    // and a client reaching that code cannot be speaking one. The modern entry
    // is reachable only through a modern request's own `_meta`.
    static const QStringList& supportedProtocolVersions();

    // Whether a revision is modern (handshake-less, per-request `_meta`).
    static bool isModernProtocolVersion(const QString& version);

    // The modern subset of supportedProtocolVersions(), newest first. What a
    // modern request may name and what `server/discover` advertises.
    static const QStringList& modernProtocolVersions();

    // The handshake-based subset of supportedProtocolVersions(), newest first.
    // What `initialize` may negotiate, and what a legacy request's
    // `MCP-Protocol-Version` header may name.
    static const QStringList& legacyProtocolVersions();

signals:
    void activeSessionCountChanged();
    void confirmationRequested(const QString& toolName, const QString& toolDescription,
                               const QString& sessionId);

public slots:
    void confirmationResolved(const QString& sessionId, bool accepted);

private:
    // Whether this POST body is a MODERN-era request.
    //
    // The discriminator is `_meta["io.modelcontextprotocol/protocolVersion"]`,
    // which `RequestMetaObject` makes REQUIRED on every modern request and which
    // no legacy revision defines — so its presence is decisive on its own.
    //
    // NOT the `Mcp-Method` / `Mcp-Name` headers, which an earlier draft of this
    // change named as the signal. They are required of a modern POST too, but
    // requiring them here would reject a modern request whose proxy stripped
    // them — a needless false negative — and they are transport decoration,
    // while `_meta` is the request itself. NOT `MCP-Protocol-Version` either:
    // legacy has sent that since 2025-06-18.
    static bool isModernRequest(const QJsonObject& request);

    // The modern era's whole envelope: version resolution, no session, and the
    // response framing. Dispatch below this is SHARED with legacy — if a fix has
    // to be made twice, the fork is in the wrong place.
    void handleModernRequest(QTcpSocket* socket, const QJsonObject& request,
                             const QString& protocolHeader);

    // JSON-RPC dispatch. `protocolVersion` is the version THIS message is
    // answered under, resolved once by resolveSessionForMessage — handlers must
    // use it rather than reading it back off the session, which can differ.
    QJsonObject handleJsonRpc(const QJsonObject& request, McpSession* session,
                              QTcpSocket* socket, const QVariant& requestId,
                              const QString& protocolVersion);
    QJsonObject handleInitialize(const QJsonObject& params, McpSession* session);
    // `server/discover` — a server MUST implement it, a client MAY call it.
    QJsonObject handleServerDiscover(const QString& protocolVersion);
    QJsonObject handleToolsList(const QJsonObject& params, const QString& protocolVersion);
    QJsonObject handleToolsCall(const QJsonObject& params, McpSession* session,
                                QTcpSocket* socket, const QVariant& requestId,
                                const QString& protocolVersion);
    QJsonObject handleResourcesList(const QJsonObject& params, const QString& protocolVersion);
    QJsonObject handleResourcesRead(const QJsonObject& params, McpSession* session,
                                    QTcpSocket* socket, const QVariant& requestId,
                                    const QString& protocolVersion);
    QJsonObject handleResourcesSubscribe(const QJsonObject& params, McpSession* session);
    QJsonObject handleResourcesUnsubscribe(const QJsonObject& params, McpSession* session);

    // Which session serves one JSON-RPC message, and why it can't be served if
    // it can't. The helper deliberately writes NOTHING to the socket, so the caller decides
    // whether the outcome becomes an HTTP status or a JSON-RPC error body.
    //
    // That split existed because a batch element and a single-message POST
    // reached the same decision and emitted it differently. Batching is gone and
    // there is now ONE caller, so the separation buys less than it did — kept
    // because resolving and answering are still different jobs, not because two
    // callers must agree.
    struct SessionResolution {
        McpSession* session = nullptr;
        int httpStatus = 0;        // non-zero → answer at the HTTP level, no JSON-RPC body
        QByteArray httpBody;
        int rpcErrorCode = 0;      // non-zero → this message gets a JSON-RPC error
        QString rpcErrorMessage;
        // The `MCP-Protocol-Version` header, when it names a version we support.
        // Empty otherwise — including when the header is absent, which is the
        // common case. NOT the version to answer under on its own: combine it
        // with the session through effectiveProtocolVersion(). See
        // resolveSessionForMessage for why a supported header wins.
        QString headerProtocolVersion;
    };

    // Which version a message is answered under. The header wins when it names
    // something we support; otherwise the session's negotiated version.
    //
    // Deliberately a live read of the session rather than a value captured at
    // resolve time. A batch resolves ONCE, from its first object element, and
    // `[initialize, tools/list]` is a realistic shape: the initialize element
    // negotiates and writes the result to the session, so a captured copy would
    // answer every later element under the PRE-negotiation default. Empty
    // version strings are worse than they look — every
    // `protocolVersion >= "2025-06-18"` gate in the registries reads one as
    // older than everything.
    static QString effectiveProtocolVersion(const McpSession* session,
                                            const QString& headerProtocolVersion);
    SessionResolution resolveSessionForMessage(const QJsonObject& request,
                                               const QString& sessionHeader,
                                               const QString& protocolHeader);

    // Whether this server ended `sessionId` itself. One rule, consulted from
    // every verb — see the definition.
    bool isTerminatedSession(const QString& sessionId) const;

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
    // content blocks arrived in 2025-06-18, which is now the lowest revision
    // served, so both are emitted unconditionally. If the tool result carries a `_resourceLinks` array,
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

    // Modern-era rate limiting, keyed on the caller's peer address.
    //
    // A stateless request has no session, so McpSession::controlCallCount() —
    // which legacy still uses, untouched — has nothing to count against. The
    // peer address is the transport-level caller identity BOTH routes already
    // have, and McpRemoteAccess has keyed a per-minute budget on it for failed
    // token attempts since long before this.
    //
    // Not the remote-access token: there is exactly one for the whole app, so
    // every remote caller presents the same one and keying on it would produce a
    // single global limiter for the entire remote route — precisely the
    // starvation the per-session counter exists to avoid.
    McpRateWindow m_modernControlCalls;

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
