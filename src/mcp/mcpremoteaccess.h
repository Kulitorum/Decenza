#pragma once

#include <QObject>
#include <QHash>
#include <QSet>
#include <QByteArray>
#include <QDateTime>
#include <QPointer>

class QTcpServer;
class QTcpSocket;
class QTimer;
class McpServer;
class SettingsMcp;
class McpTunnelTsnet;
class QNetworkAccessManager;
class QNetworkReply;

// Coordinator for the remote MCP connector (public-internet reachability for
// Claude / ChatGPT mobile custom connectors). Owns a dedicated TCP listener,
// separate from ShotServer, that serves ONLY the tokenized MCP route
// `POST/GET/DELETE /mcp/<token>` and returns a bare 404 for everything else —
// so ShotServer's web editor, REST API, and data-migration endpoints are never
// reachable through the public surface. Matching requests are forwarded
// in-process to the existing McpServer dispatch with the session flagged
// remote; access-level and confirmation gating are unchanged.
//
// Authorization is the unguessable path segment (capability URL). Rotation is
// the revocation story — rotating the token closes every live remote socket so
// the old URL dies immediately.
//
// Phase 1 wires Mode C (bring-your-own public URL: the user runs a reverse
// proxy / tunnel on any box that forwards to the tablet's LAN IP + this port).
// Embedded-tunnel modes (tsnet Funnel, ngrok) forward to the same listener and
// are added later.
class McpRemoteAccess : public QObject {
    Q_OBJECT
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    // Machine-readable status token for QML ("off"|"starting"|"active"|
    // "reconnecting"|"error") — avoids exposing the C++ enum into QML.
    Q_PROPERTY(QString statusString READ statusString NOTIFY statusChanged)
    Q_PROPERTY(QString statusDetail READ statusDetail NOTIFY statusChanged)
    Q_PROPERTY(QString connectorUrl READ connectorUrl NOTIFY connectorUrlChanged)
    Q_PROPERTY(int listenPort READ listenPort NOTIFY statusChanged)
    // Tailscale (Mode A) interactive login URL — non-empty only while the
    // embedded node is waiting for the user to authorize it. Surface as QR/link.
    Q_PROPERTY(QString loginUrl READ loginUrl NOTIFY loginUrlChanged)
    // Whether embedded-tunnel modes (Mode A) are compiled into this build.
    Q_PROPERTY(bool tunnelAvailable READ tunnelAvailable CONSTANT)

public:
    enum Status {
        Off,           // disabled
        Starting,      // listener coming up
        Active,        // listener up and serving the tokenized route
        Reconnecting,  // listener dropped; retrying
        Error          // could not start (port in use, unsupported mode)
    };
    Q_ENUM(Status)

    explicit McpRemoteAccess(QObject* parent = nullptr);
    ~McpRemoteAccess() override;

    // Non-owning dependencies.
    void setMcpServer(McpServer* server) { m_mcpServer = server; }
    void setSettings(SettingsMcp* settings);

    Status status() const { return m_status; }
    QString statusString() const;
    QString statusDetail() const { return m_statusDetail; }
    // Full connector URL to paste into claude.ai (Mode C: <base>/mcp/<token>).
    // Empty when the URL cannot be composed (disabled, no/invalid base URL, or
    // a mode whose tunnel URL is not yet known).
    QString connectorUrl() const;
    int listenPort() const;
    QString loginUrl() const;
    static bool tunnelAvailable();

    // Re-evaluate settings (enabled / mode / port) and start, stop, or restart
    // the listener accordingly. Safe to call repeatedly.
    Q_INVOKABLE void refresh();

    // Rotate the capability token and immediately drop every live remote
    // connection so the previous URL stops working at once.
    Q_INVOKABLE void rotateToken();

    // Sign out of Tailscale (Mode A): wipe the embedded tsnet node's persisted
    // identity (state dir), then re-evaluate settings so a still-enabled node
    // comes back up with a fresh login. This is the recovery path for a stored
    // nodekey that belongs to a different/deleted tailnet — the state where the
    // login otherwise loops on "device already exists" / 403. No-op when no
    // embedded tunnel exists (custom-URL mode or a build without tsnet).
    Q_INVOKABLE void forgetTailscale();

signals:
    void statusChanged();
    void connectorUrlChanged();
    void loginUrlChanged();

private slots:
    void onNewConnection();
    void onReadyRead();
    void onSocketDisconnected();
    void onReaperTick();
    void onTunnelStateChanged();

private:
    // bindLoopbackOnly: embedded tunnels (Mode A) proxy from 127.0.0.1, so the
    // listener binds loopback; Mode C needs a LAN-routable bind for an off-box
    // proxy. On a bind failure it sets status Error (callers check m_status).
    void startListener(bool bindLoopbackOnly);
    // Start / stop the embedded Tailscale node for Mode A.
    void startTunnel();
    void stopTunnel();

    // Mode A public-reachability probe: actually fetch the Funnel connector URL
    // from the app and only treat the connector as Active once it responds — the
    // tunnel's local "funnel configured" signal is NOT proof the public path
    // works (needs HTTPS certs + Funnel granted server-side).
    void startReachabilityProbe();
    void stopReachabilityProbe();
    void doReachabilityProbe();
    void stopListener();
    void setStatus(Status status, const QString& detail = QString());
    void closeAllSockets();

    // Read available bytes from a socket and drain complete requests.
    void readFromSocket(QTcpSocket* socket);
    // Process as many complete HTTP requests as the socket buffer holds.
    void processBuffer(QTcpSocket* socket);
    // Validate the request line's path and forward to McpServer, or reply 404.
    void routeRequest(QTcpSocket* socket, const QString& method, const QString& path,
                      const QByteArray& headerBlock, const QByteArray& body);
    // Constant-time comparison of a candidate token segment against the stored
    // token. Length mismatch always returns false without leaking timing.
    bool tokenMatches(const QByteArray& candidate) const;
    // Per-source failed-token limiter. Returns true when the source is over
    // budget for the current window; the caller then drops the connection
    // (forcing a reconnect) and suppresses further per-request log lines.
    bool failedTokenOverLimit(const QString& source);
    void sendBare404(QTcpSocket* socket);

    McpServer* m_mcpServer = nullptr;
    SettingsMcp* m_settings = nullptr;
    QTcpServer* m_listener = nullptr;
    McpTunnelTsnet* m_tunnel = nullptr;   // embedded Tailscale node (Mode A)
    QNetworkAccessManager* m_reachProbe = nullptr;  // Mode A reachability probe
    QTimer* m_reachTimer = nullptr;
    bool m_funnelReachable = false;       // last probe confirmed the public URL works
    bool m_probeInFlight = false;
    // Bumped whenever probing (re)starts/stops; a probe reply carrying a stale
    // generation is ignored so it can't flip status after disable/mode switch.
    quint64 m_probeGeneration = 0;
    int m_probeFailCount = 0;             // consecutive failed probes (for surfacing an error)
    QTimer* m_reaper = nullptr;

    Status m_status = Off;
    QString m_statusDetail;

    struct PendingRequest {
        QByteArray buffer;
        int headerEnd = -1;
        qint64 contentLength = -1;
        QDateTime lastActivity;
    };
    QHash<QTcpSocket*, PendingRequest> m_pending;
    QSet<QTcpSocket*> m_sockets;

    struct FailWindow {
        int count = 0;
        bool suppressionLogged = false;  // one "further requests dropped" line per window
        QDateTime windowStart;
    };
    QHash<QString, FailWindow> m_failedAttempts;

    static constexpr int MaxHeaderSize = 64 * 1024;
    static constexpr int MaxBodySize = 1 * 1024 * 1024;   // MCP JSON is tiny
    static constexpr int MaxConnections = 8;
    static constexpr int MaxFailedPerMinute = 20;
    static constexpr int IdleTimeoutSeconds = 60;
    static constexpr int ReaperIntervalMs = 30000;

#ifdef DECENZA_TESTING
    friend class tst_McpRemoteAccess;
#endif
};
