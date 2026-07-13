#pragma once

#include <QObject>
#include <QPointer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QDateTime>
#include <QUuid>
#include <QSet>

class McpSession : public QObject {
    Q_OBJECT
public:
    explicit McpSession(QObject* parent = nullptr)
        : QObject(parent)
        , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
        , m_created(QDateTime::currentDateTimeUtc())
        , m_lastActivity(QDateTime::currentDateTimeUtc())
    {}

    QString id() const { return m_id; }
    QDateTime created() const { return m_created; }
    QDateTime lastActivity() const { return m_lastActivity; }
    void touch() { m_lastActivity = QDateTime::currentDateTimeUtc(); }

    bool initialized() const { return m_initialized; }
    void setInitialized(bool v) { m_initialized = v; }

    // Negotiated MCP protocol version for this session. The default — applied
    // to legacy/auto-recovered sessions that never observed an `initialize` —
    // matches the spec's compatibility rule: assume `2025-03-26` for clients
    // that pre-date the `MCP-Protocol-Version` request header requirement.
    QString protocolVersion() const { return m_protocolVersion; }
    void setProtocolVersion(const QString& v) { m_protocolVersion = v; }

    QJsonObject clientCapabilities() const { return m_clientCapabilities; }
    void setClientCapabilities(const QJsonObject& caps) { m_clientCapabilities = caps; }

    // SSE stream socket (nullable — not all sessions have an active SSE connection)
    QTcpSocket* sseSocket() const { return m_sseSocket.data(); }
    void setSseSocket(QTcpSocket* socket) {
        m_sseSocket = socket;
        if (socket)
            m_hadSseSocket = true;
    }
    bool hadSseSocket() const { return m_hadSseSocket; }

    // A session is "stateful" only while it holds a *live* SSE stream — the one
    // thing that requires retained server-side state (server→client push). This
    // keys on a live socket (`!m_sseSocket.isNull()`), deliberately NOT the sticky
    // hadSseSocket(): the classification is transport statefulness, not "has any
    // server-side state" (a session mid machine-start confirmation has retained
    // state yet reports false here — the reaper guards that case separately).
    // Observed 2026-07-12: the cloud connectors re-`initialize` per request and
    // hold no stream open between exchanges — claude.ai (`Anthropic/ClaudeAI`) is
    // pure POST (never opens SSE); ChatGPT (`openai-mcp`) opens an SSE stream only
    // momentarily per exchange — so both are ephemeral except for ChatGPT's brief
    // per-call window. Only a LAN `mcp-remote` / Claude Desktop client keeps its
    // SSE open and is durably stateful. The client-name strings are illustrative
    // (logged, never branched on); the live SSE socket is the actual signal. Only
    // stateful sessions count toward MaxSessions, so per-request re-initializing
    // clients cannot exhaust the pool. See docs/CLAUDE_MD/MCP_SERVER.md.
    bool isStateful() const { return !m_sseSocket.isNull(); }

    // Resource subscriptions
    QSet<QString> subscribedResources() const { return m_subscribedResources; }
    void subscribe(const QString& uri) { m_subscribedResources.insert(uri); }
    void unsubscribe(const QString& uri) { m_subscribedResources.remove(uri); }

    // Rate limiting: count of control+settings calls in current window
    int controlCallCount() const { return m_controlCallCount; }
    void incrementControlCalls() { m_controlCallCount++; }
    void resetControlCalls() { m_controlCallCount = 0; }

    // True when the session arrived through the remote connector listener
    // (McpRemoteAccess) rather than the LAN /mcp route. Informational only —
    // access-level and confirmation gating are identical for both. Used for
    // status UI and log context. Sticky once set: a reconnecting client reusing
    // the session keeps its remote provenance.
    bool isRemote() const { return m_remote; }
    void setRemote(bool remote) { if (remote) m_remote = true; }

private:
    QString m_id;
    QDateTime m_created;
    QDateTime m_lastActivity;
    bool m_initialized = false;
    QJsonObject m_clientCapabilities;
    QPointer<QTcpSocket> m_sseSocket;
    bool m_hadSseSocket = false;
    QSet<QString> m_subscribedResources;
    int m_controlCallCount = 0;
    bool m_remote = false;
    QString m_protocolVersion = QStringLiteral("2025-03-26");
};
