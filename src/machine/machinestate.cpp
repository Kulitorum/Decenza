#include "machinestate.h"
#include "../ble/de1device.h"
#include "../ble/scaledevice.h"
#include <QDateTime>
#include <QDebug>

MachineState::MachineState(DE1Device* device, QObject* parent)
    : QObject(parent)
    , m_device(device)
{
    m_shotTimer = new QTimer(this);
    m_shotTimer->setInterval(100);  // Update every 100ms
    connect(m_shotTimer, &QTimer::timeout, this, &MachineState::updateShotTimer);

    if (m_device) {
        connect(m_device, &DE1Device::stateChanged, this, &MachineState::onDE1StateChanged);
        connect(m_device, &DE1Device::subStateChanged, this, &MachineState::onDE1SubStateChanged);
        connect(m_device, &DE1Device::connectedChanged, this, &MachineState::updatePhase);
    }
}

bool MachineState::isFlowing() const {
    return m_phase == Phase::Preinfusion ||
           m_phase == Phase::Pouring ||
           m_phase == Phase::Steaming ||
           m_phase == Phase::HotWater ||
           m_phase == Phase::Flushing;
}

bool MachineState::isHeating() const {
    return m_phase == Phase::Heating;
}

bool MachineState::isReady() const {
    // Allow commands when connected, even if asleep or heating
    // The machine will handle state transitions internally
    return m_phase == Phase::Ready || m_phase == Phase::Idle ||
           m_phase == Phase::Sleep || m_phase == Phase::Heating;
}

void MachineState::setScale(ScaleDevice* scale) {
    if (m_scale) {
        disconnect(m_scale, nullptr, this, nullptr);
    }

    m_scale = scale;

    if (m_scale) {
        connect(m_scale, &ScaleDevice::weightChanged,
                this, &MachineState::onScaleWeightChanged);
    }
}

void MachineState::setTargetWeight(double weight) {
    if (m_targetWeight != weight) {
        m_targetWeight = weight;
        emit targetWeightChanged();
    }
}

void MachineState::onDE1StateChanged() {
    updatePhase();
}

void MachineState::onDE1SubStateChanged() {
    updatePhase();
}

void MachineState::updatePhase() {
    if (!m_device || !m_device->isConnected()) {
        if (m_phase != Phase::Disconnected) {
            m_phase = Phase::Disconnected;
            emit phaseChanged();
        }
        return;
    }

    Phase oldPhase = m_phase;
    DE1::State state = m_device->state();
    DE1::SubState subState = m_device->subState();

    switch (state) {
        case DE1::State::Sleep:
        case DE1::State::GoingToSleep:
            m_phase = Phase::Sleep;
            break;

        case DE1::State::Idle:
        case DE1::State::SchedIdle:
            if (subState == DE1::SubState::Heating ||
                subState == DE1::SubState::FinalHeating) {
                m_phase = Phase::Heating;
            } else if (subState == DE1::SubState::Ready ||
                       subState == DE1::SubState::Stabilising) {
                m_phase = Phase::Ready;
            } else {
                m_phase = Phase::Idle;
            }
            break;

        case DE1::State::Espresso:
            if (subState == DE1::SubState::Heating ||
                subState == DE1::SubState::FinalHeating ||
                subState == DE1::SubState::Stabilising) {
                m_phase = Phase::Heating;
            } else if (subState == DE1::SubState::Preinfusion) {
                m_phase = Phase::Preinfusion;
            } else if (subState == DE1::SubState::Pouring) {
                m_phase = Phase::Pouring;
            } else if (subState == DE1::SubState::Ending) {
                m_phase = Phase::Ending;
            } else {
                m_phase = Phase::Preinfusion;
            }
            break;

        case DE1::State::Steam:
            if (subState == DE1::SubState::Steaming ||
                subState == DE1::SubState::Pouring) {
                m_phase = Phase::Steaming;
            } else {
                m_phase = Phase::Heating;
            }
            break;

        case DE1::State::HotWater:
            m_phase = Phase::HotWater;
            break;

        case DE1::State::HotWaterRinse:
            m_phase = Phase::Flushing;
            break;

        default:
            m_phase = Phase::Idle;
            break;
    }

    if (m_phase != oldPhase) {
        emit phaseChanged();

        // Start/stop shot timer
        bool wasFlowing = (oldPhase == Phase::Preinfusion ||
                          oldPhase == Phase::Pouring ||
                          oldPhase == Phase::Steaming ||
                          oldPhase == Phase::HotWater ||
                          oldPhase == Phase::Flushing);

        if (isFlowing() && !wasFlowing) {
            startShotTimer();
            m_stopAtWeightTriggered = false;
            emit shotStarted();
        } else if (!isFlowing() && wasFlowing) {
            stopShotTimer();
            emit shotEnded();
        }
    }
}

void MachineState::onScaleWeightChanged(double weight) {
    if (isFlowing() && m_device->state() == DE1::State::Espresso) {
        checkStopAtWeight(weight);
    }
}

void MachineState::checkStopAtWeight(double weight) {
    if (m_stopAtWeightTriggered) return;
    if (m_targetWeight <= 0) return;

    // Account for flow rate and lag (simple implementation)
    double flowRate = m_scale ? m_scale->flowRate() : 0;
    double lagCompensation = flowRate * 0.5;  // 500ms lag estimate

    if (weight >= (m_targetWeight - lagCompensation)) {
        m_stopAtWeightTriggered = true;
        emit targetWeightReached();

        // Stop the shot
        if (m_device) {
            m_device->stopOperation();
        }
    }
}

void MachineState::startShotTimer() {
    m_shotTime = 0.0;
    m_shotStartTime = QDateTime::currentMSecsSinceEpoch();
    m_shotTimer->start();
    emit shotTimeChanged();
}

void MachineState::stopShotTimer() {
    m_shotTimer->stop();
}

void MachineState::updateShotTimer() {
    qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_shotStartTime;
    m_shotTime = elapsed / 1000.0;
    emit shotTimeChanged();
}
