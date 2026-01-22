#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>
#include <QPointF>
#include <QPointer>
#include <QVariantList>
#include <QtCharts/QLineSeries>

struct PhaseMarker {
    double time;
    QString label;
    int frameNumber;
    bool isFlowMode = false;  // true = flow control, false = pressure control
};

class ShotDataModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList phaseMarkers READ phaseMarkersVariant NOTIFY phaseMarkersChanged)
    Q_PROPERTY(double maxTime READ maxTime NOTIFY maxTimeChanged)
    Q_PROPERTY(double rawTime READ rawTime NOTIFY rawTimeChanged)

public:
    explicit ShotDataModel(QObject* parent = nullptr);
    ~ShotDataModel();

    double maxTime() const { return m_maxTime; }
    double rawTime() const { return m_rawTime; }
    QVariantList phaseMarkersVariant() const;

    // Register chart series - C++ takes ownership of updating them
    Q_INVOKABLE void registerSeries(QLineSeries* pressure, QLineSeries* flow, QLineSeries* temperature,
                                     const QVariantList& pressureGoalSegments, const QVariantList& flowGoalSegments,
                                     QLineSeries* temperatureGoal,
                                     QLineSeries* weight, QLineSeries* extractionMarker,
                                     const QVariantList& frameMarkers);

    // Data export for visualizer upload
    const QVector<QPointF>& pressureData() const { return m_pressurePoints; }
    const QVector<QPointF>& flowData() const { return m_flowPoints; }
    const QVector<QPointF>& temperatureData() const { return m_temperaturePoints; }
    QVector<QPointF> pressureGoalData() const;  // Combines all segments
    QVector<QPointF> flowGoalData() const;      // Combines all segments
    const QVector<QPointF>& temperatureGoalData() const { return m_temperatureGoalPoints; }
    const QVector<QPointF>& weightData() const { return m_weightPoints; }  // Cumulative weight (g) for graph
    const QVector<QPointF>& cumulativeWeightData() const { return m_cumulativeWeightPoints; }  // Cumulative weight for export

public slots:
    void clear();
    void clearWeightData();  // Clear only weight samples (call when tare completes)

    // Fast data ingestion - no chart updates, just vector append
    void addSample(double time, double pressure, double flow, double temperature,
                   double pressureGoal, double flowGoal, double temperatureGoal,
                   int frameNumber = -1, bool isFlowMode = false);
    void addWeightSample(double time, double weight, double flowRate);
    void addWeightSample(double time, double weight);  // Overload without flowRate (from ShotTimingController)
    void markExtractionStart(double time);
    void addPhaseMarker(double time, const QString& label, int frameNumber = -1, bool isFlowMode = false);

signals:
    void cleared();
    void maxTimeChanged();
    void rawTimeChanged();
    void phaseMarkersChanged();

private slots:
    void flushToChart();  // Called by timer - batched update to chart

private:
    // Data storage - fast vector appends
    QVector<QPointF> m_pressurePoints;
    QVector<QPointF> m_flowPoints;
    QVector<QPointF> m_temperaturePoints;
    QVector<QVector<QPointF>> m_pressureGoalSegments;  // Separate segments for clean breaks
    QVector<QVector<QPointF>> m_flowGoalSegments;      // Separate segments for clean breaks
    QVector<QPointF> m_temperatureGoalPoints;
    QVector<QPointF> m_weightPoints;  // Cumulative weight (g) - for graphing
    QVector<QPointF> m_cumulativeWeightPoints;  // Cumulative weight (g) - for export

    // Chart series pointers (QPointer auto-nulls when QML destroys them)
    QPointer<QLineSeries> m_pressureSeries;
    QPointer<QLineSeries> m_flowSeries;
    QPointer<QLineSeries> m_temperatureSeries;
    QList<QPointer<QLineSeries>> m_pressureGoalSeriesList;  // One per segment
    QList<QPointer<QLineSeries>> m_flowGoalSeriesList;      // One per segment
    QPointer<QLineSeries> m_temperatureGoalSeries;
    QPointer<QLineSeries> m_weightSeries;
    QPointer<QLineSeries> m_extractionMarkerSeries;
    QList<QPointer<QLineSeries>> m_frameMarkerSeries;

    // Batched update timer (30fps)
    QTimer* m_flushTimer = nullptr;
    bool m_dirty = false;

    double m_maxTime = 5.0;
    double m_rawTime = 0.0;
    int m_frameMarkerIndex = 0;
    bool m_lastPumpModeIsFlow = false;  // Track for starting new goal segments
    bool m_hasPumpModeData = false;     // True after first sample with pump mode
    int m_currentPressureGoalSegment = 0;  // Current segment index
    int m_currentFlowGoalSegment = 0;      // Current segment index

    // Phase markers for QML labels
    QList<PhaseMarker> m_phaseMarkers;
    QList<QPair<double, QString>> m_pendingMarkers;  // Pending vertical lines

    static constexpr int FLUSH_INTERVAL_MS = 33;  // ~30fps
    static constexpr int INITIAL_CAPACITY = 600;  // Pre-allocate for 2min at 5Hz
};
