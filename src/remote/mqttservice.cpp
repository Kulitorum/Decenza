#include "mqttservice.h"

#include <QJSEngine>
#include <QQmlEngine>
#include <QMetaObject>
#include <QRemoteObjectNode>
#include <QUuid>

namespace { QPointer<QRemoteObjectNode> s_publishedNode; }

MqttService::MqttService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

MqttService* MqttService::create(QQmlEngine* engine, QJSEngine*)
{
    auto* service = new MqttService(publishedNode(), engine);
    QJSEngine::setObjectOwnership(service, QJSEngine::CppOwnership);
    return service;
}

void MqttService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
QRemoteObjectNode* MqttService::publishedNode() { return s_publishedNode; }

bool MqttService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode);
}

bool MqttService::ready() const { return connected() && m_ready; }
qint64 MqttService::revision() const { return connected() ? m_revision : -1; }

QVariantMap MqttService::snapshot() const
{
    if (ready()) return m_snapshot;
    return {{QStringLiteral("connected"), false},
            {QStringLiteral("status"), QStringLiteral("Disconnected")},
            {QStringLiteral("shotServerRunning"), false},
            {QStringLiteral("shotServerUrl"), QString()}};
}

QString MqttService::newRequestId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

void MqttService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] { failPending(QStringLiteral("disconnected")); }))
        return;
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, [this] {
        const QPointer<MqttService> guard(this);
        if (!connected()) failPending(QStringLiteral("disconnected"));
        if (guard) updateFromReplica();
    });
    connect(m_state, &QRemoteObjectReplica::notified, this, &MqttService::updateFromReplica);
    connect(m_state, &MqttStateReplica::readyChanged, this, &MqttService::updateFromReplica);
    connect(m_state, &MqttStateReplica::revisionChanged, this, &MqttService::updateFromReplica);
    connect(m_state, &MqttStateReplica::snapshotChanged, this, &MqttService::updateFromReplica);
    connect(m_state, &MqttStateReplica::operationCompleted, this,
            [this](const QString& id, const QString& operation, bool success,
                   const QVariantMap& result, const QString& reason, qint64 revision) {
        if (!m_pending.finish(id, operation)) return;
        if (revision >= m_revision) m_revision = revision;
        const QPointer<MqttService> guard(this);
        updateFromReplica();
        if (!guard) return;
        emit busyChanged();
        if (!guard) return;
        emit operationCompleted(id, operation, success, result, reason, m_revision);
    }, Qt::QueuedConnection);
    updateFromReplica();
}

void MqttService::updateFromReplica()
{
    if (!connected()) {
        m_snapshot.clear();
        m_revision = -1;
        m_ready = false;
        emit stateChanged();
        return;
    }
    m_snapshot = m_state->snapshot();
    m_revision = m_state->revision();
    m_ready = m_state->ready();
    emit stateChanged();
}

void MqttService::failPending(const QString& reason)
{
    const QPointer<MqttService> guard(this);
    m_pending.failAll([guard] { if (guard) emit guard->busyChanged(); },
                      [guard, reason](const QString& id, const QString& operation) {
        if (!guard) return;
        QMetaObject::invokeMethod(guard.data(), [guard, id, operation, reason] {
            if (guard) emit guard->operationCompleted(id, operation, false, {}, reason, guard->revision());
        }, Qt::QueuedConnection);
    });
}

void MqttService::refresh()
{
    if (connected())
        QMetaObject::invokeMethod(m_state, &MqttStateReplica::refresh, Qt::QueuedConnection);
}

QString MqttService::execute(const QString& operation, const QVariantMap& arguments,
                             qint64 expectedRevision, const QString& requestId)
{
    const QString id = requestId.isEmpty() ? newRequestId() : requestId;
    if (id.isEmpty() || m_pending.contains(id)) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, {}, QStringLiteral("invalidRequestId"), revision());
        }, Qt::QueuedConnection);
        return id;
    }
    if (!ready()) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, {}, QStringLiteral("unavailable"), revision());
        }, Qt::QueuedConnection);
        return id;
    }
    if (m_pending.size() >= 32) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, {}, QStringLiteral("tooManyRequests"), revision());
        }, Qt::QueuedConnection);
        return id;
    }
    const auto ticket = m_pending.insert(id, operation);
    const QPointer<MqttService> guard(this);
    const QPointer<MqttStateReplica> requested = m_state;
    emit busyChanged();
    if (!guard) return id;
    m_pending.dispatch(this, id, ticket,
        [this, requested] { return requested && requested == m_state && connected(); },
        [requested, id, operation, arguments, expectedRevision] { requested->execute(id, operation, arguments, expectedRevision); },
        [this, id, operation] {
            const QPointer<MqttService> completionGuard(this);
            emit busyChanged();
            if (!completionGuard) return;
            emit operationCompleted(id, operation, false, {}, QStringLiteral("disconnected"), revision());
        });
    return id;
}
