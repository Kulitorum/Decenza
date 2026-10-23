#include "aisettingsservice.h"

#include <QJSEngine>
#include <QMetaObject>
#include <QRemoteObjectPendingCallWatcher>
#include <QQmlEngine>
#include <QUuid>

#include <utility>

namespace { QPointer<QRemoteObjectNode> s_publishedNode; }

AISettingsService* AISettingsService::create(QQmlEngine* engine, QJSEngine*)
{
    auto* service = new AISettingsService(s_publishedNode, engine);
    QJSEngine::setObjectOwnership(service, QJSEngine::CppOwnership);
    return service;
}

AISettingsService::AISettingsService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

void AISettingsService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
QRemoteObjectNode* AISettingsService::publishedNode() { return s_publishedNode; }

bool AISettingsService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode);
}

void AISettingsService::setNode(QRemoteObjectNode* node)
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
    m_state = node ? node->acquire<AISettingsStateReplica>() : new AISettingsStateReplica;
    m_state->setParent(this);
    if (node) m_nodeDestroyed = connect(node, &QObject::destroyed, this, [this] { setNode(nullptr); });
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, [this] {
        if (!connected()) failPending(QStringLiteral("disconnected"));
        emit stateChanged();
    });
    connect(m_state, &QRemoteObjectReplica::notified, this, &AISettingsService::stateChanged);
    connect(m_state, &AISettingsStateReplica::snapshotChanged, this, &AISettingsService::stateChanged);
    connect(m_state, &AISettingsStateReplica::readyChanged, this, &AISettingsService::stateChanged);
    connect(m_state, &AISettingsStateReplica::operationCompleted, this,
            &AISettingsService::finishPending, Qt::QueuedConnection);
    emit stateChanged();
}

QVariantMap AISettingsService::snapshot() const
{
    return connected() && m_state->ready() ? m_state->snapshot() : QVariantMap();
}

bool AISettingsService::ready() const { return connected() && m_state->ready(); }

QString AISettingsService::newRequestId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

void AISettingsService::refresh()
{
    if (connected()) m_state->refresh();
}

QString AISettingsService::request(const QString& operation)
{
    static const QStringList allowed{
        QStringLiteral("testConnection"), QStringLiteral("refreshOllamaModels"),
        QStringLiteral("requestMcpUrls"), QStringLiteral("rotateMcpToken"),
        QStringLiteral("forgetTailscale")};
    const QString requestId = newRequestId();
    if (!allowed.contains(operation)) {
        QMetaObject::invokeMethod(this, [this, requestId, operation] {
            emit completed(requestId, operation, false, {}, QStringLiteral("invalidOperation"));
        }, Qt::QueuedConnection);
        return requestId;
    }
    if (!ready()) {
        QMetaObject::invokeMethod(this, [this, requestId, operation] {
            emit completed(requestId, operation, false, {}, QStringLiteral("unavailable"));
        }, Qt::QueuedConnection);
        return requestId;
    }
    if (m_pending.size() >= 32) {
        QMetaObject::invokeMethod(this, [this, requestId, operation] {
            emit completed(requestId, operation, false, {}, QStringLiteral("busy"));
        }, Qt::QueuedConnection);
        return requestId;
    }
    m_pending.insert(requestId, operation);
    if (operation == QLatin1String("testConnection")) m_state->testConnection(requestId);
    else if (operation == QLatin1String("refreshOllamaModels")) m_state->refreshOllamaModels(requestId);
    else if (operation == QLatin1String("requestMcpUrls")
             || operation == QLatin1String("rotateMcpToken")) {
        const auto reply = operation == QLatin1String("requestMcpUrls")
            ? m_state->requestMcpUrls() : m_state->rotateMcpToken();
        auto* watcher = new QRemoteObjectPendingCallWatcher(reply, this);
        connect(watcher, &QRemoteObjectPendingCallWatcher::finished, this,
                [this, watcher, requestId, operation] {
            if (m_pending.value(requestId) == operation) {
                m_pending.remove(requestId);
                bool success = watcher->error() == QRemoteObjectPendingCall::NoError;
                QVariantMap result;
                QString reason;
                if (success) {
                    const QVariantMap response = watcher->returnValue().toMap();
                    success = response.value(QStringLiteral("success")).toBool();
                    result = response.value(QStringLiteral("result")).toMap();
                    reason = response.value(QStringLiteral("reason")).toString();
                    if (!success && reason.isEmpty()) reason = QStringLiteral("requestFailed");
                } else {
                    reason = QStringLiteral("disconnected");
                }
                if (!success) {
                    m_errorMessage = reason;
                } else {
                    m_errorMessage.clear();
                }
                emit stateChanged();
                emit completed(requestId, operation, success, result, reason);
            }
            watcher->deleteLater();
        });
    } else m_state->forgetTailscale(requestId);
    return requestId;
}

void AISettingsService::finishPending(const QString& requestId, const QString& operation,
                                      bool success, const QVariantMap& result,
                                      const QString& reason)
{
    if (m_pending.value(requestId) != operation) return;
    m_pending.remove(requestId);
    if (!success) {
        m_errorMessage = reason;
    } else {
        m_errorMessage.clear();
    }
    emit stateChanged();
    emit completed(requestId, operation, success, result, reason);
}

void AISettingsService::failPending(const QString& reason)
{
    const auto pending = std::exchange(m_pending, {});
    if (pending.isEmpty()) return;
    m_errorMessage = reason;
    emit stateChanged();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const QString requestId = it.key(), operation = it.value();
        QMetaObject::invokeMethod(this, [this, requestId, operation, reason] {
            emit completed(requestId, operation, false, {}, reason);
        }, Qt::QueuedConnection);
    }
}
