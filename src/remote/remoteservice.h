#pragma once
#include <QObject>
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
