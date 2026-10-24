#include "debugconversionservice.h"

#include <QJSEngine>
#include <QMetaObject>
#include <QQmlEngine>
#include <QRemoteObjectNode>
#include <QUuid>

namespace {
QPointer<QRemoteObjectNode> s_publishedNode;
constexpr qsizetype MaxPendingRequests = 8;
}

DebugConversionService* DebugConversionService::create(QQmlEngine* engine, QJSEngine*)
{
    auto* service = new DebugConversionService(s_publishedNode, engine);
    QJSEngine::setObjectOwnership(service, QJSEngine::CppOwnership);
    return service;
}

DebugConversionService::DebugConversionService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

DebugConversionService::~DebugConversionService()
{
    if (m_state) disconnect(m_state, nullptr, this, nullptr);
}

void DebugConversionService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
QRemoteObjectNode* DebugConversionService::publishedNode() { return s_publishedNode; }

void DebugConversionService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] { failPending(QStringLiteral("disconnected")); }))
        return;
    connect(m_state, &QRemoteObjectReplica::stateChanged,
            this, &DebugConversionService::observeConnectionState);
    connect(m_state, &QRemoteObjectReplica::notified, this, &DebugConversionService::stateChanged);
    connect(m_state, &DebugConversionStateReplica::readyChanged,
            this, &DebugConversionService::observeConnectionState);
    connect(m_state, &DebugConversionStateReplica::operationCompleted, this,
            &DebugConversionService::finishPending, Qt::QueuedConnection);
    connect(m_state, &DebugConversionStateReplica::operationProgress, this,
            &DebugConversionService::relayProgress, Qt::QueuedConnection);
    emit stateChanged();
}

bool DebugConversionService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, publishedNode());
}

bool DebugConversionService::ready() const
{
    return connected() && m_state->ready();
}

void DebugConversionService::observeConnectionState()
{
    const QPointer<DebugConversionService> guard(this);
    emit stateChanged();
    if (guard && !ready()) failPending(QStringLiteral("disconnected"));
}

QString DebugConversionService::execute(const QString& operation, const QVariantMap& arguments,
                                        const QString& requestId)
{
    const QString id = requestId.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces).toLower() : requestId;
    if (id.isEmpty() || id.size() > 128 || m_pending.contains(id) || m_pending.size() >= MaxPendingRequests) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, {}, QStringLiteral("invalidRequest"));
        }, Qt::QueuedConnection);
        return id;
    }
    if (!ready()) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, {}, QStringLiteral("unavailable"));
        }, Qt::QueuedConnection);
        return id;
    }
    m_pending.insert(id, operation);
    const QPointer<DebugConversionStateReplica> requestedState = m_state;
    QMetaObject::invokeMethod(this, [this, requestedState, id, operation, arguments] {
        if (!m_pending.contains(id) || m_pending.value(id) != operation) return;
        if (!requestedState || requestedState != m_state || !connected()) {
            m_pending.remove(id);
            emit operationCompleted(id, operation, false, {}, QStringLiteral("disconnected"));
            return;
        }
        requestedState->execute(id, operation, arguments);
    }, Qt::QueuedConnection);
    return id;
}

void DebugConversionService::finishPending(const QString& id, const QString& operation,
                                           bool success, const QVariantMap& result,
                                           const QString& reason)
{
    if (!m_pending.contains(id) || m_pending.value(id) != operation) return;
    m_pending.remove(id);
    emit operationCompleted(id, operation, success, result, reason);
}

void DebugConversionService::relayProgress(const QString& id, const QVariantMap& progress)
{
    if (m_pending.contains(id) && m_pending.value(id) == QLatin1String("convertProfiles"))
        emit operationProgress(id, progress);
}

void DebugConversionService::failPending(const QString& reason)
{
    const auto pending = m_pending;
    m_pending.clear();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const QString id = it.key();
        const QString operation = it.value();
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, {}, reason);
        }, Qt::QueuedConnection);
    }
}
