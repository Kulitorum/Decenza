#include "sensorcalibrationservicesource.h"

#include "../ble/de1device.h"
#include "../controllers/sensorcalibrationcontroller.h"

#include <cmath>
#include <limits>

namespace { constexpr qint64 kMaxSafeRevision = 9007199254740991LL; }

NativeSensorCalibrationSource::NativeSensorCalibrationSource(
    SensorCalibrationController* controller, DE1Device* device, QObject* parent)
    : SensorCalibrationStateSimpleSource(parent), m_controller(controller), m_device(device)
{
    if (m_controller) {
        connect(m_controller, &SensorCalibrationController::contextChanged, this, [this] {
            publish(true, false);
        });
        connect(m_controller, &QObject::destroyed, this, [this] {
            m_controller = nullptr;
            setReady(false);
            setErrorMessage(QStringLiteral("Sensor calibration is unavailable"));
        });
    }
    if (m_device) {
        connect(m_device, &DE1Device::calibrationChanged, this, [this] { publish(false, true); });
        connect(m_device, &DE1Device::connectedChanged, this, [this] { publish(true, false); });
        connect(m_device, &QObject::destroyed, this, [this] {
            m_device = nullptr;
            publish(true, false);
        });
    }
    publish();
}

void NativeSensorCalibrationSource::refresh() { publish(); }

QVariantMap NativeSensorCalibrationSource::sensorSnapshot(int sensor) const
{
    if (!m_controller || sensor < 0 || sensor >= m_controller->sensorCount()) return {};
    const int target = m_controller->calibrationTarget(sensor);
    const bool hasHold = m_controller->isTestProfileActive(sensor)
        && std::isfinite(m_controller->declaredHoldValue(sensor));
    const bool hasStored = m_device && m_device->hasStoredCalibration(target);
    const auto* spec = SensorCalibrationController::specFor(
        static_cast<SensorCalibrationController::Sensor>(sensor));
    if (!spec) return {};
    QVariantMap result{
        {QStringLiteral("sensor"), sensor},
        {QStringLiteral("calibrationTarget"), target},
        {QStringLiteral("profileFilename"), m_controller->profileFilename(sensor)},
        {QStringLiteral("labelKey"), QString::fromLatin1(spec->labelKey)},
        {QStringLiteral("labelFallback"), QString::fromLatin1(spec->labelFallback)},
        {QStringLiteral("instrumentKey"), QString::fromLatin1(spec->instrumentKey)},
        {QStringLiteral("instrumentFallback"), QString::fromLatin1(spec->instrumentFallback)},
        {QStringLiteral("unitLabel"), m_controller->unitLabel(sensor)},
        {QStringLiteral("minValue"), m_controller->minValue(sensor)},
        {QStringLiteral("maxValue"), m_controller->maxValue(sensor)},
        {QStringLiteral("maxCorrection"), m_controller->maxCorrection(sensor)},
        {QStringLiteral("entryStep"), m_controller->entryStep(sensor)},
        {QStringLiteral("testProfileActive"), m_controller->isTestProfileActive(sensor)},
        {QStringLiteral("hasDeclaredHold"), hasHold},
        {QStringLiteral("declaredHoldValue"), hasHold ? m_controller->declaredHoldValue(sensor) : 0.0},
        {QStringLiteral("hasStored"), hasStored},
        {QStringLiteral("storedCalibration"), hasStored ? m_device->storedCalibration(target) : 0.0}
    };
    return result;
}

void NativeSensorCalibrationSource::publish(bool contextChanged, bool calibrationChanged)
{
    if (contextChanged && m_contextRevision < kMaxSafeRevision) ++m_contextRevision;
    if (calibrationChanged && m_calibrationRevision < kMaxSafeRevision) ++m_calibrationRevision;
    if (!m_controller || !m_device) {
        setSnapshot({{QStringLiteral("connected"), false},
                     {QStringLiteral("contextRevision"), m_contextRevision},
                     {QStringLiteral("calibrationRevision"), m_calibrationRevision},
                     {QStringLiteral("sensors"), QVariantList{}}});
        setReady(false);
        setErrorMessage(QStringLiteral("Sensor calibration is unavailable"));
        return;
    }
    QVariantList sensors;
    for (int sensor = 0; sensor < m_controller->sensorCount(); ++sensor)
        sensors.append(sensorSnapshot(sensor));
    setSnapshot({{QStringLiteral("connected"), m_device->isConnected()},
                 {QStringLiteral("contextRevision"), m_contextRevision},
                 {QStringLiteral("calibrationRevision"), m_calibrationRevision},
                 {QStringLiteral("sensors"), sensors}});
    setReady(true);
    setErrorMessage({});
}

void NativeSensorCalibrationSource::invalidRequest(const QString& requestId,
                                                    const QString& operation,
                                                    ServiceRequestIds::Start start)
{
    if (start == ServiceRequestIds::Start::DuplicateId) return;
    emit operationCompleted(requestId, operation, false, {},
           start == ServiceRequestIds::Start::LimitReached ? QStringLiteral("tooManyRequests")
                                                           : QStringLiteral("invalidRequestId"));
}

void NativeSensorCalibrationSource::readCalibration(QString requestId, int sensor)
{
    const QString operation = QStringLiteral("readCalibration");
    const auto start = m_requests.begin(requestId);
    if (start != ServiceRequestIds::Start::Accepted) { invalidRequest(requestId, operation, start); return; }
    if (!m_controller || !m_device) { finish(requestId, operation, false, {}, QStringLiteral("unavailable")); return; }
    if (sensor < 0 || sensor >= m_controller->sensorCount()) { finish(requestId, operation, false, {}, QStringLiteral("invalidSensor")); return; }
    const int target = m_controller->calibrationTarget(sensor);
    const bool accepted = m_device->readCalibration(target);
    publish();
    finish(requestId, operation, accepted,
           {{QStringLiteral("sensor"), sensor}, {QStringLiteral("accepted"), accepted}},
           accepted ? QString() : QStringLiteral("readRefused"));
}

void NativeSensorCalibrationSource::validateCorrection(QString requestId, int sensor,
                                                        double instrumentReading,
                                                        qint64 expectedContextRevision)
{
    const QString operation = QStringLiteral("validateCorrection");
    const auto start = m_requests.begin(requestId);
    if (start != ServiceRequestIds::Start::Accepted) { invalidRequest(requestId, operation, start); return; }
    if (!m_controller) { finish(requestId, operation, false, {}, QStringLiteral("unavailable")); return; }
    if (sensor < 0 || sensor >= m_controller->sensorCount()) { finish(requestId, operation, false, {}, QStringLiteral("invalidSensor")); return; }
    if (expectedContextRevision != m_contextRevision) { finish(requestId, operation, false, {}, QStringLiteral("conflict")); return; }
    if (!std::isfinite(instrumentReading)) { finish(requestId, operation, false, {}, QStringLiteral("invalidReading")); return; }
    const QString rejection = m_controller->rejectionReason(sensor, instrumentReading);
    finish(requestId, operation, true,
           {{QStringLiteral("reading"), instrumentReading},
            {QStringLiteral("contextRevision"), m_contextRevision},
            {QStringLiteral("rejection"), rejection}}, {});
}

void NativeSensorCalibrationSource::applyCorrection(QString requestId, int sensor,
                                                     double instrumentReading,
                                                     qint64 expectedContextRevision,
                                                     qint64 expectedCalibrationRevision)
{
    const QString operation = QStringLiteral("applyCorrection");
    const auto start = m_requests.begin(requestId);
    if (start != ServiceRequestIds::Start::Accepted) { invalidRequest(requestId, operation, start); return; }
    if (!m_controller || !m_device) { finish(requestId, operation, false, {}, QStringLiteral("unavailable")); return; }
    if (sensor < 0 || sensor >= m_controller->sensorCount()) { finish(requestId, operation, false, {}, QStringLiteral("invalidSensor")); return; }
    if (expectedContextRevision != m_contextRevision
        || expectedCalibrationRevision != m_calibrationRevision) {
        finish(requestId, operation, false, {}, QStringLiteral("conflict"));
        return;
    }
    if (!std::isfinite(instrumentReading)) { finish(requestId, operation, false, {}, QStringLiteral("invalidReading")); return; }
    const QString rejection = m_controller->rejectionReason(sensor, instrumentReading);
    if (!rejection.isEmpty()) { finish(requestId, operation, false, {}, rejection); return; }
    const bool accepted = m_controller->applyCorrection(sensor, instrumentReading);
    publish();
    finish(requestId, operation, accepted,
           {{QStringLiteral("accepted"), accepted},
            {QStringLiteral("contextRevision"), m_contextRevision},
            {QStringLiteral("calibrationRevision"), m_calibrationRevision}},
           accepted ? QString() : QStringLiteral("writeRefused"));
}

void NativeSensorCalibrationSource::finish(const QString& requestId, const QString& operation,
                                            bool success, const QVariantMap& result,
                                            const QString& reason)
{
    if (!m_requests.finish(requestId)) return;
    emit operationCompleted(requestId, operation, success, result, reason);
}
