#include "maintenanceservicesource.h"

#include "../ble/de1device.h"
#include "../controllers/maincontroller.h"
#include "../controllers/profilemanager.h"
#include "../machine/machinestate.h"
#include "maintenanceoperationpolicy.h"

#include <QMetaType>

namespace { constexpr qint64 kMaxSafeRevision = 9007199254740991LL; }

NativeMaintenanceServiceSource::NativeMaintenanceServiceSource(
    MainController* controller, DE1Device* device, MachineState* machineState,
    ProfileManager* profiles, QObject* parent)
    : MaintenanceStateSimpleSource(parent), m_controller(controller), m_device(device),
      m_machineState(machineState), m_profiles(profiles)
{
    if (m_device) {
        connect(m_device, &DE1Device::connectedChanged, this, [this] { publish(true); });
        connect(m_device, &DE1Device::stateChanged, this, [this] { publish(true); });
        connect(m_device, &DE1Device::simulationModeChanged, this, [this] { publish(true); });
        connect(m_device, &DE1Device::isHeadlessChanged, this, [this] { publish(true); });
        connect(m_device, &DE1Device::descaleProgressChanged, this, [this] {
            if (m_descaleWorkflowActive && m_machineState
                && m_machineState->phase() == MachineState::Phase::Descaling) publish();
        });
        connect(m_device, &DE1Device::shotSampleReceived, this, [this] {
            // Cooling is part of the workflow too: the shared page must see
            // the boiler cross its start threshold before a cycle starts.
            if (m_descaleWorkflowActive) publish();
        });
    }
    if (m_machineState) {
        m_lastObservedPhase = static_cast<int>(m_machineState->phase());
        m_transportWorkflowActive = m_machineState->phase() == MachineState::Phase::Transport;
        if (m_transportWorkflowActive)
            m_transportCompletion.observe(true, false, false);
        connect(m_machineState, &MachineState::phaseChanged, this, [this] {
            const auto phase = m_machineState->phase();
            const int previousPhase = m_lastObservedPhase;
            m_lastObservedPhase = static_cast<int>(phase);
            if (m_descaleWorkflowActive && m_descaleStarted) {
                m_descaleCompletion.observe(
                    phase == MachineState::Phase::Descaling,
                    phase == MachineState::Phase::Disconnected,
                    phase == MachineState::Phase::Idle || phase == MachineState::Phase::Ready);
            }
            if (!m_transportWorkflowActive && phase == MachineState::Phase::Transport
                && previousPhase != static_cast<int>(MachineState::Phase::Transport)) {
                // Also adopt a Transport phase started outside this facade (for
                // example by a physical GHC), so every client sees one lifecycle.
                m_transportWorkflowActive = true;
                m_transportCompletion.reset();
                m_transportCompletion.observe(true, false, false);
            } else if (m_transportWorkflowActive) {
                m_transportCompletion.observe(
                    phase == MachineState::Phase::Transport,
                    phase == MachineState::Phase::Disconnected,
                    phase == MachineState::Phase::Idle || phase == MachineState::Phase::Ready
                        || phase == MachineState::Phase::Heating);
            }
            publish(true);
        });
        connect(m_machineState, &MachineState::shotTimeChanged, this, [this] {
            if (m_descaleWorkflowActive && m_machineState->phase() == MachineState::Phase::Descaling)
                publish();
        });
    }
    publish();
}

void NativeMaintenanceServiceSource::refresh() { publish(); }

QVariantMap NativeMaintenanceServiceSource::capture() const
{
    if (!m_device || !m_machineState || !m_controller || !m_profiles) return {};
    const auto phase = m_machineState->phase();
    const bool canStartTransport = !m_transportWorkflowActive
        && MaintenanceOperationPolicy::startAllowed(
            m_device->simulationMode(), m_device->isConnected(), static_cast<int>(phase),
            static_cast<int>(MachineState::Phase::Idle),
            static_cast<int>(MachineState::Phase::Heating),
            static_cast<int>(MachineState::Phase::Ready));
    return {
        {QStringLiteral("connected"), m_device->isConnected()},
        {QStringLiteral("simulationMode"), m_device->simulationMode()},
        {QStringLiteral("isHeadless"), m_device->isHeadless()},
        {QStringLiteral("steamTemperature"), m_device->steamTemperature()},
        {QStringLiteral("subState"), m_device->subStateInt()},
        {QStringLiteral("descaleProgress"), m_device->descaleProgress()},
        {QStringLiteral("descaleStepIndex"), m_device->descaleStepIndex()},
        {QStringLiteral("descaleStepCount"), m_device->descaleStepCount()},
        {QStringLiteral("descaleSecondsRemaining"), m_device->descaleSecondsRemaining()},
        {QStringLiteral("descaleCycle"), m_device->descaleCycle()},
        {QStringLiteral("phase"), static_cast<int>(phase)},
        {QStringLiteral("phaseIsIdle"), phase == MachineState::Phase::Idle},
        {QStringLiteral("phaseIsHeating"), phase == MachineState::Phase::Heating},
        {QStringLiteral("phaseIsReady"), phase == MachineState::Phase::Ready},
        {QStringLiteral("phaseIsDescaling"), phase == MachineState::Phase::Descaling},
        {QStringLiteral("phaseIsTransport"), phase == MachineState::Phase::Transport},
        {QStringLiteral("shotTime"), m_machineState->shotTime()},
        {QStringLiteral("canStartTransport"), canStartTransport},
        {QStringLiteral("descaleWorkflowActive"), m_descaleWorkflowActive},
        {QStringLiteral("descaleStarted"), m_descaleStarted},
        {QStringLiteral("descaleNeedsFinish"), m_descaleCompletion.needsFinish()},
        {QStringLiteral("descaleInterrupted"), m_descaleCompletion.interrupted()},
        {QStringLiteral("descaleStopped"), m_descaleCompletion.stopped()},
        {QStringLiteral("transportWorkflowActive"), m_transportWorkflowActive},
        {QStringLiteral("transportStarted"), m_transportCompletion.observedActive()},
        {QStringLiteral("transportNeedsFinish"), m_transportCompletion.needsFinish()},
        {QStringLiteral("transportInterrupted"), m_transportCompletion.interrupted()},
        {QStringLiteral("transportStopped"), m_transportCompletion.stopped()}
    };
}

void NativeMaintenanceServiceSource::publish(bool changed)
{
    if (changed && m_revision < kMaxSafeRevision) ++m_revision;
    const QVariantMap state = capture();
    setSnapshot(state);
    setRevision(m_revision);
    setReady(!state.isEmpty());
    setErrorMessage(state.isEmpty() ? QStringLiteral("Maintenance services are unavailable") : QString());
}

void NativeMaintenanceServiceSource::finish(const QString& requestId, const QString& operation,
                                             bool success, const QVariantMap& result,
                                             const QString& reason)
{
    if (m_requests.finish(requestId))
        emit operationCompleted(requestId, operation, success, result, reason, m_revision);
}

void NativeMaintenanceServiceSource::finishDescaleWorkflow(bool stopped)
{
    if (m_descaleWorkflowActive) {
        // The old-firmware cold-preparation path defers the actual Descale request.
        // Exiting while it cools must cancel that queued request before it can fire.
        m_device->cancelPendingDescale();
        if (m_machineState->phase() == MachineState::Phase::Descaling)
            m_device->stopOperation();
        m_controller->endDescaleHeaterHold();
        // The native page restores the cold-maintenance profile on its explicit
        // cleanup path. Use the host's settled-phase restore so an explicit Stop
        // during a cycle defers the profile upload until the machine is safe.
        m_profiles->restoreCurrentProfile();
        m_descaleWorkflowActive = false;
        m_descaleStarted = false;
    }
    // A Finish/Done acknowledgement also clears an outcome after cleanup already ran.
    m_descaleCompletion.end(stopped);
}

void NativeMaintenanceServiceSource::finishTransportWorkflow(bool stopped)
{
    if (m_transportWorkflowActive) {
        if (m_machineState->phase() == MachineState::Phase::Transport)
            m_device->stopOperation();
        else
            m_device->cancelPendingAirPurge();
        m_profiles->restoreCurrentProfile();
        m_transportWorkflowActive = false;
    }
    m_transportCompletion.end(stopped);
}

void NativeMaintenanceServiceSource::execute(QString requestId, QString operation,
                                              QVariantMap arguments, qint64 expectedRevision)
{
    const auto start = m_requests.begin(requestId);
    if (start == ServiceRequestIds::Start::DuplicateId) return;
    if (start != ServiceRequestIds::Start::Accepted) {
        emit operationCompleted(requestId, operation, false, {},
            start == ServiceRequestIds::Start::LimitReached ? QStringLiteral("tooManyRequests")
                                                            : QStringLiteral("invalidRequestId"),
            m_revision);
        return;
    }
    if (!MaintenanceOperationPolicy::supported(operation)) {
        finish(requestId, operation, false, {}, QStringLiteral("unsupportedOperation"));
        return;
    }
    if (!m_device || !m_machineState || !m_controller || !m_profiles) {
        finish(requestId, operation, false, {}, QStringLiteral("unavailable"));
        return;
    }
    const bool startOperation = operation == QLatin1String("startDescale")
        || operation == QLatin1String("startTransport");
    if (startOperation && !MaintenanceOperationPolicy::revisionMatches(expectedRevision, m_revision)) {
        finish(requestId, operation, false, {}, QStringLiteral("conflict"));
        return;
    }
    if (operation == QLatin1String("setHeadless")) {
        const QVariant value = arguments.value(QStringLiteral("headless"));
        if (arguments.size() != 1 || value.metaType().id() != QMetaType::Bool) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidArguments"));
            return;
        }
        m_device->setIsHeadless(value.toBool());
        finish(requestId, operation, true,
               {{QStringLiteral("isHeadless"), m_device->isHeadless()}});
        return;
    }
    if (!arguments.isEmpty() && operation != QLatin1String("startDescale")) {
        finish(requestId, operation, false, {}, QStringLiteral("invalidArguments"));
        return;
    }

    const auto phase = m_machineState->phase();
    const bool canStart = MaintenanceOperationPolicy::startAllowed(
        m_device->simulationMode(), m_device->isConnected(), static_cast<int>(phase),
        static_cast<int>(MachineState::Phase::Idle),
        static_cast<int>(MachineState::Phase::Heating),
        static_cast<int>(MachineState::Phase::Ready));
    QVariantMap result;
    if (operation == QLatin1String("beginDescaleHold")) {
        if (!m_descaleWorkflowActive) {
            m_controller->beginDescaleHeaterHold();
            m_descaleWorkflowActive = true;
            m_descaleStarted = phase == MachineState::Phase::Descaling;
            m_descaleCompletion.reset();
            if (m_descaleStarted)
                m_descaleCompletion.observe(true, false, false);
        }
    } else if (operation == QLatin1String("finishDescale")
               || operation == QLatin1String("cancelDescale")) {
        finishDescaleWorkflow(false);
    } else if (operation == QLatin1String("startDescale")) {
        const QVariant allowHotValue = arguments.value(QStringLiteral("allowHotSteam"));
        if (arguments.size() != 1 || allowHotValue.metaType().id() != QMetaType::Bool
            || !m_descaleWorkflowActive) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidArguments")); return;
        }
        if (!canStart) { finish(requestId, operation, false, {}, QStringLiteral("notReady")); return; }
        if (!MaintenanceOperationPolicy::hotDescaleAllowed(m_device->steamTemperature(), allowHotValue.toBool())) {
            finish(requestId, operation, false, {}, QStringLiteral("steamTooHot")); return;
        }
        m_device->startDescale();
        m_descaleStarted = true;
        m_descaleCompletion.reset();
        result.insert(QStringLiteral("submitted"), true);
    } else if (operation == QLatin1String("stopDescale")) {
        const bool stopped = m_descaleWorkflowActive
            && phase == MachineState::Phase::Descaling;
        if (m_descaleWorkflowActive)
            finishDescaleWorkflow(stopped);
        result.insert(QStringLiteral("stopped"), stopped);
    } else if (operation == QLatin1String("startTransport")) {
        if (!canStart) { finish(requestId, operation, false, {}, QStringLiteral("notReady")); return; }
        if (!m_transportWorkflowActive) {
            m_transportWorkflowActive = true;
            m_transportCompletion.reset();
            if (phase == MachineState::Phase::Transport)
                m_transportCompletion.observe(true, false, false);
            m_device->startAirPurge();
            result.insert(QStringLiteral("submitted"), true);
        } else {
            result.insert(QStringLiteral("submitted"), false);
            result.insert(QStringLiteral("alreadyRunning"), true);
        }
    } else if (operation == QLatin1String("finishTransport")) {
        finishTransportWorkflow(false);
    } else if (operation == QLatin1String("stopTransport")) {
        const bool stopped = m_transportWorkflowActive
            && phase == MachineState::Phase::Transport;
        if (m_transportWorkflowActive)
            finishTransportWorkflow(stopped);
        result.insert(QStringLiteral("stopped"), stopped);
    }

    publish(true);
    finish(requestId, operation, true, result);
}
