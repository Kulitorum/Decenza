#pragma once

#include <QObject>
#include <QList>
#include <QPointF>
#include <QVariantList>

class ShotDataModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList pressureData READ pressureDataVariant NOTIFY dataChanged)
    Q_PROPERTY(QVariantList flowData READ flowDataVariant NOTIFY dataChanged)
    Q_PROPERTY(QVariantList temperatureData READ temperatureDataVariant NOTIFY dataChanged)
    Q_PROPERTY(QVariantList weightData READ weightDataVariant NOTIFY dataChanged)
    Q_PROPERTY(QVariantList flowRateData READ flowRateDataVariant NOTIFY dataChanged)

    Q_PROPERTY(double maxTime READ maxTime NOTIFY dataChanged)
    Q_PROPERTY(double maxPressure READ maxPressure NOTIFY dataChanged)
    Q_PROPERTY(double maxFlow READ maxFlow NOTIFY dataChanged)
    Q_PROPERTY(double maxWeight READ maxWeight NOTIFY dataChanged)

public:
    explicit ShotDataModel(QObject* parent = nullptr);

    QList<QPointF> pressureData() const { return m_pressureData; }
    QList<QPointF> flowData() const { return m_flowData; }
    QList<QPointF> temperatureData() const { return m_temperatureData; }
    QList<QPointF> weightData() const { return m_weightData; }
    QList<QPointF> flowRateData() const { return m_flowRateData; }

    QVariantList pressureDataVariant() const;
    QVariantList flowDataVariant() const;
    QVariantList temperatureDataVariant() const;
    QVariantList weightDataVariant() const;
    QVariantList flowRateDataVariant() const;

    double maxTime() const { return m_maxTime; }
    double maxPressure() const { return m_maxPressure; }
    double maxFlow() const { return m_maxFlow; }
    double maxWeight() const { return m_maxWeight; }

public slots:
    void clear();
    void addSample(double time, double pressure, double flow, double temperature);
    void addWeightSample(double time, double weight, double flowRate);

signals:
    void dataChanged();

private:
    QVariantList toVariantList(const QList<QPointF>& data) const;

    QList<QPointF> m_pressureData;
    QList<QPointF> m_flowData;
    QList<QPointF> m_temperatureData;
    QList<QPointF> m_weightData;
    QList<QPointF> m_flowRateData;

    double m_maxTime = 60.0;
    double m_maxPressure = 12.0;
    double m_maxFlow = 8.0;
    double m_maxWeight = 50.0;

    static const int MAX_SAMPLES = 600;  // 2 minutes at 5Hz
};
