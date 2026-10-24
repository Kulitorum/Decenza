#pragma once

#include "rep_machinecontrolservice_source.h"
#include "serviceoperation.h"

#include <QPointer>
#include <QSet>

class DE1Device;
class BLEManager;
class BatteryManager;
class MainController;
class MachineState;
class ScaleDeviceProxy;
class Settings;

class NativeMachineControlServiceSource final : public MachineControlStateSimpleSource
{
    Q_OBJECT
public:
    NativeMachineControlServiceSource(MainController* controller, Settings* settings,
                                      MachineState* machineState, DE1Device* device,
                                      BLEManager* bleManager, BatteryManager* batteryManager,
                                      ScaleDeviceProxy* scaleDevice,
                                      QObject* parent = nullptr);

    void execute(QString requestId, QString operation, QVariantMap fields,
                 QString expectedContextId) override;
    void publishPresentationEvent(const QString& event, const QVariantMap& payload = {});

private slots:
    void onSimulatedShotRequestCompleted(const QString& requestId, bool success, qint64 shotId);

private:
    void finish(const QString& requestId, const QString& operation,
                bool success, const QString& message = {},
                const QVariantMap& result = {});
    void onRecipeStartCompleted(const QString& requestId, qint64 recipeId,
                                bool success, const QString& reason);
    bool validateStart(const QString& expectedContextId, QString* reason) const;
    static bool readIndex(const QVariantMap& fields, const QString& key, int* value);
    static bool readPresetIndex(const QVariantMap& fields, const QString& key, int* value,
                                QVariantMap* expectedPreset);
    static bool readPositiveId(const QVariantMap& fields, const QString& key, qint64* value);
    static bool onlyField(const QVariantMap& fields, const QString& key);

    QPointer<MainController> m_controller;
    QPointer<Settings> m_settings;
    QPointer<MachineState> m_machineState;
    QPointer<DE1Device> m_device;
    QPointer<BLEManager> m_bleManager;
    QPointer<BatteryManager> m_batteryManager;
    QPointer<ScaleDeviceProxy> m_scaleDevice;
    ServiceRequestIds m_requestIds;
    QSet<QString> m_recipeStartRequests;
    QSet<QString> m_simulatedShotRequests;
};
