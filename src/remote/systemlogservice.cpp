#include "systemlogservice.h"

#include "mcp/mcplogfilter.h"
#include "serviceoperation.h"
#include "systemlogfilepolicy.h"

#include <QJSEngine>
#include <QMetaObject>
#include <QPointer>
#include <QRemoteObjectNode>
#include <QQmlEngine>

namespace {
QPointer<QRemoteObjectNode> s_systemLogPublishedNode;
}

SystemLogService::SystemLogService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

void SystemLogService::setPublishedNode(QRemoteObjectNode* node)
{
    s_systemLogPublishedNode = node;
}

SystemLogService::~SystemLogService()
{
    if (m_state)
        disconnect(m_state, nullptr, this, nullptr);
}

QRemoteObjectNode* SystemLogService::publishedNode()
{
    return s_systemLogPublishedNode;
}

bool SystemLogService::ready() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, publishedNode())
        && m_state->ready();
}

void SystemLogService::setNode(QRemoteObjectNode* node)
{
    const quint64 generation = ++m_nodeGeneration;
    if (m_node == node && (node || !m_nodeDestroyed)) {
        syncReplica();
        return;
    }
    const QPointer<QRemoteObjectNode> requestedNode(node);
    disconnect(m_nodeDestroyed);
    m_nodeDestroyed = {};
    const QPointer<SystemLogService> guard(this);
    m_node = nullptr;
    m_clientId.clear();
    releaseReplica();
    if (!guard || generation != m_nodeGeneration)
        return;
    m_node = requestedNode;
    m_clientId = m_node ? m_node->property("decenzaClientId").toString() : QString();
    if (m_node) {
        m_nodeDestroyed = connect(m_node, &QObject::destroyed, this,
                                  [this] { setNode(nullptr); });
    }
    syncReplica();
}

void SystemLogService::setViewActive(QObject* view, bool active)
{
    if (!view)
        return;
    const auto it = m_activeViews.find(view);
    if (active) {
        if (it != m_activeViews.end())
            return;
        const auto destroyed = connect(view, &QObject::destroyed, this, [this, view] {
            m_activeViews.remove(view);
            syncReplica();
        });
        m_activeViews.insert(view, destroyed);
    } else {
        if (it == m_activeViews.end())
            return;
        disconnect(it.value());
        m_activeViews.erase(it);
    }
    syncReplica();
}

void SystemLogService::releaseReplica()
{
    if (!m_state)
        return;
    disconnect(m_state, nullptr, this, nullptr);
    auto* replica = m_state.data();
    m_state = nullptr;
    // Qt 6.12 reuses a live node implementation on reacquire without wiring
    // in-process source signals again (qremoteobjectnode.cpp:631-636,1528-1536).
    // Wait for retirement before acquiring; rapid hide/show is otherwise ready
    // but cannot receive snapshot completions or live lines.
    m_retiringReplica = replica;
    connect(replica, &QObject::destroyed, this, [this] {
        m_retiringReplica = nullptr;
        syncReplica();
    });
    replica->deleteLater();
    const QPointer<SystemLogService> guard(this);
    failPending();
    if (guard) emit stateChanged();
}

void SystemLogService::syncReplica()
{
    if (!m_node || (m_activeViews.isEmpty() && m_pending.isEmpty() && !m_deliveringFileReply)) {
        releaseReplica();
        return;
    }
    if (m_state || m_retiringReplica)
        return;

    m_state = m_node->acquire<SystemLogStateReplica>();
    m_state->setParent(this);
    // Keep request IDs available to QML before any completion can arrive,
    // including local/in-process QtRO configurations.
    connect(m_state, &SystemLogStateReplica::snapshotCompleted,
            this, &SystemLogService::snapshotCompleted, Qt::QueuedConnection);
    connect(m_state, &SystemLogStateReplica::boundaryCompleted,
            this, &SystemLogService::boundaryCompleted, Qt::QueuedConnection);
    connect(m_state, &SystemLogStateReplica::lineAppended,
            this, &SystemLogService::lineAppended);
    connect(m_state, &SystemLogStateReplica::operationCompleted, this,
            [this, requested = m_state](const QString& id, const QString& operation, bool success,
                                       const QVariantMap& result, const QString& reason) {
        if (requested != m_state || !m_pending.finish(id, operation)) return;
        const QPointer<SystemLogService> guard(this);
        m_deliveringFileReply = true;
        emit operationCompleted(id, operation, success, result, reason);
        if (!guard) return;
        m_deliveringFileReply = false;
        syncReplica();
    }, Qt::QueuedConnection);
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, [this] {
        const QPointer<SystemLogService> guard(this);
        if (!ready()) failPending();
        if (guard) emit stateChanged();
    });
    connect(m_state, &QRemoteObjectReplica::notified,
            this, &SystemLogService::stateChanged);
    connect(m_state, &SystemLogStateReplica::readyChanged,
            this, &SystemLogService::stateChanged);
    emit stateChanged();
}

QString SystemLogService::requestSnapshot(const QStringList& markers,
                                          const QString& minLevel,
                                          int maxLines)
{
    const QString requestId = ServiceOperation::newId();
    if (!ready() || !m_state) {
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit snapshotCompleted(requestId, false, {}, 0,
                                   tr("Host log service is unavailable."));
        }, Qt::QueuedConnection);
        return requestId;
    }
    m_state->requestSnapshot(requestId, markers, minLevel, maxLines);
    return requestId;
}

QString SystemLogService::requestBoundary()
{
    const QString requestId = ServiceOperation::newId();
    if (!ready() || !m_state) {
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit boundaryCompleted(requestId, false, 0,
                                   tr("Host log service is unavailable."));
        }, Qt::QueuedConnection);
        return requestId;
    }
    m_state->requestBoundary(requestId);
    return requestId;
}

bool SystemLogService::lineMatches(const QString& line,
                                   const QStringList& markers,
                                   const QString& minLevel) const
{
    return McpLogFilter::lineMatches(line, markers, minLevel);
}

void SystemLogService::failPending()
{
    const QPointer<SystemLogService> guard(this);
    m_pending.failAll([] {}, [guard](const QString& id, const QString& operation) {
        if (guard) emit guard->operationCompleted(id, operation, false, {}, QStringLiteral("disconnected"));
    });
}

QString SystemLogService::execute(const QString& operation, const QVariantMap& arguments)
{
    const QString id = ServiceOperation::newId();
    QVariantMap owned = arguments;
    owned.remove(QStringLiteral("clientId"));
    const auto reject = [this, id, operation](const QString& reason) {
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, {}, reason);
        }, Qt::QueuedConnection);
    };
    if (!ready()) { reject(QStringLiteral("disconnected")); return id; }
    if (m_clientId.isEmpty()) { reject(QStringLiteral("clientUnavailable")); return id; }
    if (!SystemLogFilePolicy::supported(operation)
        || !SystemLogFilePolicy::validArguments(operation, owned)) {
        reject(SystemLogFilePolicy::supported(operation) ? QStringLiteral("invalidArguments")
                                                       : QStringLiteral("unsupportedOperation"));
        return id;
    }
    if (m_pending.size() >= 8) { reject(QStringLiteral("tooManyRequests")); return id; }
    owned.insert(QStringLiteral("clientId"), m_clientId);
    const QPointer<SystemLogStateReplica> requested = m_state;
    const auto ticket = m_pending.insert(id, operation);
    m_pending.dispatch(this, id, ticket,
        [this, requested] { return requested && requested == m_state && ready(); },
        [requested, id, operation, owned] { requested->execute(id, operation, owned); },
        [this, id, operation] {
            const QPointer<SystemLogService> guard(this);
            emit operationCompleted(id, operation, false, {}, QStringLiteral("disconnected"));
            if (guard) syncReplica();
        });
    return id;
}
