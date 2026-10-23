#pragma once

#include "rep_shotreviewactions_source.h"
#include "serviceoperation.h"

#include <QPointer>

class MainController;
class Settings;
class BLEManager;
class RefractometerProxy;

class NativeShotReviewActionSource final : public ShotReviewActionStateSimpleSource
{
    Q_OBJECT
public:
    explicit NativeShotReviewActionSource(MainController* controller, Settings* settings,
                                          BLEManager* bleManager, RefractometerProxy* refractometer,
                                          QObject* parent = nullptr);

    void execute(QString requestId, QString operation, QVariantMap arguments) override;

private:
    void publishUploadState();
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    bool validRequestId(const QString& requestId) const;
    void publishState();
    void publishRefractometerState();

    QPointer<MainController> m_controller;
    QPointer<Settings> m_settings;
    QPointer<BLEManager> m_bleManager;
    QPointer<RefractometerProxy> m_refractometer;
    ServiceRequestIds m_requests;
};
