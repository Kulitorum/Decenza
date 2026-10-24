#include "backupservice.h"
#include "backupoperationpolicy.h"

#include <QJSEngine>
#include <QMetaObject>
#include <QQmlEngine>
#include <QRemoteObjectNode>


namespace { QPointer<QRemoteObjectNode> s_publishedNode; }

BackupService* BackupService::create(QQmlEngine* engine, QJSEngine*)
{
    auto* service = new BackupService(s_publishedNode, engine);
    QJSEngine::setObjectOwnership(service, QJSEngine::CppOwnership);
    return service;
}
BackupService::BackupService(QRemoteObjectNode* node, QObject* parent) : RemoteService(parent)
{
    setNode(node);
}
BackupService::~BackupService()
{
    if (m_state) disconnect(m_state, nullptr, this, nullptr);
}
void BackupService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
QRemoteObjectNode* BackupService::publishedNode() { return s_publishedNode; }
bool BackupService::connected() const { return RemoteServiceReplica::isReady(m_node, m_state, publishedNode()); }
QVariantMap BackupService::snapshot() const
{
    return connected() && m_state->ready() ? m_state->snapshot() : QVariantMap();
}
bool BackupService::ready() const { return connected() && m_state->ready(); }

void BackupService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] { failPending(QStringLiteral("disconnected")); }))
        return;
    m_clientId = node ? node->property("decenzaClientId").toString() : QString();
    if (m_state) {
        connect(m_state, &QRemoteObjectReplica::stateChanged, this, &BackupService::changed);
        connect(m_state, &QRemoteObjectReplica::notified, this, &BackupService::changed);
        connect(m_state, &BackupStateReplica::snapshotChanged, this, &BackupService::stateChanged);
        connect(m_state, &BackupStateReplica::readyChanged, this, &BackupService::stateChanged);
        connect(m_state, &BackupStateReplica::operationCompleted, this,
                [this](const QString& id, const QString& operation, bool success,
                       const QVariantMap& result, const QString& reason) {
            if (!m_pending.finish(id, operation)) return;
            const QPointer<BackupService> completionGuard(this);
            emit busyChanged();
            if (!completionGuard) return;
            emit operationCompleted(id, operation, success, result, reason);
        }, Qt::QueuedConnection);
    }
    changed();
}

void BackupService::changed()
{
    const QPointer<BackupService> guard(this);
    if (!connected()) failPending(QStringLiteral("disconnected"));
    if (guard) emit stateChanged();
}
void BackupService::failPending(const QString& reason)
{
    const QPointer<BackupService> guard(this);
    m_pending.failAll([guard] { if (guard) emit guard->busyChanged(); },
                      [guard, reason](const QString& id, const QString& operation) {
        if (guard) emit guard->operationCompleted(id, operation, false, {}, reason);
    });
}
QString BackupService::newRequestId() const { return ServiceOperation::newId(); }
QString BackupService::execute(const QString& operation, const QVariantMap& arguments)
{
    const QString id = newRequestId();
    QVariantMap ownedArguments = arguments;
    ownedArguments.remove(QStringLiteral("clientId"));
    const bool fileOperation = BackupOperationPolicy::isFileOperation(operation);
    if (fileOperation) {
        if (m_clientId.isEmpty()) {
            const QPointer<BackupService> guard(this);
            QMetaObject::invokeMethod(this, [guard, id, operation] {
                if (guard) emit guard->operationCompleted(id, operation, false, {},
                                                          QStringLiteral("clientUnavailable"));
            }, Qt::QueuedConnection);
            return id;
        }
    }
    const auto reject = [this, id, operation](const QString& reason) {
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, {}, reason);
        }, Qt::QueuedConnection);
    };
    if (!connected() || !m_state->ready()) { reject(QStringLiteral("disconnected")); return id; }
    if (m_pending.size() >= 8) { reject(QStringLiteral("tooManyRequests")); return id; }
    if (!BackupOperationPolicy::supported(operation)
        || !BackupOperationPolicy::validArguments(operation, ownedArguments)) {
        reject(BackupOperationPolicy::supported(operation) ? QStringLiteral("invalidArguments")
                                                           : QStringLiteral("unsupportedOperation"));
        return id;
    }
    if (fileOperation)
        ownedArguments.insert(QStringLiteral("clientId"), m_clientId);
    const auto ticket = m_pending.insert(id, operation);
    const QPointer<BackupService> guard(this);
    const QPointer<BackupStateReplica> requested = m_state;
    emit busyChanged();
    if (!guard) return id;
    m_pending.dispatch(this, id, ticket,
        [this, requested] { return requested && requested == m_state && connected() && requested->ready(); },
        [requested, id, operation, ownedArguments] {
            requested->execute(id, operation, ownedArguments);
        },
        [this, id, operation] {
            const QPointer<BackupService> completionGuard(this);
            emit busyChanged();
            if (!completionGuard) return;
            emit operationCompleted(id, operation, false, {}, QStringLiteral("disconnected"));
        });
    return id;
}
void BackupService::refresh() { if (connected()) m_state->refresh(); }
