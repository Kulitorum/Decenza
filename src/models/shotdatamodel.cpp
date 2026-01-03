#include "shotdatamodel.h"
#include <QDebug>

ShotDataModel::ShotDataModel(QObject* parent)
    : QObject(parent)
{
    // Pre-allocate vectors to avoid reallocations during shot
    m_pressurePoints.reserve(INITIAL_CAPACITY);
    m_flowPoints.reserve(INITIAL_CAPACITY);
    m_temperaturePoints.reserve(INITIAL_CAPACITY);
    m_temperatureGoalPoints.reserve(INITIAL_CAPACITY);
    m_weightPoints.reserve(INITIAL_CAPACITY);

    // Initialize first segments for goal curves
    m_pressureGoalSegments.append(QVector<QPointF>());
    m_pressureGoalSegments[0].reserve(INITIAL_CAPACITY);
    m_flowGoalSegments.append(QVector<QPointF>());
    m_flowGoalSegments[0].reserve(INITIAL_CAPACITY);

    // Timer for batched chart updates at 30fps
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(FLUSH_INTERVAL_MS);
    m_flushTimer->setTimerType(Qt::PreciseTimer);
    connect(m_flushTimer, &QTimer::timeout, this, &ShotDataModel::flushToChart);
}

ShotDataModel::~ShotDataModel() {
    if (m_flushTimer) {
        m_flushTimer->stop();
    }
}

void ShotDataModel::registerSeries(QLineSeries* pressure, QLineSeries* flow, QLineSeries* temperature,
                                    const QVariantList& pressureGoalSegments, const QVariantList& flowGoalSegments,
                                    QLineSeries* temperatureGoal,
                                    QLineSeries* weight, QLineSeries* extractionMarker,
                                    const QVariantList& frameMarkers) {
    m_pressureSeries = pressure;
    m_flowSeries = flow;
    m_temperatureSeries = temperature;
    m_temperatureGoalSeries = temperatureGoal;
    m_weightSeries = weight;
    m_extractionMarkerSeries = extractionMarker;

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

    // Enable OpenGL for hardware acceleration on main data series
    // Note: OpenGL can cause rendering issues on some Windows systems in debug mode
#if !defined(Q_OS_WIN) || !defined(QT_DEBUG)
    if (m_pressureSeries) m_pressureSeries->setUseOpenGL(true);
    if (m_flowSeries) m_flowSeries->setUseOpenGL(true);
    if (m_temperatureSeries) m_temperatureSeries->setUseOpenGL(true);
    if (m_weightSeries) m_weightSeries->setUseOpenGL(true);
    qDebug() << "ShotDataModel: Registered series with OpenGL acceleration";
#else
    qDebug() << "ShotDataModel: Registered series (OpenGL disabled for Windows debug)";
#endif

    // Start the flush timer
    m_flushTimer->start();
}

void ShotDataModel::clear() {
    // Stop timer during clear
    m_flushTimer->stop();

    // Clear data vectors (keep capacity)
    m_pressurePoints.clear();
    m_flowPoints.clear();
    m_temperaturePoints.clear();
    m_temperatureGoalPoints.clear();
    m_weightPoints.clear();
    m_pendingMarkers.clear();

    // Reset goal segments - keep first segment with capacity
    m_pressureGoalSegments.clear();
    m_pressureGoalSegments.append(QVector<QPointF>());
    m_pressureGoalSegments[0].reserve(INITIAL_CAPACITY);
    m_flowGoalSegments.clear();
    m_flowGoalSegments.append(QVector<QPointF>());
    m_flowGoalSegments[0].reserve(INITIAL_CAPACITY);

    // Clear chart series
    if (m_pressureSeries) m_pressureSeries->clear();
    if (m_flowSeries) m_flowSeries->clear();
    if (m_temperatureSeries) m_temperatureSeries->clear();
    if (m_temperatureGoalSeries) m_temperatureGoalSeries->clear();
    if (m_weightSeries) m_weightSeries->clear();
    if (m_extractionMarkerSeries) m_extractionMarkerSeries->clear();

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
    if (m_weightSeries) {
        m_weightSeries->clear();
    }
    qDebug() << "ShotDataModel: Cleared pre-tare weight data";
}

void ShotDataModel::addSample(double time, double pressure, double flow, double temperature,
                              double pressureGoal, double flowGoal, double temperatureGoal,
                              int frameNumber, bool isFlowMode) {
    Q_UNUSED(frameNumber);

    // Pure vector append - no signals, no chart updates
    m_pressurePoints.append(QPointF(time, pressure));
    m_flowPoints.append(QPointF(time, flow));
    m_temperaturePoints.append(QPointF(time, temperature));

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
    if (time > m_rawTime) {
        m_rawTime = time;
        emit rawTimeChanged();
    }

    m_dirty = true;
}

void ShotDataModel::addWeightSample(double time, double weight, double flowRate) {
    Q_UNUSED(flowRate);

    // Ignore near-zero weights (scale noise, tare values)
    // Only start recording when we have actual weight
    if (weight < 0.1) {
        return;
    }

    // Add initial zero point when weight curve starts (so line starts from zero at correct time)
    if (m_weightPoints.isEmpty()) {
        m_weightPoints.append(QPointF(time, 0.0));
    }

    // Scale weight to fit pressure axis (divide by 5)
    m_weightPoints.append(QPointF(time, weight / 5.0));
    m_dirty = true;
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

void ShotDataModel::addPhaseMarker(double time, const QString& label, int frameNumber, bool isFlowMode) {
    m_pendingMarkers.append({time, label});

    PhaseMarker marker;
    marker.time = time;
    marker.label = label;
    marker.frameNumber = frameNumber;
    marker.isFlowMode = isFlowMode;
    m_phaseMarkers.append(marker);

    m_dirty = true;
    emit phaseMarkersChanged();
}

void ShotDataModel::flushToChart() {
    if (!m_dirty) return;

    // Batch update all series with replace() - single redraw per series
    if (m_pressureSeries && !m_pressurePoints.isEmpty()) {
        m_pressureSeries->replace(m_pressurePoints);
    }
    if (m_flowSeries && !m_flowPoints.isEmpty()) {
        m_flowSeries->replace(m_flowPoints);
    }
    if (m_temperatureSeries && !m_temperaturePoints.isEmpty()) {
        m_temperatureSeries->replace(m_temperaturePoints);
    }

    // Update pressure goal segments - each segment gets its own LineSeries
    for (int i = 0; i < m_pressureGoalSegments.size() && i < m_pressureGoalSeriesList.size(); ++i) {
        if (m_pressureGoalSeriesList[i] && !m_pressureGoalSegments[i].isEmpty()) {
            m_pressureGoalSeriesList[i]->replace(m_pressureGoalSegments[i]);
        }
    }

    // Update flow goal segments - each segment gets its own LineSeries
    for (int i = 0; i < m_flowGoalSegments.size() && i < m_flowGoalSeriesList.size(); ++i) {
        if (m_flowGoalSeriesList[i] && !m_flowGoalSegments[i].isEmpty()) {
            m_flowGoalSeriesList[i]->replace(m_flowGoalSegments[i]);
        }
    }

    if (m_temperatureGoalSeries && !m_temperatureGoalPoints.isEmpty()) {
        m_temperatureGoalSeries->replace(m_temperatureGoalPoints);
    }
    if (m_weightSeries && !m_weightPoints.isEmpty()) {
        m_weightSeries->replace(m_weightPoints);
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

    m_dirty = false;
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
