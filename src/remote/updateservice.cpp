#include "updateservice.h"
#include "updateoperationpolicy.h"

#include <QJSEngine>
#include <QMetaObject>
#include <QQmlEngine>
#include <QRemoteObjectNode>

namespace { QPointer<QRemoteObjectNode> s_publishedNode; }

UpdateService* UpdateService::create(QQmlEngine* engine, QJSEngine*)
{
    auto* service = new UpdateService(s_publishedNode, engine);
    QJSEngine::setObjectOwnership(service, QJSEngine::CppOwnership);
    return service;
}
UpdateService::UpdateService(QRemoteObjectNode* node, QObject* parent) : RemoteService(parent) { setNode(node); }
UpdateService::~UpdateService() { if (m_state) disconnect(m_state, nullptr, this, nullptr); }
void UpdateService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
QRemoteObjectNode* UpdateService::publishedNode() { return s_publishedNode; }
bool UpdateService::connected() const { return RemoteServiceReplica::isReady(m_node, m_state, publishedNode()); }
QVariantMap UpdateService::snapshot() const { return connected() && m_state->ready() ? m_state->snapshot() : QVariantMap(); }
bool UpdateService::ready() const { return connected() && m_state->ready(); }

void UpdateService::setNode(QRemoteObjectNode* node)
{
    if (m_node == node && m_state && (node || !m_nodeDestroyed)) return;
    disconnect(m_nodeDestroyed); m_nodeDestroyed = {};
    failPending(QStringLiteral("disconnected"));
    if (m_state) { auto* old = m_state.data(); disconnect(old, nullptr, this, nullptr); old->deleteLater(); }
    m_node = node;
    if (node) {
        m_nodeDestroyed = connect(node, &QObject::destroyed, this, [this] { setNode(nullptr); });
        m_state = node->acquire<UpdateStateReplica>();
    } else {
        m_state = new UpdateStateReplica;
    }
    if (m_state) {
        m_state->setParent(this);
        connect(m_state, &QRemoteObjectReplica::stateChanged, this, &UpdateService::changed);
        connect(m_state, &QRemoteObjectReplica::notified, this, &UpdateService::changed);
        connect(m_state, &UpdateStateReplica::snapshotChanged, this, &UpdateService::stateChanged);
        connect(m_state, &UpdateStateReplica::readyChanged, this, &UpdateService::stateChanged);
        connect(m_state, &UpdateStateReplica::operationCompleted, this,
            [this](const QString& id, const QString& operation, bool success,
                   const QVariantMap& result, const QString& reason) {
                if (!m_pending.finish(id, operation)) return;
                emit busyChanged();
                emit operationCompleted(id, operation, success, result, reason);
            }, Qt::QueuedConnection);
    }
    changed();
}

void UpdateService::changed()
{
    if (!connected()) failPending(QStringLiteral("disconnected"));
    emit stateChanged();
}
void UpdateService::failPending(const QString& reason)
{
    m_pending.failAll([this] { emit busyChanged(); },
                      [this, reason](const QString& id, const QString& operation) {
        emit operationCompleted(id, operation, false, {}, reason);
    });
}
QString UpdateService::newRequestId() const { return ServiceOperation::newId(); }
QString UpdateService::execute(const QString& operation, const QVariantMap& arguments)
{
    const QString id = newRequestId();
    const auto reject = [this, id, operation](const QString& reason) {
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, {}, reason);
        }, Qt::QueuedConnection);
    };
    if (!connected() || !m_state->ready()) { reject(QStringLiteral("disconnected")); return id; }
    if (m_pending.size() >= 8) { reject(QStringLiteral("tooManyRequests")); return id; }
    if (!UpdateOperationPolicy::supported(operation)
        || !UpdateOperationPolicy::validArguments(operation, arguments)) {
        reject(UpdateOperationPolicy::supported(operation) ? QStringLiteral("invalidArguments")
                                                           : QStringLiteral("unsupportedOperation"));
        return id;
    }
    const auto ticket = m_pending.insert(id, operation);
    emit busyChanged();
    const QPointer<UpdateStateReplica> requested = m_state;
    m_pending.dispatch(this, id, ticket,
        [this, requested] { return requested && requested == m_state && connected() && requested->ready(); },
        [requested, id, operation, arguments] { requested->execute(id, operation, arguments); },
        [this, id, operation] {
            emit busyChanged();
            emit operationCompleted(id, operation, false, {}, QStringLiteral("disconnected"));
        });
    return id;
}
void UpdateService::refresh() { if (connected()) m_state->refresh(); }
