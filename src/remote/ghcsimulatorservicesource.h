#pragma once

#ifdef DECENZA_SIMULATOR

#include "rep_ghcsimulatorservice_source.h"
#include "serviceoperation.h"
#include "../simulator/ghcsimulator.h"

#include <QPointer>

class DE1Device;
class DE1Simulator;

class NativeGHCSimulatorServiceSource final : public GHCSimulatorStateSimpleSource
{
    Q_OBJECT
public:
    NativeGHCSimulatorServiceSource(DE1Device* device, DE1Simulator* simulator,
                                    QObject* parent = nullptr);
    void setSimulator(DE1Simulator* simulator);

    void refresh() override;
    void execute(QString requestId, QString operation) override;

private:
    QVariantMap capture() const;
    void publish();
    void complete(const QString& requestId, const QString& operation,
                  bool success, const QString& reason = {});

    QPointer<DE1Device> m_device;
    QPointer<DE1Simulator> m_simulator;
    DE1Simulator* m_controllerSimulator = nullptr;
    GHCSimulator m_controller;
    ServiceRequestIds m_requests;
};

#endif // DECENZA_SIMULATOR
