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

    // Negotiated MCP protocol version for this session.
    //
    // The default applies ONLY to a session that never observed an `initialize`
    // — an auto-recovered one. It is not what a missing `MCP-Protocol-Version`
    // header falls back to: an initialized session answers under whatever it
    // negotiated, header or no header. (An earlier version of this comment
    // claimed otherwise, which made the deviation below look far broader than
    // it is.)
    //
    // For that narrow case the spec's rule is conditional — assume `2025-03-26`
    // when the server "has no other way to identify the version, for example by
    // relying on the protocol version negotiated during initialization". A
    // never-initialized session genuinely has no other way, so the rule binds
    // and we depart from it: `2025-03-26` is no longer NEGOTIABLE, and adopting
    // a version we would not negotiate is not a coherent state. The lowest
    // supported revision is used instead.
    //
    // Safe direction for THIS case: the lowest supported revision emits strictly
    // fewer optional fields than any above it, so such a session is under-served
    // rather than sent fields its revision does not define.
    //
    // Do not generalise that to negotiation, which deliberately does the
    // opposite: an unsupported `initialize` is answered with the NEWEST revision,
    // because the spec says it SHOULD be and the client is expected to
    // disconnect if it cannot cope. See handleInitialize().
    //
    // Note `2025-03-26` is still accepted as a HEADER value — refused to
    // negotiate, not refused outright. See resolveSessionForMessage().
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
    QString m_protocolVersion = QStringLiteral("2025-06-18");
};
