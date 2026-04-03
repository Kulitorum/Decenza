#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>
#include <QPointF>
#include <QPointer>
#include <QtCharts/QLineSeries>

class FastLineRenderer;

class SteamDataModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(double maxTime READ maxTime NOTIFY maxTimeChanged)
    Q_PROPERTY(double rawTime READ rawTime NOTIFY rawTimeChanged)
    Q_PROPERTY(int sampleCount READ sampleCount NOTIFY rawTimeChanged)

public:
    explicit SteamDataModel(QObject* parent = nullptr);
    ~SteamDataModel();

    double maxTime() const { return m_maxTime; }
    double rawTime() const { return m_rawTime; }
    int sampleCount() const { return static_cast<int>(m_pressurePoints.size()); }

    // Register fast renderers for live data series (QSGGeometryNode, pre-allocated VBO)
    Q_INVOKABLE void registerFastSeries(FastLineRenderer* pressure, FastLineRenderer* flow,
                                         FastLineRenderer* temperature);

    // Register flow goal LineSeries (infrequent updates)
    Q_INVOKABLE void registerGoalSeries(QLineSeries* flowGoal);

    // Session summary accessors (used by SteamHealthTracker after steaming ends)
    // All skip the first 2 seconds of data (pressure is intentionally high at start)
    double averagePressure() const;
    double peakPressure() const;
    double averageTemperature() const;
    double peakTemperature() const;

    // Raw data access for post-session threshold counting
    const QVector<QPointF>& pressureData() const { return m_pressurePoints; }
    const QVector<QPointF>& temperatureData() const { return m_temperaturePoints; }

public slots:
    void clear();

    // Data ingestion - vector append, chart update deferred to 33ms timer
    void addSample(double time, double pressure, double flow, double temperature);
    void addFlowGoalPoint(double time, double flowGoal);

signals:
    void cleared();
    void maxTimeChanged();
    void rawTimeChanged();
    void flushed();  // Emitted after 33ms timer flushes new data to renderers

private slots:
    void onFlushTimerTick();

private:
    // Data storage - fast vector appends
    QVector<QPointF> m_pressurePoints;
    QVector<QPointF> m_flowPoints;
    QVector<QPointF> m_temperaturePoints;
    QVector<QPointF> m_flowGoalPoints;

    // Fast renderers for live data series (QSGGeometryNode, pre-allocated VBO)
    QPointer<FastLineRenderer> m_fastPressure;
    QPointer<FastLineRenderer> m_fastFlow;
    QPointer<FastLineRenderer> m_fastTemperature;

    // Last-flushed index per fast series (for incremental appends)
    qsizetype m_lastFlushedPressure = 0;
    qsizetype m_lastFlushedFlow = 0;
    qsizetype m_lastFlushedTemp = 0;

    // Flow goal LineSeries (set once per session)
    QPointer<QLineSeries> m_flowGoalSeries;
    bool m_flowGoalDirty = false;

    // Batched update timer (30fps)
    QTimer* m_flushTimer = nullptr;
    bool m_dirty = false;

    double m_maxTime = 5.0;
    double m_rawTime = 0.0;
    bool m_rawTimeDirty = false;

    static constexpr int FLUSH_INTERVAL_MS = 33;   // ~30fps chart updates
    static constexpr int INITIAL_CAPACITY = 600;    // Pre-allocate for 2min at 5Hz
    static constexpr double TRIM_SECONDS = 2.0;     // Skip first 2s for summary stats
};
