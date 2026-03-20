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
    qint64 wallClock = QDateTime::currentMSecsSinceEpoch();

    // De-jitter: BLE events arrive on the main thread via QueuedConnection, and when
    // the main thread is busy (QML rendering), multiple events queue up and are
    // delivered in a burst. The worker thread processes them all within ~1ms, so
    // wall-clock timestamps cluster together. This makes LSLR see dt≈0 and return 0,
    // blinding SAW for the entire pour.
    //
    // Fix: detect batching (gap < 20ms) and assign synthetic timestamps spaced by
    // the calibrated scale interval. Non-batched events calibrate the interval via
    // exponential moving average, so this adapts to any scale rate (10Hz, 5Hz, 2Hz).
    constexpr qint64 kBatchThresholdMs = 20;    // Below this, events are batched
    constexpr qint64 kReconnectGapMs = 2000;    // Above this, ignore as reconnect
    constexpr double kEmaAlpha = 0.3;           // Smoothing factor for interval estimate

    qint64 sinceLast = m_lastWallClockMs > 0 ? (wallClock - m_lastWallClockMs) : -1;
    m_lastWallClockMs = wallClock;

    qint64 sampleTs;
    if (sinceLast < 0 || sinceLast > kBatchThresholdMs) {
        // First call or non-batched: use wall-clock as ground truth.
        // A previous batch may have pushed synthetic timestamps ahead of wall-clock.
        // Instead of clamping by a full interval (which causes runaway drift — each
        // clamp pushes synthetic further ahead, so subsequent events also get clamped),
        // use wall-clock directly and only enforce minimal monotonicity (+1ms).
        // This lets synthetic reconverge with wall-clock within a few samples.
        sampleTs = wallClock;
        if (m_lastSampleTs > 0 && sampleTs < m_lastSampleTs) {
            // Minimal advance: just +1ms to maintain monotonicity. This means synthetic
            // barely advances while wall-clock catches up, closing the gap naturally.
            sampleTs = m_lastSampleTs + 1;
        }
        if (sinceLast > kBatchThresholdMs && sinceLast < kReconnectGapMs) {
            // EMA calibration (ignores gaps > 2s as reconnects)
            if (m_estimatedIntervalMs == 0)
                m_estimatedIntervalMs = static_cast<int>(sinceLast);
            else
                m_estimatedIntervalMs = static_cast<int>((1.0 - kEmaAlpha) * m_estimatedIntervalMs + kEmaAlpha * sinceLast);
        }
    } else if (m_estimatedIntervalMs > 0) {
        // Batched (< 20ms since last) and calibrated: spread using estimated interval.
        // Cap synthetic so it can't drift more than 2 intervals ahead of wall-clock.
        // Without this cap, a burst of N batched events pushes synthetic N*interval ms
        // ahead, and subsequent non-batched events take many cycles to reconverge.
        sampleTs = m_lastSampleTs + m_estimatedIntervalMs;
        qint64 maxAhead = wallClock + m_estimatedIntervalMs * 2;
        if (sampleTs > maxAhead) {
            sampleTs = maxAhead;
        }
        // Enforce monotonicity after capping — cap could push below m_lastSampleTs.
        // Use estimated interval (not +1ms) to avoid near-zero dt in LSLR.
        if (sampleTs < m_lastSampleTs) {
            sampleTs = m_lastSampleTs + m_estimatedIntervalMs;
        }
    } else {
        // Batched but uncalibrated: use wall-clock (LSLR may see dt≈0 until calibrated)
        sampleTs = wallClock;
        if (m_active && !m_uncalibratedBatchWarned) {
            qWarning() << "[SAW-Worker] De-jitter: batched event before calibration"
                       << "— LSLR may return 0 until a non-batched gap is observed";
            m_uncalibratedBatchWarned = true;
        }
    }
    m_lastSampleTs = sampleTs;

    // Record sample for LSLR (1-second rolling window)
    m_weightSamples.append({sampleTs, weight});
    while (!m_weightSamples.isEmpty() && (sampleTs - m_weightSamples.first().timestamp) > 1000) {
        m_weightSamples.removeFirst();
    }

    // Compute flow rates (always, even outside extraction — for QML display and settling)
    double flowRate = computeLSLR(1000);

    // Adaptive short window: ensure at least 3 samples are covered regardless of
    // the scale's reporting rate. At 5Hz (Decent Scale) the span of 2 intervals is
    // 400ms; the 500ms floor dominates and the window stays at 500ms. At 2Hz (Bookoo)
    // the span of 2 intervals is ~1000ms, giving 1000ms+50ms, capped at 1000ms to
    // match the rolling buffer — covers 2–3 samples vs. only 1–2 at 500ms.
    // Without this, sparse scales flicker flowRateShort near 0 and SAW is gated out.
    int shortWindowMs = 500;
    if (m_weightSamples.size() >= 3) {
        qint64 spanOf3 = m_weightSamples.last().timestamp
                         - m_weightSamples[m_weightSamples.size() - 3].timestamp;
        shortWindowMs = qBound(500, static_cast<int>(spanOf3) + 50, 1000);
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
    // with a clean LSLR baseline. Requires 3 consecutive near-zero readings (~0.4s
    // at 5Hz, ~1.0s at 2Hz) to avoid re-arming while the scale is still mid-oscillation.
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
        if (wallClock - m_lastTareWarnMs >= 5000) {
            qWarning() << "[SAW-Worker] Active but tare not complete - skipping SAW, weight=" << weight;
            m_lastTareWarnMs = wallClock;
        }
        return;
    }

    // Sanity check: unreasonable weight early in extraction (likely untared cup)
    if (m_extractionStartTime > 0) {
        double extractionTime = (wallClock - m_extractionStartTime) / 1000.0;
        if (extractionTime < 3.0 && weight > 50.0) {
            qWarning() << "[SAW-Worker] Sanity check: weight" << weight
                       << "g at" << extractionTime << "s into extraction — skipping SAW (likely untared cup)";
            if (!m_untaredCupSignalled) {
                m_untaredCupSignalled = true;
                emit untaredCupDetected();
            }
            return;
        }
    }

    // Stop-at-weight check (requires valid flow rate for drip prediction)
    if (!m_stopTriggered && m_targetWeight > 0 && flowRateShort < 0.5) {
        // Throttle this log to every 5s, include LSLR diagnostic info
        if (wallClock - m_lastLowFlowLogMs >= 5000) {
            // Compute dt for the short window to diagnose why LSLR returns 0
            double shortDt = 0;
            if (m_weightSamples.size() >= 2) {
                qint64 cutoff = m_weightSamples.last().timestamp - shortWindowMs;
                qsizetype si = m_weightSamples.size() - 1;
                while (si > 0 && m_weightSamples[si - 1].timestamp >= cutoff) --si;
                shortDt = (m_weightSamples.last().timestamp - m_weightSamples[si].timestamp) / 1000.0;
            }
            qDebug() << "[SAW-Worker] Flow too low for SAW check: flowShort=" << flowRateShort
                     << "weight=" << weight << "target=" << m_targetWeight
                     << "samples=" << m_weightSamples.size()
                     << "shortWindow=" << shortWindowMs << "ms"
                     << "shortDt=" << QString::number(shortDt, 'f', 3) << "s"
                     << "gate=" << QString::number(shortWindowMs * 0.8 / 1000.0, 'f', 3) << "s";
            m_lastLowFlowLogMs = wallClock;
        }
    }
    if (!m_stopTriggered && m_targetWeight > 0 && flowRateShort >= 0.5) {
        // Log once when flow becomes valid — confirms de-jitter is working
        if (!m_flowBecameValidLogged) {
            m_flowBecameValidLogged = true;
            double extractionTime = (wallClock - m_extractionStartTime) / 1000.0;
            qDebug() << "[SAW-Worker] Flow became valid: flowShort=" << QString::number(flowRateShort, 'f', 2)
                     << "flowLong=" << QString::number(flowRate, 'f', 2)
                     << "weight=" << weight << "at" << QString::number(extractionTime, 'f', 1) << "s";
        }
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
    if (complete) {
        // Confirm tare clears any pending oscillation recovery — ensures SAW is
        // re-armed if called mid-shot (e.g. physical scale reconnects after a BLE drop).
        m_oscillationDetected = false;
        m_settleCount = 0;
    }
}

void WeightProcessor::startExtraction()
{
    m_active = true;
    m_stopTriggered = false;
    m_extractionStartTime = 0;  // Set later by markExtractionStart() when flow actually begins
    m_frameWeightSkipSent.clear();
    m_weightSamples.clear();
    m_currentFrame = -1;
    m_tareComplete = false;
    m_oscillationDetected = false;
    m_settleCount = 0;
    m_lastTareWarnMs = 0;
    m_lastLowFlowLogMs = 0;
    m_flowBecameValidLogged = false;
    m_untaredCupSignalled = false;
    m_lastWallClockMs = 0;
    m_lastSampleTs = 0;
    m_uncalibratedBatchWarned = false;
    // Keep m_estimatedIntervalMs — it calibrates across shots for the same scale
}

void WeightProcessor::markExtractionStart()
{
    m_extractionStartTime = QDateTime::currentMSecsSinceEpoch();
}

void WeightProcessor::stopExtraction()
{
    m_active = false;

    // Log measured scale reporting rate and de-jitter state — captured in shot debug log.
    // Helps diagnose SAW issues on slow-reporting scales (Bookoo ~2Hz, etc.).
    if (m_weightSamples.size() >= 3) {
        qint64 span = m_weightSamples.last().timestamp - m_weightSamples.first().timestamp;
        if (span > 0) {
            double avgIntervalMs = span / static_cast<double>(m_weightSamples.size() - 1);
            qDebug() << "[Weight-Worker] Scale interval: avg" << static_cast<int>(avgIntervalMs) << "ms"
                     << "(" << QString::number(1000.0 / avgIntervalMs, 'f', 1) << "Hz)"
                     << "over" << m_weightSamples.size() << "samples (last 1s)"
                     << "| de-jitter calibrated:" << m_estimatedIntervalMs << "ms";
        }
    }

    // Don't clear weight samples — logging block above reads them for rate diagnostics,
    // and settling still needs them for LSLR-based flow rate computation.
}

void WeightProcessor::resetForRetare()
{
    m_weightSamples.clear();
    m_extractionStartTime = 0;  // Will be set when extraction actually starts
    m_stopTriggered = false;
    m_frameWeightSkipSent.clear();
    m_lastWallClockMs = 0;
    m_lastSampleTs = 0;
    m_lastTareWarnMs = 0;
    m_lastLowFlowLogMs = 0;
    m_flowBecameValidLogged = false;
    qDebug() << "[SAW-Worker] Reset for auto-retare";
}

double WeightProcessor::computeLSLR(int windowMs) const
{
    if (m_weightSamples.size() < 2) return 0.0;

    qint64 now = m_weightSamples.last().timestamp;
    qint64 cutoff = now - windowMs;

    // Find start of window
    qsizetype startIdx = m_weightSamples.size() - 1;
    while (startIdx > 0 && m_weightSamples[startIdx - 1].timestamp >= cutoff) {
        --startIdx;
    }

    qsizetype n = m_weightSamples.size() - startIdx;
    if (n < 2) return 0.0;

    double dt = (m_weightSamples.last().timestamp - m_weightSamples[startIdx].timestamp) / 1000.0;
    if (dt < (windowMs * 0.8 / 1000.0)) return 0.0;  // Wait until window is ~80% full

    // Least-squares linear regression: fits w = slope*t + intercept
    // slope = flow rate in g/s. Uses all samples in the window, averaging
    // out noise from scale quantization and BLE timing jitter.
    qint64 t0 = m_weightSamples[startIdx].timestamp;
    double sumT = 0, sumW = 0, sumTW = 0, sumTT = 0;
    for (qsizetype i = startIdx; i < m_weightSamples.size(); ++i) {
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

    qsizetype count = qMin(m_learningDrips.size(), static_cast<qsizetype>(maxEntries));
    double weightedDripSum = 0;
    double totalWeight = 0;

    for (qsizetype i = 0; i < count; ++i) {
        double drip = m_learningDrips[i];
        double flow = m_learningFlows[i];

        // Recency weight: linear interpolation from max to min
        double recencyWeight = recencyMax - i * (recencyMax - recencyMin) / qMax(qsizetype(1), count - 1);

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
