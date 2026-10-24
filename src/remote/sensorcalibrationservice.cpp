#include "sensorcalibrationservice.h"

#include <QJSEngine>
#include <QQmlEngine>
#include <QMetaObject>
#include <QUuid>

namespace { QPointer<QRemoteObjectNode> s_sensorCalibrationNode; }

SensorCalibrationService* SensorCalibrationService::create(QQmlEngine* engine)
{
    auto* service = new SensorCalibrationService(s_sensorCalibrationNode, engine);
    QJSEngine::setObjectOwnership(service, QJSEngine::CppOwnership);
    return service;
}

SensorCalibrationService::SensorCalibrationService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

SensorCalibrationService::~SensorCalibrationService()
{
    if (m_state) disconnect(m_state, nullptr, this, nullptr);
}

void SensorCalibrationService::setPublishedNode(QRemoteObjectNode* node) { s_sensorCalibrationNode = node; }
QRemoteObjectNode* SensorCalibrationService::publishedNode() { return s_sensorCalibrationNode; }

void SensorCalibrationService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
                                      [this] { failPending(QStringLiteral("disconnected")); })) return;
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, [this] {
        const QPointer<SensorCalibrationService> guard(this);
        if (!replicaConnected()) failPending(QStringLiteral("disconnected"));
        if (guard) emit stateChanged();
    });
    connect(m_state, &QRemoteObjectReplica::notified, this, &SensorCalibrationService::stateChanged);
    connect(m_state, &SensorCalibrationStateReplica::readyChanged, this, &SensorCalibrationService::stateChanged);
    connect(m_state, &SensorCalibrationStateReplica::snapshotChanged, this, &SensorCalibrationService::stateChanged);
    connect(m_state, &SensorCalibrationStateReplica::errorMessageChanged, this, &SensorCalibrationService::stateChanged);
    connect(m_state, &SensorCalibrationStateReplica::operationCompleted, this,
            [this, replica = m_state](const QString& id, const QString& operation, bool success,
                   const QVariantMap& result, const QString& reason) {
        if (m_state != replica) return;
        if (!m_pending.finish(id, operation)) return;
        const QPointer<SensorCalibrationService> guard(this);
        emit busyChanged();
        if (guard) emit operationCompleted(id, operation, success, result, reason);
    }, Qt::QueuedConnection);
    emit stateChanged();
}

bool SensorCalibrationService::replicaConnected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_sensorCalibrationNode);
}
bool SensorCalibrationService::ready() const { return replicaConnected() && m_state->ready(); }
QVariantMap SensorCalibrationService::snapshot() const { return replicaConnected() ? m_state->snapshot() : QVariantMap(); }
QVariantList SensorCalibrationService::sensors() const { return snapshot().value(QStringLiteral("sensors")).toList(); }
QString SensorCalibrationService::errorMessage() const { return replicaConnected() ? m_state->errorMessage() : QStringLiteral("Sensor calibration is disconnected"); }
bool SensorCalibrationService::connected() const { return ready() && snapshot().value(QStringLiteral("connected")).toBool(); }
int SensorCalibrationService::sensorCount() const { return static_cast<int>(snapshot().value(QStringLiteral("sensors")).toList().size()); }
QVariantMap SensorCalibrationService::sensorState(int sensor) const
{
    const QVariantList sensors = snapshot().value(QStringLiteral("sensors")).toList();
    return sensor >= 0 && sensor < sensors.size() ? sensors.at(sensor).toMap() : QVariantMap();
}

QString SensorCalibrationService::newRequestId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

void SensorCalibrationService::refresh()
{
    if (replicaConnected()) m_state->refresh();
}

QString SensorCalibrationService::send(
    const QString& operation,
    const std::function<void(SensorCalibrationStateReplica*, const QString&)>& call,
    const QString& suppliedId)
{
    const QString id = suppliedId.isEmpty() ? newRequestId() : suppliedId;
    const auto fail = [this, id, operation](const QString& reason) {
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, {}, reason);
        }, Qt::QueuedConnection);
    };
    if (id.isEmpty() || m_pending.contains(id)) { fail(QStringLiteral("invalidRequestId")); return id; }
    if (!ready()) { fail(QStringLiteral("unavailable")); return id; }
    if (m_pending.size() >= 64) { fail(QStringLiteral("tooManyRequests")); return id; }
    const QPointer<SensorCalibrationStateReplica> replica = m_state;
    const quint64 ticket = m_pending.insert(id, operation);
    const QPointer<SensorCalibrationService> guard(this);
    emit busyChanged();
    if (!guard) return id;
    m_pending.dispatch(this, id, ticket,
                       [this, replica] { return replica && m_state == replica && ready(); },
                       [replica, call, id] { call(replica, id); },
                       [this, id, operation] {
        const QPointer<SensorCalibrationService> alive(this);
        emit busyChanged();
        if (alive)
            emit operationCompleted(id, operation, false, {}, QStringLiteral("disconnected"));
    });
    return id;
}

QString SensorCalibrationService::readCalibration(int sensor, const QString& requestId)
{
    return send(QStringLiteral("readCalibration"), [sensor](auto* state, const QString& id) {
        state->readCalibration(id, sensor);
    }, requestId);
}

QString SensorCalibrationService::validateCorrection(int sensor, double instrumentReading,
                                                      qint64 expectedContextRevision,
                                                      const QString& requestId)
{
    return send(QStringLiteral("validateCorrection"), [sensor, instrumentReading, expectedContextRevision](auto* state, const QString& id) {
        state->validateCorrection(id, sensor, instrumentReading, expectedContextRevision);
    }, requestId);
}

QString SensorCalibrationService::applyCorrection(int sensor, double instrumentReading,
                                                   qint64 expectedContextRevision,
                                                   qint64 expectedCalibrationRevision,
                                                   const QString& requestId)
{
    return send(QStringLiteral("applyCorrection"), [sensor, instrumentReading, expectedContextRevision,
                                                     expectedCalibrationRevision](auto* state, const QString& id) {
        state->applyCorrection(id, sensor, instrumentReading, expectedContextRevision,
                               expectedCalibrationRevision);
    }, requestId);
}

void SensorCalibrationService::failPending(const QString& reason)
{
    const QPointer<SensorCalibrationService> guard(this);
    m_pending.failAll([this] { emit busyChanged(); },
                      [this, guard, reason](const QString& id, const QString& operation) {
        if (!guard) return;
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, {}, reason);
        }, Qt::QueuedConnection);
    });
}
