#include "maintenanceservice.h"

#include <QJSEngine>
#include <QQmlEngine>
#include <QMetaObject>
#include <QRemoteObjectNode>
#include <QUuid>

namespace { QPointer<QRemoteObjectNode> s_publishedNode; }

MaintenanceService::MaintenanceService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

MaintenanceService::~MaintenanceService()
{
    if (m_state) disconnect(m_state, nullptr, this, nullptr);
}

void MaintenanceService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
QRemoteObjectNode* MaintenanceService::publishedNode() { return s_publishedNode; }
MaintenanceService* MaintenanceService::create(QQmlEngine* engine)
{
    return new MaintenanceService(publishedNode(), engine);
}

bool MaintenanceService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode);
}
bool MaintenanceService::ready() const { return connected() && m_ready; }
qint64 MaintenanceService::revision() const { return connected() ? m_revision : -1; }
QVariantMap MaintenanceService::snapshot() const
{
    if (ready()) return m_snapshot;
    return {
        {QStringLiteral("connected"), false}, {QStringLiteral("simulationMode"), false},
        {QStringLiteral("isHeadless"), false}, {QStringLiteral("steamTemperature"), 0.0},
        {QStringLiteral("subState"), 0}, {QStringLiteral("descaleProgress"), 0.0},
        {QStringLiteral("descaleStepIndex"), 0}, {QStringLiteral("descaleStepCount"), 0},
        {QStringLiteral("descaleSecondsRemaining"), 0}, {QStringLiteral("descaleCycle"), 0},
        {QStringLiteral("phase"), -1}, {QStringLiteral("phaseIsIdle"), false},
        {QStringLiteral("phaseIsHeating"), false}, {QStringLiteral("phaseIsReady"), false},
        {QStringLiteral("phaseIsDescaling"), false}, {QStringLiteral("phaseIsTransport"), false},
        {QStringLiteral("shotTime"), 0.0}, {QStringLiteral("canStartTransport"), false},
        {QStringLiteral("descaleWorkflowActive"), false}, {QStringLiteral("descaleStarted"), false},
        {QStringLiteral("descaleNeedsFinish"), false}, {QStringLiteral("descaleStopped"), false},
        {QStringLiteral("transportWorkflowActive"), false},
        {QStringLiteral("transportStarted"), false}, {QStringLiteral("transportNeedsFinish"), false},
        {QStringLiteral("transportStopped"), false}
    };
}
QString MaintenanceService::errorMessage() const
{
    return connected() ? m_error : tr("Maintenance service is disconnected");
}
QString MaintenanceService::newRequestId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

void MaintenanceService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] { failPending(QStringLiteral("disconnected")); }))
        return;
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, [this] {
        if (!connected()) failPending(QStringLiteral("disconnected"));
        updateFromReplica();
    });
    connect(m_state, &QRemoteObjectReplica::notified, this, &MaintenanceService::updateFromReplica);
    connect(m_state, &MaintenanceStateReplica::readyChanged, this, &MaintenanceService::updateFromReplica);
    connect(m_state, &MaintenanceStateReplica::revisionChanged, this, &MaintenanceService::updateFromReplica);
    connect(m_state, &MaintenanceStateReplica::snapshotChanged, this, &MaintenanceService::updateFromReplica);
    connect(m_state, &MaintenanceStateReplica::errorMessageChanged, this, &MaintenanceService::updateFromReplica);
    connect(m_state, &MaintenanceStateReplica::operationCompleted, this,
            [this](const QString& id, const QString& operation, bool success,
                   const QVariantMap& result, const QString& reason, qint64 revision) {
        if (!m_pending.contains(id) || m_pending.value(id) != operation) return;
        m_pending.remove(id);
        if (revision >= m_revision) m_revision = revision;
        updateFromReplica();
        m_error = success ? QString() : reason;
        emit busyChanged();
        emit stateChanged();
        emit operationCompleted(id, operation, success, result, reason, m_revision);
    }, Qt::QueuedConnection);
    updateFromReplica();
}

void MaintenanceService::updateFromReplica()
{
    if (!connected()) {
        m_snapshot.clear();
        m_revision = -1;
        m_ready = false;
        m_error.clear();
        emit stateChanged();
        return;
    }
    m_snapshot = m_state->snapshot();
    m_revision = m_state->revision();
    m_ready = m_state->ready();
    m_error = m_state->errorMessage();
    emit stateChanged();
}

void MaintenanceService::failPending(const QString& reason)
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

void MaintenanceService::refresh()
{
    if (connected())
        QMetaObject::invokeMethod(m_state, &MaintenanceStateReplica::refresh, Qt::QueuedConnection);
}

QString MaintenanceService::execute(const QString& operation, const QVariantMap& arguments,
                                    const QString& requestId)
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
    const qint64 expectedRevision = m_revision;
    const QPointer<MaintenanceStateReplica> requestedState = m_state;
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
