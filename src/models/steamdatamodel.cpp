#include "core/diagnosticlogging.h"
#include "steamdatamodel.h"
#include "rendering/fastlinerenderer.h"
#include "core/steamgraphvalidation.h"
#include <QDebug>
#include <algorithm>
#include <cmath>

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

QVariantList SteamDataModel::flowGoalPoints() const {
    QVariantList list;
    list.reserve(m_flowGoalPoints.size());
    for (const QPointF& p : m_flowGoalPoints) {
        list.append(QVariant::fromValue(p));
    }
    return list;
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

    DIAG_DEBUG(STEAM, "SteamDataModel") << "Registered fast renderers";
    m_flushTimer->start();
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

    m_flowGoalDirty = false;

    m_maxTime = 5.0;
    m_rawTime = 0.0;
    m_dirty = false;
    m_rawTimeDirty = false;
    const QPointer<SteamDataModel> guard(this);
    advanceRevision();
    if (!guard) return;

    emit cleared();
    if (!guard) return;
    emit maxTimeChanged();
    if (!guard) return;
    emit rawTimeChanged();
    if (!guard) return;
    emit flowGoalPointsChanged();
    if (!guard) return;

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
    advanceRevision();
}

void SteamDataModel::addFlowGoalPoint(double time, double flowGoal) {
    m_flowGoalPoints.append(QPointF(time, flowGoal));
    m_flowGoalDirty = true;
    m_dirty = true;
    advanceRevision();
}

bool SteamDataModel::replaceSnapshot(qint64 revision, double rawTime,
                                     const QVector<QPointF>& pressure,
                                     const QVector<QPointF>& flow,
                                     const QVector<QPointF>& temperature,
                                     const QVector<QPointF>& flowGoals)
{
    if (revision < 0 || revision > SteamGraphValidation::MaximumExactJsonInteger
        || !SteamGraphValidation::curve(pressure, SteamGraphValidation::MaximumSamples,
                                        QStringLiteral("pressure"))
        || !SteamGraphValidation::curve(flow, SteamGraphValidation::MaximumSamples,
                                        QStringLiteral("flow"))
        || !SteamGraphValidation::curve(temperature, SteamGraphValidation::MaximumSamples,
                                        QStringLiteral("temperature"))
        || !SteamGraphValidation::curve(flowGoals, SteamGraphValidation::MaximumGoalPoints,
                                        QStringLiteral("flow goals"))
        || !SteamGraphValidation::samples(rawTime, pressure, flow, temperature))
        return false;

    const bool goalsChanged = m_flowGoalPoints != flowGoals;
    const double nextMaxTime = std::max(5.0, rawTime);
    const bool maxChanged = m_maxTime != nextMaxTime;
    m_pressurePoints = pressure;
    m_flowPoints = flow;
    m_temperaturePoints = temperature;
    m_flowGoalPoints = flowGoals;
    m_rawTime = rawTime;
    m_maxTime = nextMaxTime;
    m_revision = revision;
    m_lastFlushedPressure = m_pressurePoints.size();
    m_lastFlushedFlow = m_flowPoints.size();
    m_lastFlushedTemp = m_temperaturePoints.size();
    m_flowGoalDirty = false;
    m_rawTimeDirty = false;
    m_dirty = false;

    // This is a complete replacement: attached renderers see either the old
    // graph or the entire new graph, never partial series from another revision.
    if (m_fastPressure) m_fastPressure->setPoints(m_pressurePoints);
    if (m_fastFlow) m_fastFlow->setPoints(m_flowPoints);
    if (m_fastTemperature) m_fastTemperature->setPoints(m_temperaturePoints);
    const QPointer<SteamDataModel> guard(this);
    emit revisionChanged();
    if (!guard) return true;
    if (maxChanged) emit maxTimeChanged();
    if (!guard) return true;
    emit rawTimeChanged();
    if (!guard) return true;
    if (goalsChanged) emit flowGoalPointsChanged();
    if (!guard) return true;
    emit flushed();
    return true;
}

bool SteamDataModel::appendSnapshot(qint64 baseRevision, qint64 revision, double rawTime,
                                    const QVector<QPointF>& pressure, const QVector<QPointF>& flow,
                                    const QVector<QPointF>& temperature,
                                    const QVector<QPointF>* flowGoals)
{
    if (baseRevision != m_revision || revision <= baseRevision
        || revision > SteamGraphValidation::MaximumExactJsonInteger
        || rawTime < m_rawTime
        || m_pressurePoints.size() + pressure.size() > SteamGraphValidation::MaximumSamples
        || !SteamGraphValidation::curve(pressure, SteamGraphValidation::MaximumSamples, {})
        || !SteamGraphValidation::curve(flow, SteamGraphValidation::MaximumSamples, {})
        || !SteamGraphValidation::curve(temperature, SteamGraphValidation::MaximumSamples, {})
        || !SteamGraphValidation::samples(rawTime, pressure, flow, temperature)
        || (!pressure.isEmpty() && !m_pressurePoints.isEmpty()
            && pressure.first().x() < m_pressurePoints.last().x())
        || (flowGoals && !SteamGraphValidation::curve(*flowGoals,
            SteamGraphValidation::MaximumGoalPoints, {}))) return false;
    const bool goalsChanged = flowGoals && m_flowGoalPoints != *flowGoals;
    const bool timeChanged = rawTime != m_rawTime || !pressure.isEmpty();
    const double nextMax = std::max(5.0, rawTime);
    const bool maxChanged = nextMax != m_maxTime;
    m_pressurePoints += pressure;
    m_flowPoints += flow;
    m_temperaturePoints += temperature;
    if (flowGoals) m_flowGoalPoints = *flowGoals;
    m_revision = revision;
    m_rawTime = rawTime;
    m_maxTime = nextMax;
    m_lastFlushedPressure = m_pressurePoints.size();
    m_lastFlushedFlow = m_flowPoints.size();
    m_lastFlushedTemp = m_temperaturePoints.size();
    m_dirty = m_rawTimeDirty = m_flowGoalDirty = false;
    const auto append = [](FastLineRenderer* renderer, const QVector<QPointF>& points) {
        if (renderer) for (const auto& point : points) renderer->appendPoint(point.x(), point.y());
    };
    append(m_fastPressure, pressure);
    append(m_fastFlow, flow);
    append(m_fastTemperature, temperature);
    const QPointer<SteamDataModel> guard(this);
    emit revisionChanged();
    if (!guard) return true;
    if (maxChanged) emit maxTimeChanged();
    if (!guard) return true;
    if (timeChanged) emit rawTimeChanged();
    if (!guard) return true;
    if (goalsChanged) emit flowGoalPointsChanged();
    if (!guard) return true;
    emit flushed();
    return true;
}

void SteamDataModel::advanceRevision()
{
    if (m_revision < SteamGraphValidation::MaximumExactJsonInteger)
        ++m_revision;
    emit revisionChanged();
}

void SteamDataModel::onFlushTimerTick() {
    if (!m_dirty) return;
    const QPointer<SteamDataModel> guard(this);

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

    // Goal points republished as a QML-bindable property — no series handshake needed.
    if (m_flowGoalDirty) {
        m_flowGoalDirty = false;
        emit flowGoalPointsChanged();
        if (!guard) return;
    }

    // Emit time change (deferred to flush tick to avoid per-sample signals)
    if (m_rawTimeDirty) {
        m_rawTimeDirty = false;
        if (m_rawTime > m_maxTime) {
            m_maxTime = m_rawTime;
            emit maxTimeChanged();
            if (!guard) return;
        }
        emit rawTimeChanged();
        if (!guard) return;
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
