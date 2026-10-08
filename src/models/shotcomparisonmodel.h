#pragma once

#include <QObject>
#include <QVector>
#include <QPointF>
#include <QVariantList>
#include <QThread>
#include <QVariantMap>

#include <algorithm>

#include <QtQmlIntegration/qqmlintegration.h>
class ShotHistoryStorage;
struct ShotRecord;

// Shots compared against a base shot. The base is pinned in column 0 and the
// other shots page past it, OTHER_WINDOW_SIZE at a time. What changed and what
// the shots did differently comes from ShotComparison::compare(), the same
// assembler the web page and MCP use.
class ShotComparisonModel : public QObject {
    Q_OBJECT

    // Compile-time QML registration, so qmllint, qmlcachegen and the language server can
    // follow MainController's property through to this class. A runtime qmlRegister* call is
    // invisible to all three. Full rationale in src/controllers/maincontroller.h.
    QML_ELEMENT
    QML_UNCREATABLE("ShotComparisonModel is created in C++ and reached via MainController")

    // Visible shots: the base, then up to OTHER_WINDOW_SIZE others.
    Q_PROPERTY(int shotCount READ displayShotCount NOTIFY shotsChanged)
    Q_PROPERTY(double maxTime READ maxTime NOTIFY shotsChanged)

    Q_PROPERTY(qint64 baseShotId READ baseShotId NOTIFY shotsChanged)
    // ShotComparison::compare() over the visible shots, base first.
    Q_PROPERTY(QVariantMap comparison READ comparison NOTIFY shotsChanged)
    // ShotComparisonText: {id: {key, label}}, the words the comparison is told in.
    Q_PROPERTY(QVariantMap texts READ texts CONSTANT)

    // Paging over the non-base shots.
    Q_PROPERTY(int windowStart READ windowStart NOTIFY windowChanged)
    Q_PROPERTY(int totalShots READ totalShots NOTIFY shotsChanged)
    Q_PROPERTY(int otherWindowSize READ otherWindowSize CONSTANT)
    Q_PROPERTY(bool canShiftLeft READ canShiftLeft NOTIFY windowChanged)
    Q_PROPERTY(bool canShiftRight READ canShiftRight NOTIFY windowChanged)

    // True while shot data is being loaded in the background
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    explicit ShotComparisonModel(QObject* parent = nullptr);

    void setStorage(ShotHistoryStorage* storage);

    int displayShotCount() const { return static_cast<int>(m_displayShots.size()); }
    int totalShots() const { return static_cast<int>(m_shotIds.size()); }
    double maxTime() const { return m_maxTime; }
    qint64 baseShotId() const { return m_baseShotId; }
    QVariantMap comparison() const { return m_comparison; }
    QVariantMap texts() const;
    bool loading() const { return m_loading; }

    int windowStart() const { return m_windowStart; }
    int otherWindowSize() const { return OTHER_WINDOW_SIZE; }
    bool canShiftLeft() const { return m_windowStart > 0; }
    bool canShiftRight() const { return m_windowStart + OTHER_WINDOW_SIZE < otherCount(); }

    // Adds to the selection; the oldest selected shot becomes the base.
    Q_INVOKABLE void addShots(const QVariantList& shotIds);
    Q_INVOKABLE void clearAll();
    // Make a selected shot the base; it moves to column 0.
    Q_INVOKABLE void setBaseShot(qint64 shotId);

    Q_INVOKABLE void shiftWindowLeft();
    Q_INVOKABLE void shiftWindowRight();

    Q_INVOKABLE QVariantList getPressureData(int index) const;
    Q_INVOKABLE QVariantList getFlowData(int index) const;
    Q_INVOKABLE QVariantList getTemperatureData(int index) const;
    Q_INVOKABLE QVariantList getWeightData(int index) const;
    Q_INVOKABLE QVariantList getWeightFlowRateData(int index) const;
    Q_INVOKABLE QVariantList getResistanceData(int index) const;
    Q_INVOKABLE QVariantList getConductanceData(int index) const;
    Q_INVOKABLE QVariantList getConductanceDerivativeData(int index) const;
    Q_INVOKABLE QVariantList getDarcyResistanceData(int index) const;
    Q_INVOKABLE QVariantList getTemperatureMixData(int index) const;
    Q_INVOKABLE QVariantList getTemperatureMixGoalData(int index) const;
    Q_INVOKABLE QVariantList getPhaseMarkers(int index) const;

    // id, profileName, dateTime, isBase and pourStartSec for a visible column.
    Q_INVOKABLE QVariantMap getShotInfo(int index) const;
    Q_INVOKABLE QVariantMap getValuesAtTime(int index, double time) const;

signals:
    void shotsChanged();
    void windowChanged();
    void loadingChanged();
    void errorOccurred(const QString& message);

private:
    // Start a background QThread that opens its own SQLite connection, loads the
    // base and the current window, and delivers results back to the main thread.
    void scheduleLoad();
    void calculateMaxValues();
    QVariantList pointsToVariant(const QVector<QPointF>& points) const;
    int otherCount() const { return std::max(0, totalShots() - 1); }

    struct ComparisonShot {
        qint64 id = 0;
        QString profileName;
        qint64 timestamp = 0;
        double duration = 0;
        double pourStartSec = 0;

        QVector<QPointF> pressure;
        QVector<QPointF> flow;
        QVector<QPointF> temperature;
        QVector<QPointF> weight;
        QVector<QPointF> weightFlowRate;
        QVector<QPointF> resistance;
        QVector<QPointF> conductance;
        QVector<QPointF> conductanceDerivative;
        QVector<QPointF> darcyResistance;
        QVector<QPointF> temperatureMix;
        QVector<QPointF> temperatureMixGoal;

        struct PhaseMarker {
            double time = 0;
            QString label;
            QString transitionReason;
        };
        QList<PhaseMarker> phases;
    };

    ShotHistoryStorage* m_storage = nullptr;
    QList<qint64> m_shotIds;          // every selected shot, oldest first
    qint64 m_baseShotId = 0;
    QList<ComparisonShot> m_displayShots;
    QVariantMap m_comparison;
    int m_windowStart = 0;            // into the non-base shots
    bool m_loading = false;
    int m_loadSerial = 0;             // Incremented on each scheduleLoad(); stale results ignored

    double m_maxTime = 60.0;

    static constexpr int OTHER_WINDOW_SIZE = 2;
};
