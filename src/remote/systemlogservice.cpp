#include "systemlogservice.h"

#include "mcp/mcplogfilter.h"
#include "serviceoperation.h"

#include <QJSEngine>
#include <QMetaObject>
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
    if (m_node == node && (node || !m_nodeDestroyed)) {
        syncReplica();
        return;
    }
    disconnect(m_nodeDestroyed);
    m_nodeDestroyed = {};
    releaseReplica();
    m_node = node;
    if (node) {
        m_nodeDestroyed = connect(node, &QObject::destroyed, this,
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
    replica->deleteLater();
    emit stateChanged();
}

void SystemLogService::syncReplica()
{
    if (!m_node || m_activeViews.isEmpty()) {
        releaseReplica();
        return;
    }
    if (m_state)
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
    connect(m_state, &QRemoteObjectReplica::stateChanged,
            this, &SystemLogService::stateChanged);
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
