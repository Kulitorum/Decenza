#include "shotserversettingsservice.h"
#include "shotserversettingspolicy.h"

#include <QJSEngine>
#include <QMetaObject>
#include <QRemoteObjectNode>
#include <QRemoteObjectPendingCallWatcher>
#include <QQmlEngine>
#include <QtQmlIntegration/qqmlintegration.h>
#include <QUuid>
#include <utility>

namespace { QPointer<QRemoteObjectNode> s_publishedNode; }

ShotServerSettingsService* ShotServerSettingsService::create(QQmlEngine* engine, QJSEngine*)
{
    auto* service = new ShotServerSettingsService(s_publishedNode, engine);
    QJSEngine::setObjectOwnership(service, QJSEngine::CppOwnership);
    return service;
}

ShotServerSettingsService::ShotServerSettingsService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

ShotServerSettingsService::~ShotServerSettingsService()
{
    if (m_state) disconnect(m_state, nullptr, this, nullptr);
}

void ShotServerSettingsService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
bool ShotServerSettingsService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode);
}
QVariantMap ShotServerSettingsService::snapshot() const
{
    return connected() && m_state->ready() ? m_state->snapshot() : QVariantMap();
}
bool ShotServerSettingsService::ready() const { return connected() && m_state->ready(); }

void ShotServerSettingsService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] { failPending(QStringLiteral("disconnected")); }))
        return;
    connect(m_state, &QRemoteObjectReplica::stateChanged, this,
            &ShotServerSettingsService::changed);
    connect(m_state, &QRemoteObjectReplica::notified, this,
            &ShotServerSettingsService::changed);
    connect(m_state, &ShotServerSettingsStateReplica::snapshotChanged,
            this, &ShotServerSettingsService::stateChanged);
    connect(m_state, &ShotServerSettingsStateReplica::readyChanged,
            this, &ShotServerSettingsService::stateChanged);
    emit stateChanged();
}

void ShotServerSettingsService::changed()
{
    const QPointer<ShotServerSettingsService> guard(this);
    if (!connected()) failPending(QStringLiteral("disconnected"));
    if (guard) emit stateChanged();
}

QString ShotServerSettingsService::execute(const QString& operation,
                                            const QVariantMap& arguments)
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
    static const QStringList allowed{QStringLiteral("generateTotpSetup"),
        QStringLiteral("completeTotpSetup"), QStringLiteral("resetTotpSecret"),
        QStringLiteral("factoryResetAndQuit")};
    auto failLater = [this, id, operation](const QString& reason) {
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, {}, reason);
        }, Qt::QueuedConnection);
    };
    if (!allowed.contains(operation)) { failLater(QStringLiteral("unknownOperation")); return id; }
    if (!ready()) { failLater(QStringLiteral("unavailable")); return id; }
    if (m_pending.size() >= 8) { failLater(QStringLiteral("tooManyRequests")); return id; }
    if (arguments.size() > 4) { failLater(QStringLiteral("invalidArguments")); return id; }
    const QPointer<ShotServerSettingsStateReplica> replica = m_state;
    m_pending.insert(id, operation);
    const QPointer<ShotServerSettingsService> guard(this);
    emit busyChanged();
    if (!guard || m_state != replica
        || !ShotServerSettingsPolicy::matchesPending(m_pending, id, operation)) return id;
    if (!ready()) {
        m_pending.remove(id);
        emit busyChanged();
        if (guard) failLater(QStringLiteral("disconnected"));
        return id;
    }
    auto* watcher = new QRemoteObjectPendingCallWatcher(
        replica->executePrivate(id, operation, arguments), this);
    connect(watcher, &QRemoteObjectPendingCallWatcher::finished, this,
            [this, watcher, id, operation] {
        watcher->deleteLater();
        if (!ShotServerSettingsPolicy::matchesPending(m_pending, id, operation)) return;
        m_pending.remove(id);
        const bool transportOk = watcher->error() == QRemoteObjectPendingCall::NoError;
        const QVariantMap response = transportOk ? watcher->returnValue().toMap() : QVariantMap();
        const bool success = transportOk && response.value(QStringLiteral("success")).toBool();
        const QVariantMap result = response.value(QStringLiteral("result")).toMap();
        const QString reason = transportOk
            ? response.value(QStringLiteral("reason")).toString()
            : QStringLiteral("disconnected");
        const QPointer<ShotServerSettingsService> completionGuard(this);
        emit busyChanged();
        if (completionGuard) emit operationCompleted(id, operation, success, result, reason);
    });
    return id;
}

void ShotServerSettingsService::failPending(const QString& reason)
{
    const auto pending = std::exchange(m_pending, {});
    if (pending.isEmpty()) return;
    const QPointer<ShotServerSettingsService> guard(this);
    emit busyChanged();
    if (!guard) return;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const QString id = it.key(), operation = it.value();
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, {}, reason);
        }, Qt::QueuedConnection);
    }
}

void ShotServerSettingsService::refresh()
{
    if (connected()) m_state->refresh();
}
