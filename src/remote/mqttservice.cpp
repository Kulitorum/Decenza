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
        m_state = node->acquire<MqttStateReplica>();
        if (m_state) m_state->setParent(this);
    } else {
        m_state = new MqttStateReplica;
        m_state->setParent(this);
    }
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, [this] {
        if (!connected()) failPending(QStringLiteral("disconnected"));
        updateFromReplica();
    });
    connect(m_state, &QRemoteObjectReplica::notified, this, &MqttService::updateFromReplica);
    connect(m_state, &MqttStateReplica::readyChanged, this, &MqttService::updateFromReplica);
    connect(m_state, &MqttStateReplica::revisionChanged, this, &MqttService::updateFromReplica);
    connect(m_state, &MqttStateReplica::snapshotChanged, this, &MqttService::updateFromReplica);
    connect(m_state, &MqttStateReplica::operationCompleted, this,
            [this](const QString& id, const QString& operation, bool success,
                   const QVariantMap& result, const QString& reason, qint64 revision) {
        if (!m_pending.contains(id) || m_pending.value(id) != operation) return;
        m_pending.remove(id);
        if (revision >= m_revision) m_revision = revision;
        updateFromReplica();
        emit busyChanged();
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
    if (m_pending.isEmpty()) return;
    const auto pending = m_pending;
    m_pending.clear();
    emit busyChanged();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const QString id = it.key();
        const QString operation = it.value();
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, {}, reason, revision());
        }, Qt::QueuedConnection);
    }
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
    m_pending.insert(id, operation);
    emit busyChanged();
    const QPointer<MqttStateReplica> requestedState = m_state;
    QMetaObject::invokeMethod(this, [this, requestedState, id, operation, arguments, expectedRevision] {
        if (!m_pending.contains(id)) return;
        if (!requestedState || requestedState != m_state || !connected()) {
            m_pending.remove(id);
            emit busyChanged();
            emit operationCompleted(id, operation, false, {}, QStringLiteral("disconnected"), revision());
            return;
        }
        requestedState->execute(id, operation, arguments, expectedRevision);
    }, Qt::QueuedConnection);
    return id;
}
