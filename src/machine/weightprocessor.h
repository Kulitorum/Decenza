#pragma once

#include <QObject>
#include <QThread>
#include <QList>
#include <QVector>
#include <QSet>
#include <QDateTime>
#include <functional>

#include "stepexitarbiter.h"

// Runs on a dedicated worker thread. Receives weight samples from the scale,
// computes LSLR flow rates, and makes SAW/per-frame-exit decisions
// independently of main thread congestion.
//
// Input (via QueuedConnection from main thread):
//   - processWeight(): called at the scale's own rate (2-10 Hz), and NOT
//     necessarily evenly — a transport may hand over several samples at
//     once, which is what the de-jitter block in processWeight() exists for
//   - configure(): called once at shot start with targets and learning data
//   - setTargetWeight(): may update SAW target mid-shot (e.g. user +10g bump)
//   - setCurrentFrame(): called at ~5Hz from DE1 shot samples
//
// Output (via QueuedConnection back to main thread):
//   - stopNow(triggerMs): triggers DE1Device::stopOperationUrgent(triggerMs)
//   - sawTriggered(weightAtStop, flowRateAtStop, targetWeight): carries context for SAW learning
//   - skipFrame(): triggers DE1Device::skipToNextFrame()
//   - flowRatesReady(): feeds ShotTimingController for graph/settling

class WeightProcessor : public QObject {
    Q_OBJECT

public:
    explicit WeightProcessor(QObject* parent = nullptr);

public slots:
    // Called from main thread (all via QueuedConnection — thread-safe)
    void processWeight(double weight);
    void configure(double targetWeight, int preinfuseFrameCount,
                   QVector<double> frameExitWeights,
                   QVector<FrameExitCondition> frameExitConditions,
                   QVector<double> learningDrips, QVector<double> learningFlows,
                   bool sawConverged, double sensorLagSeconds = 0.38);
    // Live SAW target update (e.g. user pressed +10g mid-shot). Writes are serialized
    // on the worker thread via QueuedConnection from main thread, so no extra locking.
    void setTargetWeight(double weight);
    // pressure/flow are the live firmware sensor readings from the same DE1
    // shot sample; cached for the step-exit arbiter. Defaulted so existing
    // callers/tests that only have a frame number still compile.
    void setCurrentFrame(int frameNumber, double pressure = 0.0, double flow = 0.0);
    // Scale-feed-liveness gate input (BLE connection-priority backstop).
    // setShotCycleActive(true) is set when the espresso cycle enters
    // EspressoPreheating and cleared on any non-preheat phase (idle/sleep/
    // extraction-end) — it widens stall detection to the pre-shot warm-up so
    // the backoff can begin before the pour.
    void setShotCycleActive(bool active);
    void setTareComplete(bool complete);
    void startExtraction();
    void markExtractionStart();  // Called when flow starts (idempotent, espresso-only)
    void stopExtraction();
    // The PAIR of startExtraction: disarms on espresso-cycle exit even when
    // flow never began, which stopExtraction (gated on shotEnded) misses.
    void endShotCycle();
    void resetForRetare();  // Clear LSLR buffer after auto-tare during preheat

#ifdef DECENZA_TESTING
public:
    // Test support: override wall-clock source. Must be called before moveToThread()
    // because std::function is not thread-safe for concurrent read/write.
    void setWallClock(std::function<qint64()> fn) {
        Q_ASSERT(thread() == QThread::currentThread());
        m_wallClock = std::move(fn);
    }
#endif

signals:
    // Emitted when SAW triggers. Includes monotonic timestamp (ms) for latency tracing.
    void stopNow(qint64 triggerMs);
    // Carries SAW context for learning (weight/flow at stop time)
    void sawTriggered(double weightAtStop, double flowRateAtStop, double targetWeight);
    void skipFrame(int frameNumber);
    void flowRatesReady(double weight, double flowRate, double flowRateShort);
    void untaredCupDetected();
    // Scale-agnostic in-shot liveness (BLE connection-priority backstop).
    // Emitted when, during an active tared extraction, the scale stopped
    // delivering weight samples for > kScaleStaleMs — evaluated on the DE1
    // shot-sample cadence (setCurrentFrame) so it fires even while the scale
    // is silent. Pure observation — no effect on SAW/flow/frame decisions.
    // `gapMs` is how long the feed had been silent when the stall was
    // detected (≥ kScaleStaleMs). enforce-mode handlers ignore the arg
    // (behavior unchanged); observe mode logs it as the stall duration.
    void scaleFeedStalled(qint64 gapMs);
    // Recovery counterpart (observe-mode change). Emitted exactly once on the
    // stall→genuine-sample edge: the feed had been signalled stalled this
    // cycle and a real sample has now arrived. gapMs is the silent duration
    // (first post-silence sample wall-clock − last pre-silence sample). Pure
    // observation — never alters SAW/flow/frame decisions; the transport
    // decides whether to log it (observe mode).
    void scaleFeedResumed(qint64 gapMs);
    // Confirmed-stall edge (epoch-scope-and-stall-confirm change). Emitted
    // once, only when a suspected stall has PERSISTED past kScaleStallConfirmMs
    // with NO intervening scaleFeedResumed (i.e. it did not self-recover).
    // This — not scaleFeedStalled — is what enforce mode latches on, and what
    // observe mode records as the real "would back off". A transient blip that
    // recovers before the confirm threshold never emits this. `gapMs` is the
    // confirmed silent duration. Pure observation; SAW/flow/frame untouched.
    void scaleFeedStallConfirmed(qint64 gapMs);

private:
    double computeLSLR(int windowMs) const;
    double getExpectedDrip(double currentFlowRate) const;
    // Scale-agnostic stall evaluation, run on the DE1 shot-sample cadence
    // (setCurrentFrame) during extraction / preheat.
    void checkScaleFeedStall(int frameNumber);
    // Single chokepoint that clears the three correlated stall fields
    // together (m_scaleFeedStale / m_scaleStallConfirmed / m_feedStallStartMs).
    // Every reset path calls this so a half-reset (e.g. confirmed left set
    // without stale) — which gates the real enforce backoff — is impossible
    // by construction, mirroring ScaleSkipHighLatch::clear()'s discipline.
    void resetStallTracking();
    // Single chokepoint for the arrival-rate calibration state, in the same spirit
    // as resetStallTracking() above: m_rateRecentCount doubles as the index bound
    // for the minimum loop, so it is only meaningful while m_rateRecentNext is back
    // at zero. Clearing one without the other reads correct and silently serves a
    // discarded measurement — see the reconnect branch in processWeight(). Having
    // one function own all five fields makes that half-reset unwritable.
    // windowStartMs is the wall-clock the fresh window begins at: pass the current
    // clock to START one (the mid-stream reconnect path, which must keep
    // measuring), or leave it 0 to leave calibration DORMANT until the next
    // sample opens a window (the per-shot resets, which have no clock to hand).
    // Zeroing it unconditionally is not a safe default — m_rateWindowStartMs == 0
    // is itself the "open a window" trigger, so a reset that always zeroed it
    // re-triggered on every following sample, the count never reached two, and
    // the estimate stayed uncalibrated for the whole stream.
    void resetRateCalibration(qint64 windowStartMs = 0);
    // Third chokepoint, same discipline: owns every per-shot diagnostic counter so
    // a partial reset cannot be written. Also arms m_djSummaryPending.
    void resetShotDiagnostics();

    // Weight sample buffer (1-second rolling window for LSLR)
    struct WeightSample {
        qint64 timestamp;
        double weight;
    };
    QList<WeightSample> m_weightSamples;

    // Spike filter: rejects single-packet BLE corruption (issue #610).
    // Scoped to active extractions via m_active — see processWeight().
    // Auto-resets after 3 consecutive rejections to handle legitimate shifts.
    double m_lastRawWeight = 0;
    bool m_hasLastWeight = false;
    int m_consecutiveRejections = 0;
    // Held from startExtraction()/resetForRetare() until the tare is observed to
    // have landed at the scale. The step from a loaded portafilter to zero is the
    // app's own doing, not corruption — see processWeight(). Cleared by a near-zero
    // sample or, at the latest, by markExtractionStart().
    bool m_awaitingTare = false;
    // A tared scale reads within a gram or two of zero; 5 g leaves room for drift
    // and a wet basket without being wide enough to swallow a real cup.
    static constexpr double kTareLandedThresholdG = 5.0;
    // Consecutive near-zero samples required before the tare is believed. Single-packet
    // corruption is the documented failure mode this filter exists for (#610), and we
    // support sixteen scale types whose behaviour around a tare is unmeasured — so one
    // packet must not be able to consume the exemption. Costs ~200 ms during preheat.
    static constexpr int kTareLandedConfirmations = 2;
    int m_tareLandedSamples = 0;

    // Scale-feed liveness (in-shot backstop). Evaluated on the DE1 tick so a
    // fully-silent scale is still detected. 2000ms mirrors the de-jitter
    // reconnect-gap value but is a distinct liveness threshold (kept separate
    // to avoid coupling de-jitter tuning to fault detection).
    static constexpr qint64 kScaleStaleMs = 2000;
    // Confirm threshold: a suspected stall only CONFIRMS (→ enforce may latch)
    // if it persists this long with no recovery. PROVISIONAL — final value is
    // calibrated from #1219 observe-mode field data (the recovered-gap
    // distribution: above the transient self-recovery cluster, below genuine
    // sustained stalls). Distinct from kScaleStaleMs so the suspected signal
    // (observe/diagnostics) and the latch trigger tune independently. Still a
    // DE1-tick-evaluated threshold, not a timer.
    static constexpr qint64 kScaleStallConfirmMs = 6000;
    bool m_scaleFeedStale = false;
    // True once the current stall has been confirmed (persisted past
    // kScaleStallConfirmMs unrecovered). Reset by recovery and every
    // extraction/cycle reset so a later independent stall re-confirms cleanly.
    bool m_scaleStallConfirmed = false;
    // Wall-clock of the last good sample before the current silent gap (set
    // when a stall is detected; 0 = no active stall). Drives scaleFeedResumed's
    // gap. Reset on the resume edge and on every extraction/cycle reset.
    qint64 m_feedStallStartMs = 0;

    // State
    bool m_active = false;
    // Espresso cycle is in the pre-shot EspressoPreheating phase (set from the
    // machine phase, distinct from m_active true-extraction). Widens the
    // liveness gate so a stall during warm-up is caught before the pour.
    bool m_preheatActive = false;
    bool m_tareComplete = false;
    bool m_stopTriggered = false;
    int m_currentFrame = -1;
    qint64 m_extractionStartTime = 0;

    // Oscillation recovery (e.g. Bookoo mid-shot tare reset)
    bool m_oscillationDetected = false;  // true while waiting for scale to re-settle after oscillation
    int m_settleCount = 0;               // consecutive near-zero readings since oscillation detected

    // De-jitter: compensates for main thread event batching (see processWeight comments)
    qint64 m_lastWallClockMs = 0;       // Wall-clock time of last processWeight() call
    qint64 m_lastSampleTs = 0;          // Last synthetic timestamp assigned to a sample
    // Calibrated from arrival RATE over a wall-clock window, counting every
    // arrival. 0 until the first window closes (~1 s) — there is deliberately no
    // single-gap seed, see processWeight().
    int m_estimatedIntervalMs = 0;
    bool m_uncalibratedBatchWarned = false;  // Throttle: log once per shot when fallback fires
    // Arrival-rate calibration window. Counts EVERY arrival, batched or not, so a
    // bursty transport reports its true cadence rather than its inter-burst gap —
    // the non-batched-gaps-only estimate this replaced inflated the interval on the
    // WiFi scale until synthetic timestamps outran wall-clock. See processWeight().
    qint64 m_rateWindowStartMs = 0;     // Wall-clock start of the current rate window
    int m_rateWindowCount = 0;          // Arrivals seen in the current rate window
    // Recent closed-window measurements, of which the MINIMUM is the estimate.
    // Dropped frames and sub-reconnect hiccups can only inflate a window's
    // measurement, never deflate it, so the smallest is the least contaminated.
    // Three windows is the shortest run that survives one bad window while still
    // following a genuine rate change within a few seconds. See processWeight().
    static constexpr int kRateRecentWindows = 3;
    int m_rateRecent[kRateRecentWindows] = {0, 0, 0};
    int m_rateRecentNext = 0;           // Ring write position
    int m_rateRecentCount = 0;          // Valid entries (< kRateRecentWindows while filling)
    // Arrivals deep into the current burst; sizes the synthetic lead allowance so
    // burst length is not silently capped. Reset by the first non-batched arrival.
    int m_burstFrames = 0;
    // Per-shot de-jitter observation counters. Pure diagnostics — nothing reads them
    // to make a decision, and they exist because every one of these questions had to
    // be inferred indirectly while diagnosing SAW going blind mid-pour: how often
    // arrivals batch, how deep the bursts get, what the interval estimate actually
    // did over a shot, and how many samples the flow estimate refused outright.
    // Reported once, at stopExtraction(); reset per shot.
    int m_djArrivals = 0;
    int m_djBatchedArrivals = 0;
    int m_djMaxBurstFrames = 0;
    int m_djIntervalMinMs = 0;
    int m_djIntervalMaxMs = 0;
    int m_djBlindSamples = 0;
    // One summary per SHOT. stopExtraction() runs on MachineState::shotEnded, which
    // fires on any flowing-to-not-flowing edge — steam, hot water and flush
    // included — so without this latch a flush after a shot logged a second summary
    // carrying that shot's counters over an elapsed time measured from its flow
    // start, indistinguishable in the log from a real one.
    bool m_djSummaryPending = false;
    // A closing rate window this far from the committed estimate (percent) is worth
    // a line. Loose enough that ordinary jitter stays silent.
    static constexpr int kRateDisagreementPct = 15;

    // Log throttle timestamps — reset each shot so warnings are never suppressed at shot start
    qint64 m_lastTareWarnMs = 0;
    qint64 m_lastLowFlowLogMs = 0;
    // Throttle for the #1176 constant-weight liveness diagnostic, logged off
    // the unconditional weightSampleReceived path. 0 = not logged this shot.
    qint64 m_lastConstantSampleLogMs = 0;
    bool m_flowBecameValidLogged = false;  // Log once when flowShort transitions 0→valid
    bool m_untaredCupSignalled = false;   // Fire untaredCupDetected only once per extraction
    // Count of consecutive samples currently satisfying the sanity check's
    // untared-cup condition (weight>50g, within the first 3s of extraction or a
    // streak already under way — see weightprocessor.cpp). 0 when no streak is
    // active. Reset on any sample that isn't consistent with a heavy cup,
    // including a spike-rejected sample that itself reads near zero, so a stale
    // reading can't revive a streak the real tare-confirmed zero already broke.
    // Event-based debounce (consecutive samples, not elapsed time) for the
    // untared-cup popup against a stale pre-tare-confirmation sample.
    int m_highWeightStreakSamples = 0;

    // Configuration (set at shot start; m_targetWeight may be updated mid-shot via setTargetWeight)
    double m_targetWeight = 0;
    int m_preinfuseFrameCount = 0;  // SAW suppressed until m_currentFrame >= this
    QVector<double> m_frameExitWeights;
    // Per-frame firmware exit conditions (parallel to m_frameExitWeights).
    // Empty/None entries mean the frame has no firmware exit → no arbitration.
    QVector<FrameExitCondition> m_frameExitConditions;
    // Latest cached firmware sensor readings (DE1 tick, ~5Hz) for the arbiter.
    double m_currentPressure = 0.0;
    double m_currentFlow = 0.0;

    // SAW learning data snapshot (filtered to current scale type at configure time)
    QVector<double> m_learningDrips;
    QVector<double> m_learningFlows;
    bool m_sawConverged = false;
    double m_sensorLagSeconds = 0.38;  // From SettingsCalibration::sensorLag() — used for first-shot default

    // Per-frame exit tracking (avoid duplicate skip commands)
    QSet<int> m_frameWeightSkipSent;

    // Arbitrates the tablet weight skip vs the firmware exit on mixed frames,
    // preventing a double frame-advance. Per-shot; reset at extraction start.
    StepExitArbiter m_stepExitArbiter;

    // Wall-clock source (injectable for testing — avoids 77s of QTest::qWait)
    std::function<qint64()> m_wallClock = [] { return QDateTime::currentMSecsSinceEpoch(); };
};
