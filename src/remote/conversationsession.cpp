#include "conversationsession.h"

#include <QMetaObject>
#include <QRemoteObjectNode>
#include <QUrl>
#include <QUuid>
#include <utility>

namespace {
QPointer<QRemoteObjectNode> s_conversationNode;
QString s_nativeSessionId;
}

ConversationSession::ConversationSession(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent), m_clientId(nativeSessionId())
{
    if (m_clientId.isEmpty())
        m_clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    setNode(node);
}

ConversationSession::~ConversationSession()
{
    m_closing = true;
    if (m_replica && m_replica->isReplicaValid())
        m_replica->closeClient(m_clientId);
    if (m_replica)
        disconnect(m_replica, nullptr, this, nullptr);
}

void ConversationSession::setPublishedNode(QRemoteObjectNode* node)
{
    s_conversationNode = node;
}

void ConversationSession::setNativeSessionId(const QString& clientId)
{
    s_nativeSessionId = clientId;
}

QRemoteObjectNode* ConversationSession::publishedNode()
{
    return s_conversationNode;
}

QString ConversationSession::nativeSessionId()
{
    return s_nativeSessionId;
}

bool ConversationSession::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_replica, s_conversationNode);
}

QString ConversationSession::reportUrl() const
{
    if (!resourceOrigin().isEmpty())
        return resourceOrigin().resolved(QUrl(QStringLiteral("/ai-conversations"))).toString();
    return m_state.value(QStringLiteral("reportUrl")).toString();
}

void ConversationSession::setNode(QRemoteObjectNode* node)
{
    if (m_node == node && m_replica && (node || !m_nodeDestroyed))
        return;

    // A transport can be replaced while an overlay request is outstanding.
    // Retire those request IDs before detaching the old replica so callers get
    // a terminal result instead of waiting for a reply that can no longer arrive.
    const auto pending = std::exchange(m_pending, {});

    disconnect(m_nodeDestroyed);
    m_nodeDestroyed = {};
    if (m_replica) {
        disconnect(m_replica, nullptr, this, nullptr);
        m_replica->deleteLater();
    }
    m_node = node;
    if (node) {
        const QString clientId = node->property("decenzaClientId").toString();
        if (!clientId.isEmpty())
            m_clientId = clientId;
    }
    m_ready = false;
    m_state.clear();
    m_errorMessage = node ? QString() : tr("AI conversation is disconnected.");
    m_replica = node ? node->acquire<ConversationSessionStateReplica>()
                     : new ConversationSessionStateReplica;
    m_replica->setParent(this);
    if (node) {
        m_nodeDestroyed = connect(node, &QObject::destroyed, this, [this] {
            setNode(nullptr);
        });
    }
    connect(m_replica, &QRemoteObjectReplica::stateChanged, this, [this] {
        if (connected()) refresh();
        else {
            m_ready = false;
            m_state.clear();
            const auto lostRequests = std::exchange(m_pending, {});
            for (auto it = lostRequests.cbegin(); it != lostRequests.cend(); ++it) {
                if (it.value() != QStringLiteral("state"))
                    emit completed(it.key(), it.value(), false, {}, QStringLiteral("disconnected"));
            }
            m_errorMessage = tr("AI conversation is disconnected.");
            emit stateChanged();
        }
    });
    connect(m_replica, &ConversationSessionStateReplica::stateReady, this,
            &ConversationSession::receiveStateReady, Qt::QueuedConnection);
    connect(m_replica, &ConversationSessionStateReplica::stateChanged, this,
            &ConversationSession::receiveState, Qt::QueuedConnection);
    connect(m_replica, &ConversationSessionStateReplica::operationCompleted, this,
            &ConversationSession::receiveCompletion, Qt::QueuedConnection);
    if (connected()) refresh();
    emit stateChanged();

    // Completion handlers may issue another request, so replace the replica
    // and update public state before reporting requests retired by the change.
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        if (it.value() != QStringLiteral("state"))
            emit completed(it.key(), it.value(), false, {}, QStringLiteral("disconnected"));
    }
}

void ConversationSession::refresh()
{
    if (connected())
        send(QStringLiteral("state"));
}

QString ConversationSession::request(const QString& operation, const QVariantMap& fields)
{
    return send(operation, fields);
}

QString ConversationSession::send(const QString& operation, const QVariantMap& fields)
{
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_pending.insert(requestId, operation);
    if (!connected()) {
        QMetaObject::invokeMethod(this, [this, requestId, operation] {
            if (!m_pending.remove(requestId)) return;
            if (operation == QStringLiteral("state")) {
                m_errorMessage = tr("AI conversation is disconnected.");
                emit stateChanged();
            } else {
                emit completed(requestId, operation, false, {}, QStringLiteral("disconnected"));
            }
        }, Qt::QueuedConnection);
        return requestId;
    }

    const QString client = m_clientId;
    if (operation == QStringLiteral("state")) {
        m_replica->requestState(client, requestId);
    } else if (operation == QStringLiteral("loadMostRecent")) {
        m_replica->loadMostRecent(client, requestId);
    } else if (operation == QStringLiteral("openShot")) {
        m_replica->openShot(client, requestId, fields.value(QStringLiteral("shot")).toMap(),
            fields.value(QStringLiteral("shotId")).toLongLong(),
            fields.value(QStringLiteral("shotLabel")).toString());
    } else if (operation == QStringLiteral("sendMessage")) {
        m_replica->sendMessage(client, requestId, fields.value(QStringLiteral("text")).toString());
    } else if (operation == QStringLiteral("retry")) {
        m_replica->retry(client, requestId);
    } else if (operation == QStringLiteral("clear")) {
        m_replica->clear(client, requestId);
    } else if (operation == QStringLiteral("dismissShot")) {
        m_replica->dismissShot(client, requestId);
    } else if (operation == QStringLiteral("save")) {
        m_replica->save(client, requestId);
    } else {
        QMetaObject::invokeMethod(this, [this, requestId, operation] {
            if (m_closing || m_pending.take(requestId) != operation)
                return;
            emit completed(requestId, operation, false, {},
                           QStringLiteral("unsupportedOperation"));
        }, Qt::QueuedConnection);
    }
    return requestId;
}

void ConversationSession::receiveState(const QString& clientId, const QVariantMap& state)
{
    if (!m_closing && clientId == m_clientId) {
        m_state = state;
        m_ready = true;
        m_errorMessage.clear();
        emit stateChanged();
    }
}

void ConversationSession::receiveStateReady(const QString& clientId, const QString& requestId,
                                            const QVariantMap& state)
{
    if (m_closing || clientId != m_clientId || m_pending.value(requestId) != QStringLiteral("state"))
        return;
    m_pending.remove(requestId);
    m_state = state;
    m_ready = true;
    m_errorMessage.clear();
    emit stateChanged();
}

void ConversationSession::receiveCompletion(const QString& clientId, const QString& requestId,
                                            const QString& operation, bool success,
                                            const QVariantMap& result, const QString& reason)
{
    if (m_closing || clientId != m_clientId || !m_pending.contains(requestId))
        return;
    const QString expected = m_pending.take(requestId);
    if (expected != operation)
        return;
    if (operation == QStringLiteral("state")) {
        if (!success) {
            m_ready = false;
            m_errorMessage = reason;
            emit stateChanged();
        }
        return;
    }
    emit completed(requestId, operation, success, result, reason);
}
