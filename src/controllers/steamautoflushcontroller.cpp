#include "steamautoflushcontroller.h"

#include "core/diagnosticlogging.h"
#include "core/settings_brew.h"
#include "ble/de1device.h"
#include "machine/machinestate.h"

SteamAutoFlushController::SteamAutoFlushController(SettingsBrew* brew, DE1Device* device,
                                                   MachineState* machineState,
                                                   QObject* parent)
    : QObject(parent), m_brew(brew), m_device(device), m_machineState(machineState)
{
    m_clock.start();
    m_timer.setInterval(100);
    connect(&m_timer, &QTimer::timeout, this, &SteamAutoFlushController::updateCountdown);

    if (m_device) {
        connect(m_device, &DE1Device::stateChanged,
                this, &SteamAutoFlushController::refreshMachineState);
        connect(m_device, &DE1Device::subStateChanged,
                this, &SteamAutoFlushController::refreshMachineState);
        connect(m_device, &DE1Device::connectedChanged,
                this, &SteamAutoFlushController::refreshMachineState);
        connect(m_device, &QObject::destroyed, this, [this] {
            m_device = nullptr;
            refreshMachineState();
        });
    }
    if (m_machineState) {
        connect(m_machineState, &MachineState::phaseChanged,
                this, &SteamAutoFlushController::refreshMachineState);
        connect(m_machineState, &QObject::destroyed, this, [this] {
            m_machineState = nullptr;
            refreshMachineState();
        });
    }
    if (m_brew) {
        connect(m_brew, &QObject::destroyed, this, [this] { m_brew = nullptr; });
    }

    refreshMachineState();
}

bool SteamAutoFlushController::isPuffing() const
{
    return m_device && m_device->isConnected()
        && m_device->state() == DE1::State::Steam
        && m_device->subState() == DE1::SubState::Puffing
        && m_machineState
        && m_machineState->phase() == MachineState::Phase::Steaming;
}

void SteamAutoFlushController::refreshMachineState()
{
    const bool puffing = isPuffing();
    if (!puffing) {
        m_wasPuffing = false;
        cancel();
        return;
    }

    if (!m_wasPuffing) {
        m_wasPuffing = true;
        const int seconds = m_brew ? m_brew->steamAutoFlushSeconds() : 0;
        if (seconds > 0) {
            DIAG_DEBUG(STEAM, "SteamAutoFlushController")
                << "Starting steam auto-flush countdown:" << seconds << "seconds";
            arm(seconds);
        }
    }
}

void SteamAutoFlushController::arm(int seconds)
{
    m_deadlineMs = nowMs() + static_cast<qint64>(seconds) * 1000;
    m_armed = true;
    m_timer.start();
    updateCountdown();
}

void SteamAutoFlushController::cancel()
{
    m_timer.stop();
    m_armed = false;
    m_deadlineMs = 0;
    if (m_remainingSeconds != 0.0) {
        m_remainingSeconds = 0.0;
        emit remainingSecondsChanged();
    }
}

void SteamAutoFlushController::updateCountdown()
{
    if (!m_armed)
        return;
    if (!isPuffing()) {
        m_wasPuffing = false;
        cancel();
        return;
    }

    const qint64 remainingMs = m_deadlineMs - nowMs();
    if (remainingMs <= 0) {
        m_timer.stop();
        m_armed = false;
        m_deadlineMs = 0;
        if (m_remainingSeconds != 0.0) {
            m_remainingSeconds = 0.0;
            emit remainingSecondsChanged();
        }
        emit expired();
        return;
    }

    const double remaining = static_cast<double>(remainingMs) / 1000.0;
    if (remaining != m_remainingSeconds) {
        m_remainingSeconds = remaining;
        emit remainingSecondsChanged();
    }
}

qint64 SteamAutoFlushController::nowMs() const
{
#ifdef DECENZA_TESTING
    if (m_testClock)
        return m_testClock();
#endif
    return m_clock.elapsed();
}

#ifdef DECENZA_TESTING
void SteamAutoFlushController::setClockForTesting(std::function<qint64()> clock)
{
    m_testClock = std::move(clock);
}

void SteamAutoFlushController::evaluateForTesting()
{
    refreshMachineState();
    updateCountdown();
}
#endif
