#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSslServer>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslKey>
#include <QUdpSocket>
#include <QHash>
#include <QSet>
#include <QFile>
#include <QJsonObject>

#include "../core/logcollapse.h"
#include <QTimer>
#include <QElapsedTimer>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QPointer>
#include <memory>

#include "../history/shotprojection.h"
#include "multicastlock.h"
#include <QtQml/qqmlregistration.h>

class ShotHistoryStorage;
struct ShotRecord;
class DE1Device;
class MachineState;
class MainController;
class ScreensaverVideoManager;
class Settings;
class ProfileStorage;
class AIManager;
class MqttClient;
class WidgetLibrary;
class LibrarySharing;
class BatteryManager;
class McpServer;
class MemoryMonitor;

struct PendingRequest {
    QByteArray headerData;          // Only headers stored in memory
    qint64 contentLength = -1;
    int headerEnd = -1;
    qint64 bodyReceived = 0;        // Track bytes received
    QFile* tempFile = nullptr;      // Stream body to temp file for large uploads
    QString tempFilePath;           // Path to temp file
    QElapsedTimer lastActivity;     // For timeout tracking
    bool isMediaUpload = false;     // Flag for media upload requests
    bool isBackupRestore = false;   // Flag for backup restore uploads
    bool isApkUpload = false;       // Flag for APK upload requests
};

class ShotServer : public QObject {
    Q_OBJECT

    // Compile-time QML registration, so qmllint, qmlcachegen and the language server can
    // follow MainController's property through to this class. A runtime qmlRegister* call is
    // invisible to all three. Full rationale in src/controllers/maincontroller.h.
    QML_ELEMENT
    QML_UNCREATABLE("ShotServer is created in C++ and reached via MainController")

    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(QString url READ url NOTIFY urlChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)
    Q_PROPERTY(bool hasTotpSecret READ hasStoredTotpSecret NOTIFY hasTotpSecretChanged)

public:
    explicit ShotServer(ShotHistoryStorage* storage, DE1Device* device, QObject* parent = nullptr);
    ~ShotServer();

    bool isRunning() const { return m_server && m_server->isListening(); }
    QString url() const;
    int port() const { return m_port; }
    void setPort(int port);

    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();

    // TOTP setup (called from QML settings dialog)
    Q_INVOKABLE QVariantMap generateTotpSetup();
    Q_INVOKABLE bool completeTotpSetup(const QString& secret, const QString& code);
    Q_INVOKABLE void resetTotpSecret();

    // Screensaver video manager for personal media upload
    void setScreensaverVideoManager(ScreensaverVideoManager* manager) { m_screensaverManager = manager; }

    // Settings and profiles for data migration
    void setSettings(Settings* settings);

    void setProfileStorage(ProfileStorage* profileStorage) { m_profileStorage = profileStorage; }

    // Machine state for home automation API
    void setMachineState(MachineState* machineState) { m_machineState = machineState; }

    // MainController for the recipes/bags/equipment surfaces (add-recipes):
    // storages + the single recipe-activation path. Non-owning.
    void setMainController(MainController* mainController) { m_mainController = mainController; }

    // AI manager for layout AI assistant
    void setAIManager(AIManager* aiManager) { m_aiManager = aiManager; }

    // MQTT client for connection test/control from web UI
    void setMqttClient(MqttClient* client) { m_mqttClient = client; }

    // Widget library and community sharing for layout editor
    void setWidgetLibrary(WidgetLibrary* library) { m_widgetLibrary = library; }
    void setLibrarySharing(LibrarySharing* sharing) { m_librarySharing = sharing; }

    // MCP server for AI remote control
    void setMcpServer(McpServer* mcp) { m_mcpServer = mcp; }

    // Remote MCP connector: live status + connector/login URLs for the web
    // settings page (Settings.mcp holds the persisted config; this owns the
    // running-state, composed connector URL, and Tailscale login URL).
    void setRemoteMcpAccess(class McpRemoteAccess* access) { m_remoteMcpAccess = access; }

    // Relay client for Pocket app remote control
    void setRelayClient(class RelayClient* client) { m_relayClient = client; }

    // System status for web telemetry
    void setBatteryManager(BatteryManager* manager) { m_batteryManager = manager; }
    void setMemoryMonitor(MemoryMonitor* monitor) { m_memoryMonitor = monitor; }

signals:
    void runningChanged();
    void urlChanged();
    void portChanged();
    void hasTotpSecretChanged();
    void clientConnected(const QString& address);
    void sleepRequested();  // Emitted when sleep command received via REST API

    /// Emitted on the main thread immediately before installApk() invokes the
    /// Android PackageInstaller JNI dispatch. Mirror of UpdateChecker's signal
    /// of the same name — see that header for the QSocketNotifier race
    /// (#865) listeners are expected to mitigate.
    void aboutToDispatchInstall();

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void onCleanupTimerTick();
    void onDiscoveryDatagram();
    void onLayoutChanged();
    void onThemeChanged();

private:
    // Collapses repeated identical request lines — see handleRequest(). Keyed BY the line, so a
    // path is logged the first time it is requested in a server run and not again; a different
    // path is a different key and prints at once. That is the whole value of the line — which
    // endpoints this run touched — and a poller re-fetching one of them every 5 s adds nothing to
    // it however long it keeps going.
    // Episodic: a run is one server lifetime. Flushed in stop(), which is where the per-path counts
    // are reported.
    LogCollapse m_requestLog{LogCollapse::kChangesOnly};

    void handleRequest(QTcpSocket* socket, const QByteArray& request);
    void sendResponse(QTcpSocket* socket, int statusCode, const QString& contentType,
                      const QByteArray& body, const QByteArray& extraHeaders = QByteArray());
    void sendJson(QTcpSocket* socket, const QByteArray& json);
    // GET /api/grind-candidates?brand=&model=&current=&rpm= — server-computed
    // stepped grind/RPM candidate lists for the web <datalist> helper
    // (replace-grind-inputs-with-picker). The PAGE passes the grinder identity
    // of the record being edited (the shot's / bag's / recipe-package's), so
    // candidates resolve against the value's own grinder, never the active one.
    // Mirrors GrindRowSource.qml: catalog stepping via
    // SettingsDye::stepGrinderSetting (notation-aware, click-indexed floor),
    // plain-numeric fallback, observed-history fallback; RPM ±5 steps around
    // the value (neutral 1000 anchor when unset). No stepping in browser JS.
    void handleGrindCandidatesApi(QTcpSocket* socket, const QString& path);
    void sendHtml(QTcpSocket* socket, const QString& html);
    void sendFile(QTcpSocket* socket, const QString& path, const QString& contentType);

    QString getLocalIpAddress() const;
    QString generateShotListPage(const QVariantList& shots) const;
    QString generateShotDetailPage(qint64 shotId, const ShotProjection& shot) const;
    QString generateComparisonPage(const QList<ShotRecord>& shots) const;
    QString generateDebugPage() const;
    QString generateUploadPage() const;
    void handleUploadFromFile(QTcpSocket* socket, const QString& tempPath, const QString& headers);
    bool installApk(const QString& apkPath);

    // Personal media upload
    QString generateMediaUploadPage() const;
    void handleMediaUpload(QTcpSocket* socket, const QString& tempFilePath, const QString& headers);
    static bool resizeImage(const QString& inputPath, const QString& outputPath, int maxWidth, int maxHeight);
    static bool resizeVideo(const QString& inputPath, const QString& outputPath, int maxWidth, int maxHeight);
    static QDateTime extractImageDate(const QString& imagePath);
    static QDateTime extractVideoDate(const QString& videoPath);
    static QDateTime extractDateWithExiftool(const QString& filePath);
    void cleanupPendingRequest(QTcpSocket* socket);
    void resetKeepAliveTimer(QTcpSocket* socket);

    // Data migration backup API
    void handleBackupManifest(QTcpSocket* socket);
    void handleBackupSettings(QTcpSocket* socket, bool includeSensitive);
    // Extra QSettings (shot-map location, accessibility, language) not covered
    // by SettingsSerializer — served for LAN parity with the full archive.
    static QJsonObject buildExtraSettingsObject();
    void handleBackupExtraSettings(QTcpSocket* socket);
    void handleBackupProfilesList(QTcpSocket* socket);
    void handleBackupProfileFile(QTcpSocket* socket, const QString& category, const QString& filename);
    void handleBackupMediaList(QTcpSocket* socket);
    void handleBackupMediaFile(QTcpSocket* socket, const QString& filename);
    void handleBackupAIConversations(QTcpSocket* socket);
    QJsonArray serializeAIConversations() const;

    // Full backup download/restore
    void handleBackupFull(QTcpSocket* socket);
    QString generateRestorePage() const;
    void handleBackupRestore(QTcpSocket* socket, const QString& tempFilePath, const QString& headers);

    // Layout editor web UI
    QString generateLayoutPage() const;
    void handleLayoutApi(QTcpSocket* socket, const QString& method, const QString& path, const QByteArray& body);

    // Theme editor web UI
    QString generateThemePage() const;
    void handleThemeApi(QTcpSocket* socket, const QString& method, const QString& path, const QByteArray& body);
    QJsonObject buildThemeJson() const;

    // Recipes web UI (add-recipes) — shotserver_recipes.cpp
    QString generateRecipesPage() const;
    void handleRecipesApi(QTcpSocket* socket, const QString& method, const QString& path, const QByteArray& body);

    // Bags web UI (add-recipes) — shotserver_bags.cpp
    QString generateBeansPage() const;
    void handleBagsApi(QTcpSocket* socket, const QString& method, const QString& path, const QByteArray& body);

    // Equipment web UI (add-recipes) — shotserver_equipment.cpp
    QString generateEquipmentPage() const;
    void handleEquipmentApi(QTcpSocket* socket, const QString& method, const QString& path, const QByteArray& body);

    // Settings web UI
    QString generateSettingsPage() const;
    void handleGetSettings(QTcpSocket* socket);
    void handleSaveSettings(QTcpSocket* socket, const QByteArray& body);
    void handleRotateRemoteMcpToken(QTcpSocket* socket);

    // Settings test/connect endpoints
    void handleVisualizerTest(QTcpSocket* socket, const QByteArray& body);
    void handleAiTest(QTcpSocket* socket, const QByteArray& body);
    void handleMqttConnect(QTcpSocket* socket, const QByteArray& body);
    void handleMqttDisconnect(QTcpSocket* socket);
    void handleMqttStatus(QTcpSocket* socket);
    void handleMqttPublishDiscovery(QTcpSocket* socket);

    // AI Conversations web UI
    QString generateAIConversationsPage() const;
    void handleAIConversationDownload(QTcpSocket* socket, const QString& key, const QString& format);

    // Pocket app pairing
    void handlePocketPair(QTcpSocket* socket, const QByteArray& body);
    void handlePocketStatus(QTcpSocket* socket);

    // HTTPS / TLS
    bool setupTls();
    bool generateSelfSignedCert(const QString& certPath, const QString& keyPath);
    bool isSecurityEnabled() const;

    // Authentication (TOTP) - implemented in shotserver_auth.cpp
    void handleAuthRoute(QTcpSocket* socket, const QString& method, const QString& path, const QByteArray& body);
    void handleTotpLogin(QTcpSocket* socket, const QByteArray& body);
    bool checkSession(const QByteArray& request) const;
    bool hasStoredTotpSecret() const;
    QString extractCookie(const QByteArray& request, const QString& cookieName) const;
    QString createSession(const QString& userAgent);
    void sendRedirect(QTcpSocket* socket, const QString& location, const QString& setCookie = QString());
    void loadSessions();
    void saveSessions();

    // TOTP rate limiting
    QHash<QString, QPair<int, QDateTime>> m_loginAttempts;  // IP -> (attempts, window start)
    bool checkRateLimit(const QString& ip);

    // Session info
    struct SessionInfo {
        QDateTime expiry;
        QString userAgent;
    };
    QHash<QString, SessionInfo> m_sessions;

    // Shared flag for destructor safety in background thread lambdas
    std::shared_ptr<bool> m_destroyed = std::make_shared<bool>(false);

    QTcpServer* m_server = nullptr;
    QUdpSocket* m_discoverySocket = nullptr;

    // Listen-socket health-check state. We only log on state change instead
    // of every 30s tick — both to cut steady-state noise and so the actual
    // signal (a flip in isListening/socketDescriptor) stands out when
    // chasing the listen-socket invalidation bug. Sentinel `-2` distinguishes
    // "no health check yet observed" from a valid `-1` socket descriptor.
    qintptr m_lastHealthFd = -2;
    bool m_lastHealthListening = false;
    // On Android, Wi-Fi filters incoming UDP broadcast/multicast frames unless the
    // app holds a WifiManager.MulticastLock; without one, discovery requests
    // silently never reach us. Held for as long as the discovery socket is bound.
    //
    // NOT inside an #ifdef, unlike the QJniObject it replaces. MulticastLock is a
    // no-op type off Android, and keeping the member unconditional means the two
    // lines that manage it in start()/stop() are compiled — and therefore
    // type-checked — on the platform this is developed on, instead of only in a
    // CI job nobody reads until it goes red.
    //
    // The lock itself is shared with mDNS rather than hand-rolled here. This
    // class used to own the only copy of that JNI, which is how three comments
    // elsewhere came to describe the lock as held app-wide: it is held only while
    // this server runs, and `shotServer/enabled` defaults to false.
    std::unique_ptr<MulticastLock::Holder> m_multicastLock;
    ShotHistoryStorage* m_storage = nullptr;
    DE1Device* m_device = nullptr;
    ScreensaverVideoManager* m_screensaverManager = nullptr;
    Settings* m_settings = nullptr;
    ProfileStorage* m_profileStorage = nullptr;
    MachineState* m_machineState = nullptr;
    MainController* m_mainController = nullptr;
    AIManager* m_aiManager = nullptr;
    MqttClient* m_mqttClient = nullptr;
    QNetworkAccessManager* m_testNetworkManager = nullptr;
    bool m_visualizerTestInFlight = false;
    bool m_aiTestInFlight = false;
    bool m_mqttConnectInFlight = false;
    WidgetLibrary* m_widgetLibrary = nullptr;
    LibrarySharing* m_librarySharing = nullptr;
    class McpRemoteAccess* m_remoteMcpAccess = nullptr;
    BatteryManager* m_batteryManager = nullptr;
    McpServer* m_mcpServer = nullptr;
    MemoryMonitor* m_memoryMonitor = nullptr;
    class RelayClient* m_relayClient = nullptr;
    int m_nextLibraryRequestId = 0;
    static constexpr int kLibraryTimeoutMs = 60000;
    enum class LibraryRequestType { Browse, Download, Upload, Delete };
    struct PendingLibraryRequest {
        LibraryRequestType type;
        QPointer<QTcpSocket> socket;
        QList<QMetaObject::Connection> connections;
        QTimer* timeoutTimer = nullptr;
        std::shared_ptr<bool> fired = std::make_shared<bool>(false);
    };
    QHash<int, PendingLibraryRequest> m_pendingLibraryRequests;
    bool hasInFlightLibraryRequest(LibraryRequestType type) const {
        for (auto it = m_pendingLibraryRequests.constBegin(); it != m_pendingLibraryRequests.constEnd(); ++it)
            if (it.value().type == type) return true;
        return false;
    }
    void invalidateLibraryRequest(PendingLibraryRequest& req);
    void completeLibraryRequest(int reqId, const QJsonObject& resp);
    void cancelAllLibraryRequests();
    // The ONE way a socket leaves this class. Every container that can hold a
    // socket pointer is cleared here, then the socket is closed and deleted.
    //
    // It has to be one function because close() is NOT guaranteed to emit
    // disconnected(). Three paths in qabstractsocket.cpp (Qt 6.11.1) skip it:
    // close() on an UnconnectedState socket skips the disconnectFromHost() call
    // entirely (:2650-2659); disconnectFromHost() returns early whenever write
    // data is still pending (:2718-2725); and in ConnectingState/HostLookupState
    // it sets pendingClose and returns (:2683-2689). The dead-client checks
    // select for sockets in exactly those states — so a site that close()s,
    // deleteLater()s and trusts onDisconnected() to do the bookkeeping leaves
    // dangling pointers behind. Each call site used to maintain that bookkeeping
    // by hand and they had already drifted apart.
    //
    // "Every container" means every one in THIS class. McpServer keeps its own
    // SSE client list over the same sockets (McpServer::m_sseClients); that is
    // safe without help here only because it holds QPointers.
    void retireSocket(QTcpSocket* socket);
    void broadcastSseEvent(QSet<QTcpSocket*>& clients, const QByteArray& event);
    QTimer* m_cleanupTimer = nullptr;
    int m_port = 8888;
    int m_activeMediaUploads = 0;
    bool m_backupFullInProgress = false;
    QHash<QTcpSocket*, PendingRequest> m_pendingRequests;
    QHash<QTcpSocket*, qint64> m_uploadProgressLog;  // Track last-logged byte offset per socket (cleaned up on disconnect)
    QSet<QTcpSocket*> m_sseLayoutClients;  // SSE connections for layout change notifications
    QSet<QTcpSocket*> m_sseThemeClients;   // SSE connections for theme change notifications
    QHash<QTcpSocket*, QTimer*> m_keepAliveTimers;  // Idle timers for keep-alive connections
    // Every accepted socket, for the whole time it is alive. The other
    // containers above each hold a SUBSET once the connection has taken a shape
    // (mid-request, subscribed to SSE, idle between keep-alive requests), so
    // none of them can answer "how many clients are connected right now" — which
    // is what MAX_CONNECTIONS is enforced against. (m_pendingRequests now also
    // gets an entry at accept, before any of those shapes is known; it is
    // removed again the moment a request completes.)
    QSet<QTcpSocket*> m_clients;
    bool m_atConnectionLimit = false;   // true between hitting MAX_CONNECTIONS and dropping below it
    int m_refusedConnections = 0;       // refusals since the limit was last hit, reported on recovery

    // TLS state
    QSslCertificate m_sslCert;
    QSslKey m_sslKey;

    // Limits to prevent resource exhaustion
    static constexpr qint64 MAX_HEADER_SIZE = 64 * 1024;           // 64 KB for headers
    static constexpr qint64 MAX_SMALL_BODY_SIZE = 1024 * 1024;     // 1 MB kept in memory
    static constexpr qint64 MAX_UPLOAD_SIZE = 500 * 1024 * 1024;   // 500 MB max per file
    static constexpr int MAX_CONCURRENT_UPLOADS = 2;               // Limit concurrent media uploads
    static constexpr int CONNECTION_TIMEOUT_MS = 300000;           // 5 minute timeout
    // Ceiling on simultaneously accepted clients. Not a throughput limit — it is
    // the backstop for an unauthenticated listener bound to QHostAddress::Any and
    // advertised over mDNS, which anything on the LAN can open a socket to
    // without sending a byte.
    //
    // NOT the Tailscale Funnel path, which an earlier draft of this comment
    // claimed: the only Funnel config in the tree points at McpRemoteAccess's
    // loopback listener on remoteMcpPort (mcptunnel_tsnet.cpp, started from
    // McpRemoteAccess::startTunnel), never at this server's port. No
    // Funnel-originated socket reaches m_clients.
    //
    // 64 is well above real use (a browser opens ~6 per host, plus the
    // layout/theme/MCP SSE streams, times a few devices) and far below any
    // descriptor limit.
    static constexpr int MAX_CONNECTIONS = 64;
    static constexpr int KEEPALIVE_TIMEOUT_S = 30;                 // Close idle keep-alive connections after 30s
    // Deadline for a socket that has sent nothing at all since being accepted.
    // Deliberately the same as KEEPALIVE_TIMEOUT_S: a browser that pre-connects
    // and then idles is treated the same before its first request as after its
    // last one, and it simply reconnects. CONNECTION_TIMEOUT_MS above stays for
    // anything with a request actually in flight.
    static constexpr int SILENT_TIMEOUT_MS = KEEPALIVE_TIMEOUT_S * 1000;
    static constexpr int DISCOVERY_PORT = 8889;                    // UDP port for device discovery
    static constexpr int SESSION_LIFETIME_DAYS = 90;               // Auth session cookie lifetime
};
