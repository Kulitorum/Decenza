#include "shottimingcontroller.h"
#include "../ble/de1device.h"
#include "../ble/scaledevice.h"
#include "../core/settings.h"
#include "../machine/machinestate.h"
#include "../machine/sawlogging.h"
#include <QDebug>
#include <QScopeGuard>

// Aliases, not copies — see sawlogging.h. No logMessage signal on this
// controller, so the STDERR forms apply. "Timing" as the source: this file owns
// the stop decision and the settling pass that follows it, as distinct from the
// learning store ([SAW][Learning]) and the weight worker ([SAW][Worker]).
#define SAWT_LOG(msg)  SAW_LOG_STDERR("Timing", msg)
#define SAWT_INFO(msg) SAW_INFO_STDERR("Timing", msg)
#define SAWT_WARN(msg) SAW_WARN_STDERR("Timing", msg)

ShotTimingController::ShotTimingController(DE1Device* device, QObject* parent)
    : QObject(parent)
    , m_device(device)
{
    // Display timer - updates UI at 20Hz for smooth timer display
    m_displayTimer.setInterval(50);
    connect(&m_displayTimer, &QTimer::timeout, this, &ShotTimingController::onDisplayTimerTick);

    // SAW learning settling timer - waits for weight to stabilize after shot ends
    // Interval set by startSettlingTimer() when settling begins (currently 10s max)
    m_settlingTimer.setSingleShot(true);
    connect(&m_settlingTimer, &QTimer::timeout, this, &ShotTimingController::onSettlingComplete);
}

double ShotTimingController::shotTime() const
{
    // Show 0 during preheating, start counting when first extraction frame arrives
    if (!m_extractionStarted) {
        return 0.0;
    }
    // Calculate time from wall clock during active extraction only
    // During settling, return the frozen extraction end time so the timer
    // display stops at the correct duration (graph timestamps are computed
    // separately via wall clock in onShotSample/onWeightSample)
    if (m_shotActive && m_displayTimeBase > 0) {
        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_displayTimeBase;
        return elapsed / 1000.0;
    }
    // After shot ends (including during settling), return frozen extraction end time
    if (m_extractionEndTime > 0) {
        return m_extractionEndTime;
    }
    return m_currentTime;
}

void ShotTimingController::setScale(ScaleDevice* scale)
{
    // Signal connections to scale are managed externally in main.cpp
    m_scale = scale;
}

void ShotTimingController::setSettings(Settings* settings)
{
    m_settings = settings;
}

void ShotTimingController::setMachineState(MachineState* machineState)
{
    m_machineState = machineState;
}

void ShotTimingController::setTargetWeight(double weight)
{
    m_targetWeight = weight;
}

void ShotTimingController::setCurrentProfile(const Profile* profile)
{
    m_currentProfile = profile;
}

void ShotTimingController::startShot()
{
    // Cancel settling if in progress (user started new shot before settling completed)
    // Emit shotProcessingReady so the previous shot is saved before we reset state.
    // IMPORTANT: m_extractionEndTime must not be reset until after shotProcessingReady
    // is fully handled, because onShotEnded() reads extractionDuration() synchronously.
    if (m_sawSettling) {
        SAWT_WARN(QStringLiteral("Cancelling settling timer - new shot started, saving previous shot"));
        m_sawTriggeredThisShot = false;
        m_sawSettling = false;
        m_settlingTimer.stop();
        m_displayTimer.stop();
        emit sawSettlingChanged();
        emit shotProcessingReady();  // onShotEnded() reads extractionDuration() here
    }

    // Reset all timing state (safe now — previous shot has been saved above)
    m_currentTime = 0;
    m_extractionEndTime = 0;
    m_shotActive = true;

    // Reset weight state
    m_weight = 0;
    m_flowRate = 0;
    m_smoothedFlowRate = 0.0;
    m_flowRateInitialized = false;
    m_flowRateShort = 0;
    m_stopAtWeightTriggered = false;
    m_frameWeightSkipSent = -1;
    m_weightExitFrames.clear();
    m_currentFrameNumber = -1;
    m_extractionStarted = false;

    // Reset SAW learning state
    m_sawSettling = false;
    m_sawTriggeredThisShot = false;
    m_flowRateAtStop = 0.0;
    m_weightAtStop = 0.0;
    m_targetWeightAtStop = 0.0;
    m_lastStableWeight = 0.0;
    m_lastWeightChangeTime = 0;
    m_settlingPeakWeight = 0.0;
    m_lastCleanSettlingAvg = 0.0;

    // Reset tare state (will be set to Complete when tare() is called)
    m_tareState = TareState::Idle;

    // Start display timer for smooth UI updates
    m_displayTimeBase = QDateTime::currentMSecsSinceEpoch();
    m_displayTimer.start();

    emit shotTimeChanged();
    emit tareCompleteChanged();
    emit weightChanged();
}

void ShotTimingController::endShot()
{
    m_shotActive = false;
    // Freeze extraction end time for timer display and saved duration.
    // m_currentTime holds the last Pouring-phase sample time (Ending-phase samples
    // are rejected before reaching onShotSample, so this is accurate).
    m_extractionEndTime = m_currentTime;

    // Start settling timer if SAW triggered this shot (for learning)
    // Keep display timer running during settling so graph continues to update
    if (m_sawTriggeredThisShot) {
        startSettlingTimer();
        // Don't stop display timer - keep time incrementing for graph
        // shotProcessingReady will be emitted after settling completes
        SAWT_LOG(QStringLiteral("Triggered - waiting for weight to settle before processing shot"));
    } else {
        m_displayTimer.stop();
        // No SAW - shot can be processed immediately
        SAWT_LOG(QStringLiteral("Not triggered - emitting shotProcessingReady immediately"));
        emit shotProcessingReady();
    }

    emit shotTimeChanged();
}

void ShotTimingController::onShotSample(const ShotSample& sample, double pressureGoal,
                                         double flowGoal, double tempGoal,
                                         int frameNumber, bool isFlowMode)
{
    // Keep capturing samples during settling (shows pressure/flow declining after stop)
    bool isSettling = m_sawSettling;
    if (!m_shotActive && !isSettling) {
        return;
    }

    // Track frame number change and detect extraction start (skip during settling)
    if (!isSettling && frameNumber != m_currentFrameNumber) {
        if (m_currentProfile && frameNumber >= 0 && frameNumber < m_currentProfile->steps().size()) {
            const auto& frame = m_currentProfile->steps()[frameNumber];
            qDebug() << "FRAME CHANGE:" << m_currentFrameNumber << "->" << frameNumber
                     << "name:" << frame.name << "exitWeight:" << frame.exitWeight;
        }
        m_currentFrameNumber = frameNumber;

        // Extraction starts on the first frame we see. The DE1 may skip preheating
        // frames (0-1) if the group is already hot, jumping straight to frame 2+.
        if (!m_extractionStarted) {
            m_extractionStarted = true;
            m_displayTimeBase = QDateTime::currentMSecsSinceEpoch();
            qDebug() << "EXTRACTION STARTED at frame" << frameNumber;
        }
    }

    // Calculate time from wall clock (simple and reliable)
    double time = (QDateTime::currentMSecsSinceEpoch() - m_displayTimeBase) / 1000.0;
    m_currentTime = time;

    // shotTimeChanged deferred to ShotDataModel's 33ms flush timer (avoid blocking BLE handler)

    // Emit unified sample with consistent timestamp
    emit sampleReady(time, sample.groupPressure, sample.groupFlow, sample.headTemp,
                     pressureGoal, flowGoal, tempGoal, frameNumber, isFlowMode);

    // Emit weight sample with same timestamp as other curves (perfect sync)
    // Weight value is cached from onWeightSample, emitted here for graph alignment
    // The LSLR smoother produces clean flow rates even during the Ending phase,
    // so we always emit the real value — it naturally decays to zero as dripping stops
    if (m_extractionStarted && m_weight >= 0.1) {
        emit weightSampleReady(time, m_weight, m_flowRate);
    }
}

void ShotTimingController::onWeightSample(double weight, double flowRate, double flowRateShort)
{
    // Keep updating weight while settling is active (for SAW learning)
    if (m_sawSettling) {
        // Track peak weight during settling for cup removal detection
        if (weight > m_settlingPeakWeight) {
            m_settlingPeakWeight = weight;
        }

        // Detect cup removal during settling:
        // 1. Single-step dramatic drop (>20g decrease from current)
        // 2. Cumulative drop >20g below peak weight (catches multi-step removal
        //    where no single step exceeds 20g)
        //
        // NOTE: cup-removed detection AND the fallback chain below are
        // mirrored in tools/shot_eval/main.cpp `analyzeShotSettling()` for
        // offline corpus replay. There is no compile-time link enforcing
        // parity — when changing thresholds or the fallback ordering here,
        // update the offline tool to match.
        bool cupRemoved = (m_weight > 20.0 && weight < m_weight - 20.0) ||
                          (m_settlingPeakWeight > 20.0 && weight < m_settlingPeakWeight - 20.0);
        if (cupRemoved) {
            SAWT_WARN(QStringLiteral("Cup removed during settling (weight: %1 g peak: %2 g) "
                                     "- skipping learning")
                          .arg(weight, 0, 'f', 2).arg(m_settlingPeakWeight, 0, 'f', 2));
            // Cup removal corrupts weight data — bypass learning entirely
            // but still emit signals so the shot is saved.
            //
            // Restore m_weight to the last clean rolling-window avg so the
            // saved finalWeightG reflects what was actually in the cup. The
            // pre-removal m_weight is often a spike-artifact value that
            // squeaked past the 20 g cup-removal threshold (e.g. shot 5470,
            // issue #1280, where ShotDataModel-rejected up-spikes still
            // updated m_weight here, then a 38.5 g down-step landed below
            // both the actual settle weight and the SAW trigger weight).
            //
            // Fallback chain (4 branches; SAW_LEARNING.md has the full version):
            //   1. last clean settling avg, ONLY if its overshoot above
            //      m_weightAtStop is ≤ MAX_PLAUSIBLE_POST_STOP_DRIP_G.
            //      Real drip is 0.5–3 g.
            //   2. SCALE-FAULT snap to m_weightAtStop — fires when a
            //      clean avg WAS captured but its overshoot is too large
            //      to be physical (corpus scan revealed one shot where the
            //      scale froze at ~75 g on a ~40 g target). Both the
            //      captured avg AND the current m_weight came from the
            //      same corrupt stream, so restoring either would amplify
            //      the glitch — fall back to the SAW trigger weight.
            //   3. m_weightAtStop FLOOR — fires when NO clean avg was
            //      captured AND m_weight is below the SAW trigger weight.
            //      Post-stop drip can only ADD weight, so persisting a
            //      value below the trigger is physically impossible.
            //   4. leave m_weight as-is (cup lifted before any signal was
            //      observable AND m_weight is at/above stop weight — the
            //      legacy behavior).
            const bool haveCleanAvg = m_lastCleanSettlingAvg > 0.0;
            const bool cleanAvgPlausible =
                haveCleanAvg
                && (m_weightAtStop <= 0.0
                    || (m_lastCleanSettlingAvg - m_weightAtStop)
                           <= MAX_PLAUSIBLE_POST_STOP_DRIP_G);
            // Each branch logs which fallback fired so the per-shot debug
            // log records why finalWeight is what it is — without this the
            // four paths are indistinguishable at post-mortem.
            if (cleanAvgPlausible) {
                SAWT_LOG(QStringLiteral("Cup-removed: restored finalWeight to clean avg %1 g "
                                        "(was %2 g)")
                             .arg(m_lastCleanSettlingAvg, 0, 'f', 1).arg(m_weight, 0, 'f', 1));
                m_weight = m_lastCleanSettlingAvg;
            } else if (haveCleanAvg && m_weightAtStop > 0.0) {
                // The clean avg captured an implausibly large overshoot
                // (>MAX_PLAUSIBLE_POST_STOP_DRIP_G above SAW trigger) —
                // almost certainly a scale fault (freeze, drift, sensor
                // glitch). m_weight at the moment of cup-removal is part
                // of that same corrupt stream, so snap finalWeight back to
                // the SAW trigger weight regardless of whether m_weight is
                // currently above or below it.
                SAWT_WARN(QStringLiteral("Cup-removed: clean avg %1 g rejected as scale fault "
                                         "(overshoot %2 g > %3 g over SAW trigger %4 g) "
                                         "— snapping finalWeight to SAW trigger")
                              .arg(m_lastCleanSettlingAvg, 0, 'f', 1)
                              .arg(m_lastCleanSettlingAvg - m_weightAtStop, 0, 'f', 1)
                              .arg(MAX_PLAUSIBLE_POST_STOP_DRIP_G)
                              .arg(m_weightAtStop, 0, 'f', 1));
                m_weight = m_weightAtStop;
            } else if (m_weightAtStop > 0.0 && m_weight < m_weightAtStop) {
                SAWT_LOG(QStringLiteral("Cup-removed: floored finalWeight at SAW trigger %1 g "
                                        "(was %2 g, no clean avg captured)")
                             .arg(m_weightAtStop, 0, 'f', 1).arg(m_weight, 0, 'f', 1));
                m_weight = m_weightAtStop;
            } else {
                SAWT_LOG(QStringLiteral("Cup-removed: no fallback applied (m_weight=%1 g, "
                                        "m_weightAtStop=%2 g, haveCleanAvg=%3)")
                             .arg(m_weight, 0, 'f', 1).arg(m_weightAtStop, 0, 'f', 1)
                             .arg(haveCleanAvg ? QStringLiteral("true")
                                               : QStringLiteral("false")));
            }

            m_sawTriggeredThisShot = false;  // Prevent stale SAW state on next operation
            m_sawSettling = false;
            m_settlingTimer.stop();
            m_displayTimer.stop();
            emit sawSettlingChanged();
            emit shotProcessingReady();
            return;
        }

        m_weight = weight;
        m_flowRate = flowRate;
        emit weightChanged();

        // Also emit to graph so drip is visible (use wall clock for advancing timestamps,
        // since shotTime() is frozen at extraction end for the timer display)
        // LSLR produces clean flow rates even during settling — emit the real value
        double time = (QDateTime::currentMSecsSinceEpoch() - m_displayTimeBase) / 1000.0;
        emit weightSampleReady(time, weight, flowRate);

        // Rolling average stability detection
        // Add sample to circular buffer
        m_settlingWindow[m_settlingWindowIndex] = weight;
        m_settlingWindowIndex = (m_settlingWindowIndex + 1) % SETTLING_WINDOW_SIZE;
        if (m_settlingWindowCount < SETTLING_WINDOW_SIZE)
            m_settlingWindowCount++;

        qint64 now = QDateTime::currentMSecsSinceEpoch();

        // Also track per-sample changes for the old-style fast path
        double delta = qAbs(weight - m_lastStableWeight);
        qint64 stableMs = now - m_lastWeightChangeTime;
        if (delta >= 0.1) {
            m_lastStableWeight = weight;
            m_lastWeightChangeTime = now;
            // The run of stillness ends AT this sample, so it is 0 ms long — not
            // the length of the run that preceded it. `stableMs` was sampled before
            // this update, and the fast path below completes settling at `weight`,
            // so leaving it stale lets a sample that MOVED the scale be recorded as
            // the settled weight: a cup lifted after a second of stillness logged
            // "stable for 1007 ms" and settled at the disturbed reading (#1280).
            stableMs = 0;
        }

        // Calculate rolling average
        double avg = 0;
        for (int i = 0; i < m_settlingWindowCount; i++)
            avg += m_settlingWindow[i];
        avg /= m_settlingWindowCount;

        double avgDrift = qAbs(avg - m_lastSettlingAvg);

        SAWT_LOG(QStringLiteral("Settling: %1 g delta: %2 avg: %3 drift: %4 stable: %5 ms")
                     .arg(weight, 0, 'f', 1).arg(delta, 0, 'f', 2).arg(avg, 0, 'f', 1)
                     .arg(avgDrift, 0, 'f', 2).arg(stableMs));

        // Fast path: absolute stillness for 1 second (original behavior)
        if (stableMs >= 1000) {
            SAWT_INFO(QStringLiteral("Weight stabilized at %1 g (stable for %2 ms)")
                           .arg(weight, 0, 'f', 2).arg(stableMs));
            m_settlingTimer.stop();
            onSettlingComplete();
        }
        // Rolling average path: tolerates oscillations
        else if (m_settlingWindowCount >= SETTLING_WINDOW_SIZE) {
            // Sanity guard: drip only adds weight, so the settled average must be
            // at least the weight when SAW triggered.  If it's below, the scale is
            // still recovering from pump-vibration artifacts — don't declare stable.
            bool avgBelowStop = (m_weightAtStop > 0 && avg < m_weightAtStop - 0.5);

            // Drip-still-ongoing guard: if the current weight is significantly above
            // the rolling average, the circular buffer still contains earlier (lower)
            // samples from the start of the drip — the average is lagging, not stable.
            // A slow drip (e.g. 0.15 g/sample at 4Hz) produces avg drift close to the
            // drift rate itself — near but below SETTLING_AVG_THRESHOLD — so the drift
            // check alone is insufficient. Requiring weight ≤ avg + SETTLING_ABOVE_AVG_MARGIN
            // ensures we don't declare stable until the current reading has caught up
            // to the window mean (i.e. the drip has effectively stopped).
            bool weightAboveAvg = (weight > avg + SETTLING_ABOVE_AVG_MARGIN);

            if (avgDrift < SETTLING_AVG_THRESHOLD && !avgBelowStop && !weightAboveAvg) {
                // Average is stable and current weight is within it - check how long.
                if (m_settlingAvgStableSince == 0)
                    m_settlingAvgStableSince = now;

                qint64 avgStableMs = now - m_settlingAvgStableSince;
                // Capture the clean avg only after the gate has held continuously
                // for SETTLING_CLEAN_CAPTURE_MS (#1280). Capturing on every gate
                // fire (the original PR-1282 attempt) over-fired on noisy/oscillating
                // settles where the window avg transiently satisfied the gate at
                // values nowhere near the true cup weight; corpus-scan of 953
                // real shots found 2 such false-positive transients vs 2
                // legitimate recoveries before this guard was added.
                if (avgStableMs >= SETTLING_CLEAN_CAPTURE_MS) {
                    // Log the FIRST capture each settling cycle (m_lastCleanSettlingAvg
                    // is reset to 0 at startShot/startSettlingTimer). Subsequent gate
                    // fires keep updating the value silently — logging every one would
                    // be redundant with the per-sample `[SAW] Settling:` line.
                    if (m_lastCleanSettlingAvg <= 0.0) {
                        SAWT_LOG(QStringLiteral("First clean-avg capture at %1 g "
                                                "(gate held %2 ms, SAW trigger %3 g)")
                                     .arg(avg, 0, 'f', 1).arg(avgStableMs)
                                     .arg(m_weightAtStop, 0, 'f', 1));
                    }
                    m_lastCleanSettlingAvg = avg;
                }
                if (avgStableMs >= SETTLING_STABLE_MS) {
                    SAWT_INFO(QStringLiteral("Weight settled by avg at %1 g (avg stable for %2 ms, "
                                             "current: %3 g)")
                                  .arg(avg, 0, 'f', 1).arg(avgStableMs).arg(weight, 0, 'f', 2));
                    m_weight = avg;  // Use the average as final weight
                    m_settlingTimer.stop();
                    onSettlingComplete();
                }
            } else {
                // Average still drifting, weight still rising, or below stop weight - reset
                if (avgBelowStop && m_settlingAvgStableSince > 0)
                    SAWT_LOG(QStringLiteral("Avg %1 g below stop weight %2 g - not settling yet")
                                 .arg(avg, 0, 'f', 1).arg(m_weightAtStop, 0, 'f', 1));
                if (weightAboveAvg && m_settlingAvgStableSince > 0) {
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                    if (nowMs - m_lastDripOngoingLogMs >= 1000) {
                        SAWT_LOG(QStringLiteral("Weight %1 g still above avg %2 g - drip still ongoing")
                                     .arg(weight, 0, 'f', 1).arg(avg, 0, 'f', 1));
                        m_lastDripOngoingLogMs = nowMs;
                    }
                }
                m_settlingAvgStableSince = 0;
            }
            m_lastSettlingAvg = avg;
        } else {
            m_lastSettlingAvg = avg;
        }

        // Don't process stop conditions - just track weight
        return;
    }

    if (!m_shotActive || !m_extractionStarted) {
        return;
    }

    m_weight = weight;
    m_flowRateShort = flowRateShort;

    // Apply EMA smoothing to flow rate for display/recording (alpha=0.3, ~0.6s at 5Hz)
    // Combined with 1s LSLR window gives ~1.6s effective smoothing
    constexpr double alpha = 0.3;
    if (!m_flowRateInitialized) {
        m_smoothedFlowRate = flowRate;  // Bootstrap first sample
        m_flowRateInitialized = true;
    } else {
        m_smoothedFlowRate = alpha * flowRate + (1.0 - alpha) * m_smoothedFlowRate;
    }
    m_flowRate = m_smoothedFlowRate;

    emit weightChanged();

    // Weight is cached here, emitted to graph in onShotSample for perfect timestamp sync.
    // SOW and per-frame weight checks are now handled by WeightProcessor on a dedicated
    // worker thread, eliminating main-thread congestion from the critical stop path.
}

void ShotTimingController::tare()
{
    if (m_scale && m_scale->isConnected()) {
        m_scale->tare();
        m_scale->resetFlowCalculation();  // Avoid flow rate spikes after tare
    }

    // Fire-and-forget: assume tare worked, set weight to 0 immediately
    // Weight samples are ignored until extraction starts anyway (preheating phase)
    m_weight = 0;
    m_tareState = TareState::Complete;
    emit tareCompleteChanged();
    emit weightChanged();
}

void ShotTimingController::onTareTimeout()
{
    // No longer used - tare is fire-and-forget now
    // Weight samples are ignored until extraction starts (preheating phase)
}

void ShotTimingController::onDisplayTimerTick()
{
    // shotTimeChanged deferred to ShotDataModel's 33ms flush timer

    // Check settling stability here (in case scale stops sending samples)
    if (m_sawSettling && m_lastWeightChangeTime > 0) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();

        // Fast path: no weight samples at all for 1 second — apply drip guard using
        // the rolling average to catch BLE packet loss during an active drip.
        qint64 stableMs = now - m_lastWeightChangeTime;
        if (stableMs >= 1000) {
            double avg = 0;
            for (int i = 0; i < m_settlingWindowCount; i++)
                avg += m_settlingWindow[i];
            if (m_settlingWindowCount > 0)
                avg /= m_settlingWindowCount;
            if (stableMs >= SETTLING_SILENCE_OVERRIDE_MS) {
                // Weight hasn't changed by 0.1g in 2+ seconds — definitely stable.
                // The rolling avg is polluted by early rising samples and can't
                // catch up without new readings, so bypass the avg margin check.
                SAWT_INFO(QStringLiteral("Weight stabilized at %1 g (no change for %2 ms, "
                                         "detected by timer)")
                              .arg(m_weight, 0, 'f', 2).arg(stableMs));
                m_settlingTimer.stop();
                onSettlingComplete();
            } else if (m_settlingWindowCount > 0 && m_weight > avg + SETTLING_ABOVE_AVG_MARGIN) {
                // BLE silence during potential active drip (1-2s): weight above
                // rolling avg. Wait for more samples before declaring stable.
                // Throttle to 1/sec — this fires every 50ms tick and produces
                // 100+ log lines per shot when the scale goes silent.
                if (now - m_lastDripOngoingLogMs >= 1000) {
                    SAWT_LOG(QStringLiteral("Timer: silent but weight %1 g still above avg %2 g "
                                            "- drip may still be ongoing")
                                .arg(m_weight, 0, 'f', 1).arg(avg, 0, 'f', 1));
                    m_lastDripOngoingLogMs = now;
                }
                m_settlingAvgStableSince = 0;
            } else {
                SAWT_INFO(QStringLiteral("Weight stabilized at %1 g (stable for %2 ms, "
                                         "detected by timer)")
                              .arg(m_weight, 0, 'f', 2).arg(stableMs));
                m_settlingTimer.stop();
                onSettlingComplete();
            }
        }
        // Rolling average path: check if avg has been stable long enough
        else if (m_settlingAvgStableSince > 0) {
            qint64 avgStableMs = now - m_settlingAvgStableSince;
            if (avgStableMs >= SETTLING_STABLE_MS) {
                double avg = 0;
                for (int i = 0; i < m_settlingWindowCount; i++)
                    avg += m_settlingWindow[i];
                avg /= m_settlingWindowCount;
                // Apply drip-still-ongoing guard (same threshold as onWeightSample,
                // checked at settlement decision time since no new sample is available).
                if (m_weight > avg + SETTLING_ABOVE_AVG_MARGIN) {
                    SAWT_LOG(QStringLiteral("Timer: weight %1 g still above avg %2 g "
                                            "- resetting stable clock")
                                .arg(m_weight, 0, 'f', 1).arg(avg, 0, 'f', 1));
                    m_settlingAvgStableSince = 0;
                } else {
                    SAWT_INFO(QStringLiteral("Weight settled by avg at %1 g (detected by timer)")
                                  .arg(avg, 0, 'f', 1));
                    // #1280: mirror the onWeightSample capture so the
                    // BLE-silence completion path also records the last
                    // clean avg. Cup-removed only fires from onWeightSample
                    // today, so this is a future-safety capture matching
                    // the design contract in design.md.
                    m_lastCleanSettlingAvg = avg;
                    m_weight = avg;
                    m_settlingTimer.stop();
                    onSettlingComplete();
                }
            }
        }
    }
}

void ShotTimingController::onSawTriggered(double weightAtStop, double flowRateAtStop, double targetWeight)
{
    // Called on main thread (via QueuedConnection) when WeightProcessor detects SAW.
    // Captures state for SAW learning — settling will run after the shot ends.
    m_stopAtWeightTriggered = true;
    m_sawTriggeredThisShot = true;
    m_flowRateAtStop = flowRateAtStop;
    m_weightAtStop = weightAtStop;
    m_targetWeightAtStop = targetWeight;
    // DEBUG, and deliberately so: [SAW][Worker] already reports this stop at INFO
    // with MORE of the decision on it (threshold and expectedDrip, which this
    // call site does not receive). Logging it again here at INFO would put the
    // same event on two lines 5 ms apart in different words — the drift pair this
    // subsystem's logging was cleaned up to remove. This line records only that
    // the controller took delivery of the decision, which is developer detail.
    SAWT_LOG(QStringLiteral("Recorded stop from worker: weight=%1 g flow=%2 g/s target=%3 g")
                 .arg(weightAtStop, 0, 'f', 2).arg(flowRateAtStop, 0, 'f', 2)
                 .arg(targetWeight, 0, 'f', 2));
}

void ShotTimingController::recordWeightExit(int frameNumber)
{
    // Called by WeightProcessor (via QueuedConnection) when per-frame weight exit fires.
    // Tracks which frames exited by weight for transition reason inference.
    m_weightExitFrames.insert(frameNumber);
}

void ShotTimingController::startSettlingTimer()
{
    SAWT_LOG(QStringLiteral("Starting settling (max 10s, or avg stable for %1 ms) "
                            "- current weight: %2 g")
                .arg(SETTLING_STABLE_MS).arg(m_weight, 0, 'f', 2));
    m_lastStableWeight = m_weight;
    m_settlingPeakWeight = m_weight;
    m_lastWeightChangeTime = QDateTime::currentMSecsSinceEpoch();

    // Initialize rolling average window
    m_settlingWindowCount = 0;
    m_settlingWindowIndex = 0;
    m_lastSettlingAvg = m_weight;
    m_lastCleanSettlingAvg = 0.0;  // No clean avg yet this cycle (#1280)
    m_settlingAvgStableSince = 0;
    m_lastDripOngoingLogMs = 0;  // Allow first "drip ongoing" log immediately

    m_sawSettling = true;
    m_settlingTimer.setInterval(10000);  // 10 second max timeout
    m_settlingTimer.start();
    emit sawSettlingChanged();
}

void ShotTimingController::onSettlingComplete()
{
    // Reset flags FIRST to prevent re-triggering if another operation ends (e.g., steaming)
    m_sawTriggeredThisShot = false;
    m_sawSettling = false;

    // Settling is done - stop display timer and notify UI
    m_displayTimer.stop();
    emit sawSettlingChanged();

    // Emit shotProcessingReady on scope exit so qDebug from the SAW path lands in
    // the per-shot log before the downstream slot closes the capture window.
    // SAW_LEARNING.md requires those lines in the per-shot log. Relies on direct
    // (same-thread) connections — a queued connection on either signal would defeat
    // the ordering.
    auto deferProcessing = qScopeGuard([this] { emit shotProcessingReady(); });

    // Check a REAL scale is still serving. isConnected() alone cannot fail the way
    // this guard intends: every path that drops a physical scale installs FlowScale
    // in its place, and FlowScale's constructor calls setConnected(true) — it is
    // permanently "connected". So a USB or BLE scale lost mid-pour left this check
    // passing and learning proceeded on a flow-integral ESTIMATE, writing a
    // fabricated drip into the real scale's pool.
    //
    // Without a physical scale there is no OBSERVATION of what landed in the cup, so
    // there is nothing for SAW to learn from — the estimate and the thing it would be
    // corrected against are the same number.
    //
    // Concretely, drip is what arrives after the stop command: m_weight - m_weightAtStop,
    // where m_weightAtStop is captured in onSawTriggered() when SAW FIRES and m_weight
    // keeps advancing until endShot() starts settling. On a real scale that spans two
    // physical contributions — flow still leaving the group during the DE1's stop
    // latency, AND the gravity drip off the puck afterwards. FlowScale can only ever
    // see the first: MachineState::onFlowSample() is gated on isFlowing(), so the
    // moment the pour ends FlowScale goes silent and every gram that drips after it is
    // invisible. Its drip is therefore a real number but a systematically LOW one, and
    // it is an integral of the DE1's own flow model rather than a measurement.
    //
    // That biased value used to land in the SAVED scale's pool (currentScaleType()
    // falls back to it for a virtual scale), pulling a physical scale's learned drip
    // down so SAW stops late and overshoots once the user reconnects it.
    //
    // (An earlier revision of this comment claimed the learned drip was structurally
    // ZERO, reasoning that FlowScale falls silent at the stop. It does — but not until
    // endShot(), which is well after onSawTriggered() captured m_weightAtStop, so the
    // stop-latency flow is still integrated. Recorded because the wrong version is
    // more memorable than the right one, and the conclusion is unchanged either way.)
    //
    // FlowScale still SERVES SAW; it just must never train it.
    if (!m_scale || !m_scale->isConnected() || m_scale->isFlowScale()) {
        SAWT_WARN(QStringLiteral("No physical scale at settling (scale=%1), skipping learning")
                      .arg(m_scale ? m_scale->type() : QStringLiteral("none")));
        return;
    }

    // Validate flow rate at stop (low flow makes division unstable)
    if (m_flowRateAtStop < 0.5) {
        SAWT_WARN(QStringLiteral("Flow at stop too low (%1 g/s), skipping learning")
                      .arg(m_flowRateAtStop, 0, 'f', 2));
        return;
    }

    // Calculate how much weight came after we sent the stop command
    double drip = m_weight - m_weightAtStop;
    if (drip < 0) {
        SAWT_WARN(QStringLiteral("Negative drip (%1 g), clamping to 0").arg(drip, 0, 'f', 2));
        drip = 0;  // Weight can't decrease
    }

    double overshoot = m_weight - m_targetWeightAtStop;

    // Validate settled weight is reasonable. Scale readings can go haywire after
    // the shot (drip tray interference, cup removal, scale oscillation). A 20g miss
    // is clearly a scale glitch; 10-15g can happen with a badly miscalibrated prediction
    // and the system needs to learn from those to recover.
    if (m_weight < 0 || qAbs(overshoot) > 20.0) {
        SAWT_WARN(QStringLiteral("Settled weight unreasonable (weight=%1 g overshoot=%2 g), "
                                 "skipping learning")
                      .arg(m_weight, 0, 'f', 2).arg(overshoot, 0, 'f', 2));
        return;
    }

    // Extra cup-removal guard at completion time. Handles slow/multi-step cup
    // removal paths that may not trigger single-sample bypass checks.
    if (m_settlingPeakWeight > 20.0 && m_weight < m_settlingPeakWeight - 20.0) {
        SAWT_WARN(QStringLiteral("Possible cup removal detected at settling complete "
                                 "(weight=%1 g peak=%2 g), skipping learning")
                      .arg(m_weight, 0, 'f', 2).arg(m_settlingPeakWeight, 0, 'f', 2));
        return;
    }

    // Validate drip is in reasonable range (0 to 20 grams)
    // Widened from 15g to allow learning from badly miscalibrated predictions
    if (drip > 20.0) {
        SAWT_WARN(QStringLiteral("Drip out of range (%1 g), skipping learning")
                      .arg(drip, 0, 'f', 2));
        return;
    }

    SAWT_LOG(QStringLiteral("Learning: final=%1 g target=%2 g drip=%3 g flow=%4 g/s overshoot=%5 g")
                .arg(m_weight, 0, 'f', 2).arg(m_targetWeightAtStop, 0, 'f', 2)
                .arg(drip, 0, 'f', 2).arg(m_flowRateAtStop, 0, 'f', 2)
                .arg(overshoot, 0, 'f', 2));

    // Emit signal for main.cpp to handle persistence (drip and flow, not lag)
    emit sawLearningComplete(drip, m_flowRateAtStop, overshoot);
}
