#include "core/diagnosticlogging.h"
#include "shottimingcontroller.h"
#include "../ble/de1device.h"
#include "../ble/scaledevice.h"
#include "../core/settings.h"
#include "../machine/machinestate.h"
#include "../machine/sawlogging.h"
#include "../profile/profile.h"
#include <QDebug>
#include <QScopeGuard>

using namespace Settling;

// Aliases, not copies — see sawlogging.h. No logMessage signal on this
// controller, so the STDERR forms apply. "Timing" as the source: this file owns
// the stop decision and the settling pass that follows it, as distinct from the
// learning store ([SAW][Learning]) and the weight worker ([SAW][Worker]).
#define SAWT_LOG(msg)  SAW_LOG_STDERR("Timing", msg)
#define SAWT_INFO(msg) SAW_INFO_STDERR("Timing", msg)
#define SAWT_WARN(msg) SAW_WARN_STDERR("Timing", msg)

namespace {
enum class SettledBy { Stillness, Silence, Average };

// One wording per criterion, whichever path completes it: the sample and timer
// paths had drifted into two formats for the same average completion.
QString settledLine(SettledBy by, double settledG, qint64 heldMs, bool byTimer, double currentG = 0.0)
{
    QString line;
    switch (by) {
    case SettledBy::Stillness:
        line = QStringLiteral("Weight stabilized at %1 g (stable for %2 ms")
                   .arg(settledG, 0, 'f', 2).arg(heldMs);
        break;
    case SettledBy::Silence:
        line = QStringLiteral("Weight stabilized at %1 g (no change for %2 ms")
                   .arg(settledG, 0, 'f', 2).arg(heldMs);
        break;
    case SettledBy::Average:
        line = QStringLiteral("Weight settled by avg at %1 g (avg stable for %2 ms, current %3 g")
                   .arg(settledG, 0, 'f', 1).arg(heldMs).arg(currentG, 0, 'f', 2);
        break;
    }
    if (byTimer)
        line += QStringLiteral(", detected by timer");
    return line + QLatin1Char(')');
}
}  // namespace

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
    connect(&m_settlingTimer, &QTimer::timeout, this, &ShotTimingController::completeSettling);
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

std::optional<double> ShotTimingController::sensorTime(qint64 receivedAtMs) const
{
    if (!m_extractionStarted || m_displayTimeBase <= 0 || (!m_shotActive && !m_sawSettling))
        return std::nullopt;
    const double time = (receivedAtMs - m_displayTimeBase) / 1000.0;
    return time >= 0 ? std::optional<double>(time) : std::nullopt;
}

QString ShotTimingController::servingWeightSource() const
{
    if (!m_scale || !m_scale->isConnected() || m_scale->isSimulated()) return QStringLiteral("unknown");
    return m_scale->isFlowScale() ? QStringLiteral("flow_scale_virtual") : QStringLiteral("physical_scale");
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
    flushPostSettleTrace(postSettleEndReason());
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
    m_settleStartWeight = 0.0;
    m_settleFlowRefGps = -1.0;
    m_disturbedRun = 0;
    m_settleRateRecovery = {};
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
    // shotEnded also fires for flush/steam/hot water; only startShot() (espresso) sets this.
    const bool wasEspresso = m_shotActive;
    m_shotActive = false;
    // Freeze extraction end time for timer display and saved duration.
    // m_currentTime holds the last Pouring-phase sample time (Ending-phase samples
    // are rejected before reaching onShotSample, so this is accurate).
    m_extractionEndTime = m_currentTime;

    // Start settling timer if SAW triggered this shot (for learning)
    // Keep display timer running during settling so graph continues to update
    if (m_sawTriggeredThisShot) {
        startSettlingTimer();
        // The shot's end arrives queued, so hot water started at the Idle report
        // can already be pouring; its flow-start call found no settle to end.
        // End this one now, before the water's tare lands.
        if (m_machineState && m_machineState->phase() == MachineState::Phase::HotWater) {
            endSettlingForHotWater();
            emit shotTimeChanged();
            return;
        }
        // Don't stop display timer - keep time incrementing for graph
        // shotProcessingReady will be emitted after settling completes
        SAWT_LOG(QStringLiteral("Triggered - waiting for weight to settle before processing shot"));
    } else {
        m_displayTimer.stop();
        // No SAW - shot can be processed immediately
        if (wasEspresso)
            SAWT_LOG(QStringLiteral("Shot ended without a stop-at-weight trigger — processing it now"));
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
            DIAG_DEBUG(SHOT, "ShotTimingController") << "FRAME CHANGE:" << m_currentFrameNumber << "->" << frameNumber
                     << "name:" << frame.name << "exitWeight:" << frame.exitWeight;
        }
        m_currentFrameNumber = frameNumber;

        // Extraction starts on the first frame we see. The DE1 may skip preheating
        // frames (0-1) if the group is already hot, jumping straight to frame 2+.
        if (!m_extractionStarted) {
            m_extractionStarted = true;
            m_displayTimeBase = QDateTime::currentMSecsSinceEpoch();
            emit extractionClockStarted();
            DIAG_DEBUG(SHOT, "ShotTimingController") << "EXTRACTION STARTED at frame" << frameNumber;
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
        // The upward twins of a cup lift: more weight than drip can add, or weight
        // arriving faster than drip can run. A hand left on the cup reads this way (Oct
        // 2026: 36 g of coffee climbed to 57 g and was saved). A lone disturbed sample is
        // dropped, not acted on: one corrupted packet is not a hand, and feeding it to
        // the window would break the stability gate.
        if (m_settleFlowRefGps < 0.0)
            m_settleFlowRefGps = settleFlowRefGps(m_flowRateAtStop, flowRateShort);
        const SettleDisturbance candidate = classifySettleSample(
            weight, flowRateShort, settleBaseG(m_weightAtStop, m_settleStartWeight),
            m_flowRateAtStop, m_settleFlowRefGps, m_settleAmountCheck);
        const SettleDisturbance disturbance = m_settleRateRecovery.filter(
            candidate, weight, m_weight, m_disturbedRun > 0, m_pendingReadingG);
        m_disturbedRun = disturbance != SettleDisturbance::None ? m_disturbedRun + 1 : 0;
        if (m_disturbedRun > 0 && m_disturbedRun < DISTURBED_SAMPLES) {
            m_pendingReadingG = weight;
            m_pendingFlowGps = flowRateShort;
            m_pendingDisturbance = disturbance;
            return;
        }

        // Track peak weight during settling for cup removal detection
        if (weight > m_settlingPeakWeight) {
            m_settlingPeakWeight = weight;
        }

        // Detect cup removal during settling: any drop of CUP_REMOVED_DROP_G below
        // the settling peak. Measuring from the peak catches a multi-step removal.
        //
        // It used to ALSO require the weight to have EXCEEDED 20 g, which hid the lift
        // entirely on a shot that never got there. A lift reads as the cup's
        // tared-out mass, tens of grams negative (shot 5470 logged -28.0), so the
        // drop was never marginal — the precondition just blinded the detector on
        // small shots, which then settled at that negative value. Drip can only ADD
        // weight, so the drop test alone is the signal. The single-step clause went
        // with it as redundant, not as a policy change: m_settlingPeakWeight is
        // updated just above and is >= m_weight, so a drop from the previous sample
        // implies a drop from the peak.
        //
        // Only catches a lift that reads NEGATIVE. One landing near ZERO on a small
        // shot is a sub-20 g drop and still gets through; lowering the threshold
        // needs a corpus, and the settling corpus has one shot and no cup-lift.
        if (disturbance != SettleDisturbance::None || isCupLift(weight, m_settlingPeakWeight)) {
            abandonSettling(weight, flowRateShort, disturbance);
            return;
        }

        emit weightSourceUsed(servingWeightSource());
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
        // How long the run this sample BROKE had lasted, or -1 if it broke none.
        // `stableMs` is zeroed below on a moving sample, so without this the log
        // reads `stable: 0 ms` and a submitted log can no longer show how long the
        // scale had been still before it moved. Only a NEAR-MISS is worth saying:
        // during a drip most samples move, and each one's broken "run" is just the
        // sample interval, so reporting every one would add ~100 redundant lines a
        // shot to the logs this subsystem is diagnosed from. Half the threshold is
        // the point past which the sample stopped a settle that was under way.
        qint64 endedStillMs = -1;
        if (delta >= SETTLING_STILL_DELTA_G) {
            m_lastStableWeight = weight;
            m_lastWeightChangeTime = now;
            // The run of stillness ends AT this sample, so it is 0 ms long — not
            // the length of the run that preceded it. `stableMs` was sampled before
            // this update, and the fast path below completes settling at `weight`,
            // so leaving it stale let a sample that MOVED the scale be recorded as
            // the settled weight, logging "stable for 1007 ms" for a run that had
            // ended one sample ago.
            //
            // What reaches here is any change of at least 0.1 g the gates above did
            // not claim: an increase still within maxPlausibleDripG() of the stop
            // weight, or a drop under CUP_REMOVED_DROP_G — a small shot lifted to
            // near zero. So a real disturbance can reach this branch; an earlier
            // draft claimed a cup lift "cannot happen" here and was wrong. Found
            // from a suite failure, not from #1280.
            if (stableMs >= SETTLING_STABLE_MS / 2)
                endedStillMs = stableMs;
            stableMs = 0;
        }

        // Calculate rolling average
        double avg = 0;
        for (int i = 0; i < m_settlingWindowCount; i++)
            avg += m_settlingWindow[i];
        avg /= m_settlingWindowCount;

        double avgDrift = qAbs(avg - m_lastSettlingAvg);

        // Only a broken stability run explains a delay; routine samples are in the shot data.
        if (endedStillMs >= 0) {
            SAWT_LOG(QStringLiteral("Stillness interrupted: weight=%1 g delta=%2 g avg=%3 g drift=%4 g endedStableMs=%5")
                         .arg(weight, 0, 'f', 1).arg(delta, 0, 'f', 2).arg(avg, 0, 'f', 1)
                         .arg(avgDrift, 0, 'f', 2).arg(endedStillMs));
        }

        // Fast path: absolute stillness for SETTLING_STABLE_MS (original behavior)
        if (stableMs >= SETTLING_STABLE_MS) {
            SAWT_INFO(settledLine(SettledBy::Stillness, weight, stableMs, false));
            completeSettling();
        }
        // Rolling average path: tolerates oscillations
        else if (m_settlingWindowCount >= SETTLING_WINDOW_SIZE) {
            // Sanity guard: drip only adds weight, so the settled average must be
            // at least the weight when SAW triggered.  If it's below, the scale is
            // still recovering from pump-vibration artifacts — don't declare stable.
            bool avgBelowStop = (m_weightAtStop > 0 && avg < m_weightAtStop - SETTLING_AVG_BELOW_STOP_G);

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
                    // fires keep updating the value silently; the final result retains it.
                    if (m_lastCleanSettlingAvg <= 0.0) {
                        SAWT_LOG(QStringLiteral("First clean-avg capture at %1 g "
                                                "(gate held %2 ms, SAW trigger %3 g)")
                                     .arg(avg, 0, 'f', 1).arg(avgStableMs)
                                     .arg(m_weightAtStop, 0, 'f', 1));
                    }
                    m_lastCleanSettlingAvg = avg;
                }
                if (avgStableMs >= SETTLING_STABLE_MS) {
                    SAWT_INFO(settledLine(SettledBy::Average, avg, avgStableMs, false, weight));
                    m_weight = avg;  // Use the average as final weight
                    completeSettling();
                }
            } else {
                // Average still drifting, weight still rising, or below stop weight - reset
                if (avgBelowStop && m_settlingAvgStableSince > 0)
                    SAWT_LOG(QStringLiteral("Avg %1 g below stop weight %2 g - not settling yet")
                                 .arg(avg, 0, 'f', 1).arg(m_weightAtStop, 0, 'f', 1));
                if (weightAboveAvg && m_settlingAvgStableSince > 0) {
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                    if (nowMs - m_lastDripOngoingLogMs >= DRIP_ONGOING_LOG_THROTTLE_MS) {
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

    if (m_postSettle)
        tracePostSettle(weight);

    if (!m_shotActive || !m_extractionStarted) {
        return;
    }

    emit weightSourceUsed(servingWeightSource());
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

    // Preserve measured pre-drip baselines; onShotSample only publishes positive weights.
    if (weight >= 0.0 && weight < 0.1)
        emit weightBaselineReady((QDateTime::currentMSecsSinceEpoch() - m_displayTimeBase) / 1000.0, weight);
    emit weightChanged();

    // Weight is cached here, emitted to graph in onShotSample for perfect timestamp sync.
    // SOW and per-frame weight checks are now handled by WeightProcessor on a dedicated
    // worker thread, eliminating main-thread congestion from the critical stop path.
}

// The first sample at or after each whole second since the save, labelled with its
// actual elapsed time so a gap in the feed cannot put a value on the wrong second.
// The shot's zero correction is cleared once it is saved, so each sample is put back
// into the saved value's frame (emitted = raw - applied offset).
void ShotTimingController::tracePostSettle(double weight)
{
    PostSettleTrace& t = *m_postSettle;
    const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - t.startMs;
    if (elapsedMs > (POST_SETTLE_TRACE_S + 2) * 1000) {
        flushPostSettleTrace(QStringLiteral("scale feed paused"));
        return;
    }
    const double w = weight + m_appliedZeroOffsetG - t.zeroOffsetG;
    if (w < t.savedG - CUP_REMOVED_DROP_G) {
        t.text += QStringLiteral("  dropped over %1 g at +%2s (cup lifted or re-tared)")
                      .arg(CUP_REMOVED_DROP_G, 0, 'f', 0).arg(elapsedMs / 1000.0, 0, 'f', 1);
        flushPostSettleTrace();
        return;
    }
    t.largestStepG = qMax(t.largestStepG, w - t.lastG);
    t.maxRiseG = qMax(t.maxRiseG, w - t.savedG);
    t.lastG = w;
    t.elapsedSec = elapsedMs / 1000.0;
    if (elapsedMs < t.nextSecond * 1000)
        return;
    t.text += QStringLiteral("  +%1s %2").arg(elapsedMs / 1000.0, 0, 'f', 1).arg(w, 0, 'f', 1);
    t.nextSecond = static_cast<int>(elapsedMs / 1000) + 1;
    if (t.nextSecond > POST_SETTLE_TRACE_S)
        flushPostSettleTrace();
}

// Why an unfinished trace is being closed when the next shot starts.
QString ShotTimingController::postSettleEndReason() const
{
    if (!m_postSettle)
        return QString();
    const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - m_postSettle->startMs;
    return elapsedMs <= POST_SETTLE_TRACE_S * 1000 ? QStringLiteral("cut short by the next shot")
                                                   : QStringLiteral("scale feed stopped");
}

void ShotTimingController::flushPostSettleTrace(const QString& reason)
{
    if (!m_postSettle)
        return;
    if (!m_postSettle->text.isEmpty() || !reason.isEmpty()) {
        SAWT_LOG(QStringLiteral("After settle (saved %1 g):%2%3")
                     .arg(m_postSettle->savedG, 0, 'f', 1)
                     .arg(m_postSettle->text,
                          reason.isEmpty() ? QString() : QStringLiteral("  (%1)").arg(reason)));
    }
    // Diagnostic only: neither a slow rise nor cup contact certifies a new beverage yield.
    if (m_postSettle->maxRiseG >= 0.5) {
        const auto& t = *m_postSettle;
        const auto savedAt = QDateTime::fromMSecsSinceEpoch(t.startMs);
        SAWT_INFO(QStringLiteral("Post-save weight changed: outcome=unverified-change settledAt=%1 "
                                 "savedG=%2 laterG=%3 deltaG=%4 maxRiseG=%5 largestStepG=%6 elapsedSec=%7 "
                                 "savedYieldUnchanged=true learningUnchanged=true")
            .arg(savedAt.toOffsetFromUtc(savedAt.offsetFromUtc()).toString(Qt::ISODate))
            .arg(t.savedG, 0, 'f', 1).arg(t.lastG, 0, 'f', 1)
            .arg(t.lastG - t.savedG, 0, 'f', 1).arg(t.maxRiseG, 0, 'f', 1).arg(t.largestStepG, 0, 'f', 1)
            .arg(t.elapsedSec, 0, 'f', 1));
    }
    m_postSettle.reset();
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

        // Fast path: no weight samples at all for SETTLING_STABLE_MS — apply drip guard
        // using the rolling average to catch BLE packet loss during an active drip.
        qint64 stableMs = now - m_lastWeightChangeTime;
        if (stableMs >= SETTLING_STABLE_MS) {
            double avg = 0;
            for (int i = 0; i < m_settlingWindowCount; i++)
                avg += m_settlingWindow[i];
            if (m_settlingWindowCount > 0)
                avg /= m_settlingWindowCount;
            if (stableMs >= SETTLING_SILENCE_OVERRIDE_MS) {
                // Weight hasn't changed by 0.1g in 2+ seconds — definitely stable.
                // The rolling avg is polluted by early rising samples and can't
                // catch up without new readings, so bypass the avg margin check.
                SAWT_INFO(settledLine(SettledBy::Silence, m_weight, stableMs, true));
                completeSettling();
            } else if (m_settlingWindowCount > 0 && m_weight > avg + SETTLING_ABOVE_AVG_MARGIN) {
                // BLE silence during potential active drip (1-2s): weight above
                // rolling avg. Wait for more samples before declaring stable.
                // Throttle to 1/sec — this fires every 50ms tick and produces
                // 100+ log lines per shot when the scale goes silent.
                if (now - m_lastDripOngoingLogMs >= DRIP_ONGOING_LOG_THROTTLE_MS) {
                    SAWT_LOG(QStringLiteral("Timer: silent but weight %1 g still above avg %2 g "
                                            "- drip may still be ongoing")
                                .arg(m_weight, 0, 'f', 1).arg(avg, 0, 'f', 1));
                    m_lastDripOngoingLogMs = now;
                }
                m_settlingAvgStableSince = 0;
            } else {
                SAWT_INFO(settledLine(SettledBy::Stillness, m_weight, stableMs, true));
                completeSettling();
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
                    SAWT_INFO(settledLine(SettledBy::Average, avg, avgStableMs, true, m_weight));
                    // #1280: mirror the onWeightSample capture so the
                    // BLE-silence completion path also records the last
                    // clean avg. Cup-removed only fires from onWeightSample
                    // today, so this is a future-safety capture matching
                    // the design contract in design.md.
                    m_lastCleanSettlingAvg = avg;
                    m_weight = avg;
                    completeSettling();
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

void ShotTimingController::completeSettling()
{
    if (m_disturbedRun > 0) {
        abandonSettling(m_pendingReadingG, m_pendingFlowGps, m_pendingDisturbance);
        return;
    }
    m_settlingTimer.stop();
    onSettlingComplete();
}

void ShotTimingController::abandonSettling(double reading, double flowShort,
                                           SettleDisturbance disturbance)
{
    // Save what was in the cup before the disturbance; neither a lift nor a disturbance
    // teaches the predictor anything, so learning is skipped.
    const double baseG = settleBaseG(m_weightAtStop, m_settleStartWeight);
    const AbandonedSettle saved = abandonedSettleWeight(
        m_lastCleanSettlingAvg, baseG, m_weight, disturbance != SettleDisturbance::None);
    const QString savedFrom = QString::fromLatin1(abandonedSettleSourceText(saved.source));
    // INFO, like a lift: the usual cause is a hand on the cup, a user action rather than
    // a fault, and INFO is the tier the user's views show.
    if (disturbance == SettleDisturbance::OverRise) {
        SAWT_INFO(QStringLiteral("Reading %1 g is %2 g over the stop weight, more than "
                                 "drip can add (%3 g at %4 g/s) — saved %5 g, %6; "
                                 "skipping learning")
                      .arg(reading, 0, 'f', 1).arg(reading - baseG, 0, 'f', 1)
                      .arg(maxPlausibleDripG(m_flowRateAtStop), 0, 'f', 1)
                      .arg(m_flowRateAtStop, 0, 'f', 2)
                      .arg(saved.weightG, 0, 'f', 1).arg(savedFrom));
    } else if (disturbance == SettleDisturbance::RateRise) {
        SAWT_INFO(QStringLiteral("Weight rising %1 g/s at %2 g, faster than drip can run "
                                 "(%3 g/s at the stop) — saved %4 g, %5; skipping learning")
                      .arg(flowShort, 0, 'f', 1).arg(reading, 0, 'f', 1)
                      .arg(m_settleFlowRefGps, 0, 'f', 1)
                      .arg(saved.weightG, 0, 'f', 1).arg(savedFrom));
    } else {
        SAWT_INFO(QStringLiteral("Cup removed during settling (sample %1 g, peak %2 g) "
                                 "— saved %3 g, %4; skipping learning")
                      .arg(reading, 0, 'f', 1).arg(m_settlingPeakWeight, 0, 'f', 1)
                      .arg(saved.weightG, 0, 'f', 1).arg(savedFrom));
    }
    finishAbandonedSettle(saved.weightG);
}

void ShotTimingController::endSettlingForHotWater()
{
    if (!m_sawSettling)
        return;
    // A clean average is a settled shot that simply ran out of time, so it still
    // teaches SAW. Without one, save what a cup lift would: the reading at the
    // flow-start edge predates the hot water's tare, so it is still the coffee.
    if (m_lastCleanSettlingAvg > 0.0) {
        SAWT_INFO(QStringLiteral("Hot water started during settling — saved %1 g, the last "
                                 "steady reading")
                      .arg(m_lastCleanSettlingAvg, 0, 'f', 1));
        m_weight = m_lastCleanSettlingAvg;
        emit weightChanged();
        m_settlingTimer.stop();
        onSettlingComplete();
        return;
    }
    const AbandonedSettle saved = abandonedSettleWeight(
        0.0, settleBaseG(m_weightAtStop, m_settleStartWeight), m_weight, false);
    SAWT_INFO(QStringLiteral("Hot water started before the weight settled — saved %1 g, %2; "
                             "skipping learning")
                  .arg(saved.weightG, 0, 'f', 1)
                  .arg(QString::fromLatin1(abandonedSettleSourceText(saved.source))));
    finishAbandonedSettle(saved.weightG);
}

void ShotTimingController::finishAbandonedSettle(double savedG)
{
    m_weight = savedG;
    emit weightChanged();

    m_sawTriggeredThisShot = false;  // Prevent stale SAW state on next operation
    m_sawSettling = false;
    m_settlingTimer.stop();
    m_displayTimer.stop();
    emit sawSettlingChanged();
    emit shotProcessingReady();
}

void ShotTimingController::startSettlingTimer()
{
    SAWT_LOG(QStringLiteral("Starting settling (max 10s, or avg stable for %1 ms) "
                            "- current weight: %2 g")
                .arg(SETTLING_STABLE_MS).arg(m_weight, 0, 'f', 2));
    m_lastStableWeight = m_weight;
    m_settlingPeakWeight = m_weight;
    m_settleStartWeight = m_weight;
    m_settleFlowRefGps = -1.0;
    m_settleAmountCheck = !m_currentProfile
        || BeverageType::group(m_currentProfile->beverageType()) == QLatin1String("espresso");
    m_disturbedRun = 0;
    m_settleRateRecovery = {};
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

    m_postSettle = PostSettleTrace{m_weight, m_appliedZeroOffsetG,
                                   QDateTime::currentMSecsSinceEpoch(), 1, QString()};
    m_postSettle->lastG = m_weight;

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

    // Calculate how much weight came after we sent the stop command. A negative
    // drip is physically impossible — drip only adds — so it means the weight
    // stream is corrupt, most often a cup lift whose drop stayed under
    // CUP_REMOVED_DROP_G. This used to be clamped to 0 and then learned from,
    // which laundered it past addSawLearningPoint's own `drip < 0` reject — the
    // guard written for exactly this value. Left negative so that guard sees it.
    const double drip = m_weight - m_weightAtStop;

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

    // Backstop for the one path that can break the per-sample gate's invariant.
    // That gate leaves m_weight >= m_settlingPeakWeight - CUP_REMOVED_DROP_G after
    // every accepted sample, so the fast path and both timer paths cannot arrive
    // here violating it; only `m_weight = avg` can, and only if the window
    // straddles the sample that raised the peak. NOT a multi-step-removal
    // backstop, which is what this comment used to claim: the peak is sticky, so
    // the per-sample check catches the step that crosses the threshold. Note the
    // `m_weight < 0` guard above also claims the small-shot case first, so the
    // 20 g precondition coming off here changed nothing on its own.
    if (m_weight < m_settlingPeakWeight - CUP_REMOVED_DROP_G) {
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

    SAWT_INFO(QStringLiteral("Learning: final=%1 g target=%2 g drip=%3 g flow=%4 g/s overshoot=%5 g")
                .arg(m_weight, 0, 'f', 2).arg(m_targetWeightAtStop, 0, 'f', 2)
                .arg(drip, 0, 'f', 2).arg(m_flowRateAtStop, 0, 'f', 2)
                .arg(overshoot, 0, 'f', 2));

    // Emit signal for main.cpp to handle persistence (drip and flow, not lag)
    emit sawLearningComplete(drip, m_flowRateAtStop, overshoot);
}
