#include "shotcomparisonmodel.h"
#include "../history/shothistorystorage.h"

#include <QDateTime>
#include <algorithm>

// Shot colors: Green, Blue, Orange
const QList<QColor> ShotComparisonModel::SHOT_COLORS = {
    QColor("#4CAF50"),  // Green
    QColor("#2196F3"),  // Blue
    QColor("#FF9800")   // Orange
};

const QList<QColor> ShotComparisonModel::SHOT_COLORS_LIGHT = {
    QColor("#81C784"),  // Light Green
    QColor("#64B5F6"),  // Light Blue
    QColor("#FFB74D")   // Light Orange
};

ShotComparisonModel::ShotComparisonModel(QObject* parent)
    : QObject(parent)
{
}

void ShotComparisonModel::setStorage(ShotHistoryStorage* storage)
{
    m_storage = storage;
}

QVariantList ShotComparisonModel::shotsVariant() const
{
    QVariantList result;
    for (int i = 0; i < m_shots.size(); ++i) {
        result.append(getShotInfo(i));
    }
    return result;
}

bool ShotComparisonModel::addShot(qint64 shotId)
{
    if (!m_storage) {
        emit errorOccurred("Storage not available");
        return false;
    }

    if (m_shotIds.size() >= MAX_COMPARISON_SHOTS) {
        emit errorOccurred("Maximum 3 shots can be compared");
        return false;
    }

    if (m_shotIds.contains(shotId)) {
        return true;  // Already added
    }

    m_shotIds.append(shotId);
    loadShotData();
    emit shotsChanged();
    return true;
}

void ShotComparisonModel::removeShot(qint64 shotId)
{
    int index = m_shotIds.indexOf(shotId);
    if (index >= 0) {
        m_shotIds.removeAt(index);
        loadShotData();
        emit shotsChanged();
    }
}

void ShotComparisonModel::clearAll()
{
    m_shotIds.clear();
    m_shots.clear();
    m_maxTime = 60.0;
    m_maxPressure = 12.0;
    m_maxFlow = 8.0;
    m_maxWeight = 50.0;
    emit shotsChanged();
}

bool ShotComparisonModel::hasShotId(qint64 shotId) const
{
    return m_shotIds.contains(shotId);
}

void ShotComparisonModel::loadShotData()
{
    m_shots.clear();

    if (!m_storage) return;

    QList<ShotRecord> records = m_storage->getShotsForComparison(m_shotIds);

    for (const ShotRecord& record : records) {
        ComparisonShot shot;
        shot.id = record.summary.id;
        shot.profileName = record.summary.profileName;
        shot.beanBrand = record.summary.beanBrand;
        shot.beanType = record.summary.beanType;
        shot.duration = record.summary.duration;
        shot.doseWeight = record.summary.doseWeight;
        shot.finalWeight = record.summary.finalWeight;
        shot.enjoyment = record.summary.enjoyment;
        shot.timestamp = record.summary.timestamp;
        shot.notes = record.espressoNotes;

        shot.pressure = record.pressure;
        shot.flow = record.flow;
        shot.temperature = record.temperature;
        shot.weight = record.weight;

        for (const auto& phase : record.phases) {
            ComparisonShot::PhaseMarker marker;
            marker.time = phase.time;
            marker.label = phase.label;
            shot.phases.append(marker);
        }

        m_shots.append(shot);
    }

    calculateMaxValues();
}

void ShotComparisonModel::calculateMaxValues()
{
    m_maxTime = 60.0;
    m_maxPressure = 12.0;
    m_maxFlow = 8.0;
    m_maxWeight = 50.0;

    for (const auto& shot : m_shots) {
        // Max time from duration
        if (shot.duration > m_maxTime) {
            m_maxTime = shot.duration + 5.0;
        }

        // Max pressure
        for (const auto& pt : shot.pressure) {
            if (pt.y() > m_maxPressure) {
                m_maxPressure = pt.y() + 2.0;
            }
        }

        // Max flow
        for (const auto& pt : shot.flow) {
            if (pt.y() > m_maxFlow) {
                m_maxFlow = pt.y() + 1.0;
            }
        }

        // Max weight
        for (const auto& pt : shot.weight) {
            if (pt.y() > m_maxWeight) {
                m_maxWeight = pt.y() + 10.0;
            }
        }
    }
}

QVariantList ShotComparisonModel::pointsToVariant(const QVector<QPointF>& points) const
{
    QVariantList result;
    for (const auto& pt : points) {
        QVariantMap p;
        p["x"] = pt.x();
        p["y"] = pt.y();
        result.append(p);
    }
    return result;
}

QVariantList ShotComparisonModel::getPressureData(int index) const
{
    if (index < 0 || index >= m_shots.size()) return QVariantList();
    return pointsToVariant(m_shots[index].pressure);
}

QVariantList ShotComparisonModel::getFlowData(int index) const
{
    if (index < 0 || index >= m_shots.size()) return QVariantList();
    return pointsToVariant(m_shots[index].flow);
}

QVariantList ShotComparisonModel::getTemperatureData(int index) const
{
    if (index < 0 || index >= m_shots.size()) return QVariantList();
    return pointsToVariant(m_shots[index].temperature);
}

QVariantList ShotComparisonModel::getWeightData(int index) const
{
    if (index < 0 || index >= m_shots.size()) return QVariantList();
    return pointsToVariant(m_shots[index].weight);
}

QVariantList ShotComparisonModel::getPhaseMarkers(int index) const
{
    if (index < 0 || index >= m_shots.size()) return QVariantList();

    QVariantList result;
    for (const auto& phase : m_shots[index].phases) {
        QVariantMap p;
        p["time"] = phase.time;
        p["label"] = phase.label;
        result.append(p);
    }
    return result;
}

QVariantMap ShotComparisonModel::getShotInfo(int index) const
{
    QVariantMap result;
    if (index < 0 || index >= m_shots.size()) return result;

    const auto& shot = m_shots[index];
    result["id"] = shot.id;
    result["profileName"] = shot.profileName;
    result["beanBrand"] = shot.beanBrand;
    result["beanType"] = shot.beanType;
    result["duration"] = shot.duration;
    result["doseWeight"] = shot.doseWeight;
    result["finalWeight"] = shot.finalWeight;
    result["enjoyment"] = shot.enjoyment;
    result["timestamp"] = shot.timestamp;
    result["notes"] = shot.notes;

    // Format date
    QDateTime dt = QDateTime::fromSecsSinceEpoch(shot.timestamp);
    result["dateTime"] = dt.toString("MMM d, hh:mm");

    // Ratio
    if (shot.doseWeight > 0) {
        result["ratio"] = QString("1:%1").arg(shot.finalWeight / shot.doseWeight, 0, 'f', 1);
    } else {
        result["ratio"] = "-";
    }

    // Color
    result["color"] = getShotColor(index);

    return result;
}

QColor ShotComparisonModel::getShotColor(int index) const
{
    if (index < 0 || index >= SHOT_COLORS.size()) {
        return QColor("#888888");
    }
    return SHOT_COLORS[index];
}

QColor ShotComparisonModel::getShotColorLight(int index) const
{
    if (index < 0 || index >= SHOT_COLORS_LIGHT.size()) {
        return QColor("#AAAAAA");
    }
    return SHOT_COLORS_LIGHT[index];
}
