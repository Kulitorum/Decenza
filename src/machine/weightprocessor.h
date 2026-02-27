#pragma once

#include <QObject>
#include <QList>
#include <QVector>
#include <QSet>
#include <QDateTime>

// Runs on a dedicated worker thread. Receives weight samples from the scale,
// computes LSLR flow rates, and makes SAW/per-frame-exit decisions
// independently of main thread congestion.
//
// Input (via QueuedConnection from main thread):
//   - processWeight(): called at ~5Hz with each scale reading
//   - configure(): called once at shot start with targets and learning data
//   - setCurrentFrame(): called at ~5Hz from DE1 shot samples
//
// Output:
//   - stopNow(): triggers DE1Device::writeUrgentStop() via DirectConnection
//     (executes immediately on this worker thread, bypasses main thread)
//   - sawTriggered(): carries context for SAW learning (QueuedConnection to main)
//   - skipFrame(): triggers DE1Device::skipToNextFrame() (QueuedConnection to main)
//   - flowRatesReady(): feeds ShotTimingController for graph/settling

class WeightProcessor : public QObject {
    Q_OBJECT

public:
    explicit WeightProcessor(QObject* parent = nullptr);

public slots:
    // Called from main thread (all via QueuedConnection — thread-safe)
    void processWeight(double weight);
    void configure(double targetWeight, QVector<double> frameExitWeights,
                   QVector<double> learningDrips, QVector<double> learningFlows,
                   bool sawConverged);
    void setCurrentFrame(int frameNumber);
    void setTareComplete(bool complete);
    void startExtraction();
    void stopExtraction();

signals:
    void stopNow();
    // Carries SAW context for learning (weight/flow at stop time)
    void sawTriggered(double weightAtStop, double flowRateAtStop, double targetWeight);
    void skipFrame(int frameNumber);
    void flowRatesReady(double weight, double flowRate, double flowRateShort);

private:
    double computeLSLR(int windowMs) const;
    double getExpectedDrip(double currentFlowRate) const;

    // Weight sample buffer (1-second rolling window for LSLR)
    struct WeightSample {
        qint64 timestamp;
        double weight;
    };
    QList<WeightSample> m_weightSamples;

    // State
    bool m_active = false;
    bool m_tareComplete = false;
    bool m_stopTriggered = false;
    int m_currentFrame = -1;
    qint64 m_extractionStartTime = 0;

    // Configuration (set once at shot start, read-only during extraction)
    double m_targetWeight = 0;
    QVector<double> m_frameExitWeights;

    // SAW learning data snapshot (filtered to current scale type at configure time)
    QVector<double> m_learningDrips;
    QVector<double> m_learningFlows;
    bool m_sawConverged = false;

    // Per-frame exit tracking (avoid duplicate skip commands)
    QSet<int> m_frameWeightSkipSent;
};
