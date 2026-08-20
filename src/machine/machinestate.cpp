#include "machinestate.h"

#include "core/logtags.h"
#include "sawlogging.h"
#include "../ble/de1device.h"
#include "../ble/scaledevice.h"
#include "../ble/scales/scaletypeids.h"
#include "../ble/scales/scalelogging.h"

// Alias rather than hand-typing "AutoTare" at each call site — a repeated tag is a
// drift opportunity, and every other multi-site tag in this subsystem is aliased the
// same way (SCALEFEED_LOG in weightprocessor.cpp, SIMSCALE_LOG in simulatedscale.cpp).
#define SCALE_AUTOTARE_LOG(msg)  SCALE_LOG_STDERR_TAGGED("AutoTare", msg)
#define SCALE_AUTOTARE_INFO(msg) SCALE_INFO_STDERR_TAGGED("AutoTare", msg)
#define SCALE_AUTOTARE_WARN(msg) SCALE_WARN_STDERR_TAGGED("AutoTare", msg)
#include "../core/settings.h"
#include "../core/settings_calibration.h"  // currentScaleType()/setServingScale() — settings.h
                                           // only forward-declares the domain sub-objects
#include "../core/settings_brew.h"
#include "../controllers/shottimingcontroller.h"
#include <QDateTime>
#include <QDebug>
#include <QMetaEnum>
#include <QQmlEngine>
#include <QJSEngine>

MachineState *MachineState::s_qmlInstance = nullptr;

void MachineState::setQmlInstance(MachineState *instance)
{
    s_qmlInstance = instance;
}

MachineState *MachineState::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    if (!s_qmlInstance) {
        // Reached only if QML resolves the singleton before main.cpp published the instance.
        // Name the missing call: the symptom otherwise is every phase-driven binding in the app
        // reading as undefined, which looks like a dozen unrelated bugs rather than one missing
        // line.
        qCritical("MachineState: QML asked for the singleton before "
                  "MachineState::setQmlInstance() was called. Publish the instance before "
                  "QQmlEngine::load().");
        return nullptr;
    }
    // No per-engine state here, so no second-engine guard — same reasoning as MainController.
    // The engine would otherwise take ownership of a stack object owned by main().
    QJSEngine::setObjectOwnership(s_qmlInstance, QJSEngine::CppOwnership);
    return s_qmlInstance;
}

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
        // standbySwitchOpen's firmware gate needs this too: firmwareBuildNumber() starts at 0
        // (unknown) and is only populated later by an MMR read, well after the STATE_INFO read
        // that can already be reporting Error_NoAC at connect. Without this, a machine already
        // in Error_NoAC when the app launches never gets re-evaluated once the firmware build
        // number lands — Error_NoAC is latching, so no later STATE_INFO notification would ever
        // re-trigger it. de1app hits this exact gap and papers over it with an `after 6000`
        // timer (de1_de1.tcl); a signal-driven recheck is the event-based equivalent this
        // project's no-timers-as-guards rule calls for.
        connect(m_device, &DE1Device::firmwareVersionChanged, this, &MachineState::updatePhase);

        // Sync initial phase from device (handles case where device was already
        // connected before MachineState was constructed, e.g. simulator mode)
        updatePhase();
    }
}

MachineState::~MachineState() {
    // The provider installed in syncServingScale() captures `this`. Settings is not
    // owned by MachineState and outlives it in the tests that build both on the stack,
    // so leaving a dangling closure behind would turn any later currentScaleType()
    // into a use-after-free.
    if (m_settings && m_settings->calibration())
        m_settings->calibration()->setServingScaleTypeProvider(nullptr);
}

bool MachineState::isFlowing() const {
    // For steam, only count as flowing during SubState::Steaming or
    // SubState::Pouring (whitelist). All other substates that map to
    // Phase::Steaming — Puffing, Ending, FinalHeating, PausedSteam, etc. —
    // return false.
    if (m_phase == Phase::Steaming && m_device) {
        DE1::SubState subState = m_device->subState();
        return subState == DE1::SubState::Steaming ||
               subState == DE1::SubState::Pouring;
    }

    // Phase::Transport (air purge) is deliberately excluded: no scale
    // participates and no shot/purge timer UI reads shotTime, so it needs no
    // flow tracking. Add it here only if a purge-elapsed readout is introduced.
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
        // Auto-tare runs off sample ARRIVALS, not value changes — see the comment on
        // onScaleWeightSampleReceived().
        connect(m_scale, &ScaleDevice::weightSampleReceived,
                this, &MachineState::onScaleWeightSampleReceived);
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
        // activeScaleType() answers differently once the link comes up or drops, and
        // nothing else would tell QML. Same connection lifetime as the weight relays
        // above (disconnected wholesale on the next swap).
        connect(m_scale, &ScaleDevice::connectedChanged,
                this, &MachineState::activeScaleTypeChanged);
        // Emit immediately so QML picks up current weight
        emit scaleWeightChanged();
        emit scaleFlowRateChanged();
    }

    emit activeScaleTypeChanged();
}

void MachineState::syncServingScale() {
    // SettingsCalibration resolves the SAW pool key for every consumer, so it needs to
    // know which scale is serving. Hand it a closure over m_scale rather than the value:
    // main.cpp swaps the serving scale at a dozen device-event sites and connectedChanged
    // fires independently of those, so a pushed value would need re-pushing from both
    // and would be wrong the first time one was missed. Reading live state cannot go
    // stale, so this only has to be installed once.
    //
    // Raw identity only — empty when nothing real is connected. The canonical-vocabulary
    // test and the saved-scale fallback belong to SettingsCalibration, and duplicating
    // either here would recreate the second implementation this refactor removed.
    if (!m_settings || !m_settings->calibration())
        return;
    m_settings->calibration()->setServingScaleTypeProvider([this]() -> QString {
        return m_scale && m_scale->isConnected() ? m_scale->type() : QString();
    });
}

QString MachineState::activeScaleType() const {
    // Forwarder. The resolution itself lives on SettingsCalibration, which owns the
    // SAW pools and which every consumer already holds a pointer to — so a call site
    // that needs the key does NOT need a MachineState. This property exists purely to
    // expose it to QML (the Calibration tab) with a notify signal; it is not a second
    // implementation, and must never become one.
    //
    // The fallback to the saved type when the serving scale is virtual is only safe
    // for the PREDICTION read: flow-derived shots must not feed SAW learning at all,
    // which is enforced upstream by the isFlowScale() guard in
    // ShotTimingController::onSettlingComplete() — see the reasoning there. Were that
    // guard removed, this fallback is the exact mechanism by which a scale-less shot
    // would write into a physical scale's pool.
    return m_settings && m_settings->calibration()
               ? m_settings->calibration()->currentScaleType() : QString();
}

QString MachineState::activeScaleName() const {
    const QString saved = m_settings ? m_settings->scaleType() : QString();
    const QString active = activeScaleType();

    // Normal case: the serving scale IS the saved primary, so the user's own label
    // (which may be a custom name) is both correct and the friendlier answer.
    if (active == saved)
        return m_settings ? m_settings->scaleName() : QString();

    // Diverged — the saved label names a scale that is not serving. Report the
    // canonical name of the one that is; a stale label next to a live model is
    // exactly the confusion activeScaleType exists to remove.
    const QString name = ScaleTypeIds::scaleTypeNameForId(active);
    return name.isEmpty() ? (m_settings ? m_settings->scaleName() : QString()) : name;
}


void MachineState::setSettings(Settings* settings) {
    if (m_settings == settings) return;
    if (m_settings) disconnect(m_settings, nullptr, this, nullptr);

    m_settings = settings;

    // activeScaleType()/activeScaleName() fall back to the SAVED scale whenever the
    // serving one is virtual or disconnected — which is the common case, since the app
    // starts on FlowScale. Without these, changing the primary scale (Connections tab,
    // removing a known scale, a settings restore) would move the answer while every
    // QML binding on it kept the old value. The Calibration tab would then show one
    // pool's lag and model tier while its reset button — an imperative read, not a
    // binding — cleared a different pool. That read/write divergence is precisely what
    // activeScaleType exists to prevent, so leaving it unwired would have reintroduced
    // the bug one layer up.
    if (m_settings) {
        connect(m_settings, &Settings::scaleTypeChanged,
                this, &MachineState::activeScaleTypeChanged);
        connect(m_settings, &Settings::scaleNameChanged,
                this, &MachineState::activeScaleTypeChanged);
    }

    syncServingScale();
    emit activeScaleTypeChanged();
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
        // A BLE drop mid-steam must disarm the steam flow-stop event WITHOUT
        // emitting it: a dropped connection is not a completion (no "Steam
        // done"), and a stale armed flag would otherwise leak across the
        // disconnect and ghost-fire on a later steam whose flow was never
        // observed — violating the steamFlowStopped exactly-once/observed-flow
        // contract (see the signal doc).
        m_steamFlowStopPending = false;
        if (m_phase != Phase::Disconnected) {
            // A drop mid-shot LEAVES the espresso cycle, so the cycle-ended
            // pair must fire here too — this branch returns before the normal
            // exit detection below ever runs. Without it a BLE drop during a
            // pour leaks anything held since cycle start (the shot latch),
            // which is the same failure shotEnded had, reached by a different
            // road. Unlike steamFlowStopped above, this is not a "completion"
            // event whose absence matters: it says the cycle is no longer
            // running, which a disconnect makes true.
            const bool wasInEspresso = (m_phase == Phase::EspressoPreheating ||
                                        m_phase == Phase::Preinfusion ||
                                        m_phase == Phase::Pouring ||
                                        m_phase == Phase::Ending);
            m_phase = Phase::Disconnected;
            emit phaseChanged();
            if (wasInEspresso)
                emit espressoCycleEnded();
        }
        if (m_standbySwitchOpen) {
            m_standbySwitchOpen = false;
            emit standbySwitchOpenChanged();
        }
        return;
    }

    Phase oldPhase = m_phase;
    DE1::State state = m_device->state();
    DE1::SubState subState = m_device->subState();
    DE1::SubState previousSubState = m_previousSubState;
    m_previousSubState = subState;

    // Front standby switch cutting AC (Error_NoAC). Firmware < 1337 reports this
    // substate spuriously, matching de1app's own gate — see DE1::SubState::Error_NoAC.
    const bool standbySwitchOpen = (subState == DE1::SubState::Error_NoAC)
        && m_device->firmwareBuildNumber() >= 1337;
    if (standbySwitchOpen != m_standbySwitchOpen) {
        m_standbySwitchOpen = standbySwitchOpen;
        emit standbySwitchOpenChanged();
    }

    // Log steam substate transitions to reconstruct the full
    // Steaming->Puffing->Ending->Idle sequence in bug reports.
    // Gate on oldPhase==Steaming so the first entry into Steam doesn't log
    // a spurious transition from m_previousSubState's init value.
    if (state == DE1::State::Steam && oldPhase == Phase::Steaming
        && subState != previousSubState) {
        qDebug().noquote() << QString("MachineState: steam substate: %1 -> %2")
            .arg(DE1::subStateToString(previousSubState),
                 DE1::subStateToString(subState));
    }

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
            //
            // Load-bearing invariant: SteamPage.qml's session-end handler
            // (onIsSteamingChanged's isSteaming=false branch) resets the per-preset
            // steamTimeout / steamFlow and re-sends ShotSettings (today via
            // MainController::releaseSteamEventPermission — sendSteamTemperature,
            // which this comment used to name, no longer exists). That
            // handler fires when `phase !== Steaming`, so reclassifying Puffing or
            // Ending out of Steaming here would cause the reset to fire
            // mid-purge — writing to ShotSettings while the DE1 is still handling
            // the end-of-steam sequence. If you need to split Puffing into its own
            // phase, update SteamPage.qml in the same change.
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

        case DE1::State::AirPurge:
            m_phase = Phase::Transport;
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

            // Arm the steam flow-stop event: steamFlowStopped fires exactly
            // once per steam, and only for a steam whose flow was actually
            // observed here. isFlowing() treats Steaming AND Pouring as
            // flowing, so a steam first seen at Pouring still arms; only a
            // steam first seen on a non-flowing substate (Puffing/Ending —
            // both flowing notifications missed) never arms, keeping its
            // phase exit silent.
            if (m_phase == Phase::Steaming)
                m_steamFlowStopPending = true;

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
                m_hotWaterSawTriggerWeight = -1.0;
                resetAutoTareGates();  // new flow cycle
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

                // Reset and start scale timer when non-espresso flow starts.
                // For espresso, reset + start are handled separately below
                // (split by preheating phase for scales with independent reset).
                if (m_scale && !isInEspresso) {
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

                    // Start scale timer. For scales with independent reset, reset was
                    // already sent at cycle start. For others, send reset+start together.
                    if (m_scale) {
                        if (!m_scale->hasIndependentTimerReset()) {
                            m_scale->resetTimer();
                        }
                        m_scale->startTimer();
                        qDebug() << "=== SCALE TIMER: Started (espresso extraction began) ===";
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
                // Steam left the Steaming phase. Two cases reach this: a manual
                // stop straight out of flowing steam (the pending flag emits
                // here), or the Steaming->Idle exit AFTER an auto-stop —
                // wasFlowing derives from oldPhase alone, so this branch also
                // runs seconds after flow actually stopped. The pending flag
                // (consumed by whichever site fires first) keeps the signal
                // exactly-once per steam across both calls.
                if (oldPhase == Phase::Steaming && m_steamFlowStopPending) {
                    m_steamFlowStopPending = false;
                    emit steamFlowStopped();
                }
            }

            // Hot water SAW learning
            if (oldPhase == Phase::HotWater) {
                // Learn from this pour: measure settled weight after a short delay
                // to let final drops land, then compute overshoot vs trigger weight.
                if (m_hotWaterSawTriggerWeight >= 0.0 && m_scale && m_settings) {
                    double triggerWeight = m_hotWaterSawTriggerWeight;
                    QTimer::singleShot(1500, this, [this, triggerWeight]() {
                        if (!m_scale || !m_settings) return;
                        // Skip if a new operation started (scale weight no longer reflects the pour)
                        if (isFlowing()) return;
                        double settledWeight = m_scale->weight();
                        double overshoot = settledWeight - triggerWeight;

                        // Clear the frozen display weight now that the post-pour
                        // settling window is over. The freeze is held this long so the
                        // hot-water completion overlay has a stable trigger value to
                        // show; once the overlay's settling window closes, scaleWeight()
                        // should return the live reading. Without this, the freeze
                        // persists into the next operation cycle (e.g. EspressoPreheating)
                        // and poisons UI surfaces that read scaleWeight pre-flow, like
                        // the cup-fill view.
                        m_hotWaterFrozenWeight = -1.0;

                        // Sanity: ignore if overshoot is wildly negative (cup removed)
                        // or extremely large (scale glitch)
                        if (overshoot < -2.0 || overshoot > 20.0) {
                            SAW_LOG_STDERR("HotWaterLearn",
                                QStringLiteral("Ignoring outlier: overshoot=%1 g settled=%2 g trigger=%3 g")
                                    .arg(overshoot, 0, 'f', 2).arg(settledWeight, 0, 'f', 2)
                                    .arg(triggerWeight, 0, 'f', 2));
                            return;
                        }

                        // Exponential moving average, heavier weight on early samples
                        int n = m_settings->brew()->hotWaterSawSampleCount();
                        double oldOffset = m_settings->brew()->hotWaterSawOffset();
                        double alpha = (n < 3) ? 0.5 : 0.3;  // Learn faster initially
                        double newOffset = (1.0 - alpha) * oldOffset + alpha * qMax(0.0, overshoot);

                        // Clamp to reasonable range
                        newOffset = qBound(0.0, newOffset, 10.0);

                        m_settings->brew()->setHotWaterSawOffset(newOffset);
                        m_settings->brew()->setHotWaterSawSampleCount(n + 1);
                        SAW_LOG_STDERR("HotWaterLearn",
                            QStringLiteral("overshoot=%1 g settled=%2 g trigger=%3 g "
                                           "offset %4 -> %5 g samples=%6")
                                .arg(overshoot, 0, 'f', 2).arg(settledWeight, 0, 'f', 2)
                                .arg(triggerWeight, 0, 'f', 2).arg(oldOffset, 0, 'f', 2)
                                .arg(newOffset, 0, 'f', 2).arg(n + 1));
                    });
                }
                m_hotWaterSawTriggerWeight = -1.0;
                // m_hotWaterFrozenWeight is cleared inside the 1500ms SAW-learn
                // callback above (or by the flow-start path at the top of this
                // function on the next pour, whichever happens first).
            }
        }

        // Stop scale timer when exiting espresso cycle (e.g., Ending -> Idle)
        if (wasInEspresso && !isInEspresso) {
            if (m_scale) {
                m_scale->stopTimer();
                qDebug() << "=== SCALE TIMER: Stopped (espresso cycle ended) ===";
            }
            // The pair of espressoCycleStarted — emitted on EVERY exit,
            // including a cycle that never flowed. shotEnded cannot serve this
            // role: it is gated on flow having started, so an abort during
            // preheat would never release anything held since cycle start.
            emit espressoCycleEnded();
        }

        // Reset timer state when entering espresso cycle (before signals)
        // This ensures timer shows 0 during preheating and properly starts at preinfusion
        // Without this, m_shotStartTime would contain the PREVIOUS shot's timestamp,
        // causing the timer to show huge elapsed values when preinfusion starts
        if (isInEspresso && !wasInEspresso) {
            m_shotTime = 0.0;
            m_shotStartTime = 0;  // Mark as invalid so preinfusion properly starts it
            resetAutoTareGates();  // new espresso cycle
            emit shotTimeChanged();  // Update UI to show 0 during preheating

            // Reset scale timer at cycle start (like de1app's on_major_state_change).
            // Normally startTimer() is sent later when extraction begins, separating
            // the two commands by the preheating phase to avoid BLE command contention
            // (WriteWithoutResponse can silently drop back-to-back packets).
            // Only split for scales with a true independent reset — some scales send the
            // same bytes for resetTimer() as startTimer() (DiFluid), so splitting would
            // cause unwanted side effects during preheating.
            // If already flowing (machine skipped preheating), send both now — there
            // won't be a separate extraction-start transition to send startTimer().
            if (m_scale && m_scale->hasIndependentTimerReset()) {
                m_scale->resetTimer();
                if (isFlowing()) {
                    m_scale->startTimer();
                    qDebug() << "=== SCALE TIMER: Reset + Started (espresso cycle started, already flowing) ===";
                } else {
                    qDebug() << "=== SCALE TIMER: Reset (espresso cycle started, waiting for extraction) ===";
                }
            } else if (m_scale && isFlowing()) {
                // Safety net: machine skipped preheating (missed BLE substate notification).
                // Non-independent-reset scales won't reach the startingExtraction path
                // (requires wasInEspresso=true), so send reset+start together here.
                m_scale->resetTimer();
                m_scale->startTimer();
                qDebug() << "=== SCALE TIMER: Reset + Started (espresso cycle started, already flowing, non-independent reset) ===";
            }

            // CRITICAL: Emit espressoCycleStarted IMMEDIATELY (not deferred) so MainController
            // can reset its m_shotStartTime before any shot samples arrive via BLE.
            // If deferred, shot samples could arrive first with wrong timestamps.
            emit espressoCycleStarted();
        }

        // Defer other signal emissions to allow pending BLE notifications to process first.
        // This prevents QML binding updates from blocking the event loop during the BLE callback chain.
        // Note: espressoCycleStarted is emitted immediately above to avoid race conditions.
        QMetaObject::invokeMethod(this, [this, wasFlowing, oldPhase]() {
            emit phaseChanged();

            if (isFlowing() && !wasFlowing) {
                if (m_phase == Phase::Steaming)
                    qDebug().noquote() << "MachineState: steam flow started (phase entered Steaming)";
                emit shotStarted();
            } else if (!isFlowing() && wasFlowing) {
                if (oldPhase == Phase::Steaming)
                    qDebug().noquote() << "MachineState: steam flow stopped (phase left Steaming)";
                emit shotEnded();
            }
        }, Qt::QueuedConnection);
    }

    // Also check for timer stop on substate changes (even if phase didn't change)
    // This handles steam stopping (Puffing/Ending substates) where phase stays Steaming
    if (!isFlowing() && m_shotTimer->isActive()) {
        qDebug() << "=== TIMER STOP: isFlowing() became false (substate change) ===";
        if (m_device && m_device->state() == DE1::State::Steam) {
            qDebug().noquote() << QString("MachineState: steam flow stopped via substate change (substate=%1)")
                .arg(DE1::subStateToString(m_device->subState()));
        }
        stopShotTimer();
        if (m_scale) {
            m_scale->stopTimer();
            qDebug() << "=== SCALE TIMER: Stopped (substate change) ===";
        }
        // Steam auto-stop path: substate left Steaming (Puffing/Ending) while
        // the phase stays Steaming. This emission marks the actual end of flow;
        // consuming the pending flag here keeps the later Steaming->Idle
        // phase-change path (which would otherwise re-fire — wasFlowing derives
        // from oldPhase alone) silent, so the signal is exactly-once per steam.
        if (m_phase == Phase::Steaming && m_steamFlowStopPending) {
            m_steamFlowStopPending = false;
            emit steamFlowStopped();
        }
    }
}

// Clears every auto-tare gate. One definition, called from each site that starts a
// fresh flow cycle and from the pre-flow window exit — the four counters used to be
// re-listed at each of those, which is how m_retareAttempts came to be reset at some
// of them and not others.
void MachineState::resetAutoTareGates() {
    m_autoTareWindow.reset();
    m_zeroDriftStreak.reset();
    m_unsettledCupStreak.reset();
    m_retareAttempts = 0;
    m_awaitingTareEffect = false;
    m_liftSuspected = false;
    m_retareIssuedThisWindow = false;
    m_settleWaitLogged = false;
}

// Auto-tare and its safety gates. Driven by weightSampleReceived — every sample
// ARRIVAL — not by weightChanged, which ScaleDevice::setWeight dedupes on value
// (the `if (m_weight != weight)` guard; scaledevice.cpp:168 at the time of writing,
// but go by the guard, not the line). The gates below measure consecutive samples,
// and a scale holding a genuinely constant reading emits no weightChanged at all:
// on the deduped signal a perfectly still cup would never fill a settle window and
// could never be tared. Constant-weight windows are real on this hardware — the
// scale-feed liveness detector exists precisely because they are (#1176).
void MachineState::onScaleWeightSampleReceived(double weight) {
    // Auto-tare during "flow before" phase (like de1app: heating substates before water flows)
    // Handles forgotten-cup scenario for both Espresso and HotWater
    constexpr double AUTO_TARE_THRESHOLD = 2.0;  // grams (de1app uses 0.04g, 2g avoids noise)

    // A tare zeroes the scale on whatever the load cell reads at that instant, so
    // taring while the cell is still moving bakes the transient in as the new zero —
    // and every reading for the rest of the shot is off by it, silently, with
    // `tare= true` in the log the whole way.
    //
    // The worked example is the 2026-08-20 9:37 AM shot (id 1111 in the local shot
    // database; not reproducible from this repo). Its log shows a -6.8 g reading and
    // an +8.3 g reading 13 ms apart, with WeightProcessor's "Scale oscillation
    // detected" warning between them, and the auto-tare firing on the second. Note
    // 13 ms is far tighter than the 2-10 Hz these scales nominally report, so those
    // two lines are best read as a burst or as two log sites observing one
    // disturbance — the exact sample cadence was never established. What IS on the
    // record and is what matters here: the scale acknowledged the tare with 0.00 g,
    // then read -20.6 g at rest before extraction started. SAW spent the shot
    // chasing a 36 g target that was really 56.6 g and stopped 23 g late; the grind
    // detector then read the 59.4 g yield as a loose puck and told the user to grind
    // finer. Nothing in the pipeline disagreed.
    //
    // Two gates, because they fail differently:
    //   1. SETTLE — do not tare a moving reading. Prevention.
    //   2. ZERO VERIFICATION — after taring, confirm the zero HELD. Detection, and
    //      the one that actually guarantees correctness: a hardware tare can settle
    //      badly even when issued at a perfectly still moment, and only watching the
    //      result can catch that.
    //
    // Settle is measured as the SPREAD across a window, not as a per-sample delta.
    // That distinction is load-bearing: the -20.6 g drift above moved roughly 0.47 g
    // per sample, so a per-step band of 1 g would have called it still and tared
    // anyway — the gate would not have caught the bug it was written for.
    constexpr double AUTO_TARE_SETTLE_BAND_G = 1.0;   // max spread across the window
    constexpr double ZERO_DRIFT_G = -2.0;             // below this, the zero did not hold
    constexpr int ZERO_DRIFT_SAMPLES = 3;             // consecutive drifted samples before re-taring
    constexpr int MAX_RETARE_ATTEMPTS = 3;            // give up rather than loop on a sick scale
    // A cup being lifted off is a STEP — a whole cup mass inside one or two samples.
    // A bad zero is a gradual drift. Discriminating by SHAPE rather than by magnitude
    // is what lets this work for a cup of any weight; an earlier revision used a
    // -35 g floor derived from the author's lightest cup, which silently re-tared an
    // empty platform for anyone using a 20-30 g glass. Re-taring a lifted cup is the
    // one way this gate can make things worse than doing nothing: the returning cup
    // then reads as yield and stops the next shot almost immediately.
    constexpr double LIFT_STEP_G = -8.0;
    // If the reading never settles we still have to tare, or a cup on a permanently
    // noisy scale is never tared at all — the forgotten-cup failure that the
    // auto-tare exists to prevent (#299/#303). Sample-counted, not timed.
    constexpr int UNSETTLED_CUP_FALLBACK_SAMPLES = 50;

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

    if (!isFlowBefore) {
        // Outside the pre-flow window no gate applies. Clear everything so a stale
        // streak cannot authorise a tare on the next window's first sample, and so
        // weight legitimately arriving in the cup during a pour is never mistaken for
        // a drifted zero. m_retareAttempts is cleared here too — it is documented as
        // per-window, and leaving it set was making the gate die permanently for a
        // user who repeatedly started and cancelled hot water.
        resetAutoTareGates();
        return;
    }

    // A step this large is a cup arriving or leaving, not a zero drifting. Latch it
    // until the reading comes back up, so the drift gate cannot fire while a cup is
    // off the scale.
    if (!m_autoTareWindow.isEmpty() && (weight - m_autoTareLastSample) <= LIFT_STEP_G)
        m_liftSuspected = true;
    if (weight > ZERO_DRIFT_G)
        m_liftSuspected = false;
    m_autoTareLastSample = weight;

    m_autoTareWindow.add(weight);
    const bool scaleSettled = m_autoTareWindow.withinBand(AUTO_TARE_SETTLE_BAND_G);

    // The tare interlock. A tare command is only "spent" once the scale is seen to
    // zero; until then no further tare goes out. This replaces a 1 s wall-clock
    // holdoff, which CLAUDE.md bans as a guard and which could not tell "the tare I
    // sent has not been processed yet" from "the tare was processed and came out
    // wrong" — on a slow-tare scale that difference is three queued BLE tares, one
    // of which can land mid-extraction and zero away the shot.
    if (m_awaitingTareEffect) {
        if (qAbs(weight) <= AUTO_TARE_THRESHOLD) {
            m_awaitingTareEffect = false;
            m_zeroDriftStreak.reset();
        } else {
            return;  // still waiting for the previous tare to show up
        }
    }

    // Zero verification. Nothing lands in the cup before flow starts, so a reading
    // that has drifted below zero here can only mean the zero itself is wrong.
    //
    // Espresso only. Hot water tares fire-and-forget and SAW works from
    // (weight - m_hotWaterTareBaseline), so a non-zero raw reading there is expected
    // and already compensated — reading it as a bad zero would re-tare needlessly and
    // then claim, falsely, that the shot's weights are off.
    const bool zeroDrifted = m_tareCompleted
                             && m_phase == Phase::EspressoPreheating
                             && !m_liftSuspected
                             && weight < ZERO_DRIFT_G;
    m_zeroDriftStreak.update(zeroDrifted);

    // Report the recovery, not just the fault. A re-tare that WORKS used to log
    // nothing at all, so a submitted log showed the warnings and no resolution and
    // read as "the weights were wrong" when they had in fact been fixed. INFO because
    // this is the user-visible tier (LOGGING.md).
    if (m_retareIssuedThisWindow && !zeroDrifted && qAbs(weight) <= AUTO_TARE_THRESHOLD) {
        m_retareIssuedThisWindow = false;
        SCALE_AUTOTARE_INFO(
            QStringLiteral("Zero held after re-tare %1 — scale now reads %2 g, weights are good")
                .arg(m_retareAttempts).arg(weight, 0, 'f', 1));
    }

    const bool cupPlaced = m_tareCompleted && weight > AUTO_TARE_THRESHOLD;
    m_unsettledCupStreak.update(cupPlaced && !scaleSettled);

    if (cupPlaced) {
        // Tare once the reading is still — or, if it never becomes still, once we
        // have waited long enough that never taring is the worse outcome.
        const bool fallback = m_unsettledCupStreak.reached(UNSETTLED_CUP_FALLBACK_SAMPLES);
        if (!scaleSettled && !fallback) {
            if (!m_settleWaitLogged) {
                m_settleWaitLogged = true;
                SCALE_AUTOTARE_LOG(
                    QStringLiteral("Cup placed during %1 (weight %2 g) — holding tare until the "
                                   "reading settles (spread under %3 g across %4 samples)")
                        .arg(phaseString()).arg(weight, 0, 'f', 1)
                        .arg(AUTO_TARE_SETTLE_BAND_G).arg(m_autoTareWindow.size()));
            }
            return;
        }
        if (fallback) {
            SCALE_AUTOTARE_WARN(
                QStringLiteral("Cup reading never settled over %1 samples (now %2 g) — taring "
                               "anyway rather than leaving the cup untared; expect the zero to "
                               "be a little off")
                    .arg(UNSETTLED_CUP_FALLBACK_SAMPLES).arg(weight, 0, 'f', 1));
        }
        SCALE_AUTOTARE_INFO(QStringLiteral("Cup placed during %1 (weight %2 g) — taring")
                                .arg(phaseString()).arg(weight, 0, 'f', 1));
        issueAutoTare();
        return;
    }

    if (m_zeroDriftStreak.reached(ZERO_DRIFT_SAMPLES) && scaleSettled) {
        if (m_retareAttempts >= MAX_RETARE_ATTEMPTS) {
            // Warn once, on the attempt that crosses the cap, then stay quiet.
            // The advisory names the consequence because this log is what a remote
            // reader (or the user's own AI) has to diagnose from: an uncorrected
            // offset here does not announce itself later — it just makes SAW stop at
            // the wrong weight and every downstream detector reason from a yield that
            // never happened.
            if (m_retareAttempts == MAX_RETARE_ATTEMPTS) {
                ++m_retareAttempts;  // latch so this fires exactly once
                SCALE_AUTOTARE_WARN(
                    QStringLiteral("Zero still off by %1 g after %2 re-tares — giving up. "
                                   "The scale is reporting a bad zero; this shot's weights "
                                   "and any stop-at-weight will be off by roughly that much.")
                        .arg(weight, 0, 'f', 1).arg(MAX_RETARE_ATTEMPTS));
            }
            return;
        }
        ++m_retareAttempts;
        m_retareIssuedThisWindow = true;
        SCALE_AUTOTARE_WARN(
            QStringLiteral("Zero did not hold — scale settled at %1 g with nothing in the cup, "
                           "re-taring (attempt %2 of %3)")
                .arg(weight, 0, 'f', 1).arg(m_retareAttempts).arg(MAX_RETARE_ATTEMPTS));
        issueAutoTare();
    }
}

// The one place a tare goes out from the auto-tare gates, so the interlock and the
// window reset can never be set at one call site and forgotten at another.
void MachineState::issueAutoTare() {
    m_awaitingTareEffect = true;
    m_autoTareWindow.reset();   // the tare itself moves the reading
    m_zeroDriftStreak.reset();
    m_unsettledCupStreak.reset();
    m_settleWaitLogged = false;
    tareScale(m_autoTareLastSample);
    emit flowBeforeAutoTare();
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
    // Guard: only clear if we haven't seen significant water flow yet (< 3g effective)
    // OR if we're still within the tare burst window (first 2s after tare request).
    // Within the burst window, the scale zeroing is clearly a tare response, not a
    // coincidence — so clear baseline unconditionally and reset maxEffectiveWeight
    // to prevent the stale baseline from causing a false SAW trigger.
    // After the burst window, the < 3g guard protects against slow-tare scales
    // (e.g. Eureka Precisa) that process tare after water has been dispensed.
    if (m_phase == Phase::HotWater && m_hotWaterTareBaseline != 0.0 && qAbs(weight) < 1.0) {
        bool inTareWindow = m_hotWaterTareTimeMs > 0
            && (QDateTime::currentMSecsSinceEpoch() - m_hotWaterTareTimeMs) < 2000;
        if (inTareWindow || m_hotWaterMaxEffectiveWeight < 3.0) {
            qDebug() << "=== TARE: Scale zeroed, clearing hot water baseline ===";
            m_hotWaterTareBaseline = 0.0;
            m_hotWaterMaxEffectiveWeight = 0.0;  // Reset so SAW uses fresh absolute weight
        }
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
            SAW_LOG_STDERR("HotWater", QStringLiteral("Tare 2s summary: baseline=%1")
                .arg(m_hotWaterTareBaseline == 0.0
                         ? QStringLiteral("cleared (tare OK)")
                         : QString::number(m_hotWaterTareBaseline, 'f', 1)
                               + QStringLiteral("g (tare FAILED, using baseline)")));
            m_hotWaterTareTimeMs = 0;  // Stop burst logging
        }
    }


    if (!m_device) return;
    DE1::State state = m_device->state();

    // Throttled weight logging during SAW-relevant phases (~every 2s)
    if (state == DE1::State::Espresso || state == DE1::State::HotWater) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastWeightLogMs >= 2000) {
            if (state == DE1::State::HotWater && m_hotWaterTareBaseline != 0.0) {
                qDebug() << "[" DECENZA_LOG_MARKER_SCALE "] weight=" << QString::number(weight, 'f', 1)
                         << "effective=" << QString::number(weight - m_hotWaterTareBaseline, 'f', 1)
                         << "baseline=" << QString::number(m_hotWaterTareBaseline, 'f', 1)
                         << "phase=" << phaseString()
                         << "tare=" << m_tareCompleted;
            } else {
                qDebug() << "[" DECENZA_LOG_MARKER_SCALE "] weight=" << QString::number(weight, 'f', 1)
                         << "phase=" << phaseString()
                         << "tare=" << m_tareCompleted;
            }
            m_lastWeightLogMs = now;
        }
    }
    // Hot water: MachineState handles stop-at-weight (ShotTimingController not active)
    // Espresso: WeightProcessor handles SAW on worker thread (adaptive lag via learned drip data)
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
            SAW_WARN_STDERR("HotWater",
                QStringLiteral("Tare not completed — skipping stop-at-weight check, weight=%1 g")
                    .arg(weight, 0, 'f', 2));
            s_lastTareWarnMs = nowMs;
        }
        return;
    }

    // Volume mode: machine handles auto-stop via flowmeter, don't interfere
    if (m_settings && m_settings->brew()->waterVolumeMode() == "volume") return;

    double target = m_settings ? m_settings->brew()->waterVolume() : 0;  // ml ≈ g for water
    if (target <= 0) return;

    // Use weight relative to the baseline recorded at tare time.
    // If the BLE tare command succeeded, baseline was cleared to 0 (absolute weight).
    // If the BLE tare was lost/ignored, baseline holds the pre-tare weight so we
    // measure only the water added since the tare was requested.
    double effectiveWeight = weight - m_hotWaterTareBaseline;

    // Learned offset: starts at 2g default, adapts from measured overshoot after each pour.
    // After stopping, the app measures how much weight landed after the stop command and
    // adjusts the offset so subsequent pours hit the target more accurately.
    double sawOffset = m_settings ? m_settings->brew()->hotWaterSawOffset() : 2.0;
    double stopThreshold = target - sawOffset;

    if (effectiveWeight >= stopThreshold) {
        m_stopAtWeightTriggered = true;
        m_hotWaterFrozenWeight = effectiveWeight;      // Freeze UI display at trigger weight
        m_hotWaterSawTriggerWeight = weight;            // Raw scale weight for learning overshoot
        // INFO: the stop itself is the user-facing answer to "why did my hot
        // water stop there". Once per dispense.
        SAW_INFO_STDERR("HotWater",
            QStringLiteral("Stop triggered: effectiveWeight=%1 g scaleWeight=%2 g baseline=%3 g "
                           "threshold=%4 g target=%5 g offset=%6 g")
                .arg(effectiveWeight, 0, 'f', 2).arg(weight, 0, 'f', 2)
                .arg(m_hotWaterTareBaseline, 0, 'f', 2).arg(stopThreshold, 0, 'f', 2)
                .arg(target, 0, 'f', 2).arg(sawOffset, 0, 'f', 2));
        emit targetWeightReached();

        if (m_device) {
            m_device->stopOperationUrgent();  // Front of the BLE queue, ahead of everything waiting
        }
    }
}

void MachineState::checkStopAtVolume() {
    if (m_stopAtVolumeTriggered) return;
    if (!m_tareCompleted) return;  // Don't check until tare has happened

    // Skip volume-based stop when a physical scale is configured and the user has
    // opted in. Uses "configured" (scaleAddress non-empty) not "connected" so a
    // momentary BLE disconnect mid-shot doesn't re-enable SAV unexpectedly.
    if (m_settings && m_settings->brew()->ignoreVolumeWithScale()
        && !m_settings->scaleAddress().isEmpty()) return;

    // Skip SAV for basic profiles when a scale is configured. The volume value in
    // settings_2a/2b profiles (e.g. 36ml in Default) is invisible to users and fires far
    // too early (~15g in cup when 36ml is pumped). Matches de1app's skip_sav_check logic.
    // Uses "configured" (scaleAddress non-empty) not "connected" for saved BLE scales so a
    // momentary BLE disconnect mid-shot doesn't re-enable SAV. SimulatedScale and USB scales
    // have no saved address so the third condition covers them via isConnected(); the
    // SimulatedScale debug toggle calls simulateDisconnection() which clears isConnected().
    bool isBasicProfile = (m_profileType == QLatin1String("settings_2a")
                        || m_profileType == QLatin1String("settings_2b"));
    bool scaleConfigured = (m_settings && !m_settings->scaleAddress().isEmpty())
                        || (m_settings && m_settings->useFlowScale())
                        || (m_scale && m_scale->isConnected() && !m_scale->isFlowScale());
    if (isBasicProfile && scaleConfigured) return;

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
    if (!m_tareCompleted) return;  // Don't check until tare has happened

    // Hot water SAV logic (based on de1app but improved):
    // - Scale configured (physical BLE address set, or flow scale enabled): safety net above
    //   the user's target so SAW stops first. Uses max(waterVolume + 50, 250) to handle large
    //   volumes (de1app hardcodes 250). Uses "configured" not "connected" — same reasoning
    //   as the espresso SAV skip above.
    // - No scale: target = waterVolume setting (app-side volume stop is primary)
    double target;
    bool scaleConfigured = (m_settings && !m_settings->scaleAddress().isEmpty())
                        || (m_settings && m_settings->useFlowScale())
                        || (m_scale && m_scale->isConnected() && !m_scale->isFlowScale());
    if (scaleConfigured) {
        target = qMax(static_cast<double>(m_settings->brew()->waterVolume()) + 50.0, 250.0);
    } else {
        target = m_settings->brew()->waterVolume();
    }
    if (target <= 0) return;

    if (m_pourVolume >= target) {
        m_stopAtVolumeTriggered = true;
        emit targetVolumeReached();

        qDebug() << "MachineState: Hot water volume stop -" << m_pourVolume
                 << "ml /" << target << "ml"
                 << (scaleConfigured ? "(safety net)" : "(no scale)");

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
    // pouring → pour_volume. Other substates (heating, stabilising) never reach
    // here because isFlowing() excludes them above.
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
        // Steam timeout is handled by DE1 firmware via ShotSettings.steamTimeout.
        // The firmware stops steam flow but keeps the machine in Steaming state so
        // pressing GHC Stop triggers a final cleaning purge puff. App-side stop
        // exits steaming entirely, killing the GHC light and purge opportunity.
        // Only use app-side stop for the simulator (which has no firmware timer).
        if (m_device && m_device->simulationMode()) {
            target = m_settings->brew()->steamTimeout();
        }
    } else if (m_phase == Phase::Flushing) {
        target = m_settings->brew()->flushSeconds();
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

// `triggerWeight` is the sample that prompted the tare. The hot-water branch needs
// it because auto-tare now runs on weightSampleReceived, which ScaleDevice::setWeight
// emits BEFORE assigning m_weight — so m_scale->weight() would hand back the PREVIOUS
// sample. A negative default keeps every other caller (QML, manual tare) on the
// property read they already used.
void MachineState::tareScale(double triggerWeight) {
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
        m_hotWaterTareBaseline = (triggerWeight > -1e9) ? triggerWeight : m_scale->weight();
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
