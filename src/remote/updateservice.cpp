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
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] { failPending(QStringLiteral("disconnected")); }))
        return;
    if (m_state) {
        connect(m_state, &QRemoteObjectReplica::stateChanged, this, &UpdateService::changed);
        connect(m_state, &QRemoteObjectReplica::notified, this, &UpdateService::changed);
        connect(m_state, &UpdateStateReplica::snapshotChanged, this, &UpdateService::stateChanged);
        connect(m_state, &UpdateStateReplica::readyChanged, this, &UpdateService::stateChanged);
        connect(m_state, &UpdateStateReplica::operationCompleted, this,
            [this](const QString& id, const QString& operation, bool success,
                   const QVariantMap& result, const QString& reason) {
                if (!m_pending.finish(id, operation)) return;
                const QPointer<UpdateService> completionGuard(this);
                emit busyChanged();
                if (!completionGuard) return;
                emit operationCompleted(id, operation, success, result, reason);
            }, Qt::QueuedConnection);
    }
    changed();
}

void UpdateService::changed()
{
    const QPointer<UpdateService> guard(this);
    if (!connected()) failPending(QStringLiteral("disconnected"));
    if (guard) emit stateChanged();
}
void UpdateService::failPending(const QString& reason)
{
    const QPointer<UpdateService> guard(this);
    m_pending.failAll([guard] { if (guard) emit guard->busyChanged(); },
                      [guard, reason](const QString& id, const QString& operation) {
        if (guard) emit guard->operationCompleted(id, operation, false, {}, reason);
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
    const QPointer<UpdateService> guard(this);
    const QPointer<UpdateStateReplica> requested = m_state;
    emit busyChanged();
    if (!guard) return id;
    m_pending.dispatch(this, id, ticket,
        [this, requested] { return requested && requested == m_state && connected() && requested->ready(); },
        [requested, id, operation, arguments] { requested->execute(id, operation, arguments); },
        [this, id, operation] {
            const QPointer<UpdateService> completionGuard(this);
            emit busyChanged();
            if (!completionGuard) return;
            emit operationCompleted(id, operation, false, {}, QStringLiteral("disconnected"));
        });
    return id;
}
void UpdateService::refresh() { if (connected()) m_state->refresh(); }
