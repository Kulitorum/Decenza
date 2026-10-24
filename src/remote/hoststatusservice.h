#pragma once

#include "remoteservice.h"
#include "rep_hoststatusservice_replica.h"
#include <QPointer>

class HostStatusService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(HostStatusStateReplica* state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
public:
    HostStatusStateReplica* state() const { return m_state; }
    bool ready() const;
    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);
signals:
    void stateChanged();
private:
    friend struct HostStatusServiceForeign;
    explicit HostStatusService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    QPointer<QRemoteObjectNode> m_node;
    QPointer<HostStatusStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
};
