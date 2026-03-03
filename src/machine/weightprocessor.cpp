#include "weightprocessor.h"
#include <QtMath>
#include <QDebug>
#include <chrono>

namespace {
qint64 monotonicMsNow()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
}

WeightProcessor::WeightProcessor(QObject* parent)
    : QObject(parent)
{
}

void WeightProcessor::processWeight(double weight)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Record sample for LSLR (1-second rolling window)
    m_weightSamples.append({now, weight});
    while (!m_weightSamples.isEmpty() && (now - m_weightSamples.first().timestamp) > 1000) {
        m_weightSamples.removeFirst();
    }

    // Compute flow rates (always, even outside extraction — for QML display and settling)
    double flowRate = computeLSLR(1000);

    // Adaptive short window: ensure at least 3 samples are included regardless of
    // the scale's reporting rate. At 5Hz (Decent Scale) this stays at 500ms; at
    // 2Hz (Bookoo) it expands to ~1050ms so LSLR has 3 points instead of 2.
    // Without this, sparse scales flicker flowRateShort near 0 and SAW is gated out.
    int shortWindowMs = 500;
    if (m_weightSamples.size() >= 3) {
        qint64 spanOf3 = m_weightSamples.last().timestamp
                         - m_weightSamples[m_weightSamples.size() - 3].timestamp;
        shortWindowMs = qMax(shortWindowMs, (int)spanOf3 + 50);
    }
    double flowRateShort = computeLSLR(shortWindowMs);

    emit flowRatesReady(weight, flowRate, flowRateShort);

    // SOW and per-frame checks only during active extraction
    if (!m_active) return;

    // Bookoo (and any scale) tare oscillation guard: large negative swing during
    // extraction means the scale is mid-reset. Block SAW and enter recovery mode —
    // matching de1app's on_tare_seen pattern: clear the LSLR and wait for the scale
    // to return to ~0g before re-arming, so oscillation samples never corrupt SAW.
    if (m_tareComplete && weight < -5.0) {
        m_tareComplete = false;
        m_oscillationDetected = true;
        m_settleCount = 0;
        m_weightSamples.clear();  // Discard oscillation samples from LSLR
        qWarning() << "[SAW-Worker] Scale oscillation detected (weight=" << weight
                   << "g) - SAW blocked, awaiting settle";
    }

    // Mid-shot oscillation recovery: once scale returns to ~0g stably, re-arm SAW
    // with a clean LSLR baseline. Requires 3 consecutive near-zero readings (~0.6s
    // at 5Hz) to avoid re-arming while the scale is still mid-oscillation.
    // This mirrors de1app's _tare_awaiting_zero / on_tare_seen mechanism.
    if (!m_tareComplete && m_oscillationDetected) {
        if (qAbs(weight) < 2.0) {
            if (++m_settleCount >= 3) {
                m_tareComplete = true;
                m_oscillationDetected = false;
                m_settleCount = 0;
                m_weightSamples.clear();  // Fresh LSLR baseline from post-settle readings
                qDebug() << "[SAW-Worker] Scale settled after oscillation, SAW re-armed";
            }
        } else {
            m_settleCount = 0;  // Reset counter if weight leaves the near-zero band
        }
    }

    if (!m_tareComplete) {
        // Throttle warning to every 5s to avoid log spam at 5Hz
        static qint64 s_lastTareWarnMs = 0;
        if (now - s_lastTareWarnMs >= 5000) {
            qWarning() << "[SAW-Worker] Active but tare not complete - skipping SAW, weight=" << weight;
            s_lastTareWarnMs = now;
        }
        return;
    }

    // Sanity check: unreasonable weight early in extraction (likely untared cup)
    if (m_extractionStartTime > 0) {
        double extractionTime = (now - m_extractionStartTime) / 1000.0;
        if (extractionTime < 3.0 && weight > 50.0) return;
    }

    // Stop-at-weight check (requires valid flow rate for drip prediction)
    if (!m_stopTriggered && m_targetWeight > 0 && flowRateShort < 0.5) {
        // Throttle this log to every 5s
        static qint64 s_lastLowFlowLogMs = 0;
        if (now - s_lastLowFlowLogMs >= 5000) {
            qDebug() << "[SAW-Worker] Flow too low for SAW check: flowShort=" << flowRateShort
                     << "weight=" << weight << "target=" << m_targetWeight;
            s_lastLowFlowLogMs = now;
        }
    }
    if (!m_stopTriggered && m_targetWeight > 0 && flowRateShort >= 0.5) {
        double cappedFlow = qMin(flowRateShort, 12.0);
        double expectedDrip = getExpectedDrip(cappedFlow);
        double stopThreshold = m_targetWeight - expectedDrip;

        if (weight >= stopThreshold) {
            m_stopTriggered = true;
            qint64 triggerMs = monotonicMsNow();
            qDebug() << "[SAW-Worker] Stop triggered: weight=" << weight
                     << "threshold=" << stopThreshold
                     << "flow=" << flowRateShort << "(short)"
                     << "expectedDrip=" << expectedDrip
                     << "target=" << m_targetWeight;
            emit sawTriggered(weight, flowRateShort, m_targetWeight);
            emit stopNow(triggerMs);
        }
    }

    // Per-frame weight exit check
    if (m_currentFrame >= 0 && m_currentFrame < m_frameExitWeights.size()) {
        double exitWeight = m_frameExitWeights[m_currentFrame];
        if (exitWeight > 0 && weight >= exitWeight && !m_frameWeightSkipSent.contains(m_currentFrame)) {
            qDebug() << "[Weight-Worker] FRAME-WEIGHT EXIT: weight" << weight
                     << ">=" << exitWeight << "on frame" << m_currentFrame;
            m_frameWeightSkipSent.insert(m_currentFrame);
            emit skipFrame(m_currentFrame);
        }
    }
}

void WeightProcessor::configure(double targetWeight, QVector<double> frameExitWeights,
                                QVector<double> learningDrips, QVector<double> learningFlows,
                                bool sawConverged, double sensorLagSeconds)
{
    m_targetWeight = targetWeight;
    m_frameExitWeights = frameExitWeights;
    m_learningDrips = learningDrips;
    m_learningFlows = learningFlows;
    m_sawConverged = sawConverged;
    m_sensorLagSeconds = sensorLagSeconds;
}

void WeightProcessor::setCurrentFrame(int frameNumber)
{
    m_currentFrame = frameNumber;
}

void WeightProcessor::setTareComplete(bool complete)
{
    m_tareComplete = complete;
}

void WeightProcessor::startExtraction()
{
    m_active = true;
    m_stopTriggered = false;
    m_extractionStartTime = QDateTime::currentMSecsSinceEpoch();
    m_frameWeightSkipSent.clear();
    m_weightSamples.clear();
    m_currentFrame = -1;
    m_tareComplete = false;
    m_oscillationDetected = false;
    m_settleCount = 0;
}

void WeightProcessor::stopExtraction()
{
    m_active = false;

    // Log measured scale reporting rate — captured in shot debug log.
    // Helps diagnose SAW issues on slow-reporting scales (Bookoo ~2Hz, etc.).
    if (m_weightSamples.size() >= 3) {
        qint64 span = m_weightSamples.last().timestamp - m_weightSamples.first().timestamp;
        double avgIntervalMs = span / (double)(m_weightSamples.size() - 1);
        qDebug() << "[Weight-Worker] Scale interval: avg" << (int)avgIntervalMs << "ms"
                 << "(" << QString::number(1000.0 / avgIntervalMs, 'f', 1) << "Hz)"
                 << "over" << m_weightSamples.size() << "samples (last 1s)";
    }

    // Don't clear weight samples — settling still needs flow rate data
}

void WeightProcessor::resetForRetare()
{
    m_weightSamples.clear();
    m_extractionStartTime = QDateTime::currentMSecsSinceEpoch();
    m_stopTriggered = false;
    m_frameWeightSkipSent.clear();
    qDebug() << "[SAW-Worker] Reset for auto-retare";
}

double WeightProcessor::computeLSLR(int windowMs) const
{
    if (m_weightSamples.size() < 2) return 0.0;

    qint64 now = m_weightSamples.last().timestamp;
    qint64 cutoff = now - windowMs;

    // Find start of window
    int startIdx = m_weightSamples.size() - 1;
    while (startIdx > 0 && m_weightSamples[startIdx - 1].timestamp >= cutoff) {
        --startIdx;
    }

    int n = m_weightSamples.size() - startIdx;
    if (n < 2) return 0.0;

    double dt = (m_weightSamples.last().timestamp - m_weightSamples[startIdx].timestamp) / 1000.0;
    if (dt < (windowMs * 0.8 / 1000.0)) return 0.0;  // Wait until window is ~80% full

    // Least-squares linear regression: fits w = slope*t + intercept
    // slope = flow rate in g/s. Uses all samples in the window, averaging
    // out noise from scale quantization and BLE timing jitter.
    qint64 t0 = m_weightSamples[startIdx].timestamp;
    double sumT = 0, sumW = 0, sumTW = 0, sumTT = 0;
    for (int i = startIdx; i < m_weightSamples.size(); ++i) {
        double t = (m_weightSamples[i].timestamp - t0) / 1000.0;
        sumT += t;
        sumW += m_weightSamples[i].weight;
        sumTW += t * m_weightSamples[i].weight;
        sumTT += t * t;
    }
    double denom = n * sumTT - sumT * sumT;
    double slope = (denom > 1e-12) ? (n * sumTW - sumT * sumW) / denom : 0.0;

    return qMax(0.0, slope);
}

double WeightProcessor::getExpectedDrip(double currentFlowRate) const
{
    // Uses snapshot of SAW learning data taken at configure() time.
    // Algorithm matches Settings::getExpectedDrip — weighted average with
    // recency and flow-similarity weights.
    if (m_learningDrips.isEmpty()) {
        // No learning data — use scale-specific sensor lag as first-shot default.
        // Matches de1app: flow × (sensor_lag + 0.1s DE1 machine lag), capped at 8g.
        return qMin(currentFlowRate * (m_sensorLagSeconds + 0.1), 8.0);
    }

    int maxEntries = m_sawConverged ? 12 : 8;
    double recencyMax = 10.0;
    double recencyMin = m_sawConverged ? 3.0 : 1.0;

    int count = qMin(m_learningDrips.size(), maxEntries);
    double weightedDripSum = 0;
    double totalWeight = 0;

    for (int i = 0; i < count; ++i) {
        double drip = m_learningDrips[i];
        double flow = m_learningFlows[i];

        // Recency weight: linear interpolation from max to min
        double recencyWeight = recencyMax - i * (recencyMax - recencyMin) / qMax(1, count - 1);

        // Flow similarity: gaussian with sigma=1.5 ml/s
        double flowDiff = qAbs(flow - currentFlowRate);
        double flowWeight = qExp(-(flowDiff * flowDiff) / 4.5);  // sigma^2 * 2 = 4.5

        double w = recencyWeight * flowWeight;
        weightedDripSum += drip * w;
        totalWeight += w;
    }

    if (totalWeight < 0.01) {
        return qMin(currentFlowRate * (m_sensorLagSeconds + 0.1), 8.0);  // All entries have very different flow rates
    }

    return qBound(0.5, weightedDripSum / totalWeight, 20.0);
}
