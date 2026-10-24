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
    if (m_node == node && m_state && (node || !m_nodeDestroyed))
        return;
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
        m_nodeDestroyed = connect(node, &QObject::destroyed, this,
                                  [this] { setNode(nullptr); });
        m_state = node->acquire<ConnectionsStateReplica>();
    } else {
        m_state = new ConnectionsStateReplica;
    }
    if (m_state) {
        m_state->setParent(this);
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
            emit busyChanged();
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
    if (!connected())
        failPending(QStringLiteral("disconnected"));
    emit stateChanged();
}

void SettingsConnectionsService::failPending(const QString& reason)
{
    if (m_pending.isEmpty())
        return;
    const auto pending = m_pending;
    m_pending.clear();
    emit busyChanged();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        emit operationCompleted(it.key(), it.value(), false, {}, reason);
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
        m_pending.insert(id, operation);
        if (!wasBusy)
            emit busyChanged();
        QMetaObject::invokeMethod(this, [this, id, operation] {
            if (!m_pending.contains(id) || m_pending.value(id) != operation)
                return;
            try {
                s_nativeLogShareHandler();
                m_pending.remove(id);
                emit busyChanged();
                emit operationCompleted(id, operation, true,
                                        {{QStringLiteral("nativeShare"), true}}, {});
            } catch (...) {
                m_pending.remove(id);
                emit busyChanged();
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
    m_pending.insert(id, operation);
    if (!wasBusy)
        emit busyChanged();
    m_state->execute(id, operation, arguments);
    return id;
}
