#include "settingsconnectionsservice.h"

#include "serviceoperation.h"

#include <QMetaObject>
#include <QRemoteObjectNode>
#include <QQmlEngine>

namespace {
QPointer<QRemoteObjectNode> s_publishedNode;
std::function<void()> s_nativeLogShareHandler;
}

SettingsConnectionsService* SettingsConnectionsService::create(QQmlEngine* engine, QJSEngine*)
{
    auto* service = new SettingsConnectionsService(s_publishedNode, engine);
    QJSEngine::setObjectOwnership(service, QJSEngine::CppOwnership);
    return service;
}

SettingsConnectionsService::SettingsConnectionsService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

SettingsConnectionsService::~SettingsConnectionsService()
{
    if (m_state)
        disconnect(m_state, nullptr, this, nullptr);
}

void SettingsConnectionsService::setPublishedNode(QRemoteObjectNode* node)
{
    s_publishedNode = node;
}

void SettingsConnectionsService::setNativeLogShareHandler(std::function<void()> handler)
{
    s_nativeLogShareHandler = std::move(handler);
}

QRemoteObjectNode* SettingsConnectionsService::publishedNode()
{
    return s_publishedNode;
}

bool SettingsConnectionsService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, publishedNode());
}

QVariantMap SettingsConnectionsService::snapshot() const
{
    return connected() && m_state->ready() ? m_state->snapshot() : QVariantMap();
}

bool SettingsConnectionsService::ready() const
{
    return connected() && m_state->ready();
}

QString SettingsConnectionsService::errorMessage() const
{
    return connected() ? QString() : tr("Connection settings are disconnected.");
}

void SettingsConnectionsService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] { failPending(QStringLiteral("disconnected")); }))
        return;
    if (m_state) {
        connect(m_state, &QRemoteObjectReplica::stateChanged,
                this, &SettingsConnectionsService::changed);
        connect(m_state, &QRemoteObjectReplica::notified,
                this, &SettingsConnectionsService::changed);
        connect(m_state, &ConnectionsStateReplica::snapshotChanged,
                this, &SettingsConnectionsService::stateChanged);
        connect(m_state, &ConnectionsStateReplica::readyChanged,
                this, &SettingsConnectionsService::stateChanged);
        connect(m_state, &ConnectionsStateReplica::operationCompleted, this,
                [this](const QString& id, const QString& operation, bool success,
                       const QVariantMap& result, const QString& reason) {
            if (!m_pending.contains(id) || m_pending.value(id) != operation)
                return;
            m_pending.remove(id);
            const QPointer<SettingsConnectionsService> completionGuard(this);
            emit busyChanged();
            if (!completionGuard) return;
            emit operationCompleted(id, operation, success, result, reason);
        }, Qt::QueuedConnection);
        connect(m_state, &ConnectionsStateReplica::manualWifiMdnsDiscovered,
                this, &SettingsConnectionsService::manualWifiMdnsDiscovered,
                Qt::QueuedConnection);
        connect(m_state, &ConnectionsStateReplica::manualWifiMdnsProbeFinished,
                this, &SettingsConnectionsService::manualWifiMdnsProbeFinished,
                Qt::QueuedConnection);
        connect(m_state, &ConnectionsStateReplica::manualWifiValidationFailed,
                this, &SettingsConnectionsService::manualWifiValidationFailed,
                Qt::QueuedConnection);
        connect(m_state, &ConnectionsStateReplica::manualWifiValidationSucceeded,
                this, &SettingsConnectionsService::manualWifiValidationSucceeded,
                Qt::QueuedConnection);
        connect(m_state, &ConnectionsStateReplica::manualWifiValidationCancelled,
                this, &SettingsConnectionsService::manualWifiValidationCancelled,
                Qt::QueuedConnection);
        connect(m_state, &ConnectionsStateReplica::activeHdsScaleChanged,
                this, &SettingsConnectionsService::activeHdsScaleChanged,
                Qt::QueuedConnection);
    }
    changed();
}

void SettingsConnectionsService::changed()
{
    const QPointer<SettingsConnectionsService> guard(this);
    if (!connected())
        failPending(QStringLiteral("disconnected"));
    if (guard) emit stateChanged();
}

void SettingsConnectionsService::failPending(const QString& reason)
{
    if (m_pending.isEmpty())
        return;
    const auto pending = m_pending;
    m_pending.clear();
    const QPointer<SettingsConnectionsService> guard(this);
    emit busyChanged();
    if (!guard) return;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        emit operationCompleted(it.key(), it.value(), false, {}, reason);
        if (!guard) return;
    }
}

QString SettingsConnectionsService::newRequestId() const
{
    return ServiceOperation::newId();
}

void SettingsConnectionsService::refresh()
{
    if (connected())
        m_state->refresh();
}

QString SettingsConnectionsService::resolveLogDownloadUrl(const QString& path) const
{
    if (path != QLatin1String("/api/debug/file/zip"))
        return {};
    const QUrl origin = resourceOrigin();
    if (origin.isEmpty())
        return path;
    return origin.resolved(QUrl(path)).toString(QUrl::FullyEncoded);
}

QString SettingsConnectionsService::execute(const QString& operation,
                                            const QVariantMap& arguments)
{
    const QString id = newRequestId();
    if (id.isEmpty())
        return {};
    if (operation == QLatin1String("requestSystemLogDownload")
        && arguments.isEmpty() && m_node && m_node == publishedNode()
        && s_nativeLogShareHandler) {
        if (m_pending.size() >= 64) {
            QMetaObject::invokeMethod(this, [this, id, operation] {
                emit operationCompleted(id, operation, false, {}, QStringLiteral("tooManyRequests"));
            }, Qt::QueuedConnection);
            return id;
        }
        const bool wasBusy = busy();
        const QPointer<SettingsConnectionsService> admissionGuard(this);
        m_pending.insert(id, operation);
        if (!wasBusy)
            emit busyChanged();
        if (!admissionGuard) return id;
        QMetaObject::invokeMethod(this, [this, id, operation] {
            if (!m_pending.contains(id) || m_pending.value(id) != operation)
                return;
            const QPointer<SettingsConnectionsService> guard(this);
            const auto share = s_nativeLogShareHandler;
            try {
                share();
                if (!guard || m_pending.value(id) != operation) return;
                m_pending.remove(id);
                const QPointer<SettingsConnectionsService> completionGuard(this);
                emit busyChanged();
                if (!completionGuard) return;
                emit operationCompleted(id, operation, true,
                                        {{QStringLiteral("nativeShare"), true}}, {});
            } catch (...) {
                if (!guard || m_pending.value(id) != operation) return;
                m_pending.remove(id);
                const QPointer<SettingsConnectionsService> completionGuard(this);
                emit busyChanged();
                if (!completionGuard) return;
                emit operationCompleted(id, operation, false, {}, QStringLiteral("shareFailed"));
            }
        }, Qt::QueuedConnection);
        return id;
    }
    if (!connected() || !ready()) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, {}, QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
        return id;
    }
    if (m_pending.size() >= 64) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, {}, QStringLiteral("tooManyRequests"));
        }, Qt::QueuedConnection);
        return id;
    }
    const bool wasBusy = busy();
    const QPointer<ConnectionsStateReplica> requested = m_state;
    const QPointer<SettingsConnectionsService> guard(this);
    m_pending.insert(id, operation);
    if (!wasBusy)
        emit busyChanged();
    if (!guard || requested != m_state || !m_pending.contains(id)
        || m_pending.value(id) != operation) return id;
    if (!ready()) { failPending(QStringLiteral("disconnected")); return id; }
    requested->execute(id, operation, arguments);
    return id;
}
