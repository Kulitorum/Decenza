#include "ghcsimulatorservice.h"

#include "ghcsimulatoroperationpolicy.h"

#include <QJSEngine>
#include <QMetaObject>
#include <QRemoteObjectNode>
#include <QQmlEngine>
#include <QUuid>

namespace { QPointer<QRemoteObjectNode> s_publishedNode; }

GHCSimulatorService* GHCSimulatorService::create(QQmlEngine* engine, QJSEngine*)
{
    auto* service = new GHCSimulatorService(s_publishedNode, engine);
    QJSEngine::setObjectOwnership(service, QJSEngine::CppOwnership);
    return service;
}

GHCSimulatorService::GHCSimulatorService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

GHCSimulatorService::~GHCSimulatorService()
{
    if (m_state) disconnect(m_state, nullptr, this, nullptr);
}

void GHCSimulatorService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
QRemoteObjectNode* GHCSimulatorService::publishedNode() { return s_publishedNode; }

bool GHCSimulatorService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode);
}

QVariantMap GHCSimulatorService::snapshot() const
{
    return connected() && m_state->ready() ? m_state->snapshot() : QVariantMap();
}

QVariantList GHCSimulatorService::ledColors() const
{
    return snapshot().value(QStringLiteral("ledColors")).toList();
}

bool GHCSimulatorService::stopPressed() const
{
    return snapshot().value(QStringLiteral("stopPressed")).toBool();
}

int GHCSimulatorService::activeFunction() const
{
    return snapshot().value(QStringLiteral("activeFunction")).toInt();
}

bool GHCSimulatorService::available() const
{
    return snapshot().value(QStringLiteral("available")).toBool();
}

bool GHCSimulatorService::ready() const
{
    return connected() && m_state->ready();
}

void GHCSimulatorService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
                                      [this] { failPending(QStringLiteral("disconnected")); })) return;
    connect(m_state, &QRemoteObjectReplica::stateChanged, this,
            &GHCSimulatorService::changed);
    connect(m_state, &QRemoteObjectReplica::notified, this,
            &GHCSimulatorService::changed);
    connect(m_state, &GHCSimulatorStateReplica::snapshotChanged,
            this, &GHCSimulatorService::stateChanged);
    connect(m_state, &GHCSimulatorStateReplica::readyChanged,
            this, &GHCSimulatorService::stateChanged);
    connect(m_state, &GHCSimulatorStateReplica::operationCompleted, this,
            &GHCSimulatorService::complete);
    emit stateChanged();
}

void GHCSimulatorService::changed()
{
    const QPointer<GHCSimulatorService> guard(this);
    if (!connected()) failPending(QStringLiteral("disconnected"));
    if (guard) emit stateChanged();
}

void GHCSimulatorService::complete(const QString& requestId, const QString& operation,
                                    bool success, const QString& reason)
{
    if (!m_pending.finish(requestId, operation)) return;
    const QPointer<GHCSimulatorService> guard(this);
    emit busyChanged();
    if (guard) emit operationCompleted(requestId, operation, success, reason);
}

QString GHCSimulatorService::execute(const QString& operation)
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
    const auto rejectLater = [this, id, operation](const QString& reason) {
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, reason);
        }, Qt::QueuedConnection);
    };
    if (!GHCSimulatorOperationPolicy::supported(operation)) {
        rejectLater(QStringLiteral("unsupportedOperation"));
        return id;
    }
    if (!ready()) {
        rejectLater(QStringLiteral("unavailable"));
        return id;
    }
    if (m_state->snapshot().value(QStringLiteral("available")).toBool() != true) {
        rejectLater(QStringLiteral("simulatorUnavailable"));
        return id;
    }
    if (m_pending.size() >= 16) {
        rejectLater(QStringLiteral("tooManyRequests"));
        return id;
    }

    const QPointer<GHCSimulatorStateReplica> replica = m_state;
    const quint64 ticket = m_pending.insert(id, operation);
    const QPointer<GHCSimulatorService> guard(this);
    emit busyChanged();
    if (!guard) return id;
    m_pending.dispatch(this, id, ticket,
                       [this, replica] { return replica && m_state == replica && ready() && available(); },
                       [replica, id, operation] { replica->execute(id, operation); },
                       [this, id, operation] {
        const QPointer<GHCSimulatorService> alive(this);
        emit busyChanged();
        if (alive)
            emit operationCompleted(id, operation, false, QStringLiteral("disconnected"));
    });
    return id;
}

void GHCSimulatorService::pressEspresso() { execute(QStringLiteral("pressEspresso")); }
void GHCSimulatorService::pressSteam() { execute(QStringLiteral("pressSteam")); }
void GHCSimulatorService::pressHotWater() { execute(QStringLiteral("pressHotWater")); }
void GHCSimulatorService::pressFlush() { execute(QStringLiteral("pressFlush")); }

void GHCSimulatorService::pressStop()
{
    execute(QStringLiteral("pressStop"));
}

void GHCSimulatorService::releaseStop()
{
    // Remote pressStop is a terminal stop command. The host clears the local
    // display state immediately after issuing it, so there is no release RPC
    // that could interfere with another client's button interaction.
}

void GHCSimulatorService::failPending(const QString& reason)
{
    const QPointer<GHCSimulatorService> guard(this);
    m_pending.failAll([this] { emit busyChanged(); },
                      [this, guard, reason](const QString& id, const QString& operation) {
        if (!guard) return;
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, reason);
        }, Qt::QueuedConnection);
    });
}

void GHCSimulatorService::refresh()
{
    if (connected()) m_state->refresh();
}

void GHCSimulatorService::mainWindowActivated()
{
    if (m_isRaisingWindows) return;
    m_isRaisingWindows = true;
    const QPointer<GHCSimulatorService> guard(this);
    emit raiseGhcWindow();
    if (guard) QMetaObject::invokeMethod(this, [this] { m_isRaisingWindows = false; },
                              Qt::QueuedConnection);
}

void GHCSimulatorService::ghcWindowActivated()
{
    if (m_isRaisingWindows) return;
    m_isRaisingWindows = true;
    const QPointer<GHCSimulatorService> guard(this);
    emit raiseMainWindow();
    if (guard) QMetaObject::invokeMethod(this, [this] { m_isRaisingWindows = false; },
                              Qt::QueuedConnection);
}
