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
    for (int i = 0; i < m_displayShots.size(); ++i) {
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

    if (m_shotIds.contains(shotId)) {
        return true;  // Already added
    }

    // Add and sort by shot ID (chronological order - older shots have lower IDs)
    m_shotIds.append(shotId);
    std::sort(m_shotIds.begin(), m_shotIds.end());

    // Update window start if needed (keep showing the same relative position)
    loadDisplayWindow();
    emit shotsChanged();
    return true;
}

void ShotComparisonModel::removeShot(qint64 shotId)
{
    int index = m_shotIds.indexOf(shotId);
    if (index >= 0) {
        m_shotIds.removeAt(index);
        // Adjust window start if needed
        int shotCount = static_cast<int>(m_shotIds.size());
        if (m_windowStart >= shotCount) {
            m_windowStart = std::max(0, shotCount - DISPLAY_WINDOW_SIZE);
        }
        loadDisplayWindow();
        emit shotsChanged();
        emit windowChanged();
    }
}

void ShotComparisonModel::clearAll()
{
    m_shotIds.clear();
    m_displayShots.clear();
    m_windowStart = 0;
    m_maxTime = 60.0;
    m_maxPressure = 12.0;
    m_maxFlow = 8.0;
    m_maxWeight = 50.0;
    emit shotsChanged();
    emit windowChanged();
}

void ShotComparisonModel::shiftWindowLeft()
{
    if (canShiftLeft()) {
        m_windowStart--;
        loadDisplayWindow();
        emit shotsChanged();  // Triggers graph/data refresh
        emit windowChanged();
    }
}

void ShotComparisonModel::shiftWindowRight()
{
    if (canShiftRight()) {
        m_windowStart++;
        loadDisplayWindow();
        emit shotsChanged();  // Triggers graph/data refresh
        emit windowChanged();
    }
}

void ShotComparisonModel::setWindowStart(int index)
{
    int maxStart = std::max(0, static_cast<int>(m_shotIds.size()) - DISPLAY_WINDOW_SIZE);
    int newStart = std::max(0, std::min(index, maxStart));
    if (newStart != m_windowStart) {
        m_windowStart = newStart;
        loadDisplayWindow();
        emit shotsChanged();  // Triggers graph/data refresh
        emit windowChanged();
    }
}

bool ShotComparisonModel::hasShotId(qint64 shotId) const
{
    return m_shotIds.contains(shotId);
}

void ShotComparisonModel::loadDisplayWindow()
{
    m_displayShots.clear();

    if (!m_storage || m_shotIds.isEmpty()) return;

    // Ensure window start is valid
    int shotCount = static_cast<int>(m_shotIds.size());
    if (m_windowStart < 0) m_windowStart = 0;
    if (m_windowStart >= shotCount) m_windowStart = std::max(0, shotCount - DISPLAY_WINDOW_SIZE);

    // Get the subset of shot IDs for the current window
    QList<qint64> windowIds;
    int windowEnd = std::min(m_windowStart + DISPLAY_WINDOW_SIZE, shotCount);
    for (int i = m_windowStart; i < windowEnd; ++i) {
        windowIds.append(m_shotIds[i]);
    }

    QList<ShotRecord> records = m_storage->getShotsForComparison(windowIds);

    for (const ShotRecord& record : records) {
        ComparisonShot shot;
        shot.id = record.summary.id;
        shot.profileName = record.summary.profileName;
        shot.beanBrand = record.summary.beanBrand;
        shot.beanType = record.summary.beanType;
        shot.roastDate = record.roastDate;
        shot.roastLevel = record.roastLevel;
        shot.grinderModel = record.grinderModel;
        shot.grinderSetting = record.grinderSetting;
        shot.duration = record.summary.duration;
        shot.doseWeight = record.summary.doseWeight;
        shot.finalWeight = record.summary.finalWeight;
        shot.drinkTds = record.drinkTds;
        shot.drinkEy = record.drinkEy;
        shot.enjoyment = record.summary.enjoyment;
        shot.timestamp = record.summary.timestamp;
        shot.notes = record.espressoNotes;
        shot.barista = record.barista;
        shot.temperatureOverride = record.temperatureOverride;
        shot.yieldOverride = record.yieldOverride;

        shot.pressure = record.pressure;
        shot.flow = record.flow;
        shot.temperature = record.temperature;
        shot.weight = record.weight;
        shot.weightFlowRate = record.weightFlowRate;
        shot.resistance = record.resistance;

        for (const auto& phase : record.phases) {
            ComparisonShot::PhaseMarker marker;
            marker.time = phase.time;
            marker.label = phase.label;
            marker.transitionReason = phase.transitionReason;
            shot.phases.append(marker);
        }

        m_displayShots.append(shot);
    }

    calculateMaxValues();
}

void ShotComparisonModel::calculateMaxValues()
{
    m_maxTime = 0.0;
    m_maxPressure = 12.0;
    m_maxFlow = 8.0;
    m_maxWeight = 50.0;

    for (const auto& shot : m_displayShots) {
        // Max time from duration
        if (shot.duration > m_maxTime) {
            m_maxTime = shot.duration;
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
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();
    return pointsToVariant(m_displayShots[index].pressure);
}

QVariantList ShotComparisonModel::getFlowData(int index) const
{
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();
    return pointsToVariant(m_displayShots[index].flow);
}

QVariantList ShotComparisonModel::getTemperatureData(int index) const
{
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();
    return pointsToVariant(m_displayShots[index].temperature);
}

QVariantList ShotComparisonModel::getWeightData(int index) const
{
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();
    return pointsToVariant(m_displayShots[index].weight);
}

QVariantList ShotComparisonModel::getWeightFlowRateData(int index) const
{
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();
    return pointsToVariant(m_displayShots[index].weightFlowRate);
}

QVariantList ShotComparisonModel::getResistanceData(int index) const
{
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();
    return pointsToVariant(m_displayShots[index].resistance);
}

QVariantList ShotComparisonModel::getPhaseMarkers(int index) const
{
    if (index < 0 || index >= m_displayShots.size()) return QVariantList();

    QVariantList result;
    for (const auto& phase : m_displayShots[index].phases) {
        QVariantMap p;
        p["time"] = phase.time;
        p["label"] = phase.label;
        p["transitionReason"] = phase.transitionReason;
        result.append(p);
    }
    return result;
}

QVariantMap ShotComparisonModel::getShotInfo(int index) const
{
    QVariantMap result;
    if (index < 0 || index >= m_displayShots.size()) return result;

    const auto& shot = m_displayShots[index];
    result["id"] = shot.id;
    result["profileName"] = shot.profileName;
    result["beanBrand"] = shot.beanBrand;
    result["beanType"] = shot.beanType;
    result["roastDate"] = shot.roastDate;
    result["roastLevel"] = shot.roastLevel;
    result["grinderModel"] = shot.grinderModel;
    result["grinderSetting"] = shot.grinderSetting;
    result["duration"] = shot.duration;
    result["doseWeight"] = shot.doseWeight;
    result["finalWeight"] = shot.finalWeight;
    result["drinkTds"] = shot.drinkTds;
    result["drinkEy"] = shot.drinkEy;
    result["enjoyment"] = shot.enjoyment;
    result["timestamp"] = shot.timestamp;
    result["notes"] = shot.notes;
    result["barista"] = shot.barista;
    result["temperatureOverride"] = shot.temperatureOverride;
    result["yieldOverride"] = shot.yieldOverride;

    // Format date
    QDateTime dt = QDateTime::fromSecsSinceEpoch(shot.timestamp);
    result["dateTime"] = dt.toString("MMM d, HH:mm");

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

QVariantMap ShotComparisonModel::getValuesAtTime(int index, double time) const
{
    QVariantMap result;
    if (index < 0 || index >= m_displayShots.size()) return result;

    const auto& shot = m_displayShots[index];

    // Returns the Y value of the nearest point within 1 second, or -1.0 if none.
    auto findNearest = [](const QVector<QPointF>& points, double t) -> double {
        if (points.isEmpty()) return -1.0;
        double closest = points[0].y();
        double minDist = std::abs(points[0].x() - t);
        for (const auto& pt : points) {
            double dist = std::abs(pt.x() - t);
            if (dist < minDist) {
                minDist = dist;
                closest = pt.y();
            } else if (dist > minDist) {
                break;  // data sorted by x
            }
        }
        return minDist < 1.0 ? closest : -1.0;
    };

    double pressure    = findNearest(shot.pressure, time);
    double flow        = findNearest(shot.flow, time);
    double temp        = findNearest(shot.temperature, time);
    double weight      = findNearest(shot.weight, time);
    double weightFlow  = findNearest(shot.weightFlowRate, time);
    double resistance  = findNearest(shot.resistance, time);

    result["hasPressure"]    = pressure    >= 0.0;
    result["hasFlow"]        = flow        >= 0.0;
    result["hasTemperature"] = temp        >= 0.0;
    result["hasWeight"]      = weight      >= 0.0;
    result["hasWeightFlow"]  = weightFlow  >= 0.0;
    result["hasResistance"]  = resistance  >= 0.0;
    result["pressure"]       = pressure;
    result["flow"]           = flow;
    result["temperature"]    = temp;
    result["weight"]         = weight;
    result["weightFlow"]     = weightFlow;
    result["resistance"]     = resistance;

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
