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
//   - processWeight(): called at ~5Hz with each scale reading
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
    int m_estimatedIntervalMs = 0;      // Calibrated from non-batched gaps (0 = uncalibrated)
    bool m_uncalibratedBatchWarned = false;  // Throttle: log once per shot when fallback fires

    // Log throttle timestamps — reset each shot so warnings are never suppressed at shot start
    qint64 m_lastTareWarnMs = 0;
    qint64 m_lastLowFlowLogMs = 0;
    // Throttle for the #1176 constant-weight liveness diagnostic, logged off
    // the unconditional weightSampleReceived path. 0 = not logged this shot.
    qint64 m_lastConstantSampleLogMs = 0;
    bool m_flowBecameValidLogged = false;  // Log once when flowShort transitions 0→valid
    bool m_untaredCupSignalled = false;   // Fire untaredCupDetected only once per extraction

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
