#pragma once
#include <QObject>
#include <QPointer>
#include <QRemoteObjectNode>
#include <QRemoteObjectReplica>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

namespace RemoteServiceReplica {

// Network replicas must be Valid. The local in-process node can also expose
// an initialized replica without Valid state. In Qt 6.12,
// qremoteobjectreplica.cpp:308 counts Suspect as initialized, while
// setDisconnected() clears the connection and sets Suspect at lines 480-484.
inline bool isReady(QRemoteObjectNode* node, QRemoteObjectReplica* replica,
                    QRemoteObjectNode* publishedNode)
{
    return node && replica && (replica->isReplicaValid()
        || (node == publishedNode && replica->isInitialized()
            && replica->state() != QRemoteObjectReplica::Suspect));
}

// Replaces the standard eagerly owned replica. Domain request invalidation runs
// after detaching the node's destruction callback and before detaching the old
// replica, matching facade callback ordering. Signal wiring and state resets
// outside that boundary stay with the facade.
template<class Owner, class Replica, class BeforeReplace>
bool replace(Owner* owner, QRemoteObjectNode* nextNode,
             QPointer<QRemoteObjectNode>& currentNode, QPointer<Replica>& replica,
             QMetaObject::Connection& nodeDestroyed, BeforeReplace beforeReplace)
{
    if (currentNode == nextNode && replica && (nextNode || !nodeDestroyed))
        return false;
    QObject::disconnect(nodeDestroyed);
    nodeDestroyed = {};
    beforeReplace();
    if (replica) {
        QObject::disconnect(replica.data(), nullptr, owner, nullptr);
        replica->deleteLater();
    }
    currentNode = nextNode;
    replica = nextNode ? nextNode->acquire<Replica>() : new Replica;
    replica->setParent(owner);
    if (nextNode) {
        nodeDestroyed = QObject::connect(nextNode, &QObject::destroyed, owner,
                                        [owner] { owner->setNode(nullptr); });
    }
    return true;
}

} // namespace RemoteServiceReplica

// Every shared facade attaches to the same authenticated node. Keeping this
// contract common avoids per-domain connection/reconnection plumbing.
class RemoteService : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
public:
    using QObject::QObject;
    virtual void setNode(QRemoteObjectNode* node) = 0;
    // The transport supplies its already validated same-origin asset base.
    void setResourceOrigin(const QUrl& origin) { m_resourceOrigin = origin; }
protected:
    QUrl resourceOrigin() const { return m_resourceOrigin; }
private:
    QUrl m_resourceOrigin;
};
