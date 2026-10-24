#include "ghcsimulatorservicesource.h"

#ifdef DECENZA_SIMULATOR

#include "../ble/de1device.h"
#include "../simulator/de1simulator.h"
#include "ghcsimulatoroperationpolicy.h"

NativeGHCSimulatorServiceSource::NativeGHCSimulatorServiceSource(
    DE1Device* device, DE1Simulator* simulator, QObject* parent)
    : GHCSimulatorStateSimpleSource(parent), m_device(device), m_simulator(simulator)
{
    // The service controller intentionally has no DE1Device set. GHCSimulator's
    // normal physical-device fallback therefore cannot be reached from this RPC.
    connect(&m_controller, &GHCSimulator::ledColorsChanged, this,
            &NativeGHCSimulatorServiceSource::publish);
    connect(&m_controller, &GHCSimulator::stopPressedChanged, this,
            &NativeGHCSimulatorServiceSource::publish);
    connect(&m_controller, &GHCSimulator::activeFunctionChanged, this,
            &NativeGHCSimulatorServiceSource::publish);
    if (device) {
        connect(device, &DE1Device::simulationModeChanged, this,
                &NativeGHCSimulatorServiceSource::publish);
        connect(device, &QObject::destroyed, this, [this] {
            m_device = nullptr;
            publish();
        });
    }
    setSimulator(simulator);
}

void NativeGHCSimulatorServiceSource::setSimulator(DE1Simulator* simulator)
{
    if (m_simulator == simulator && m_controllerSimulator == simulator)
        return;
    if (m_simulator)
        disconnect(m_simulator, nullptr, this, nullptr);

    m_simulator = simulator;
    m_controllerSimulator = simulator;
    m_controller.setDE1Simulator(simulator);
    if (simulator) {
        connect(simulator, &DE1Simulator::stateChanged, this,
                &NativeGHCSimulatorServiceSource::publish);
        connect(simulator, &DE1Simulator::subStateChanged, this,
                &NativeGHCSimulatorServiceSource::publish);
        connect(simulator, &QObject::destroyed, this, [this] {
            m_simulator = nullptr;
            m_controllerSimulator = nullptr;
            m_controller.setDE1Simulator(nullptr);
            if (m_controller.stopPressed())
                m_controller.releaseStop();
            publish();
        });
        m_controller.onSimulatorStateChanged();
    } else if (m_controller.stopPressed()) {
        m_controller.releaseStop();
    }
    publish();
}

QVariantMap NativeGHCSimulatorServiceSource::capture() const
{
    return GHCSimulatorOperationPolicy::snapshot(
        m_device && m_device->simulationMode(), !m_simulator.isNull(),
        m_controller.ledColors(), m_controller.stopPressed(),
        static_cast<int>(m_controller.activeFunction()));
}

void NativeGHCSimulatorServiceSource::publish()
{
    if ((!m_device || !m_device->simulationMode() || m_simulator.isNull())
        && m_controller.stopPressed()) {
        m_controller.releaseStop();
    }
    const QVariantMap state = capture();
    setSnapshot(state);
    setReady(m_device != nullptr);
}

void NativeGHCSimulatorServiceSource::refresh()
{
    publish();
}

void NativeGHCSimulatorServiceSource::complete(const QString& requestId,
                                                const QString& operation,
                                                bool success,
                                                const QString& reason)
{
    if (m_requests.finish(requestId))
        emit operationCompleted(requestId, operation, success, reason);
}

void NativeGHCSimulatorServiceSource::execute(QString requestId, QString operation)
{
    const auto start = m_requests.begin(requestId);
    if (start != ServiceRequestIds::Start::Accepted) {
        // A duplicate ID belongs to the original in-flight request. Completing
        // it here could make the replica consume the first request's response.
        if (start == ServiceRequestIds::Start::DuplicateId)
            return;
        const QString reason = start == ServiceRequestIds::Start::LimitReached
            ? QStringLiteral("tooManyRequests")
            : QStringLiteral("invalidRequestId");
        emit operationCompleted(requestId, operation, false, reason);
        return;
    }

    const bool simulationMode = m_device && m_device->simulationMode();
    if (!GHCSimulatorOperationPolicy::supported(operation)) {
        complete(requestId, operation, false, QStringLiteral("unsupportedOperation"));
        return;
    }
    if (!GHCSimulatorOperationPolicy::simulatorAvailable(simulationMode,
                                                         !m_simulator.isNull())) {
        complete(requestId, operation, false, QStringLiteral("simulatorUnavailable"));
        return;
    }

    if (operation == QLatin1String("pressEspresso")) m_controller.pressEspresso();
    else if (operation == QLatin1String("pressSteam")) m_controller.pressSteam();
    else if (operation == QLatin1String("pressHotWater")) m_controller.pressHotWater();
    else if (operation == QLatin1String("pressFlush")) m_controller.pressFlush();
    else if (operation == QLatin1String("pressStop")) {
        // Stop is terminal in DE1Simulator::stop(). Remote clients share this
        // source, so don't keep a client-owned held state that another client
        // could release or a disconnect could strand.
        m_controller.pressStop();
        m_controller.releaseStop();
    }

    publish();
    complete(requestId, operation, true);
}

#endif // DECENZA_SIMULATOR
