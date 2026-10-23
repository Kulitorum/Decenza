#include "backupservice.h"
#include "backupoperationpolicy.h"
#include "serviceoperation.h"

#include <QJSEngine>
#include <QMetaObject>
#include <QQmlEngine>
#include <QRemoteObjectNode>

#include <utility>

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
    if (m_node == node && m_state && (node || !m_nodeDestroyed)) return;
    disconnect(m_nodeDestroyed);
    m_nodeDestroyed = {};
    failPending(QStringLiteral("disconnected"));
    if (m_state) {
        auto* old = m_state.data();
        disconnect(old, nullptr, this, nullptr);
        old->deleteLater();
    }
    m_state = nullptr;
    m_node = node;
    if (node) {
        m_nodeDestroyed = connect(node, &QObject::destroyed, this, [this] { setNode(nullptr); });
        m_state = node->acquire<BackupStateReplica>();
        if (m_state) m_state->setParent(this);
    } else {
        m_state = new BackupStateReplica;
        m_state->setParent(this);
    }
    if (m_state) {
        connect(m_state, &QRemoteObjectReplica::stateChanged, this, &BackupService::changed);
        connect(m_state, &QRemoteObjectReplica::notified, this, &BackupService::changed);
        connect(m_state, &BackupStateReplica::snapshotChanged, this, &BackupService::stateChanged);
        connect(m_state, &BackupStateReplica::readyChanged, this, &BackupService::stateChanged);
        connect(m_state, &BackupStateReplica::operationCompleted, this,
                [this](const QString& id, const QString& operation, bool success,
                       const QVariantMap& result, const QString& reason) {
            if (!m_pending.contains(id) || m_pending.value(id) != operation) return;
            m_pending.remove(id);
            emit busyChanged();
            emit operationCompleted(id, operation, success, result, reason);
        }, Qt::QueuedConnection);
    }
    changed();
}

void BackupService::changed()
{
    if (!connected()) failPending(QStringLiteral("disconnected"));
    emit stateChanged();
}
void BackupService::failPending(const QString& reason)
{
    if (m_pending.isEmpty()) return;
    const auto pending = m_pending;
    m_pending.clear();
    emit busyChanged();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        emit operationCompleted(it.key(), it.value(), false, {}, reason);
}
QString BackupService::newRequestId() const { return ServiceOperation::newId(); }
QString BackupService::execute(const QString& operation, const QVariantMap& arguments)
{
    const QString id = newRequestId();
    const auto reject = [this, id, operation](const QString& reason) {
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, {}, reason);
        }, Qt::QueuedConnection);
    };
    if (!connected() || !m_state->ready()) { reject(QStringLiteral("disconnected")); return id; }
    if (m_pending.size() >= 8) { reject(QStringLiteral("tooManyRequests")); return id; }
    if (!BackupOperationPolicy::supported(operation)
        || !BackupOperationPolicy::validArguments(operation, arguments)) {
        reject(BackupOperationPolicy::supported(operation) ? QStringLiteral("invalidArguments")
                                                           : QStringLiteral("unsupportedOperation"));
        return id;
    }
    m_pending.insert(id, operation);
    emit busyChanged();
    const QPointer<BackupStateReplica> requested = m_state;
    QMetaObject::invokeMethod(this, [this, requested, id, operation, arguments] {
        if (!m_pending.contains(id)) return;
        if (!requested || requested != m_state || !connected() || !requested->ready()) {
            m_pending.remove(id);
            emit busyChanged();
            emit operationCompleted(id, operation, false, {}, QStringLiteral("disconnected"));
            return;
        }
        requested->execute(id, operation, arguments);
    }, Qt::QueuedConnection);
    return id;
}
void BackupService::refresh() { if (connected()) m_state->refresh(); }
