#pragma once

#include <QObject>
#include <QVector>
#include <QPointF>
#include <QVariantList>
#include <memory>

class ShotHistoryStorage;
class SettingsCalibration;
class DE1Device;
struct ShotRecord;

class FlowCalibrationModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(double multiplier READ multiplier WRITE setMultiplier NOTIFY multiplierChanged)
    Q_PROPERTY(QVariantList flowData READ flowData NOTIFY dataChanged)
    Q_PROPERTY(QVariantList weightFlowData READ weightFlowData NOTIFY dataChanged)
    Q_PROPERTY(QVariantList pressureData READ pressureData NOTIFY dataChanged)
    Q_PROPERTY(double maxTime READ maxTime NOTIFY dataChanged)
    Q_PROPERTY(QString shotInfo READ shotInfo NOTIFY dataChanged)
    Q_PROPERTY(bool hasPreviousShot READ hasPreviousShot NOTIFY navigationChanged)
    Q_PROPERTY(bool hasNextShot READ hasNextShot NOTIFY navigationChanged)
    Q_PROPERTY(int shotCount READ shotCount NOTIFY navigationChanged)
    Q_PROPERTY(int currentShotIndex READ currentShotIndex NOTIFY navigationChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY dataChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    // The multiplier the displayed shot was RECORDED at, or 0 if unknown
    // (pre-migration-39, imported, or a dev fake). Distinct from `multiplier`,
    // which is what the slider is proposing. 0 is not 1.0 and must not be shown
    // as if it were: the difference is "this shot poured at neutral" versus "we
    // do not know what this shot poured at", and only the second means the
    // overlay below is an assumption.
    Q_PROPERTY(double shotRecordedMultiplier READ shotRecordedMultiplier NOTIFY dataChanged)
    // Whether the displayed shot HAS a recorded multiplier. The threshold is a
    // property of the sentinel (0 = not recorded) and belongs in one place —
    // re-typing `> 0.001` in the QML put the same rule on both sides of a
    // language boundary, where nothing fails when the two drift.
    Q_PROPERTY(bool shotMultiplierRecorded READ shotMultiplierRecorded NOTIFY dataChanged)

public:
    explicit FlowCalibrationModel(QObject* parent = nullptr);
    ~FlowCalibrationModel() override;

    void setStorage(ShotHistoryStorage* storage);
    void setSettings(SettingsCalibration* settings);
    void setDevice(DE1Device* device);

    double multiplier() const { return m_multiplier; }
    void setMultiplier(double m);

    QVariantList flowData() const;
    QVariantList weightFlowData() const;
    QVariantList pressureData() const;
    double maxTime() const { return m_maxTime; }
    QString shotInfo() const { return m_shotInfo; }
    bool hasPreviousShot() const { return m_currentIndex > 0; }
    bool hasNextShot() const { return m_currentIndex >= 0 && m_currentIndex < m_shotIds.size() - 1; }
    int shotCount() const { return static_cast<int>(m_shotIds.size()); }
    int currentShotIndex() const { return m_currentIndex; }
    bool hasData() const { return !m_originalFlow.isEmpty(); }
    QString errorMessage() const { return m_errorMessage; }
    bool loading() const { return m_loading; }
    double shotRecordedMultiplier() const { return m_shotMultiplier; }
    bool shotMultiplierRecorded() const { return m_shotMultiplier > kRecordedMultiplierEpsilon; }

    Q_INVOKABLE void loadRecentShots();
    Q_INVOKABLE void previousShot();
    Q_INVOKABLE void nextShot();
    Q_INVOKABLE void save();
    Q_INVOKABLE void resetToFactory();

signals:
    void multiplierChanged();
    void dataChanged();
    void navigationChanged();
    void errorChanged();
    void loadingChanged();

private:
    void loadCurrentShot();
    void applyShotRecord(const ShotRecord& record);
    void recalculateFlow();
    void setLoading(bool loading);
    QVariantList pointsToVariant(const QVector<QPointF>& points) const;

    ShotHistoryStorage* m_storage = nullptr;
    SettingsCalibration* m_settings = nullptr;
    DE1Device* m_device = nullptr;

    QVector<qint64> m_shotIds;
    int m_currentIndex = -1;
    double m_multiplier = 1.0;
    // Below this, treat the stored value as "not recorded" rather than as a
    // multiplier. Named once; recalculateFlow() and shotMultiplierRecorded()
    // both use it, and the QML asks the bool rather than repeating the number.
    static constexpr double kRecordedMultiplierEpsilon = 0.001;
    double m_shotMultiplier = 0.0;  // Multiplier the shot was recorded at; 0 = unknown
    bool m_loading = false;
    std::shared_ptr<bool> m_destroyed = std::make_shared<bool>(false);

    // Current shot data
    QVector<QPointF> m_originalFlow;
    QVector<QPointF> m_recalculatedFlow;
    QVector<QPointF> m_weightFlowRate;
    QVector<QPointF> m_pressure;
    double m_maxTime = 60.0;
    QString m_shotInfo;
    QString m_errorMessage;
};
