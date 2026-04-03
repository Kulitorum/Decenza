#include "steamdatamodel.h"
#include "rendering/fastlinerenderer.h"
#include <QDebug>
#include <algorithm>

SteamDataModel::SteamDataModel(QObject* parent)
    : QObject(parent)
{
    m_pressurePoints.reserve(INITIAL_CAPACITY);
    m_flowPoints.reserve(INITIAL_CAPACITY);
    m_temperaturePoints.reserve(INITIAL_CAPACITY);
    m_flowGoalPoints.reserve(4);

    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(FLUSH_INTERVAL_MS);
    m_flushTimer->setTimerType(Qt::PreciseTimer);
    connect(m_flushTimer, &QTimer::timeout, this, &SteamDataModel::onFlushTimerTick);
}

SteamDataModel::~SteamDataModel() {
    if (m_flushTimer) {
        m_flushTimer->stop();
    }
}

void SteamDataModel::registerFastSeries(FastLineRenderer* pressure, FastLineRenderer* flow,
                                          FastLineRenderer* temperature) {
    m_fastPressure = pressure;
    m_fastFlow = flow;
    m_fastTemperature = temperature;

    m_lastFlushedPressure = 0;
    m_lastFlushedFlow = 0;
    m_lastFlushedTemp = 0;

    // Bulk-load any existing data (e.g., returning to steam page mid-session)
    if (!m_pressurePoints.isEmpty()) {
        if (m_fastPressure) m_fastPressure->setPoints(m_pressurePoints);
        if (m_fastFlow) m_fastFlow->setPoints(m_flowPoints);
        if (m_fastTemperature) m_fastTemperature->setPoints(m_temperaturePoints);

        m_lastFlushedPressure = m_pressurePoints.size();
        m_lastFlushedFlow = m_flowPoints.size();
        m_lastFlushedTemp = m_temperaturePoints.size();
    }

    qDebug() << "SteamDataModel: Registered fast renderers";
    m_flushTimer->start();
}

void SteamDataModel::registerGoalSeries(QLineSeries* flowGoal) {
    m_flowGoalSeries = flowGoal;

    // Flush any existing goal data
    if (m_flowGoalSeries && !m_flowGoalPoints.isEmpty()) {
        m_flowGoalSeries->replace(m_flowGoalPoints);
    }
}

void SteamDataModel::clear() {
    m_flushTimer->stop();

    m_pressurePoints.clear();
    m_flowPoints.clear();
    m_temperaturePoints.clear();
    m_flowGoalPoints.clear();

    if (m_fastPressure) m_fastPressure->clear();
    if (m_fastFlow) m_fastFlow->clear();
    if (m_fastTemperature) m_fastTemperature->clear();
    m_lastFlushedPressure = 0;
    m_lastFlushedFlow = 0;
    m_lastFlushedTemp = 0;

    if (m_flowGoalSeries) m_flowGoalSeries->clear();
    m_flowGoalDirty = false;

    m_maxTime = 5.0;
    m_rawTime = 0.0;
    m_dirty = false;
    m_rawTimeDirty = false;

    emit cleared();
    emit maxTimeChanged();
    emit rawTimeChanged();

    m_flushTimer->start();
}

void SteamDataModel::addSample(double time, double pressure, double flow, double temperature) {
    m_pressurePoints.append(QPointF(time, pressure));
    m_flowPoints.append(QPointF(time, flow));
    m_temperaturePoints.append(QPointF(time, temperature));

    if (time > m_rawTime) {
        m_rawTime = time;
        m_rawTimeDirty = true;
    }

    m_dirty = true;
}

void SteamDataModel::addFlowGoalPoint(double time, double flowGoal) {
    m_flowGoalPoints.append(QPointF(time, flowGoal));
    m_flowGoalDirty = true;
    m_dirty = true;
}

void SteamDataModel::onFlushTimerTick() {
    if (!m_dirty) return;

    // Incrementally append new points to fast renderers
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

    // Update flow goal LineSeries
    if (m_flowGoalDirty && m_flowGoalSeries && !m_flowGoalPoints.isEmpty()) {
        m_flowGoalSeries->replace(m_flowGoalPoints);
        m_flowGoalDirty = false;
    }

    // Emit time change (deferred to flush tick to avoid per-sample signals)
    if (m_rawTimeDirty) {
        m_rawTimeDirty = false;
        if (m_rawTime > m_maxTime) {
            m_maxTime = m_rawTime;
            emit maxTimeChanged();
        }
        emit rawTimeChanged();
    }

    m_dirty = false;
    emit flushed();
}

// --- Session summary accessors (skip first TRIM_SECONDS) ---

double SteamDataModel::averagePressure() const {
    double sum = 0.0;
    int count = 0;
    for (const auto& pt : m_pressurePoints) {
        if (pt.x() >= TRIM_SECONDS) {
            sum += pt.y();
            ++count;
        }
    }
    return count > 0 ? sum / count : 0.0;
}

double SteamDataModel::peakPressure() const {
    double peak = 0.0;
    for (const auto& pt : m_pressurePoints) {
        if (pt.x() >= TRIM_SECONDS) {
            peak = std::max(peak, pt.y());
        }
    }
    return peak;
}

double SteamDataModel::averageTemperature() const {
    double sum = 0.0;
    int count = 0;
    for (const auto& pt : m_temperaturePoints) {
        if (pt.x() >= TRIM_SECONDS) {
            sum += pt.y();
            ++count;
        }
    }
    return count > 0 ? sum / count : 0.0;
}

double SteamDataModel::peakTemperature() const {
    double peak = 0.0;
    for (const auto& pt : m_temperaturePoints) {
        if (pt.x() >= TRIM_SECONDS) {
            peak = std::max(peak, pt.y());
        }
    }
    return peak;
}
