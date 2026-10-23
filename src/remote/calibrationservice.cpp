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
    if (m_node == node && m_state && (node || !m_nodeDestroyed)) return;
    disconnect(m_nodeDestroyed); m_nodeDestroyed = {};
    failPending(QStringLiteral("disconnected"));
    if (m_state) { disconnect(m_state, nullptr, this, nullptr); m_state->deleteLater(); }
    m_node = node;
    m_state = node ? node->acquire<CalibrationStateReplica>() : new CalibrationStateReplica;
    m_state->setParent(this);
    if (node) m_nodeDestroyed = connect(node, &QObject::destroyed, this, [this] { setNode(nullptr); });
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, &CalibrationService::updateFromReplica);
    connect(m_state, &QRemoteObjectReplica::notified, this, &CalibrationService::updateFromReplica);
    connect(m_state, &CalibrationStateReplica::snapshotChanged, this, &CalibrationService::updateFromReplica);
    connect(m_state, &CalibrationStateReplica::revisionChanged, this, &CalibrationService::updateFromReplica);
    connect(m_state, &CalibrationStateReplica::readyChanged, this, &CalibrationService::updateFromReplica);
    connect(m_state, &CalibrationStateReplica::errorMessageChanged, this, &CalibrationService::updateFromReplica);
    connect(m_state, &CalibrationStateReplica::operationCompleted, this,
        [this](const QString& id, const QString& operation, bool success,
               const QVariantMap& result, const QString& reason, qint64 revision) {
            if (!m_pending.contains(id) || m_pending.value(id) != operation) return;
            m_pending.remove(id);
            if (revision >= m_revision) m_revision = revision;
            if (m_state && m_state->snapshot().value(QStringLiteral("revision")).toLongLong() >= m_revision)
                m_snapshot = m_state->snapshot();
            m_error = success ? QString() : reason;
            if (m_busy != busy()) { m_busy = busy(); emit busyChanged(); }
            emit stateChanged();
            emit operationCompleted(id, operation, success, result, reason, m_revision);
        }, Qt::QueuedConnection);
    updateFromReplica();
}
void CalibrationService::updateFromReplica()
{
    if (!connected()) {
        m_snapshot.clear(); m_revision = -1; m_ready = false; m_error.clear();
        failPending(QStringLiteral("disconnected")); emit stateChanged(); return;
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
    const auto pending = std::move(m_pending); m_pending.clear();
    if (m_busy) { m_busy = false; emit busyChanged(); }
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        emit operationCompleted(it.key(), it.value(), false, {}, reason, m_revision);
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
    m_pending.insert(id, operation);
    if (!m_busy) { m_busy = true; emit busyChanged(); }
    const QPointer<CalibrationStateReplica> requestedState = m_state;
    QMetaObject::invokeMethod(this, [this, requestedState, id, operation, arguments, revision=m_revision] {
        if (!m_pending.contains(id)) return;
        if (!requestedState || requestedState != m_state || !connected()) {
            m_pending.remove(id);
            m_error = QStringLiteral("disconnected");
            if (m_busy != busy()) { m_busy = busy(); emit busyChanged(); }
            emit stateChanged();
            emit operationCompleted(id, operation, false, {}, m_error, m_revision);
            return;
        }
        requestedState->execute(id, operation, arguments, revision);
    }, Qt::QueuedConnection);
    return id;
}
