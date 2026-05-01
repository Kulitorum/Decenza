#pragma once

#include <QObject>
#include <QThread>
#include <QList>
#include <QVector>
#include <QSet>
#include <QDateTime>
#include <functional>

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
                   QVector<double> learningDrips, QVector<double> learningFlows,
                   bool sawConverged, double sensorLagSeconds = 0.38);
    // Live SAW target update (e.g. user pressed +10g mid-shot). Writes are serialized
    // on the worker thread via QueuedConnection from main thread, so no extra locking.
    void setTargetWeight(double weight);
    void setCurrentFrame(int frameNumber);
    void setTareComplete(bool complete);
    void startExtraction();
    void markExtractionStart();  // Called when flow starts (idempotent, espresso-only)
    void stopExtraction();
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

private:
    double computeLSLR(int windowMs) const;
    double getExpectedDrip(double currentFlowRate) const;

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

    // State
    bool m_active = false;
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
    bool m_flowBecameValidLogged = false;  // Log once when flowShort transitions 0→valid
    bool m_untaredCupSignalled = false;   // Fire untaredCupDetected only once per extraction

    // Configuration (set at shot start; m_targetWeight may be updated mid-shot via setTargetWeight)
    double m_targetWeight = 0;
    int m_preinfuseFrameCount = 0;  // SAW suppressed until m_currentFrame >= this
    QVector<double> m_frameExitWeights;

    // SAW learning data snapshot (filtered to current scale type at configure time)
    QVector<double> m_learningDrips;
    QVector<double> m_learningFlows;
    bool m_sawConverged = false;
    double m_sensorLagSeconds = 0.38;  // From SettingsCalibration::sensorLag() — used for first-shot default

    // Per-frame exit tracking (avoid duplicate skip commands)
    QSet<int> m_frameWeightSkipSent;

    // Wall-clock source (injectable for testing — avoids 77s of QTest::qWait)
    std::function<qint64()> m_wallClock = [] { return QDateTime::currentMSecsSinceEpoch(); };
};
