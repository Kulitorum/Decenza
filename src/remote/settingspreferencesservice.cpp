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
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] { failPending(QStringLiteral("disconnected")); }))
        return;
    if (m_state) {
        connect(m_state, &QRemoteObjectReplica::stateChanged, this, [this] {
            const QPointer<SettingsPreferencesService> guard(this);
            if (!connected())
                failPending(QStringLiteral("disconnected"));
            if (!guard) return;
            emit preferencesChanged();
            if (guard) emit stateChanged();
        });
        connect(m_state, &QRemoteObjectReplica::notified, this, [this] {
            const QPointer<SettingsPreferencesService> guard(this);
            emit preferencesChanged();
            if (guard) emit stateChanged();
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
    const QPointer<SettingsPreferencesService> guard(this);
    emit preferencesChanged();
    if (guard) emit stateChanged();
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

    const QPointer<SettingsPreferencesStateReplica> requested = m_state;
    const QPointer<SettingsPreferencesService> guard(this);
    m_pending.insert(requestId, {domain, name});
    m_errorMessage.clear();
    emit stateChanged();
    if (!guard) return;
    emit busyChanged();
    if (!guard) return;
    const auto pending = m_pending.constFind(requestId);
    if (requested != m_state || pending == m_pending.cend()
        || pending->domain != domain || pending->name != name)
        return;
    if (!ready()) {
        failPending(QStringLiteral("disconnected"));
        return;
    }
    requested->setPreference(requestId, domain, name, value, expectedRevision);
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
    const QPointer<SettingsPreferencesService> guard(this);
    emit busyChanged();
    if (!guard) return;
    emit stateChanged();
    if (!guard) return;
    emit preferenceCompleted(requestId, domain, name, success, revision, reason);
}

void SettingsPreferencesService::failPending(const QString& reason)
{
    const auto pending = std::exchange(m_pending, {});
    if (pending.isEmpty())
        return;
    m_errorMessage = reason;
    const QPointer<SettingsPreferencesService> guard(this);
    emit busyChanged();
    if (!guard) return;
    emit stateChanged();
    if (!guard) return;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const QString requestId = it.key();
        const QString domain = it->domain;
        const QString name = it->name;
        QMetaObject::invokeMethod(this, [this, requestId, domain, name, reason] {
            emit preferenceCompleted(requestId, domain, name, false, 0, reason);
        }, Qt::QueuedConnection);
    }
}
