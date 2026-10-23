#pragma once

#include "rep_maintenanceservice_source.h"
#include "maintenanceoperationpolicy.h"
#include "serviceoperation.h"

#include <QPointer>

class DE1Device;
class MachineState;
class MainController;
class ProfileManager;

class NativeMaintenanceServiceSource final : public MaintenanceStateSimpleSource
{
    Q_OBJECT
public:
    NativeMaintenanceServiceSource(MainController* controller, DE1Device* device,
                                    MachineState* machineState, ProfileManager* profiles,
                                    QObject* parent = nullptr);

    void refresh() override;
    void execute(QString requestId, QString operation, QVariantMap arguments,
                 qint64 expectedRevision) override;

private:
    void publish(bool changed = false);
    QVariantMap capture() const;
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    void finishDescaleWorkflow(bool stopped = false);
    void finishTransportWorkflow(bool stopped = false);

    QPointer<MainController> m_controller;
    QPointer<DE1Device> m_device;
    QPointer<MachineState> m_machineState;
    QPointer<ProfileManager> m_profiles;
    ServiceRequestIds m_requests;
    qint64 m_revision = 0;
    bool m_descaleWorkflowActive = false;
    bool m_descaleStarted = false;
    MaintenanceWorkflowCompletion m_descaleCompletion;
    bool m_transportWorkflowActive = false;
    MaintenanceWorkflowCompletion m_transportCompletion;
    int m_lastObservedPhase = -1;
};
