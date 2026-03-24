#include "machinestate.h"
#include "../ble/de1device.h"
#include "../ble/scaledevice.h"
#include "../core/settings.h"
#include "../controllers/shottimingcontroller.h"
#include <QDateTime>
#include <QDebug>
#include <QMetaEnum>

MachineState::MachineState(DE1Device* device, QObject* parent)
    : QObject(parent)
    , m_device(device)
{
    m_weightEmitTimer.start();
    m_flowRateEmitTimer.start();

    // Trailing-edge timers: start on the first update suppressed by the 10Hz
    // throttle. When they fire 100ms later, QML re-reads the property getter,
    // picking up whatever the latest cached value is at that point.
    m_weightTrailingTimer = new QTimer(this);
    m_weightTrailingTimer->setSingleShot(true);
    m_weightTrailingTimer->setInterval(100);
    connect(m_weightTrailingTimer, &QTimer::timeout, this, [this]() {
        m_weightEmitTimer.restart();
        emit scaleWeightChanged();
    });

    m_flowRateTrailingTimer = new QTimer(this);
    m_flowRateTrailingTimer->setSingleShot(true);
    m_flowRateTrailingTimer->setInterval(100);
    connect(m_flowRateTrailingTimer, &QTimer::timeout, this, [this]() {
        m_flowRateEmitTimer.restart();
        emit scaleFlowRateChanged();
    });

    m_shotTimer = new QTimer(this);
    m_shotTimer->setInterval(100);  // Update every 100ms
    connect(m_shotTimer, &QTimer::timeout, this, &MachineState::onShotTimerTick);

    if (m_device) {
        connect(m_device, &DE1Device::stateChanged, this, &MachineState::onDE1StateChanged);
        connect(m_device, &DE1Device::subStateChanged, this, &MachineState::onDE1SubStateChanged);
        connect(m_device, &DE1Device::connectedChanged, this, &MachineState::updatePhase);

        // Sync initial phase from device (handles case where device was already
        // connected before MachineState was constructed, e.g. simulator mode)
        updatePhase();
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
    // de1app does no state check — it sends commands directly and lets the DE1
    // firmware decide. Including Heating here lets users queue operations while
    // the machine warms up, matching that behavior.
    return m_phase == Phase::Ready || m_phase == Phase::Idle ||
           m_phase == Phase::Sleep || m_phase == Phase::Heating;
}

double MachineState::shotTime() const {
    // Use timing controller only for espresso phases
    bool isEspressoPhase = (m_phase == Phase::EspressoPreheating ||
                           m_phase == Phase::Preinfusion ||
                           m_phase == Phase::Pouring ||
                           m_phase == Phase::Ending);
    if (m_timingController && isEspressoPhase) {
        return m_timingController->shotTime();
    }
    // Use local timer for steam/hot water/flush and fallback
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

ScaleDevice* MachineState::scale() const {
    return m_scale;
}

void MachineState::setScale(ScaleDevice* scale) {
    if (m_scale == scale) {
        // Same scale pointer — just refresh QML, don't add duplicate connections.
        // Without this guard, each call adds 2 more signal connections that never
        // get disconnected, causing progressive main-thread congestion over days.
        if (m_scale) {
            emit scaleWeightChanged();
            emit scaleFlowRateChanged();
        }
        return;
    }

    if (m_scale) {
        disconnect(m_scale, nullptr, this, nullptr);
        m_weightTrailingTimer->stop();
        m_flowRateTrailingTimer->stop();
    }

    m_scale = scale;

    if (m_scale) {
        connect(m_scale, &ScaleDevice::weightChanged,
                this, &MachineState::onScaleWeightChanged);
        // Relay weight changes to QML via scaleWeightChanged signal (throttled to 10Hz)
        // LSLR flow rate is computed by WeightProcessor on a worker thread
        // and cached via updateCachedFlowRates()
        connect(m_scale, &ScaleDevice::weightChanged, this, [this](double) {
            if (m_weightEmitTimer.elapsed() >= 100) {  // 10Hz cap
                m_weightEmitTimer.restart();
                m_weightTrailingTimer->stop();
                emit scaleWeightChanged();
            } else if (!m_weightTrailingTimer->isActive()) {
                m_weightTrailingTimer->start();
            }
        });
        // Emit immediately so QML picks up current weight
        emit scaleWeightChanged();
        emit scaleFlowRateChanged();
    }
}


void MachineState::setSettings(Settings* settings) {
    m_settings = settings;
}

void MachineState::setTimingController(ShotTimingController* controller) {
    m_timingController = controller;
    if (m_timingController) {
        // Forward timing controller signals
        connect(m_timingController, &ShotTimingController::shotTimeChanged,
                this, &MachineState::shotTimeChanged);
        // Handle tare complete - need to update m_tareCompleted flag AND emit signal
        connect(m_timingController, &ShotTimingController::tareCompleteChanged,
                this, &MachineState::onTimingControllerTareComplete);
    }
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
            // Always clear hot water frozen weight when any new flow starts.
            // This is outside the !wasInEspresso guard because the frozen weight
            // is a hot-water-only display feature that must not persist into
            // espresso cycles. Without this, hot water SAW freezing the display
            // at e.g. 75g causes scaleWeight() to return that stale value for
            // the entire subsequent espresso shot (issue #529).
            m_hotWaterFrozenWeight = -1.0;

            // Don't restart timer mid-espresso cycle (BLE phase glitch protection)
            // For espresso, the timer starts at preinfusion and should not reset
            // if there's a brief glitch to a non-flowing state and back
            if (!wasInEspresso) {
                startShotTimer();
                m_stopAtWeightTriggered = false;
                m_stopAtVolumeTriggered = false;
                m_stopAtTimeTriggered = false;
                m_hotWaterTareBaseline = 0.0;
                m_hotWaterTareTimeMs = 0;
                m_hotWaterMaxEffectiveWeight = 0.0;
                m_lastAutoTareTime = 0;  // Reset holdoff for new flow cycle
                m_preinfusionVolume = 0.0;
                m_pourVolume = 0.0;
                m_cumulativeVolume = 0.0;
                m_lastEmittedCumulativeVolumeMl = -1;
                m_lastEmittedPreinfusionVolumeMl = -1;
                m_lastEmittedPourVolumeMl = -1;

                // CRITICAL: Clear any pending BLE commands to prevent stale profile uploads
                // from executing during active operations. This fixes a bug where queued
                // profile commands could corrupt a running shot.
                if (m_device) {
                    m_device->clearCommandQueue();
                }

                m_tareCompleted = false;
                m_waitingForTare = false;
                if (m_tareTimeoutTimer)
                    m_tareTimeoutTimer->stop();

                // Reset and start scale timer when flow starts (like de1app)
                if (m_scale) {
                    m_scale->resetTimer();
                    m_scale->startTimer();
                    qDebug() << "=== SCALE TIMER: Reset + Started (flow began) ===";
                }

                // Auto-tare for Hot Water (espresso tares at cycle start via MainController)
                // Delay 200ms after resetTimer/startTimer to avoid BLE command contention —
                // WriteWithoutResponse can silently drop packets when sent in rapid succession.
                if (m_phase == Phase::HotWater) {
                    QTimer::singleShot(200, this, [this]() {
                        if (m_phase != Phase::HotWater) return;  // Operation ended before timer fired
                        tareScale();
                        qDebug() << "=== TARE: Hot Water started (200ms after timer cmds) ===";
                    });
                }
            } else {
                // Mid-espresso: either starting extraction (from preheating) or glitch recovery
                bool startingExtraction = (oldPhase == Phase::EspressoPreheating);

                if (startingExtraction) {
                    // EspressoPreheating is wasInEspresso=true, so the outer !wasInEspresso
                    // reset block does not fire. Reset counters here for a fresh extraction.
                    startShotTimer();
                    m_stopAtWeightTriggered = false;
                    m_stopAtVolumeTriggered = false;
                    m_stopAtTimeTriggered = false;
                        m_preinfusionVolume = 0.0;
                    m_pourVolume = 0.0;
                    m_cumulativeVolume = 0.0;
                    m_lastEmittedCumulativeVolumeMl = -1;
                    m_lastEmittedPreinfusionVolumeMl = -1;
                    m_lastEmittedPourVolumeMl = -1;

                    // Reset and start scale timer (like de1app)
                    if (m_scale) {
                        m_scale->resetTimer();
                        m_scale->startTimer();
                        qDebug() << "=== SCALE TIMER: Reset + Started (espresso extraction began) ===";
                    }
                } else if (!m_shotTimer->isActive()) {
                    // Actual glitch recovery: restart timer without resetting state
                    // This preserves stop-at-weight triggers and cumulative tracking
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
                // Stop scale timer when flow ends
                if (m_scale) {
                    m_scale->stopTimer();
                    qDebug() << "=== SCALE TIMER: Stopped (flow ended) ===";
                }
            }
        }

        // Stop scale timer when exiting espresso cycle (e.g., Ending -> Idle)
        if (wasInEspresso && !isInEspresso) {
            if (m_scale) {
                m_scale->stopTimer();
                qDebug() << "=== SCALE TIMER: Stopped (espresso cycle ended) ===";
            }
        }

        // Reset timer state when entering espresso cycle (before signals)
        // This ensures timer shows 0 during preheating and properly starts at preinfusion
        // Without this, m_shotStartTime would contain the PREVIOUS shot's timestamp,
        // causing the timer to show huge elapsed values when preinfusion starts
        if (isInEspresso && !wasInEspresso) {
            m_shotTime = 0.0;
            m_shotStartTime = 0;  // Mark as invalid so preinfusion properly starts it
            m_lastAutoTareTime = 0;  // Reset holdoff for new espresso cycle
            emit shotTimeChanged();  // Update UI to show 0 during preheating

            // CRITICAL: Emit espressoCycleStarted IMMEDIATELY (not deferred) so MainController
            // can reset its m_shotStartTime before any shot samples arrive via BLE.
            // If deferred, shot samples could arrive first with wrong timestamps.
            emit espressoCycleStarted();
        }

        // Defer other signal emissions to allow pending BLE notifications to process first.
        // This prevents QML binding updates from blocking the event loop during the BLE callback chain.
        // Note: espressoCycleStarted is emitted immediately above to avoid race conditions.
        QMetaObject::invokeMethod(this, [this, wasInEspresso, isInEspresso, wasFlowing]() {
            emit phaseChanged();

            if (isFlowing() && !wasFlowing) {
                emit shotStarted();
            } else if (!isFlowing() && wasFlowing) {
                emit shotEnded();
            }
        }, Qt::QueuedConnection);
    }

    // Also check for timer stop on substate changes (even if phase didn't change)
    // This handles steam stopping (Puffing/Ending substates) where phase stays Steaming
    if (!isFlowing() && m_shotTimer->isActive()) {
        qDebug() << "=== TIMER STOP: isFlowing() became false (substate change) ===";
        stopShotTimer();
        if (m_scale) {
            m_scale->stopTimer();
            qDebug() << "=== SCALE TIMER: Stopped (substate change) ===";
        }
    }
}

void MachineState::onScaleWeightChanged(double weight) {
    // Check if tare completed (scale reported near-zero after tare command)
    if (m_waitingForTare && qAbs(weight) < 1.0) {
        m_waitingForTare = false;
        m_tareCompleted = true;
        if (m_tareTimeoutTimer)
            m_tareTimeoutTimer->stop();
        emit tareCompleted();
    }

    // Hot water fire-and-forget: if the BLE tare actually worked (scale zeroed),
    // clear the baseline so SAW uses absolute weight from now on.
    // Guard: only clear if we haven't seen significant water flow yet (< 3g effective).
    // Slow-tare scales (e.g. Eureka Precisa) may process the tare command after water
    // has been dispensed, causing weight to drop to 0 — clearing baseline then would
    // make the effective weight wrong.
    if (m_phase == Phase::HotWater && m_hotWaterTareBaseline != 0.0 && m_hotWaterTareTimeMs > 0
        && qAbs(weight) < 1.0 && m_hotWaterMaxEffectiveWeight < 3.0) {
        qDebug() << "=== TARE: Scale zeroed, clearing hot water baseline ===";
        m_hotWaterTareBaseline = 0.0;
    }

    // Track peak effective weight during hot water (used to guard baseline clearing)
    if (m_phase == Phase::HotWater && m_hotWaterTareBaseline != 0.0) {
        double effective = weight - m_hotWaterTareBaseline;
        if (effective > m_hotWaterMaxEffectiveWeight) {
            m_hotWaterMaxEffectiveWeight = effective;
        }
    }

    // Burst-log every weight sample for 2s after hot water tare (debugging BLE tare reliability)
    if (m_phase == Phase::HotWater && m_hotWaterTareTimeMs > 0) {
        qint64 sinceMs = QDateTime::currentMSecsSinceEpoch() - m_hotWaterTareTimeMs;
        if (sinceMs < 2000) {
            qDebug() << "[HW-Tare+" << sinceMs << "ms] scale=" << weight
                     << "effective=" << (weight - m_hotWaterTareBaseline)
                     << "baseline=" << m_hotWaterTareBaseline;
        } else {
            // First sample past 2s — log whether tare succeeded
            qDebug() << "[HW-Tare] 2s summary: baseline="
                     << (m_hotWaterTareBaseline == 0.0 ? "cleared (tare OK)" : QString::number(m_hotWaterTareBaseline, 'f', 1) + "g (tare FAILED, using baseline)");
            m_hotWaterTareTimeMs = 0;  // Stop burst logging
        }
    }


    // Auto-tare during "flow before" phase (like de1app: heating substates before water flows)
    // Handles forgotten-cup scenario for both Espresso and HotWater
    constexpr double AUTO_TARE_THRESHOLD = 2.0;  // grams (de1app uses 0.04g, 2g avoids noise)
    constexpr qint64 AUTO_TARE_HOLDOFF_MS = 1000;  // 1s between tares (matches de1app)

    bool isFlowBefore = false;

    // Both Espresso preheat and HotWater heating use the same substates before flow.
    // Check substate directly (not just m_phase) because m_phase can lag behind
    // BLE state changes — avoids taring after water has already started flowing.
    if ((m_phase == Phase::EspressoPreheating || m_phase == Phase::HotWater) && m_device) {
        DE1::SubState subState = m_device->subState();
        isFlowBefore = (subState == DE1::SubState::Heating ||
                        subState == DE1::SubState::FinalHeating ||
                        subState == DE1::SubState::Stabilising);
    }

    if (isFlowBefore && m_tareCompleted && weight > AUTO_TARE_THRESHOLD) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastAutoTareTime >= AUTO_TARE_HOLDOFF_MS) {
            m_lastAutoTareTime = now;
            qDebug() << "=== AUTO-TARE: Cup placed during" << phaseString()
                     << "(weight:" << weight << "g) ===";
            tareScale();
            emit flowBeforeAutoTare();
        }
    }

    if (!m_device) return;
    DE1::State state = m_device->state();

    // Throttled weight logging during SAW-relevant phases (~every 2s)
    if (state == DE1::State::Espresso || state == DE1::State::HotWater) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastWeightLogMs >= 2000) {
            if (state == DE1::State::HotWater && m_hotWaterTareBaseline != 0.0) {
                qDebug() << "[Scale] weight=" << QString::number(weight, 'f', 1)
                         << "effective=" << QString::number(weight - m_hotWaterTareBaseline, 'f', 1)
                         << "baseline=" << QString::number(m_hotWaterTareBaseline, 'f', 1)
                         << "phase=" << phaseString()
                         << "tare=" << m_tareCompleted;
            } else {
                qDebug() << "[Scale] weight=" << QString::number(weight, 'f', 1)
                         << "phase=" << phaseString()
                         << "tare=" << m_tareCompleted;
            }
            m_lastWeightLogMs = now;
        }
    }
    // Hot water: MachineState handles stop-at-weight (ShotTimingController not active)
    // Espresso: ShotTimingController handles stop-at-weight (with proper 1.5s lag compensation)
    if (state == DE1::State::HotWater) {
        checkStopAtWeightHotWater(weight);
    }
}

void MachineState::checkStopAtWeightHotWater(double weight) {
    if (m_stopAtWeightTriggered) return;
    if (!m_tareCompleted) {
        // Throttle this warning to every 5s to avoid log spam at 5Hz
        static qint64 s_lastTareWarnMs = 0;
        qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs - s_lastTareWarnMs >= 5000) {
            qWarning() << "[SAW-HotWater] Tare not completed - skipping SAW check, weight=" << weight;
            s_lastTareWarnMs = nowMs;
        }
        return;
    }

    // Volume mode: machine handles auto-stop via flowmeter, don't interfere
    if (m_settings && m_settings->waterVolumeMode() == "volume") return;

    double target = m_settings ? m_settings->waterVolume() : 0;  // ml ≈ g for water
    if (target <= 0) return;

    // Use weight relative to the baseline recorded at tare time.
    // If the BLE tare command succeeded, baseline was cleared to 0 (absolute weight).
    // If the BLE tare was lost/ignored, baseline holds the pre-tare weight so we
    // measure only the water added since the tare was requested.
    double effectiveWeight = weight - m_hotWaterTareBaseline;

    // Hot water: use fixed 5g offset (predictable, avoids scale-dependent issues)
    double stopThreshold = target - 5.0;

    if (effectiveWeight >= stopThreshold) {
        m_stopAtWeightTriggered = true;
        m_hotWaterFrozenWeight = effectiveWeight;  // Freeze UI display at trigger weight
        qDebug() << "[SAW-HotWater] STOP triggered: effectiveWeight=" << effectiveWeight
                 << "scaleWeight=" << weight << "baseline=" << m_hotWaterTareBaseline
                 << "threshold=" << stopThreshold << "target=" << target;
        emit targetWeightReached();

        if (m_device) {
            m_device->stopOperation();
        }
    }
}

void MachineState::checkStopAtVolume() {
    if (m_stopAtVolumeTriggered) return;
    if (!m_tareCompleted) return;  // Don't check until tare has happened

    // Skip volume-based stop when a physical scale is configured and the user has
    // opted in. Uses "configured" (scaleAddress non-empty) not "connected" so a
    // momentary BLE disconnect mid-shot doesn't re-enable SAV unexpectedly.
    if (m_settings && m_settings->ignoreVolumeWithScale()
        && !m_settings->scaleAddress().isEmpty()) return;

    // Skip SAV for basic profiles when a physical scale is configured (matches de1app's
    // skip_sav_check / expecting_present). Beta testing in de1app revealed basic profiles
    // have unrealistically low volume targets that trigger early stops. Uses "configured"
    // not "connected" so a momentary BLE disconnect doesn't re-enable SAV mid-session.
    bool isBasicProfile = (m_profileType == QLatin1String("settings_2a")
                        || m_profileType == QLatin1String("settings_2b"));
    if (isBasicProfile && m_settings && !m_settings->scaleAddress().isEmpty()) return;

    double target = m_targetVolume;
    if (target <= 0) return;

    // No lag compensation for SAV (matches de1app). Volume is already imprecise
    // from the flow sensor, and de1app uses a raw comparison intentionally.
    if (m_pourVolume >= target) {
        m_stopAtVolumeTriggered = true;
        emit targetVolumeReached();

        qDebug() << "MachineState: Target pour volume reached -" << m_pourVolume
                 << "ml (preinfusion:" << m_preinfusionVolume << "ml, total:" << m_cumulativeVolume << "ml) /" << target << "ml";

        // Stop the operation
        if (m_device) {
            m_device->stopOperation();
        }
    }
}

void MachineState::checkStopAtVolumeHotWater() {
    if (m_stopAtVolumeTriggered) return;
    if (!m_settings) return;

    // Match de1app's hot water SAV logic:
    // - Scale configured: target = 250 ml (huge safety net, SAW does the real stopping)
    // - No scale: target = waterVolume setting (app-side volume stop is primary)
    double target;
    if (!m_settings->scaleAddress().isEmpty()) {
        target = 250.0;
    } else {
        target = m_settings->waterVolume();
    }
    if (target <= 0) return;

    if (m_pourVolume >= target) {
        m_stopAtVolumeTriggered = true;
        emit targetVolumeReached();

        qDebug() << "MachineState: Hot water volume stop -" << m_pourVolume
                 << "ml /" << target << "ml"
                 << (m_settings->scaleAddress().isEmpty() ? "(no scale)" : "(safety net)");

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

    // Integrate flow to track volume (ml), split by DE1 substate (matches de1app).
    // de1app routes volume by substate: preinfusion → preinfusion_volume,
    // pouring → pour_volume. Other substates (heating, stabilising) are ignored.
    // flowRate is in ml/s, deltaTime is in seconds.
    double volumeDelta = flowRate * deltaTime;
    if (volumeDelta > 0) {
        if (m_phase == Phase::Preinfusion) {
            m_preinfusionVolume += volumeDelta;
            int roundedMl = static_cast<int>(m_preinfusionVolume);
            if (roundedMl != m_lastEmittedPreinfusionVolumeMl) {
                m_lastEmittedPreinfusionVolumeMl = roundedMl;
                emit preinfusionVolumeChanged();
            }
        } else {
            // Pouring, HotWater, and all other flowing states count as pour volume
            m_pourVolume += volumeDelta;
            int roundedMl = static_cast<int>(m_pourVolume);
            if (roundedMl != m_lastEmittedPourVolumeMl) {
                m_lastEmittedPourVolumeMl = roundedMl;
                emit pourVolumeChanged();
            }
        }
        m_cumulativeVolume = m_preinfusionVolume + m_pourVolume;
        // Only emit when rounded ml changes (avoids ~206 samples/shot of QML binding churn at 5Hz)
        int roundedMl = static_cast<int>(m_cumulativeVolume);
        if (roundedMl != m_lastEmittedCumulativeVolumeMl) {
            m_lastEmittedCumulativeVolumeMl = roundedMl;
            emit cumulativeVolumeChanged();
        }

        // Check volume-based stops (matches de1app: SAV runs for both Espresso and HotWater)
        if (state == DE1::State::Espresso) {
            checkStopAtVolume();
        } else if (state == DE1::State::HotWater) {
            checkStopAtVolumeHotWater();
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

void MachineState::onShotTimerTick() {
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
    if (!m_scale) return 0.0;

    // After hot water SAW triggered, freeze the display to prevent late tare from
    // showing 0g. Checked outside the phase guard because the completion overlay
    // reads scaleWeight() after phase has already transitioned to Idle.
    // Safe unconditionally: reset to -1.0 at the start of each new flow cycle.
    if (m_hotWaterFrozenWeight >= 0.0)
        return m_hotWaterFrozenWeight;

    double raw = m_scale->weight();

    // During hot water, return effective weight (accounting for fire-and-forget baseline).
    // This ensures the UI shows water added, not cup+water, on slow-tare scales.
    if (m_phase == Phase::HotWater && m_hotWaterTareBaseline != 0.0)
        return qMax(0.0, raw - m_hotWaterTareBaseline);

    return raw;
}

double MachineState::scaleFlowRate() const {
    return smoothedScaleFlowRate();
}

// LSLR flow rate is now computed by WeightProcessor on a dedicated worker thread.
// These methods return cached values updated via updateCachedFlowRates().
double MachineState::smoothedScaleFlowRate() const {
    return m_cachedFlowRate;
}

void MachineState::updateCachedFlowRates(double flowRate, double flowRateShort) {
    m_cachedFlowRate = flowRate;
    m_cachedFlowRateShort = flowRateShort;
    if (m_flowRateEmitTimer.elapsed() >= 100) {  // 10Hz cap
        m_flowRateEmitTimer.restart();
        m_flowRateTrailingTimer->stop();
        emit scaleFlowRateChanged();
    } else if (!m_flowRateTrailingTimer->isActive()) {
        m_flowRateTrailingTimer->start();
    }
}

void MachineState::tareScale() {
    // Delegate to timing controller if available (new centralized timing)
    if (m_timingController && m_phase != Phase::HotWater) {
        m_timingController->tare();
        return;
    }

    // Hot water: fire-and-forget tare (matches de1app behavior).
    // Record baseline weight so SAW can use (scale_weight - baseline) regardless
    // of whether the BLE tare command is actually executed by the scale.
    // When the scale does tare, onScaleWeightChanged detects the drop and clears
    // the baseline so SAW switches to using absolute weight.
    if (m_phase == Phase::HotWater && m_scale && m_scale->isConnected()) {
        m_hotWaterTareBaseline = m_scale->weight();
        m_hotWaterTareTimeMs = QDateTime::currentMSecsSinceEpoch();
        m_scale->tare();
        m_scale->resetFlowCalculation();
        m_tareCompleted = true;
        m_waitingForTare = false;
        if (m_tareTimeoutTimer)
            m_tareTimeoutTimer->stop();
        qDebug() << "=== TARE: Hot Water fire-and-forget, baseline=" << m_hotWaterTareBaseline << "g ===";
        emit tareCompleted();
        return;
    }

    // Fallback: legacy wait-for-zero tare (used when no timing controller)
    if (m_scale && m_scale->isConnected()) {
        // Skip if a tare is already in progress — sending another BLE tare command
        // while waiting for the scale to respond confuses the scale and can cause it
        // to never report ~0g, eventually triggering a 6s timeout (issue #430).
        if (m_waitingForTare) {
            qDebug() << "=== TARE: Skipped (already waiting for scale response) ===";
            return;
        }

        m_tareCompleted = false;
        m_waitingForTare = true;

        m_scale->tare();
        m_scale->resetFlowCalculation();

        if (!m_tareTimeoutTimer) {
            m_tareTimeoutTimer = new QTimer(this);
            m_tareTimeoutTimer->setSingleShot(true);
            m_tareTimeoutTimer->setInterval(6000);
            connect(m_tareTimeoutTimer, &QTimer::timeout, this, [this]() {
                if (m_waitingForTare) {
                    qWarning() << "Tare timeout: scale didn't report ~0g within 6s";
                    m_waitingForTare = false;
                    m_tareCompleted = true;
                    emit tareCompleted();
                }
            });
        }
        m_tareTimeoutTimer->start();
    }
}

void MachineState::onTimingControllerTareComplete() {
    m_tareCompleted = true;
    m_waitingForTare = false;
    emit tareCompleted();
}
