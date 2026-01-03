#pragma once

#include <QObject>
#include <QVector>
#include <QPointF>
#include <QVariantList>
#include <QColor>

class ShotHistoryStorage;
struct ShotRecord;

// Model for comparing up to 3 shots
class ShotComparisonModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(int shotCount READ shotCount NOTIFY shotsChanged)
    Q_PROPERTY(QVariantList shots READ shotsVariant NOTIFY shotsChanged)
    Q_PROPERTY(double maxTime READ maxTime NOTIFY shotsChanged)
    Q_PROPERTY(double maxPressure READ maxPressure NOTIFY shotsChanged)
    Q_PROPERTY(double maxFlow READ maxFlow NOTIFY shotsChanged)
    Q_PROPERTY(double maxWeight READ maxWeight NOTIFY shotsChanged)

public:
    explicit ShotComparisonModel(QObject* parent = nullptr);

    void setStorage(ShotHistoryStorage* storage);

    int shotCount() const { return m_shotIds.size(); }
    QVariantList shotsVariant() const;
    double maxTime() const { return m_maxTime; }
    double maxPressure() const { return m_maxPressure; }
    double maxFlow() const { return m_maxFlow; }
    double maxWeight() const { return m_maxWeight; }

    // Add/remove shots to comparison (max 3)
    Q_INVOKABLE bool addShot(qint64 shotId);
    Q_INVOKABLE void removeShot(qint64 shotId);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE bool hasShotId(qint64 shotId) const;

    // Get data for specific shot (0, 1, or 2)
    Q_INVOKABLE QVariantList getPressureData(int index) const;
    Q_INVOKABLE QVariantList getFlowData(int index) const;
    Q_INVOKABLE QVariantList getTemperatureData(int index) const;
    Q_INVOKABLE QVariantList getWeightData(int index) const;
    Q_INVOKABLE QVariantList getPhaseMarkers(int index) const;

    // Get shot metadata
    Q_INVOKABLE QVariantMap getShotInfo(int index) const;

    // Colors for each shot in comparison (consistent assignment)
    Q_INVOKABLE QColor getShotColor(int index) const;
    Q_INVOKABLE QColor getShotColorLight(int index) const;  // For goal/secondary lines

signals:
    void shotsChanged();
    void errorOccurred(const QString& message);

private:
    void loadShotData();
    void calculateMaxValues();
    QVariantList pointsToVariant(const QVector<QPointF>& points) const;

    struct ComparisonShot {
        qint64 id = 0;
        QString profileName;
        QString beanBrand;
        QString beanType;
        double duration = 0;
        double doseWeight = 0;
        double finalWeight = 0;
        int enjoyment = 0;
        qint64 timestamp = 0;
        QString notes;

        QVector<QPointF> pressure;
        QVector<QPointF> flow;
        QVector<QPointF> temperature;
        QVector<QPointF> weight;

        struct PhaseMarker {
            double time = 0;
            QString label;
        };
        QList<PhaseMarker> phases;
    };

    ShotHistoryStorage* m_storage = nullptr;
    QList<qint64> m_shotIds;
    QList<ComparisonShot> m_shots;

    double m_maxTime = 60.0;
    double m_maxPressure = 12.0;
    double m_maxFlow = 8.0;
    double m_maxWeight = 50.0;

    static constexpr int MAX_COMPARISON_SHOTS = 3;
    static const QList<QColor> SHOT_COLORS;
    static const QList<QColor> SHOT_COLORS_LIGHT;
};
