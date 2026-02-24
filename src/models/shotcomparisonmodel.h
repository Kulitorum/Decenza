#pragma once

#include <QObject>
#include <QVector>
#include <QPointF>
#include <QVariantList>
#include <QColor>
#include <QThread>

class ShotHistoryStorage;
struct ShotRecord;

// Model for comparing shots with sliding window display (shows 3 at a time)
class ShotComparisonModel : public QObject {
    Q_OBJECT

    // Display window properties (shows max 3 shots at a time)
    Q_PROPERTY(int shotCount READ displayShotCount NOTIFY shotsChanged)
    Q_PROPERTY(QVariantList shots READ shotsVariant NOTIFY shotsChanged)
    Q_PROPERTY(double maxTime READ maxTime NOTIFY shotsChanged)
    Q_PROPERTY(double maxPressure READ maxPressure NOTIFY shotsChanged)
    Q_PROPERTY(double maxFlow READ maxFlow NOTIFY shotsChanged)
    Q_PROPERTY(double maxWeight READ maxWeight NOTIFY shotsChanged)

    // Window navigation properties
    Q_PROPERTY(int windowStart READ windowStart NOTIFY windowChanged)
    Q_PROPERTY(int totalShots READ totalShots NOTIFY shotsChanged)
    Q_PROPERTY(bool canShiftLeft READ canShiftLeft NOTIFY windowChanged)
    Q_PROPERTY(bool canShiftRight READ canShiftRight NOTIFY windowChanged)

    // True while shot data is being loaded in the background
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    explicit ShotComparisonModel(QObject* parent = nullptr);

    void setStorage(ShotHistoryStorage* storage);

    int displayShotCount() const { return static_cast<int>(m_displayShots.size()); }
    int totalShots() const { return static_cast<int>(m_shotIds.size()); }

    QVariantList shotsVariant() const;
    double maxTime() const { return m_maxTime; }
    double maxPressure() const { return m_maxPressure; }
    double maxFlow() const { return m_maxFlow; }
    double maxWeight() const { return m_maxWeight; }

    bool loading() const { return m_loading; }

    int windowStart() const { return m_windowStart; }
    bool canShiftLeft() const { return m_windowStart > 0; }
    bool canShiftRight() const { return m_windowStart + DISPLAY_WINDOW_SIZE < static_cast<int>(m_shotIds.size()); }

    Q_INVOKABLE bool addShot(qint64 shotId);
    // Batch-add: one DB load, one shotsChanged emission.
    Q_INVOKABLE void addShots(const QVariantList& shotIds);
    Q_INVOKABLE void removeShot(qint64 shotId);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE bool hasShotId(qint64 shotId) const;

    Q_INVOKABLE void shiftWindowLeft();
    Q_INVOKABLE void shiftWindowRight();
    Q_INVOKABLE void setWindowStart(int index);

    // Bulk-populate LineSeries objects for one shot slot using C++ QXYSeries::replace().
    // Out-of-range shotIdx clears all series.
    Q_INVOKABLE void populateSeries(int shotIdx,
                                    QObject* pSeries, QObject* fSeries,
                                    QObject* tSeries, QObject* wSeries,
                                    QObject* wfSeries, QObject* rSeries) const;

    Q_INVOKABLE QVariantList getPressureData(int index) const;
    Q_INVOKABLE QVariantList getFlowData(int index) const;
    Q_INVOKABLE QVariantList getTemperatureData(int index) const;
    Q_INVOKABLE QVariantList getWeightData(int index) const;
    Q_INVOKABLE QVariantList getWeightFlowRateData(int index) const;
    Q_INVOKABLE QVariantList getResistanceData(int index) const;
    Q_INVOKABLE QVariantList getPhaseMarkers(int index) const;

    Q_INVOKABLE QVariantMap getShotInfo(int index) const;
    Q_INVOKABLE QVariantMap getValuesAtTime(int index, double time) const;

    Q_INVOKABLE QColor getShotColor(int index) const;
    Q_INVOKABLE QColor getShotColorLight(int index) const;

signals:
    void shotsChanged();
    void windowChanged();
    void loadingChanged();
    void errorOccurred(const QString& message);

private:
    // Start a background QThread that opens its own SQLite connection, loads the
    // current window, and delivers results back to the main thread via finished().
    void scheduleLoad();
    void calculateMaxValues();
    QVariantList pointsToVariant(const QVector<QPointF>& points) const;

    struct ComparisonShot {
        qint64 id = 0;
        QString profileName;
        QString beanBrand;
        QString beanType;
        QString roastDate;
        QString roastLevel;
        QString grinderModel;
        QString grinderSetting;
        double duration = 0;
        double doseWeight = 0;
        double finalWeight = 0;
        double drinkTds = 0;
        double drinkEy = 0;
        int enjoyment = 0;
        qint64 timestamp = 0;
        QString notes;
        QString barista;
        double temperatureOverride = 0;
        double yieldOverride = 0;

        QVector<QPointF> pressure;
        QVector<QPointF> flow;
        QVector<QPointF> temperature;
        QVector<QPointF> weight;
        QVector<QPointF> weightFlowRate;
        QVector<QPointF> resistance;

        struct PhaseMarker {
            double time = 0;
            QString label;
            QString transitionReason;
        };
        QList<PhaseMarker> phases;
    };

    ShotHistoryStorage* m_storage = nullptr;
    QList<qint64> m_shotIds;
    QList<ComparisonShot> m_displayShots;
    int m_windowStart = 0;
    bool m_loading = false;
    QThread* m_loadThread = nullptr;  // Tracked so superseded loads can be abandoned
    int m_loadSerial = 0;             // Incremented on each scheduleLoad(); stale results ignored

    double m_maxTime = 60.0;
    double m_maxPressure = 12.0;
    double m_maxFlow = 8.0;
    double m_maxWeight = 50.0;

    static constexpr int DISPLAY_WINDOW_SIZE = 3;
    static const QList<QColor> SHOT_COLORS;
    static const QList<QColor> SHOT_COLORS_LIGHT;
};
