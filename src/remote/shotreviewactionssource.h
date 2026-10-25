#pragma once

#include "rep_shotreviewactions_source.h"
#include "serviceoperation.h"
#include "shotreviewsessions.h"

#include <QPointer>

class MainController;
class Settings;
class BLEManager;
class RefractometerProxy;
class RemoteAccess;

class NativeShotReviewActionSource final : public ShotReviewActionStateSimpleSource
{
    Q_OBJECT
public:
    explicit NativeShotReviewActionSource(MainController* controller, Settings* settings,
                                          BLEManager* bleManager, RefractometerProxy* refractometer,
                                          QObject* parent = nullptr);

    // Whether the remote connector is live decides if Discuss pastes the shot.
    void setRemoteAccess(RemoteAccess* remoteAccess);
    void execute(QString requestId, QString operation, QVariantMap arguments) override;
    void openClient(const QString& clientId, bool native = false);
    void closeClient(const QString& clientId);

private:
    void publishUploadState();
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    bool validRequestId(const QString& requestId) const;
    void publishState();
    void publishRefractometerState();
    void synchronizeReviewSessions();
    void completeMeasurement();

    QPointer<MainController> m_controller;
    QPointer<Settings> m_settings;
    QPointer<BLEManager> m_bleManager;
    QPointer<RefractometerProxy> m_refractometer;
    QPointer<RemoteAccess> m_remoteAccess;
    ServiceRequestIds m_requests;
    ShotReviewSessions m_sessions;
    quint64 m_measurementGeneration = 0;
};
