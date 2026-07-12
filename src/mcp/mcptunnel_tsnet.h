#pragma once

#include <QObject>
#include <QString>
#include <atomic>

class QThread;

// C++ wrapper around the (Decenza fork of) libtailscale C API. Embeds a
// userspace Tailscale node and enables Funnel, giving a stable public HTTPS URL
// (https://<host>.<tailnet>.ts.net) that proxies to Decenza's loopback MCP
// listener. This is Mode A of the Remote MCP connector.
//
// Only does real work when the app is built with -DENABLE_TSNET=ON
// (DECENZA_ENABLE_TSNET). Otherwise every call is a no-op and isAvailable()
// returns false, so McpRemoteAccess can reference this class unconditionally and
// simply report Mode A as unavailable.
//
// All blocking libtailscale calls run on a single worker thread (the C handle is
// only ever touched from there); results are posted back to the main thread via
// queued signals. Uses the non-blocking tailscale_start() + status polling so
// the interactive login URL can be surfaced (as a QR/link) before the node is
// authorized.
class McpTunnelTsnet : public QObject {
    Q_OBJECT
public:
    enum State {
        Stopped,     // not started
        Starting,    // backend coming up
        NeedsLogin,  // waiting for the user to authorize (see authUrl())
        Running,     // node up, Funnel active (see certDomain())
        Error        // see lastError()
    };
    Q_ENUM(State)

    explicit McpTunnelTsnet(QObject* parent = nullptr);
    ~McpTunnelTsnet() override;

    // Compiled with tsnet support? False in stock builds.
    static bool isAvailable();

    // Start the node and enable Funnel to 127.0.0.1:localPort (async).
    // stateDir persists the tailnet identity across launches; hostname is the
    // node name and becomes the Funnel subdomain. Safe to call once; call stop()
    // before starting again.
    void start(const QString& stateDir, const QString& hostname, quint16 localPort);

    // Bring the node down and tear down the worker (async, then blocks briefly
    // on join). Keeps the persisted state so the next start() reuses the identity.
    void stop();

    // "Forget this tailnet": stop(), then delete the state directory so the next
    // start() logs in fresh.
    void wipeState();

    State state() const { return m_state; }
    QString authUrl() const { return m_authUrl; }       // login URL (empty unless NeedsLogin)
    QString certDomain() const { return m_certDomain; } // Funnel FQDN (empty until Running)
    QString lastError() const { return m_lastError; }

signals:
    void stateChanged();
    void authUrlChanged();
    void certDomainChanged();

private:
    // Applies a status update on the main thread (called via queued invocation
    // from the worker). Emits the matching change signals.
    void applyUpdate(State state, const QString& authUrl, const QString& certDomain,
                     const QString& errorMsg);

    void runWorker(QString stateDir, QString hostname, quint16 localPort);

    QThread* m_worker = nullptr;
    std::atomic<int> m_handle{-1};   // libtailscale handle (int), -1 when none
    std::atomic<bool> m_stopRequested{false};

    State m_state = Stopped;
    QString m_authUrl;
    QString m_certDomain;
    QString m_lastError;
    QString m_stateDir;              // remembered for wipeState()
};
