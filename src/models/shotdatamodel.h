#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>
#include <QPointF>
#include <QPointer>
#include <QVariantList>
#include <QtCharts/QLineSeries>

class FastLineRenderer;

struct PhaseMarker {
    double time;
    QString label;
    int frameNumber;
    bool isFlowMode = false;  // true = flow control, false = pressure control
    QString transitionReason;  // "weight", "pressure", "flow", "time", or "" (unknown/old data)
};

class ShotDataModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList phaseMarkers READ phaseMarkersVariant NOTIFY phaseMarkersChanged)
    Q_PROPERTY(double maxTime READ maxTime NOTIFY maxTimeChanged)
    Q_PROPERTY(double rawTime READ rawTime NOTIFY rawTimeChanged)
    Q_PROPERTY(double stopTime READ stopTime NOTIFY stopTimeChanged)
    Q_PROPERTY(double weightAtStop READ weightAtStop NOTIFY weightAtStopChanged)
    Q_PROPERTY(double finalWeight READ finalWeight NOTIFY finalWeightChanged)

public:
    explicit ShotDataModel(QObject* parent = nullptr);
    ~ShotDataModel();

    double maxTime() const { return m_maxTime; }
    double rawTime() const { return m_rawTime; }
    double stopTime() const { return m_stopTime; }
    double weightAtStop() const { return m_weightAtStop; }
    double finalWeight() const;
    QVariantList phaseMarkersVariant() const;

    // Register goal/marker chart series (LineSeries - infrequent updates)
    Q_INVOKABLE void registerSeries(const QVariantList& pressureGoalSegments, const QVariantList& flowGoalSegments,
                                     QLineSeries* temperatureGoal,
                                     QLineSeries* extractionMarker,
                                     QLineSeries* stopMarker,
                                     const QVariantList& frameMarkers);

    // Register fast renderers for live data series (QSGGeometryNode - pre-allocated VBO)
    Q_INVOKABLE void registerFastSeries(FastLineRenderer* pressure, FastLineRenderer* flow,
                                         FastLineRenderer* temperature,
                                         FastLineRenderer* weight, FastLineRenderer* weightFlow,
                                         FastLineRenderer* resistance = nullptr);

    // Data export for visualizer upload
    const QVector<QPointF>& pressureData() const { return m_pressurePoints; }
    const QVector<QPointF>& flowData() const { return m_flowPoints; }
    const QVector<QPointF>& temperatureData() const { return m_temperaturePoints; }
    const QVector<QPointF>& temperatureMixData() const { return m_temperatureMixPoints; }
    const QVector<QPointF>& resistanceData() const { return m_resistancePoints; }
    const QVector<QPointF>& waterDispensedData() const { return m_waterDispensedPoints; }
    QVector<QPointF> pressureGoalData() const;  // Combines all segments
    QVector<QPointF> flowGoalData() const;      // Combines all segments
    const QVector<QPointF>& temperatureGoalData() const { return m_temperatureGoalPoints; }
    const QVector<QPointF>& weightData() const { return m_weightPoints; }  // Cumulative weight (g) for graph
    const QVector<QPointF>& cumulativeWeightData() const { return m_cumulativeWeightPoints; }  // Cumulative weight for export
    const QVector<QPointF>& weightFlowRateData() const { return m_weightFlowRatePoints; }  // Flow rate from scale (g/s) for export

public slots:
    void clear();
    void clearWeightData();  // Clear only weight samples (call when tare completes)

    // Data ingestion - vector append, chart update deferred to 33ms timer
    void addSample(double time, double pressure, double flow, double temperature,
                   double mixTemp,
                   double pressureGoal, double flowGoal, double temperatureGoal,
                   int frameNumber = -1, bool isFlowMode = false);
    void addWeightSample(double time, double weight, double flowRate);
    void addWeightSample(double time, double weight);  // Overload without flowRate (from ShotTimingController)
    void markExtractionStart(double time);
    void markStopAt(double time);  // Mark when SAW or user stopped the shot
    void smoothWeightFlowRate(int window = 5);  // Apply centered moving average to weight flow rate
    void addPhaseMarker(double time, const QString& label, int frameNumber = -1, bool isFlowMode = false, const QString& transitionReason = QString());

signals:
    void cleared();
    void maxTimeChanged();
    void rawTimeChanged();
    void phaseMarkersChanged();
    void stopTimeChanged();
    void weightAtStopChanged();
    void finalWeightChanged();
    void flushed();  // Emitted after 33ms timer flushes new data to renderers

private slots:
    void flushToChart();  // Called by timer - batched update to chart

private:
    // Data storage - fast vector appends
    QVector<QPointF> m_pressurePoints;
    QVector<QPointF> m_flowPoints;
    QVector<QPointF> m_temperaturePoints;
    QVector<QPointF> m_temperatureMixPoints;
    QVector<QPointF> m_resistancePoints;
    QVector<QPointF> m_waterDispensedPoints;
    QVector<QVector<QPointF>> m_pressureGoalSegments;  // Separate segments for clean breaks
    QVector<QVector<QPointF>> m_flowGoalSegments;      // Separate segments for clean breaks
    QVector<QPointF> m_temperatureGoalPoints;
    QVector<QPointF> m_weightPoints;  // Cumulative weight (g) - for graphing
    QVector<QPointF> m_cumulativeWeightPoints;  // Cumulative weight (g) - for export
    QVector<QPointF> m_weightFlowRatePoints;  // Flow rate from scale (g/s) - for visualizer export

    // Fast renderers for live data series (QSGGeometryNode, pre-allocated VBO)
    QPointer<FastLineRenderer> m_fastPressure;
    QPointer<FastLineRenderer> m_fastFlow;
    QPointer<FastLineRenderer> m_fastTemperature;
    QPointer<FastLineRenderer> m_fastWeight;
    QPointer<FastLineRenderer> m_fastWeightFlow;
    QPointer<FastLineRenderer> m_fastResistance;

    // Last-flushed index per fast series (for incremental appends)
    int m_lastFlushedPressure = 0;
    int m_lastFlushedFlow = 0;
    int m_lastFlushedTemp = 0;
    int m_lastFlushedWeight = 0;
    int m_lastFlushedWeightFlow = 0;
    int m_lastFlushedResistance = 0;

    // Chart series for goals/markers (QPointer auto-nulls when QML destroys them)
    QList<QPointer<QLineSeries>> m_pressureGoalSeriesList;  // One per segment
    QList<QPointer<QLineSeries>> m_flowGoalSeriesList;      // One per segment
    QPointer<QLineSeries> m_temperatureGoalSeries;
    QPointer<QLineSeries> m_extractionMarkerSeries;
    QPointer<QLineSeries> m_stopMarkerSeries;
    QList<QPointer<QLineSeries>> m_frameMarkerSeries;

    // Batched update timer (30fps)
    QTimer* m_flushTimer = nullptr;
    bool m_dirty = false;

    double m_maxTime = 5.0;
    double m_rawTime = 0.0;
    bool m_rawTimeDirty = false;  // Deferred: emit rawTimeChanged in flushToChart()
    int m_frameMarkerIndex = 0;
    bool m_lastPumpModeIsFlow = false;  // Track for starting new goal segments
    bool m_hasPumpModeData = false;     // True after first sample with pump mode
    int m_currentPressureGoalSegment = 0;  // Current segment index
    int m_currentFlowGoalSegment = 0;      // Current segment index

    // Phase markers for QML labels
    QList<PhaseMarker> m_phaseMarkers;
    QList<QPair<double, QString>> m_pendingMarkers;  // Pending vertical lines
    double m_pendingStopTime = -1;  // Stop marker time (-1 = none)
    double m_stopTime = -1;          // Recorded stop time for accessibility
    double m_weightAtStop = 0.0;     // Weight when stop was triggered

    static constexpr int FLUSH_INTERVAL_MS = 33;  // Chart update timer (~30fps); batches BLE and scale samples
    static constexpr int INITIAL_CAPACITY = 600;  // Pre-allocate for 2min at 5Hz
};
