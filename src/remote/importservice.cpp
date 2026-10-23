#include "importservice.h"
#include "serviceoperation.h"

#include <QJSEngine>
#include <QMetaObject>
#include <QRemoteObjectNode>
#include <QQmlEngine>

namespace { QPointer<QRemoteObjectNode> s_publishedNode; }

ImportService* ImportService::create(QQmlEngine* engine)
{
    auto* service = new ImportService(s_publishedNode, engine);
    QJSEngine::setObjectOwnership(service, QJSEngine::CppOwnership);
    return service;
}

ImportService::ImportService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent) { setNode(node); }

ImportService::~ImportService()
{
    if (m_state) disconnect(m_state, nullptr, this, nullptr);
}

void ImportService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
QRemoteObjectNode* ImportService::publishedNode() { return s_publishedNode; }

bool ImportService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, publishedNode());
}

QVariantMap ImportService::snapshot() const
{
    return connected() && m_state->ready() ? m_state->snapshot() : QVariantMap();
}

bool ImportService::ready() const { return connected() && m_state->ready(); }

void ImportService::setNode(QRemoteObjectNode* node)
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
    m_node = node;
    if (node) {
        m_nodeDestroyed = connect(node, &QObject::destroyed, this, [this] { setNode(nullptr); });
        m_state = node->acquire<ImportStateReplica>();
    } else {
        m_state = new ImportStateReplica;
    }
    m_state->setParent(this);
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, &ImportService::changed);
    connect(m_state, &QRemoteObjectReplica::notified, this, &ImportService::changed);
    connect(m_state, &ImportStateReplica::snapshotChanged, this, &ImportService::changed);
    connect(m_state, &ImportStateReplica::readyChanged, this, &ImportService::changed);
    connect(m_state, &ImportStateReplica::operationCompleted, this,
            [this](const QString& id, const QString& operation, bool success,
                   const QVariantMap& result, const QString& reason) {
        if (!m_pending.contains(id) || m_pending.value(id) != operation) return;
        m_pending.remove(id);
        emit busyChanged();
        emit operationCompleted(id, operation, success, result, reason);
    }, Qt::QueuedConnection);
    connect(m_state, &ImportStateReplica::operationProgress, this,
            [this](const QString& id, const QVariantMap& snapshot) {
        if (m_pending.contains(id)) emit operationProgress(id, snapshot);
    }, Qt::QueuedConnection);
    emit stateChanged();
}

void ImportService::changed()
{
    if (!connected()) failPending(QStringLiteral("disconnected"));
    emit stateChanged();
}

void ImportService::failPending(const QString& reason)
{
    if (m_pending.isEmpty()) return;
    const auto pending = m_pending;
    m_pending.clear();
    emit busyChanged();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        emit operationCompleted(it.key(), it.value(), false, {}, reason);
}

QString ImportService::newRequestId() const { return ServiceOperation::newId(); }

QString ImportService::execute(const QString& operation, const QVariantMap& arguments)
{
    const QString id = newRequestId();
    const auto reject = [this, id, operation](const QString& reason) {
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, {}, reason);
        }, Qt::QueuedConnection);
    };
    if (!connected() || !m_state->ready()) {
        reject(QStringLiteral("disconnected"));
        return id;
    }
    const bool interrupt = operation == QLatin1String("cancel")
        || operation == QLatin1String("cancelTransfer");
    if (!m_pending.isEmpty() && !interrupt) {
        reject(QStringLiteral("busy"));
        return id;
    }
    if (m_pending.size() >= 16) {
        reject(QStringLiteral("tooManyRequests"));
        return id;
    }
    m_pending.insert(id, operation);
    emit busyChanged();
    m_state->execute(id, operation, arguments);
    return id;
}

void ImportService::refresh()
{
    if (connected()) QMetaObject::invokeMethod(m_state, &ImportStateReplica::refresh, Qt::QueuedConnection);
}
