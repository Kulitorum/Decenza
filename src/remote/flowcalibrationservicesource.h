#pragma once

#include "rep_flowcalibrationservice_source.h"
#include "serviceoperation.h"

#include <QPointer>

class FlowCalibrationModel;
class SettingsCalibration;

class NativeFlowCalibrationSource final : public FlowCalibrationStateSimpleSource
{
    Q_OBJECT
public:
    NativeFlowCalibrationSource(FlowCalibrationModel* model, SettingsCalibration* settings,
                                 QObject* parent = nullptr);

    void refresh() override;
    void loadRecentShots(QString requestId) override;
    void loadShot(QString requestId, int index, quint64 expectedShotListRevision) override;
    void save(QString requestId, double multiplier, qint64 expectedCalibrationRevision) override;

private:
    void publish();
    void beginLoad(const QString& requestId, const QString& operation, int index);
    void completeLoad();
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    QVariantMap currentShot() const;
    void setFailure(const QString& requestId, const QString& operation,
                    const QString& reason);

    QPointer<FlowCalibrationModel> m_model;
    QPointer<SettingsCalibration> m_settings;
    ServiceRequestIds m_requests;
    QString m_loadingRequestId;
    QString m_loadingOperation;
    int m_requestedIndex = -1;
    qint64 m_calibrationRevision = 0;
};
