#include "shotdatamodel.h"
#include "ai/conductance.h"
#include "rendering/fastlinerenderer.h"
#include <QDebug>

ShotDataModel::ShotDataModel(QObject* parent)
    : QObject(parent)
{
    // Pre-allocate vectors to avoid reallocations during shot
    m_pressurePoints.reserve(INITIAL_CAPACITY);
    m_flowPoints.reserve(INITIAL_CAPACITY);
    m_temperaturePoints.reserve(INITIAL_CAPACITY);
    m_temperatureMixPoints.reserve(INITIAL_CAPACITY);
    m_resistancePoints.reserve(INITIAL_CAPACITY);
    m_conductancePoints.reserve(INITIAL_CAPACITY);
    m_darcyResistancePoints.reserve(INITIAL_CAPACITY);
    m_waterDispensedPoints.reserve(INITIAL_CAPACITY);
    m_temperatureGoalPoints.reserve(INITIAL_CAPACITY);
    m_weightPoints.reserve(INITIAL_CAPACITY);
    m_cumulativeWeightPoints.reserve(INITIAL_CAPACITY);
    m_weightFlowRatePoints.reserve(INITIAL_CAPACITY);

    // Initialize first segments for goal curves
    m_pressureGoalSegments.append(QVector<QPointF>());
    m_pressureGoalSegments[0].reserve(INITIAL_CAPACITY);
    m_flowGoalSegments.append(QVector<QPointF>());
    m_flowGoalSegments[0].reserve(INITIAL_CAPACITY);

    // Chart update timer (~30fps) - batches data samples for efficient chart redraw
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(FLUSH_INTERVAL_MS);
    m_flushTimer->setTimerType(Qt::PreciseTimer);
    connect(m_flushTimer, &QTimer::timeout, this, &ShotDataModel::onFlushTimerTick);
}

ShotDataModel::~ShotDataModel() {
    if (m_flushTimer) {
        m_flushTimer->stop();
    }
}

void ShotDataModel::registerSeries(const QVariantList& pressureGoalSegments, const QVariantList& flowGoalSegments,
                                    QLineSeries* temperatureGoal,
                                    QLineSeries* extractionMarker,
                                    QLineSeries* stopMarker,
                                    const QVariantList& frameMarkers) {
    m_temperatureGoalSeries = temperatureGoal;
    m_extractionMarkerSeries = extractionMarker;
    m_stopMarkerSeries = stopMarker;

    // Register pressure goal segment series
    m_pressureGoalSeriesList.clear();
    for (const QVariant& v : pressureGoalSegments) {
        if (auto* series = qobject_cast<QLineSeries*>(v.value<QObject*>())) {
            m_pressureGoalSeriesList.append(series);
        }
    }

    // Register flow goal segment series
    m_flowGoalSeriesList.clear();
    for (const QVariant& v : flowGoalSegments) {
        if (auto* series = qobject_cast<QLineSeries*>(v.value<QObject*>())) {
            m_flowGoalSeriesList.append(series);
        }
    }

    m_frameMarkerSeries.clear();
    for (const QVariant& v : frameMarkers) {
        if (auto* series = qobject_cast<QLineSeries*>(v.value<QObject*>())) {
            m_frameMarkerSeries.append(series);
        }
    }

    qDebug() << "ShotDataModel: Registered goal/marker series";

    // If we have existing goal/marker data, flush it
    if (!m_pressureGoalSegments[0].isEmpty() || !m_pendingMarkers.isEmpty()) {
        m_dirty = true;
        onFlushTimerTick();
    }

    // Start the flush timer
    m_flushTimer->start();
}

void ShotDataModel::registerFastSeries(FastLineRenderer* pressure, FastLineRenderer* flow,
                                        FastLineRenderer* temperature,
                                        FastLineRenderer* weight, FastLineRenderer* weightFlow,
                                        FastLineRenderer* resistance,
                                        FastLineRenderer* conductance,
                                        FastLineRenderer* darcyResistance,
                                        FastLineRenderer* temperatureMix) {
    m_fastPressure = pressure;
    m_fastFlow = flow;
    m_fastTemperature = temperature;
    m_fastWeight = weight;
    m_fastWeightFlow = weightFlow;
    m_fastResistance = resistance;
    m_fastConductance = conductance;
    m_fastDarcyResistance = darcyResistance;
    m_fastTemperatureMix = temperatureMix;

    // Reset flush indices
    m_lastFlushedPressure = 0;
    m_lastFlushedFlow = 0;
    m_lastFlushedTemp = 0;
    m_lastFlushedWeight = 0;
    m_lastFlushedWeightFlow = 0;
    m_lastFlushedResistance = 0;
    m_lastFlushedConductance = 0;
    m_lastFlushedDarcyResistance = 0;
    m_lastFlushedTemperatureMix = 0;

    // Bulk-load any existing data (e.g., returning to espresso page after shot)
    if (!m_pressurePoints.isEmpty() || !m_flowPoints.isEmpty() || !m_weightPoints.isEmpty()) {
        qDebug() << "ShotDataModel: Populating fast renderers with existing data ("
                 << m_pressurePoints.size() << " pressure,"
                 << m_flowPoints.size() << " flow,"
                 << m_weightPoints.size() << " weight,"
                 << m_weightFlowRatePoints.size() << " weight flow)";
        if (m_fastPressure) m_fastPressure->setPoints(m_pressurePoints);
        if (m_fastFlow) m_fastFlow->setPoints(m_flowPoints);
        if (m_fastTemperature) m_fastTemperature->setPoints(m_temperaturePoints);
        if (m_fastWeight) m_fastWeight->setPoints(m_weightPoints);
        if (m_fastWeightFlow) m_fastWeightFlow->setPoints(m_weightFlowRatePoints);
        if (m_fastResistance) m_fastResistance->setPoints(m_resistancePoints);
        if (m_fastConductance) m_fastConductance->setPoints(m_conductancePoints);
        if (m_fastDarcyResistance) m_fastDarcyResistance->setPoints(m_darcyResistancePoints);
        if (m_fastTemperatureMix) m_fastTemperatureMix->setPoints(m_temperatureMixPoints);

        // Mark all as flushed
        m_lastFlushedPressure = m_pressurePoints.size();
        m_lastFlushedFlow = m_flowPoints.size();
        m_lastFlushedTemp = m_temperaturePoints.size();
        m_lastFlushedWeight = m_weightPoints.size();
        m_lastFlushedWeightFlow = m_weightFlowRatePoints.size();
        m_lastFlushedResistance = m_resistancePoints.size();
        m_lastFlushedConductance = m_conductancePoints.size();
        m_lastFlushedDarcyResistance = m_darcyResistancePoints.size();
        m_lastFlushedTemperatureMix = m_temperatureMixPoints.size();
    }

    qDebug() << "ShotDataModel: Registered fast renderers (QSGGeometryNode, pre-allocated VBO)";
}

void ShotDataModel::clear() {
    // Stop timer during clear
    m_flushTimer->stop();

    // Clear data vectors (keep capacity)
    m_pressurePoints.clear();
    m_flowPoints.clear();
    m_temperaturePoints.clear();
    m_temperatureMixPoints.clear();
    m_resistancePoints.clear();
    m_conductancePoints.clear();
    m_darcyResistancePoints.clear();
    m_conductanceDerivativePoints.clear();
    m_waterDispensedPoints.clear();
    m_temperatureGoalPoints.clear();
    m_weightPoints.clear();
    m_cumulativeWeightPoints.clear();
    m_weightFlowRatePoints.clear();
    m_weightFlowRateRawPoints.clear();
    m_pendingMarkers.clear();

    // Reset goal segments - keep first segment with capacity
    m_pressureGoalSegments.clear();
    m_pressureGoalSegments.append(QVector<QPointF>());
    m_pressureGoalSegments[0].reserve(INITIAL_CAPACITY);
    m_flowGoalSegments.clear();
    m_flowGoalSegments.append(QVector<QPointF>());
    m_flowGoalSegments[0].reserve(INITIAL_CAPACITY);

    // Clear fast renderers
    if (m_fastPressure) m_fastPressure->clear();
    if (m_fastFlow) m_fastFlow->clear();
    if (m_fastTemperature) m_fastTemperature->clear();
    if (m_fastWeight) m_fastWeight->clear();
    if (m_fastWeightFlow) m_fastWeightFlow->clear();
    if (m_fastResistance) m_fastResistance->clear();
    if (m_fastConductance) m_fastConductance->clear();
    if (m_fastDarcyResistance) m_fastDarcyResistance->clear();
    if (m_fastTemperatureMix) m_fastTemperatureMix->clear();
    m_lastFlushedPressure = 0;
    m_lastFlushedFlow = 0;
    m_lastFlushedTemp = 0;
    m_lastFlushedWeight = 0;
    m_lastFlushedWeightFlow = 0;
    m_lastFlushedResistance = 0;
    m_lastFlushedConductance = 0;
    m_lastFlushedDarcyResistance = 0;
    m_lastFlushedTemperatureMix = 0;

    // Clear goal/marker chart series
    if (m_temperatureGoalSeries) m_temperatureGoalSeries->clear();
    if (m_extractionMarkerSeries) m_extractionMarkerSeries->clear();
    if (m_stopMarkerSeries) m_stopMarkerSeries->clear();
    m_pendingStopTime = -1;
    m_stopTime = -1;
    m_weightAtStop = 0.0;

    // Clear all goal segment series
    for (const auto& series : m_pressureGoalSeriesList) {
        if (series) series->clear();
    }
    for (const auto& series : m_flowGoalSeriesList) {
        if (series) series->clear();
    }

    for (const auto& series : m_frameMarkerSeries) {
        if (series) series->clear();
    }

    m_frameMarkerIndex = 0;
    m_phaseMarkers.clear();
    m_maxTime = 5.0;
    m_rawTime = 0.0;
    m_lastPumpModeIsFlow = false;
    m_hasPumpModeData = false;
    m_currentPressureGoalSegment = 0;
    m_currentFlowGoalSegment = 0;
    m_dirty = false;
    m_rawTimeDirty = false;

    emit cleared();
    emit phaseMarkersChanged();
    emit maxTimeChanged();
    emit rawTimeChanged();

    // Restart timer
    m_flushTimer->start();
}

void ShotDataModel::clearWeightData() {
    // Clear any pre-tare weight samples (race condition fix)
    m_weightPoints.clear();
    m_cumulativeWeightPoints.clear();
    m_weightFlowRatePoints.clear();
    m_weightFlowRateRawPoints.clear();
    if (m_fastWeight) m_fastWeight->clear();
    if (m_fastWeightFlow) m_fastWeightFlow->clear();
    m_lastFlushedWeight = 0;
    m_lastFlushedWeightFlow = 0;
    qDebug() << "ShotDataModel: Cleared pre-tare weight data";
}

void ShotDataModel::addSample(double time, double pressure, double flow, double temperature,
                              double mixTemp,
                              double pressureGoal, double flowGoal, double temperatureGoal,
                              int frameNumber, bool isFlowMode) {
    Q_UNUSED(frameNumber);

    // Pure vector append - no signals, no chart updates
    m_pressurePoints.append(QPointF(time, pressure));
    m_flowPoints.append(QPointF(time, flow));
    m_temperaturePoints.append(QPointF(time, temperature));
    m_temperatureMixPoints.append(QPointF(time, mixTemp));

    // Resistance: pressure / flow (DSx2 formula), clamped to avoid spikes
    // during phase transitions when flow is near zero
    double resistance = 0.0;
    if (flow > 0.05) {
        resistance = qMin(pressure / flow, 15.0);
    }
    m_resistancePoints.append(QPointF(time, resistance));

    // Conductance: F^2 / P (Darcy's law for laminar flow through porous media)
    // Darcy Resistance: P / F^2 (inverse of conductance)
    // Both clamped to 19 to match Visualizer.coffee convention.
    // Conductance formula is shared with tools/shot_eval via Conductance::sample
    // so per-sample production behavior and batch offline evaluation agree.
    const double conductance = Conductance::sample(pressure, flow);
    double darcyResistance = 0.0;
    if (flow > 0.05 && pressure > 0.05) {
        darcyResistance = qMin(pressure / (flow * flow), 19.0);
    }
    m_conductancePoints.append(QPointF(time, conductance));
    m_darcyResistancePoints.append(QPointF(time, darcyResistance));

    // Water dispensed: cumulative flow integration (flow is ml/s)
    double waterDispensed = 0.0;
    if (!m_waterDispensedPoints.isEmpty()) {
        double lastWater = m_waterDispensedPoints.last().y();
        double lastTime = m_waterDispensedPoints.last().x();
        double dt = time - lastTime;
        if (dt > 0) {
            waterDispensed = lastWater + flow * dt;
        }
    }
    m_waterDispensedPoints.append(QPointF(time, waterDispensed));

    // Start new segments when pump mode changes (creates visual gap in goal curves)
    if (m_hasPumpModeData && isFlowMode != m_lastPumpModeIsFlow) {
        if (isFlowMode) {
            // Switching to flow mode: start new pressure goal segment
            m_currentPressureGoalSegment++;
            if (m_currentPressureGoalSegment >= m_pressureGoalSegments.size()) {
                m_pressureGoalSegments.append(QVector<QPointF>());
            }
        } else {
            // Switching to pressure mode: start new flow goal segment
            m_currentFlowGoalSegment++;
            if (m_currentFlowGoalSegment >= m_flowGoalSegments.size()) {
                m_flowGoalSegments.append(QVector<QPointF>());
            }
        }
    }
    m_lastPumpModeIsFlow = isFlowMode;
    m_hasPumpModeData = true;

    // Add goal points to current segments
    if (pressureGoal > 0) {
        m_pressureGoalSegments[m_currentPressureGoalSegment].append(QPointF(time, pressureGoal));
    }
    if (flowGoal > 0) {
        m_flowGoalSegments[m_currentFlowGoalSegment].append(QPointF(time, flowGoal));
    }
    m_temperatureGoalPoints.append(QPointF(time, temperatureGoal));

    // Update raw time - QML uses this to calculate axis max with pixel-based padding
    // Signal deferred to onFlushTimerTick() to avoid triggering chart axis recalc on every 5Hz sample
    if (time > m_rawTime) {
        m_rawTime = time;
        m_rawTimeDirty = true;
    }

    m_dirty = true;
    // Timer-driven: onFlushTimerTick() runs every 33ms (~30fps), batching samples
}

void ShotDataModel::addWeightSample(double time, double weight, double flowRate) {
    // Store weight flow rate for visualizer export (flow["by_weight"])
    // Clamp negative values to 0 (can occur from scale noise)
    m_weightFlowRatePoints.append(QPointF(time, qMax(0.0, flowRate)));
    addWeightSample(time, weight);
}

void ShotDataModel::addWeightSample(double time, double weight) {
    // Ignore near-zero weights (scale noise / pre-drip)
    if (weight < 0.1) {
        return;
    }

    // Spike filtering: reject readings that jump unrealistically from the last value
    // Max reasonable flow is ~5g/s, so anything faster is likely a scale glitch
    if (!m_weightPoints.isEmpty()) {
        double lastWeight = m_weightPoints.last().y();
        double lastTime = m_weightPoints.last().x();
        double deltaWeight = qAbs(weight - lastWeight);
        double deltaTime = time - lastTime;

        // Max allowed change rate: 10g/s (very generous to avoid false positives)
        // Minimum deltaTime of 0.05s to avoid division issues
        if (deltaTime > 0.05) {
            double changeRate = deltaWeight / deltaTime;
            if (changeRate > 10.0) {
                qWarning() << "ShotDataModel: Rejecting spike - weight:" << weight
                           << "lastWeight:" << lastWeight
                           << "deltaWeight:" << deltaWeight
                           << "deltaTime:" << deltaTime
                           << "rate:" << changeRate << "g/s";
                return;
            }
        }
    }

    // Store cumulative weight for export (visualizer, shot history)
    m_cumulativeWeightPoints.append(QPointF(time, weight));

    // Add initial zero point when weight curve starts (so line starts from zero at correct time)
    if (m_weightPoints.isEmpty()) {
        m_weightPoints.append(QPointF(time, 0.0));
    }

    // Plot cumulative weight (g) - shows weight progression during shot (0g -> 36g typical)
    m_weightPoints.append(QPointF(time, weight));
    m_dirty = true;
    // Timer-driven: onFlushTimerTick() runs every 33ms (~30fps), batching samples
    emit finalWeightChanged();  // For accessibility announcement
}

void ShotDataModel::markExtractionStart(double time) {
    m_pendingMarkers.append({time, "Start"});

    PhaseMarker marker;
    marker.time = time;
    marker.label = "Start";
    marker.frameNumber = 0;
    m_phaseMarkers.append(marker);

    m_dirty = true;
    emit phaseMarkersChanged();
}

void ShotDataModel::markStopAt(double time) {
    m_pendingStopTime = time;
    m_stopTime = time;

    // Find the weight at or just before the stop time
    m_weightAtStop = 0.0;
    for (qsizetype i = m_weightPoints.size() - 1; i >= 0; --i) {
        if (m_weightPoints[i].x() <= time) {
            m_weightAtStop = m_weightPoints[i].y();
            break;
        }
    }

    PhaseMarker marker;
    marker.time = time;
    marker.label = "End";
    marker.frameNumber = -1;
    m_phaseMarkers.append(marker);

    m_dirty = true;
    emit phaseMarkersChanged();
    emit stopTimeChanged();
    emit weightAtStopChanged();
}

void ShotDataModel::smoothWeightFlowRate(int window) {
    // Save raw copy before smoothing (for by_weight_raw export)
    m_weightFlowRateRawPoints = m_weightFlowRatePoints;

    qsizetype n = m_weightFlowRatePoints.size();
    if (n < 3) return;

    // Centered moving average: each point averages with `window` neighbors on each side.
    // With window=5 and ~5Hz data, this spans ~2.2s on top of the 1s LSLR recording window
    // and the real-time EMA smoothing. X values (timestamps) are preserved.
    QVector<QPointF> smoothed;
    smoothed.reserve(n);
    for (qsizetype i = 0; i < n; i++) {
        qsizetype lo = qMax(qsizetype(0), i - window);
        qsizetype hi = qMin(n - 1, i + window);
        double sum = 0;
        for (qsizetype j = lo; j <= hi; j++) {
            sum += m_weightFlowRatePoints[j].y();
        }
        smoothed.append(QPointF(m_weightFlowRatePoints[i].x(), sum / (hi - lo + 1)));
    }
    m_weightFlowRatePoints = smoothed;
}

void ShotDataModel::computeConductanceDerivative() {
    // Delegate to Conductance::derivative so ShotDataModel (per-sample live
    // data) and tools/shot_eval (batch offline data) share one formula —
    // keeps live-graph curves identical to offline-evaluation curves.
    m_conductanceDerivativePoints = Conductance::derivative(m_conductancePoints);
    qDebug() << "ShotDataModel: Computed conductance derivative ("
             << m_conductanceDerivativePoints.size() << " points)";
}

void ShotDataModel::trimSettlingData() {
    // Find the last sample with non-zero pressure — samples after this are from the
    // SAW settling period where the DE1 reports 0 pressure/flow while the scale settles.
    // De1app stops recording at the end of pouring substate; we trim at save time to
    // preserve live drip visualization during settling but produce clean history graphs.
    qsizetype trimIndex = m_pressurePoints.size();
    while (trimIndex > 0 && m_pressurePoints[trimIndex - 1].y() <= 0.0) {
        --trimIndex;
    }

    if (trimIndex >= m_pressurePoints.size()) {
        return;  // Nothing to trim
    }

    if (trimIndex == 0) {
        qWarning() << "[ShotDataModel] trimSettlingData: all" << m_pressurePoints.size()
                   << "samples have zero pressure — skipping trim to preserve data";
        return;
    }

    qsizetype removed = m_pressurePoints.size() - trimIndex;
    qDebug() << "[ShotDataModel] Trimming" << removed << "trailing zero-pressure settling samples"
             << "(keeping" << trimIndex << "of" << m_pressurePoints.size() << ")";

    // Trim sensor data series to the same length
    m_pressurePoints.resize(trimIndex);
    m_flowPoints.resize(qMin(m_flowPoints.size(), trimIndex));
    m_temperaturePoints.resize(qMin(m_temperaturePoints.size(), trimIndex));
    m_temperatureMixPoints.resize(qMin(m_temperatureMixPoints.size(), trimIndex));
    m_resistancePoints.resize(qMin(m_resistancePoints.size(), trimIndex));
    m_conductancePoints.resize(qMin(m_conductancePoints.size(), trimIndex));
    m_darcyResistancePoints.resize(qMin(m_darcyResistancePoints.size(), trimIndex));
    m_waterDispensedPoints.resize(qMin(m_waterDispensedPoints.size(), trimIndex));

    // Trim time-based series using cutoff from last retained pressure sample.
    // Goals and weight flow rate have different sample counts than DE1 sensor data.
    // (trimIndex is guaranteed > 0 by the early returns above)
    double cutoffTime = m_pressurePoints.last().x();
    for (auto& segment : m_pressureGoalSegments) {
        while (!segment.isEmpty() && segment.last().x() > cutoffTime)
            segment.removeLast();
    }
    for (auto& segment : m_flowGoalSegments) {
        while (!segment.isEmpty() && segment.last().x() > cutoffTime)
            segment.removeLast();
    }
    while (!m_temperatureGoalPoints.isEmpty() && m_temperatureGoalPoints.last().x() > cutoffTime)
        m_temperatureGoalPoints.removeLast();
    while (!m_weightFlowRatePoints.isEmpty() && m_weightFlowRatePoints.last().x() > cutoffTime)
        m_weightFlowRatePoints.removeLast();
    while (!m_weightFlowRateRawPoints.isEmpty() && m_weightFlowRateRawPoints.last().x() > cutoffTime)
        m_weightFlowRateRawPoints.removeLast();

    // Do NOT trim cumulative weight data (m_weightPoints, m_cumulativeWeightPoints) —
    // weight continues to change during settling and the settled final weight is accurate.
}

void ShotDataModel::addPhaseMarker(double time, const QString& label, int frameNumber, bool isFlowMode, const QString& transitionReason) {
    m_pendingMarkers.append({time, label});

    PhaseMarker marker;
    marker.time = time;
    marker.label = label;
    marker.frameNumber = frameNumber;
    marker.isFlowMode = isFlowMode;
    marker.transitionReason = transitionReason;
    m_phaseMarkers.append(marker);

    m_dirty = true;
    emit phaseMarkersChanged();
}

void ShotDataModel::onFlushTimerTick() {
    if (!m_dirty) return;

    // Incrementally append new points to fast renderers (pre-allocated VBO, no rebuild)
    if (m_fastPressure) {
        for (qsizetype i = m_lastFlushedPressure; i < m_pressurePoints.size(); ++i)
            m_fastPressure->appendPoint(m_pressurePoints[i].x(), m_pressurePoints[i].y());
        m_lastFlushedPressure = m_pressurePoints.size();
    }
    if (m_fastFlow) {
        for (qsizetype i = m_lastFlushedFlow; i < m_flowPoints.size(); ++i)
            m_fastFlow->appendPoint(m_flowPoints[i].x(), m_flowPoints[i].y());
        m_lastFlushedFlow = m_flowPoints.size();
    }
    if (m_fastTemperature) {
        for (qsizetype i = m_lastFlushedTemp; i < m_temperaturePoints.size(); ++i)
            m_fastTemperature->appendPoint(m_temperaturePoints[i].x(), m_temperaturePoints[i].y());
        m_lastFlushedTemp = m_temperaturePoints.size();
    }
    if (m_fastWeight) {
        for (qsizetype i = m_lastFlushedWeight; i < m_weightPoints.size(); ++i)
            m_fastWeight->appendPoint(m_weightPoints[i].x(), m_weightPoints[i].y());
        m_lastFlushedWeight = m_weightPoints.size();
    }
    if (m_fastWeightFlow) {
        for (qsizetype i = m_lastFlushedWeightFlow; i < m_weightFlowRatePoints.size(); ++i)
            m_fastWeightFlow->appendPoint(m_weightFlowRatePoints[i].x(), m_weightFlowRatePoints[i].y());
        m_lastFlushedWeightFlow = m_weightFlowRatePoints.size();
    }
    if (m_fastResistance) {
        for (qsizetype i = m_lastFlushedResistance; i < m_resistancePoints.size(); ++i)
            m_fastResistance->appendPoint(m_resistancePoints[i].x(), m_resistancePoints[i].y());
        m_lastFlushedResistance = m_resistancePoints.size();
    }
    if (m_fastConductance) {
        for (qsizetype i = m_lastFlushedConductance; i < m_conductancePoints.size(); ++i)
            m_fastConductance->appendPoint(m_conductancePoints[i].x(), m_conductancePoints[i].y());
        m_lastFlushedConductance = m_conductancePoints.size();
    }
    if (m_fastDarcyResistance) {
        for (qsizetype i = m_lastFlushedDarcyResistance; i < m_darcyResistancePoints.size(); ++i)
            m_fastDarcyResistance->appendPoint(m_darcyResistancePoints[i].x(), m_darcyResistancePoints[i].y());
        m_lastFlushedDarcyResistance = m_darcyResistancePoints.size();
    }
    if (m_fastTemperatureMix) {
        for (qsizetype i = m_lastFlushedTemperatureMix; i < m_temperatureMixPoints.size(); ++i)
            m_fastTemperatureMix->appendPoint(m_temperatureMixPoints[i].x(), m_temperatureMixPoints[i].y());
        m_lastFlushedTemperatureMix = m_temperatureMixPoints.size();
    }

    // Update goal curve LineSeries (infrequent updates, replace() is fine)
    for (qsizetype i = 0; i < m_pressureGoalSegments.size() && i < m_pressureGoalSeriesList.size(); ++i) {
        if (m_pressureGoalSeriesList[i] && !m_pressureGoalSegments[i].isEmpty()) {
            m_pressureGoalSeriesList[i]->replace(m_pressureGoalSegments[i]);
        }
    }
    for (qsizetype i = 0; i < m_flowGoalSegments.size() && i < m_flowGoalSeriesList.size(); ++i) {
        if (m_flowGoalSeriesList[i] && !m_flowGoalSegments[i].isEmpty()) {
            m_flowGoalSeriesList[i]->replace(m_flowGoalSegments[i]);
        }
    }
    if (m_temperatureGoalSeries && !m_temperatureGoalPoints.isEmpty()) {
        m_temperatureGoalSeries->replace(m_temperatureGoalPoints);
    }

    // Process pending vertical markers
    for (const auto& marker : m_pendingMarkers) {
        if (marker.second == "Start") {
            if (m_extractionMarkerSeries) {
                m_extractionMarkerSeries->append(marker.first, 0);
                m_extractionMarkerSeries->append(marker.first, 12);
            }
        } else {
            if (m_frameMarkerIndex < m_frameMarkerSeries.size()) {
                const auto& series = m_frameMarkerSeries[m_frameMarkerIndex];
                if (series) {
                    series->append(marker.first, 0);
                    series->append(marker.first, 12);
                }
                m_frameMarkerIndex++;
            }
        }
    }
    m_pendingMarkers.clear();

    // Draw stop marker if pending
    if (m_pendingStopTime >= 0 && m_stopMarkerSeries) {
        m_stopMarkerSeries->clear();  // Clear any existing line
        m_stopMarkerSeries->append(m_pendingStopTime, 0);
        m_stopMarkerSeries->append(m_pendingStopTime, 12);
        m_pendingStopTime = -1;  // Mark as drawn
    }

    // Emit deferred rawTimeChanged (axis recalc) at flush rate instead of per-sample
    if (m_rawTimeDirty) {
        m_rawTimeDirty = false;
        emit rawTimeChanged();
    }

    m_dirty = false;
    emit flushed();
}

double ShotDataModel::finalWeight() const {
    if (m_weightPoints.isEmpty()) return 0.0;
    return m_weightPoints.last().y();
}

QVariantList ShotDataModel::phaseMarkersVariant() const {
    QVariantList result;
    result.reserve(m_phaseMarkers.size());
    for (const PhaseMarker& marker : m_phaseMarkers) {
        QVariantMap map;
        map["time"] = marker.time;
        map["label"] = marker.label;
        map["frameNumber"] = marker.frameNumber;
        map["isFlowMode"] = marker.isFlowMode;
        map["transitionReason"] = marker.transitionReason;
        result.append(map);
    }
    return result;
}

QVector<QPointF> ShotDataModel::pressureGoalData() const {
    // Combine all segments for export
    QVector<QPointF> combined;
    for (const auto& segment : m_pressureGoalSegments) {
        combined.append(segment);
    }
    return combined;
}

QVector<QPointF> ShotDataModel::flowGoalData() const {
    // Combine all segments for export
    QVector<QPointF> combined;
    for (const auto& segment : m_flowGoalSegments) {
        combined.append(segment);
    }
    return combined;
}
