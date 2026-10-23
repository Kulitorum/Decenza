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
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [] {}))
        return;
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, &HostStatusService::stateChanged);
    connect(m_state, &QRemoteObjectReplica::notified, this, &HostStatusService::stateChanged);
    emit stateChanged();
}
