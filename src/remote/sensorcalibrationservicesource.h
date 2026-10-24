#pragma once

#include "rep_sensorcalibrationservice_source.h"
#include "serviceoperation.h"

#include <QPointer>

class DE1Device;
class SensorCalibrationController;

class NativeSensorCalibrationSource final : public SensorCalibrationStateSimpleSource
{
    Q_OBJECT
public:
    NativeSensorCalibrationSource(SensorCalibrationController* controller, DE1Device* device,
                                  QObject* parent = nullptr);

    void refresh() override;
    void readCalibration(QString requestId, int sensor) override;
    void validateCorrection(QString requestId, int sensor, double instrumentReading,
                            qint64 expectedContextRevision) override;
    void applyCorrection(QString requestId, int sensor, double instrumentReading,
                         qint64 expectedContextRevision,
                         qint64 expectedCalibrationRevision) override;

private:
    void publish(bool contextChanged = false, bool calibrationChanged = false);
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    void invalidRequest(const QString& requestId, const QString& operation,
                        ServiceRequestIds::Start start);
    QVariantMap sensorSnapshot(int sensor) const;

    QPointer<SensorCalibrationController> m_controller;
    QPointer<DE1Device> m_device;
    ServiceRequestIds m_requests;
    qint64 m_contextRevision = 0;
    qint64 m_calibrationRevision = 0;
};
