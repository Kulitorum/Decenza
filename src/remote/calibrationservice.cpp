#include "calibrationservice.h"

#include <QJSEngine>
#include <QMetaObject>
#include <QQmlEngine>
#include <QRemoteObjectNode>
#include <QUuid>
#include <type_traits>

namespace { QPointer<QRemoteObjectNode> s_publishedNode; }

CalibrationService::CalibrationService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent) { setNode(node); }
CalibrationService::~CalibrationService()
{ if (m_state) disconnect(m_state, nullptr, this, nullptr); }
void CalibrationService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
QRemoteObjectNode* CalibrationService::publishedNode() { return s_publishedNode; }
CalibrationService* CalibrationService::create(QQmlEngine* engine)
{ return new CalibrationService(publishedNode(), engine); }
bool CalibrationService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode);
}
bool CalibrationService::ready() const { return connected() && m_ready; }
qint64 CalibrationService::revision() const { return connected() ? m_revision : -1; }
QVariantMap CalibrationService::snapshot() const
{
    if (connected() && m_ready) return m_snapshot;
    // Keep the shared tab safe to render during connection startup. Mutations
    // remain disabled by execute() until an authoritative snapshot is ready.
    return {
        {QStringLiteral("revision"), -1},
        {QStringLiteral("profileFilename"), QString()}, {QStringLiteral("profileTitle"), QString()},
        {QStringLiteral("activeScaleType"), QString()}, {QStringLiteral("activeScaleName"), QString()},
        {QStringLiteral("basketBrand"), QString()}, {QStringLiteral("basketModel"), QString()},
        {QStringLiteral("autoFlowCalibration"), true}, {QStringLiteral("perProfileFlowCalVersion"), 0},
        {QStringLiteral("effectiveFlowCalibration"), 1.0}, {QStringLiteral("hasProfileFlowCalibration"), false},
        {QStringLiteral("useFlowScale"), false}, {QStringLiteral("ignoreVolumeWithScale"), false},
        {QStringLiteral("sawLearnedLag"), 0.0}, {QStringLiteral("sawModelSource"), QStringLiteral("default")},
        {QStringLiteral("hasSawLearningForProfile"), false},
        {QStringLiteral("heaterIdleTemp"), 990}, {QStringLiteral("heaterWarmupFlow"), 20},
        {QStringLiteral("heaterTestFlow"), 40}, {QStringLiteral("heaterWarmupTimeout"), 10},
        {QStringLiteral("fanThreshold"), 60}, {QStringLiteral("steamTwoTapStop"), false},
        {QStringLiteral("connected"), false}, {QStringLiteral("nominalHeaterVoltage"), 0},
        {QStringLiteral("steamSessionCount"), 0}, {QStringLiteral("steamHasData"), false},
        {QStringLiteral("steamBaselinePressure"), 0.0}, {QStringLiteral("steamBaselineTemperature"), 0.0},
        {QStringLiteral("steamCurrentPressure"), 0.0}, {QStringLiteral("steamCurrentTemperature"), 0.0},
        {QStringLiteral("steamPressureThreshold"), 0.0}, {QStringLiteral("steamTemperatureThreshold"), 0.0},
        {QStringLiteral("steamMinSessionsForTrend"), 5}, {QStringLiteral("steamBaselineState"), 0}
    };
}
QString CalibrationService::errorMessage() const
{ return connected() ? m_error : tr("Calibration settings are disconnected."); }
QString CalibrationService::newRequestId() const { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
void CalibrationService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] { failPending(QStringLiteral("disconnected")); }))
        return;
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, &CalibrationService::updateFromReplica);
    connect(m_state, &QRemoteObjectReplica::notified, this, &CalibrationService::updateFromReplica);
    connect(m_state, &CalibrationStateReplica::snapshotChanged, this, &CalibrationService::updateFromReplica);
    connect(m_state, &CalibrationStateReplica::revisionChanged, this, &CalibrationService::updateFromReplica);
    connect(m_state, &CalibrationStateReplica::readyChanged, this, &CalibrationService::updateFromReplica);
    connect(m_state, &CalibrationStateReplica::errorMessageChanged, this, &CalibrationService::updateFromReplica);
    connect(m_state, &CalibrationStateReplica::operationCompleted, this,
        [this, origin = m_state](const QString& id, const QString& operation, bool success,
               const QVariantMap& result, const QString& reason, qint64 revision) {
            if (!origin || m_state != origin || !m_pending.contains(id)
                || m_pending.value(id) != operation) return;
            const QPointer<CalibrationService> guard(this);
            m_pending.remove(id);
            if (revision >= m_revision) m_revision = revision;
            if (m_state && m_state->snapshot().value(QStringLiteral("revision")).toLongLong() >= m_revision)
                m_snapshot = m_state->snapshot();
            m_error = success ? QString() : reason;
            if (m_busy != busy()) {
                m_busy = busy();
                emit busyChanged();
                if (!guard || m_state != origin) return;
            }
            emit stateChanged();
            if (!guard || m_state != origin) return;
            const qint64 currentRevision = m_revision;
            emit operationCompleted(id, operation, success, result, reason, currentRevision);
        }, Qt::QueuedConnection);
    updateFromReplica();
}
void CalibrationService::updateFromReplica()
{
    if (!connected()) {
        m_snapshot.clear(); m_revision = -1; m_ready = false; m_error.clear();
        const QPointer<CalibrationService> guard(this);
        failPending(QStringLiteral("disconnected"));
        if (guard) emit stateChanged();
        return;
    }
    const auto revision = m_state->revision();
    if (revision >= m_revision) {
        m_revision = revision; m_snapshot = m_state->snapshot();
        m_ready = m_state->ready(); m_error = m_state->errorMessage();
    }
    emit stateChanged();
}
void CalibrationService::failPending(const QString& reason)
{
    const QPointer<CalibrationService> guard(this);
    const auto pending = std::move(m_pending); m_pending.clear();
    if (m_busy) {
        m_busy = false;
        emit busyChanged();
        if (!guard) return;
    }
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        emit operationCompleted(it.key(), it.value(), false, {}, reason, m_revision);
        if (!guard) return;
    }
}
void CalibrationService::refresh()
{ if (connected()) QMetaObject::invokeMethod(m_state, &CalibrationStateReplica::refresh, Qt::QueuedConnection); }
QString CalibrationService::execute(const QString& operation, const QVariantMap& arguments)
{
    const QString id = newRequestId();
    const auto reject = [this, id, operation](const QString& reason) {
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, {}, reason, m_revision);
        }, Qt::QueuedConnection);
    };
    if (!connected() || !m_ready || m_revision < 0) {
        reject(QStringLiteral("unavailable"));
        return id;
    }
    if (!m_pending.isEmpty()) {
        reject(QStringLiteral("busy"));
        return id;
    }
    const QPointer<CalibrationService> guard(this);
    const QPointer<CalibrationStateReplica> requestedState = m_state;
    const qint64 expectedRevision = m_revision;
    m_pending.insert(id, operation);
    if (!m_busy) { m_busy = true; emit busyChanged(); }
    if (!guard)
        return id;
    if (guard->m_pending.value(id) != operation || requestedState != guard->m_state
        || !requestedState || !guard->connected())
        return id;
    QMetaObject::invokeMethod(guard.data(), [guard, requestedState, id, operation, arguments, expectedRevision] {
        if (!guard || guard->m_pending.value(id) != operation) return;
        if (!requestedState || requestedState != guard->m_state || !guard->connected()) {
            guard->m_pending.remove(id);
            guard->m_error = QStringLiteral("disconnected");
            if (guard->m_busy != guard->busy()) {
                guard->m_busy = guard->busy();
                emit guard->busyChanged();
                if (!guard) return;
            }
            emit guard->stateChanged();
            if (!guard) return;
            const qint64 revision = guard->m_revision;
            emit guard->operationCompleted(id, operation, false, {}, guard->m_error, revision);
            return;
        }
        requestedState->execute(id, operation, arguments, expectedRevision);
    }, Qt::QueuedConnection);
    return id;
}
