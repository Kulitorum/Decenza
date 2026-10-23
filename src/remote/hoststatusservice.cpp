#include "hoststatusservice.h"

namespace {
QPointer<QRemoteObjectNode> s_hostStatusNode;
}

HostStatusService::HostStatusService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

void HostStatusService::setPublishedNode(QRemoteObjectNode* node) { s_hostStatusNode = node; }
QRemoteObjectNode* HostStatusService::publishedNode() { return s_hostStatusNode; }

bool HostStatusService::ready() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_hostStatusNode);
}

void HostStatusService::setNode(QRemoteObjectNode* node)
{
    if (m_node == node && m_state && (node || !m_nodeDestroyed)) return;
    disconnect(m_nodeDestroyed);
    m_nodeDestroyed = {};
    if (m_state) {
        disconnect(m_state, nullptr, this, nullptr);
        m_state->deleteLater();
    }
    m_node = node;
    m_state = node ? node->acquire<HostStatusStateReplica>() : new HostStatusStateReplica;
    m_state->setParent(this);
    if (node) {
        m_nodeDestroyed = connect(node, &QObject::destroyed, this, [this] { setNode(nullptr); });
    }
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, &HostStatusService::stateChanged);
    connect(m_state, &QRemoteObjectReplica::notified, this, &HostStatusService::stateChanged);
    emit stateChanged();
}
