#include "machinestate.h"
#include "../ble/de1device.h"
#include "../ble/scaledevice.h"
#include "../core/settings.h"
#include <QDateTime>
#include <QDebug>
#include <QMetaEnum>

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
    // For steam, only count as flowing if actually steaming (not purging/ending)
    if (m_phase == Phase::Steaming && m_device) {
        DE1::SubState subState = m_device->subState();
        return subState == DE1::SubState::Steaming ||
               subState == DE1::SubState::Pouring;
    }

    return m_phase == Phase::Preinfusion ||
           m_phase == Phase::Pouring ||
           m_phase == Phase::HotWater ||
           m_phase == Phase::Flushing ||
           m_phase == Phase::Descaling ||
           m_phase == Phase::Cleaning;
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

double MachineState::shotTime() const {
    // Calculate time on-demand for accuracy (timer updates can be delayed on Android)
    if (m_shotTimer->isActive() && m_shotStartTime > 0) {
        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_shotStartTime;
        return elapsed / 1000.0;
    }
    return m_shotTime;
}

QString MachineState::phaseString() const {
    QMetaEnum metaEnum = QMetaEnum::fromType<Phase>();
    return QString::fromLatin1(metaEnum.valueToKey(static_cast<int>(m_phase)));
}

void MachineState::setScale(ScaleDevice* scale) {
    // Use static_cast<void*> to avoid QDebug dereferencing the pointer (which could crash if dangling)
    qDebug() << "MachineState::setScale called with" << static_cast<void*>(scale) << "current m_scale:" << static_cast<void*>(m_scale);

    if (m_scale && m_scale != scale) {
        qDebug() << "Disconnecting old scale:" << static_cast<void*>(m_scale);
        disconnect(m_scale, nullptr, this, nullptr);
    }

    m_scale = scale;

    if (m_scale) {
        qDebug() << "Connecting new scale:" << static_cast<void*>(m_scale);
        connect(m_scale, &ScaleDevice::weightChanged,
                this, &MachineState::onScaleWeightChanged);
        // Relay weight changes to QML via scaleWeightChanged signal
        connect(m_scale, &ScaleDevice::weightChanged, this, [this](double) {
            emit scaleWeightChanged();
        });
        // Emit immediately so QML picks up current weight
        emit scaleWeightChanged();
    }
}


void MachineState::setSettings(Settings* settings) {
    m_settings = settings;
}

void MachineState::setTargetWeight(double weight) {
    if (m_targetWeight != weight) {
        m_targetWeight = weight;
        emit targetWeightChanged();
    }
}

void MachineState::setTargetVolume(double volume) {
    if (m_targetVolume != volume) {
        m_targetVolume = volume;
        emit targetVolumeChanged();
    }
}

void MachineState::setStopAtType(StopAtType type) {
    if (m_stopAtType != type) {
        m_stopAtType = type;
        emit stopAtTypeChanged();
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
                m_phase = Phase::EspressoPreheating;  // Use specific phase for espresso preheating
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
            // Map all active steam substates to Steaming phase
            // This keeps the live view visible during purge (Puffing) and ending
            // Only show Heating for pre-steam warmup (Heating/FinalHeating substates)
            if (subState == DE1::SubState::Steaming ||
                subState == DE1::SubState::Pouring ||
                subState == DE1::SubState::Puffing ||
                subState == DE1::SubState::Ending) {
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

        case DE1::State::Refill:
            m_phase = Phase::Refill;
            break;

        case DE1::State::Descale:
            m_phase = Phase::Descaling;
            break;

        case DE1::State::Clean:
            m_phase = Phase::Cleaning;
            break;

        default:
            m_phase = Phase::Idle;
            break;
    }

    if (m_phase != oldPhase) {
        // Detect espresso cycle start (entering preheating from non-espresso state)
        bool wasInEspresso = (oldPhase == Phase::EspressoPreheating ||
                              oldPhase == Phase::Preinfusion ||
                              oldPhase == Phase::Pouring ||
                              oldPhase == Phase::Ending);
        bool isInEspresso = (m_phase == Phase::EspressoPreheating ||
                             m_phase == Phase::Preinfusion ||
                             m_phase == Phase::Pouring ||
                             m_phase == Phase::Ending);

        // Start/stop shot timer (do this immediately, before deferred signals)
        bool wasFlowing = (oldPhase == Phase::Preinfusion ||
                          oldPhase == Phase::Pouring ||
                          oldPhase == Phase::Steaming ||
                          oldPhase == Phase::HotWater ||
                          oldPhase == Phase::Flushing ||
                          oldPhase == Phase::Descaling ||
                          oldPhase == Phase::Cleaning);

        if (isFlowing() && !wasFlowing) {
            // Don't restart timer mid-espresso cycle (BLE phase glitch protection)
            // For espresso, the timer starts at preinfusion and should not reset
            // if there's a brief glitch to a non-flowing state and back
            if (!wasInEspresso) {
                startShotTimer();
                m_stopAtWeightTriggered = false;
                m_stopAtVolumeTriggered = false;
                m_stopAtTimeTriggered = false;
                m_cumulativeVolume = 0.0;  // Reset volume tracking

                // CRITICAL: Clear any pending BLE commands to prevent stale profile uploads
                // from executing during active operations. This fixes a bug where queued
                // profile commands could corrupt a running shot.
                if (m_device) {
                    m_device->clearCommandQueue();
                }

                m_tareCompleted = false;

                // Auto-tare for Hot Water (espresso tares at cycle start via MainController)
                if (m_phase == Phase::HotWater) {
                    QTimer::singleShot(100, this, [this]() {
                        tareScale();
                        qDebug() << "=== TARE: Hot Water started ===";
                    });
                }
            } else {
                // Mid-espresso glitch recovery: restart timer without resetting state
                // This preserves stop-at-weight triggers and cumulative tracking
                if (!m_shotTimer->isActive()) {
                    qDebug() << "=== TIMER RESTART: recovering from mid-espresso phase glitch ===";
                    // If m_shotStartTime is invalid (0 or in the future), reset it
                    qint64 now = QDateTime::currentMSecsSinceEpoch();
                    if (m_shotStartTime <= 0 || m_shotStartTime > now) {
                        qWarning() << "=== TIMER FIX: m_shotStartTime was invalid:" << m_shotStartTime << "- resetting to now ===";
                        m_shotStartTime = now;
                        m_shotTime = 0.0;
                    }
                    m_shotTimer->start();
                }
            }
        } else if (!isFlowing() && wasFlowing) {
            // Don't stop timer during espresso Ending phase - let it run until cycle ends
            if (!isInEspresso) {
                stopShotTimer();
            }
        }

        // Reset timer state when entering espresso cycle (before signals)
        // This ensures timer shows 0 during preheating and properly starts at preinfusion
        // Without this, m_shotStartTime would contain the PREVIOUS shot's timestamp,
        // causing the timer to show huge elapsed values when preinfusion starts
        if (isInEspresso && !wasInEspresso) {
            m_shotTime = 0.0;
            m_shotStartTime = 0;  // Mark as invalid so preinfusion properly starts it
            emit shotTimeChanged();  // Update UI to show 0 during preheating

            // CRITICAL: Emit espressoCycleStarted IMMEDIATELY (not deferred) so MainController
            // can reset its m_shotStartTime before any shot samples arrive via BLE.
            // If deferred, shot samples could arrive first with wrong timestamps.
            qDebug() << "  -> EMITTING espressoCycleStarted (immediate)";
            emit espressoCycleStarted();
        }

        // Defer other signal emissions to allow pending BLE notifications to process first
        // This prevents QML binding updates from blocking the event loop
        // Note: espressoCycleStarted is emitted immediately above to avoid race conditions
        QTimer::singleShot(0, this, [this, wasInEspresso, isInEspresso, wasFlowing]() {
            emit phaseChanged();

            if (isFlowing() && !wasFlowing) {
                emit shotStarted();
            } else if (!isFlowing() && wasFlowing) {
                emit shotEnded();
            }
        });
    }

    // Also check for timer stop on substate changes (even if phase didn't change)
    // This handles steam stopping (Puffing/Ending substates) where phase stays Steaming
    if (!isFlowing() && m_shotTimer->isActive()) {
        qDebug() << "=== TIMER STOP: isFlowing() became false (substate change) ===";
        stopShotTimer();
    }
}

void MachineState::onScaleWeightChanged(double weight) {
    // Check if tare completed (scale reported near-zero after tare command)
    if (m_waitingForTare && qAbs(weight) < 1.0) {
        m_waitingForTare = false;
        m_tareCompleted = true;
        qDebug() << "=== TARE COMPLETE: scale reported" << weight << "g, stop-at-weight now active ===";
        emit tareCompleted();
    }

    // Auto-tare when cup is removed (significant weight drop while idle)
    if (m_phase == Phase::Ready || m_phase == Phase::Idle) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();

        // Detect cup removal: weight was >50g and dropped to <10g within 2 seconds
        if (m_lastIdleWeight > 50.0 && weight < 10.0) {
            qint64 elapsed = now - m_lastWeightTime;
            if (elapsed < 2000) {  // Drop happened within 2 seconds
                qDebug() << "=== AUTO-TARE: Cup removed (weight dropped from"
                         << m_lastIdleWeight << "to" << weight << ") ===";
                tareScale();
            }
        }

        m_lastIdleWeight = weight;
        m_lastWeightTime = now;
        return;
    }

    // Reset tracking when not idle (so we detect removal after next shot)
    m_lastIdleWeight = 0.0;

    DE1::State state = m_device->state();
    // For espresso: only check weight when stopAtType is Weight (Volume checked in onFlowSample)
    // For hot water: always check weight (no volume option)
    if (state == DE1::State::HotWater) {
        checkStopAtWeight(weight);
    } else if (state == DE1::State::Espresso && m_stopAtType == StopAtType::Weight) {
        checkStopAtWeight(weight);
    }
}

void MachineState::checkStopAtWeight(double weight) {
    if (m_stopAtWeightTriggered) return;
    if (!m_tareCompleted) return;  // Don't check until tare has happened

    // Determine target based on current state
    double target = 0;
    DE1::State state = m_device ? m_device->state() : DE1::State::Sleep;

    if (state == DE1::State::HotWater && m_settings) {
        target = m_settings->waterVolume();  // ml ≈ g for water
    } else {
        target = m_targetWeight;  // Espresso target
    }

    if (target <= 0) return;

    // Account for flow rate and lag (simple implementation)
    double flowRate = m_scale ? m_scale->flowRate() : 0;
    // Cap flow rate to reasonable range (max ~10 g/s for espresso, higher for water)
    double maxFlowRate = (state == DE1::State::HotWater) ? 20.0 : 10.0;
    if (flowRate > maxFlowRate) flowRate = maxFlowRate;
    if (flowRate < 0) flowRate = 0;

    // Hot water has higher flow rate, needs more lag compensation
    double lagSeconds = (state == DE1::State::HotWater) ? 0.9 : 0.5;
    double lagCompensation = flowRate * lagSeconds;

    if (weight >= (target - lagCompensation)) {
        m_stopAtWeightTriggered = true;
        emit targetWeightReached();

        // Stop the operation
        if (m_device) {
            m_device->stopOperation();
        }
    }
}

void MachineState::checkStopAtVolume() {
    if (m_stopAtVolumeTriggered) return;
    if (!m_tareCompleted) return;  // Don't check until tare has happened

    double target = m_targetVolume;
    if (target <= 0) return;

    // Get current flow rate for lag compensation
    double flowRate = m_scale ? m_scale->flowRate() : 0;
    if (flowRate > 10.0) flowRate = 10.0;  // Cap to reasonable range
    if (flowRate < 0) flowRate = 0;

    // Use same lag compensation as weight-based stop
    double lagSeconds = 0.5;
    double lagCompensation = flowRate * lagSeconds;

    if (m_cumulativeVolume >= (target - lagCompensation)) {
        m_stopAtVolumeTriggered = true;
        emit targetVolumeReached();

        qDebug() << "MachineState: Target volume reached -" << m_cumulativeVolume << "ml /" << target << "ml";

        // Stop the operation
        if (m_device) {
            m_device->stopOperation();
        }
    }
}

void MachineState::onFlowSample(double flowRate, double deltaTime) {
    // Only process during active dispensing states
    auto state = m_device->state();
    if (state != DE1::State::Espresso &&
        state != DE1::State::Steam &&
        state != DE1::State::HotWater &&
        state != DE1::State::HotWaterRinse) return;
    if (!isFlowing()) return;

    // Forward flow samples to the scale (FlowScale will integrate, physical scales ignore)
    if (m_scale) {
        m_scale->addFlowSample(flowRate, deltaTime);
    }

    // Integrate flow to track cumulative volume (ml)
    // flowRate is in ml/s, deltaTime is in seconds
    double volumeDelta = flowRate * deltaTime;
    if (volumeDelta > 0) {
        m_cumulativeVolume += volumeDelta;
        emit cumulativeVolumeChanged();

        // Check if we should stop at volume (only during espresso)
        if (state == DE1::State::Espresso && m_stopAtType == StopAtType::Volume) {
            checkStopAtVolume();
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

    // Check if we've reached the target time for steam/flush
    // Real machines handle this in firmware, but this provides
    // support for the simulator and a fallback for real devices
    checkStopAtTime();
}

void MachineState::checkStopAtTime() {
    if (m_stopAtTimeTriggered) return;
    if (!m_settings) return;

    double target = 0;
    if (m_phase == Phase::Steaming) {
        // Steam timeout is handled by DE1 firmware via ShotSettings.steamTimeout
        // The machine stops steam flow but stays in Steam state until GHC stop is pressed,
        // which triggers a final cleaning puff. Only use app-side stop for simulator.
        if (m_device && m_device->simulationMode()) {
            target = m_settings->steamTimeout();
        }
    } else if (m_phase == Phase::Flushing) {
        target = m_settings->flushSeconds();
    } else {
        return;  // Only Steam and Flush use time-based stop
    }
    if (target <= 0) return;

    if (m_shotTime >= target) {
        m_stopAtTimeTriggered = true;

        // Stop the operation
        if (m_device) {
            m_device->stopOperation();
            qDebug() << "=== STOP AT TIME: reached" << target << "seconds ===";
        }
    }
}

double MachineState::scaleWeight() const {
    return m_scale ? m_scale->weight() : 0.0;
}

double MachineState::scaleFlowRate() const {
    return m_scale ? m_scale->flowRate() : 0.0;
}

void MachineState::tareScale() {
    if (m_scale && m_scale->isConnected()) {
        // Immediately disable stop-at-weight until tare completes
        // This prevents early stop if m_tareCompleted was true from a previous operation
        m_tareCompleted = false;
        m_waitingForTare = true;

        m_scale->tare();
        m_scale->resetFlowCalculation();  // Avoid flow rate spikes after tare

        qDebug() << "=== TARE SENT: waiting for scale to report ~0g ===";

        // Fallback timeout in case scale never reports near-zero
        // (e.g., scale disconnects, or tare fails)
        QTimer::singleShot(3000, this, [this]() {
            if (m_waitingForTare) {
                qWarning() << "=== TARE TIMEOUT: scale didn't report ~0g within 3s, enabling stop-at-weight anyway ===";
                m_waitingForTare = false;
                m_tareCompleted = true;
                emit tareCompleted();
            }
        });
    }
}
