#include "settingspreferencesservice.h"

#include "serviceoperation.h"

#include <QJSEngine>
#include <QQmlEngine>
#include <QMetaObject>
#include <QUuid>

#include <utility>

namespace {
QPointer<QRemoteObjectNode> s_publishedNode;
}

SettingsPreferencesService* SettingsPreferencesService::create(QQmlEngine* engine, QJSEngine*)
{
    auto* service = new SettingsPreferencesService(s_publishedNode, engine);
    QJSEngine::setObjectOwnership(service, QJSEngine::CppOwnership);
    return service;
}

SettingsPreferencesService::SettingsPreferencesService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

void SettingsPreferencesService::setPublishedNode(QRemoteObjectNode* node)
{
    s_publishedNode = node;
}

QRemoteObjectNode* SettingsPreferencesService::publishedNode()
{
    return s_publishedNode;
}

void SettingsPreferencesService::setNode(QRemoteObjectNode* node)
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
        m_nodeDestroyed = connect(node, &QObject::destroyed, this, [this] {
            setNode(nullptr);
        });
        m_state = node->acquire<SettingsPreferencesStateReplica>();
        if (m_state)
            m_state->setParent(this);
    } else {
        m_state = new SettingsPreferencesStateReplica;
        m_state->setParent(this);
    }

    if (m_state) {
        connect(m_state, &QRemoteObjectReplica::stateChanged, this, [this] {
            if (!connected())
                failPending(QStringLiteral("disconnected"));
            emit preferencesChanged();
            emit stateChanged();
        });
        connect(m_state, &QRemoteObjectReplica::notified, this, [this] {
            emit preferencesChanged();
            emit stateChanged();
        });
        connect(m_state, &SettingsPreferencesStateReplica::preferencesChanged,
                this, &SettingsPreferencesService::preferencesChanged);
        connect(m_state, &SettingsPreferencesStateReplica::revisionsChanged,
                this, &SettingsPreferencesService::preferencesChanged);
        connect(m_state, &SettingsPreferencesStateReplica::readyChanged,
                this, &SettingsPreferencesService::stateChanged);
        connect(m_state, &SettingsPreferencesStateReplica::preferenceCompleted,
                this, &SettingsPreferencesService::finishPending, Qt::QueuedConnection);
    }
    emit preferencesChanged();
    emit stateChanged();
}

bool SettingsPreferencesService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, publishedNode());
}

QVariantMap SettingsPreferencesService::preferences() const
{
    return connected() && m_state->ready() ? m_state->preferences() : QVariantMap();
}

QVariantMap SettingsPreferencesService::revisions() const
{
    return connected() && m_state->ready() ? m_state->revisions() : QVariantMap();
}

bool SettingsPreferencesService::ready() const
{
    return connected() && m_state->ready();
}

QString SettingsPreferencesService::newRequestId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

void SettingsPreferencesService::refresh()
{
    if (connected())
        m_state->refresh();
}

void SettingsPreferencesService::refreshShotMapLocation()
{
    if (connected() && ready())
        m_state->refreshShotMapLocation();
}

void SettingsPreferencesService::openShotMapLocationSettings()
{
    if (connected() && ready())
        m_state->openShotMapLocationSettings();
}

void SettingsPreferencesService::setPreference(const QString& requestId,
                                                const QString& domain,
                                                const QString& name,
                                                const QVariant& value,
                                                quint64 expectedRevision)
{
    if (requestId.isEmpty() || m_pending.contains(requestId))
        return;
    if (!ready()) {
        QMetaObject::invokeMethod(this, [this, requestId, domain, name] {
            emit preferenceCompleted(requestId, domain, name, false, 0,
                                     QStringLiteral("unavailable"));
        }, Qt::QueuedConnection);
        return;
    }
    if (m_pending.size() >= 64) {
        QMetaObject::invokeMethod(this, [this, requestId, domain, name] {
            emit preferenceCompleted(requestId, domain, name, false, 0,
                                     QStringLiteral("busy"));
        }, Qt::QueuedConnection);
        return;
    }

    m_pending.insert(requestId, {domain, name});
    m_errorMessage.clear();
    emit stateChanged();
    emit busyChanged();
    m_state->setPreference(requestId, domain, name, value,
                           expectedRevision);
}

void SettingsPreferencesService::finishPending(const QString& requestId,
                                                const QString& domain,
                                                const QString& name,
                                                bool success,
                                                quint64 revision,
                                                const QString& reason)
{
    const auto it = m_pending.constFind(requestId);
    if (it == m_pending.cend() || it->domain != domain || it->name != name)
        return;
    m_pending.remove(requestId);
    if (!success)
        m_errorMessage = reason;
    emit busyChanged();
    emit stateChanged();
    emit preferenceCompleted(requestId, domain, name, success, revision, reason);
}

void SettingsPreferencesService::failPending(const QString& reason)
{
    const auto pending = std::exchange(m_pending, {});
    if (pending.isEmpty())
        return;
    m_errorMessage = reason;
    emit busyChanged();
    emit stateChanged();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const QString requestId = it.key();
        const QString domain = it->domain;
        const QString name = it->name;
        QMetaObject::invokeMethod(this, [this, requestId, domain, name, reason] {
            emit preferenceCompleted(requestId, domain, name, false, 0, reason);
        }, Qt::QueuedConnection);
    }
}
