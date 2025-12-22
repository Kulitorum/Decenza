#include "shotdatamodel.h"

ShotDataModel::ShotDataModel(QObject* parent)
    : QObject(parent)
{
}

void ShotDataModel::clear() {
    m_pressureData.clear();
    m_flowData.clear();
    m_temperatureData.clear();
    m_weightData.clear();
    m_flowRateData.clear();
    emit dataChanged();
}

void ShotDataModel::addSample(double time, double pressure, double flow, double temperature) {
    // Limit data size
    if (m_pressureData.size() >= MAX_SAMPLES) {
        m_pressureData.removeFirst();
        m_flowData.removeFirst();
        m_temperatureData.removeFirst();
    }

    m_pressureData.append(QPointF(time, pressure));
    m_flowData.append(QPointF(time, flow));
    m_temperatureData.append(QPointF(time, temperature));

    // Update max values
    if (time > m_maxTime) m_maxTime = time + 10;
    if (pressure > m_maxPressure) m_maxPressure = pressure + 1;
    if (flow > m_maxFlow) m_maxFlow = flow + 1;

    emit dataChanged();
}

void ShotDataModel::addWeightSample(double time, double weight, double flowRate) {
    // Limit data size
    if (m_weightData.size() >= MAX_SAMPLES) {
        m_weightData.removeFirst();
        m_flowRateData.removeFirst();
    }

    m_weightData.append(QPointF(time, weight));
    m_flowRateData.append(QPointF(time, flowRate));

    // Update max weight
    if (weight > m_maxWeight) m_maxWeight = weight + 10;

    emit dataChanged();
}

QVariantList ShotDataModel::toVariantList(const QList<QPointF>& data) const {
    QVariantList result;
    for (const QPointF& point : data) {
        QVariantMap map;
        map["x"] = point.x();
        map["y"] = point.y();
        result.append(map);
    }
    return result;
}

QVariantList ShotDataModel::pressureDataVariant() const {
    return toVariantList(m_pressureData);
}

QVariantList ShotDataModel::flowDataVariant() const {
    return toVariantList(m_flowData);
}

QVariantList ShotDataModel::temperatureDataVariant() const {
    return toVariantList(m_temperatureData);
}

QVariantList ShotDataModel::weightDataVariant() const {
    return toVariantList(m_weightData);
}

QVariantList ShotDataModel::flowRateDataVariant() const {
    return toVariantList(m_flowRateData);
}
