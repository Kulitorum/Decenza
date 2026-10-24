#pragma once

#include <QObject>
#include <QJsonObject>
#include <QtQmlIntegration/qqmlintegration.h>

#define DECENZA_CAPTURE_FIELDS(B, D) \
    B(active) B(seeded) B(loadPresent) B(isCaptured) \
    B(lighterThanPitcher) B(belowMinimum) \
    D(rawWeight) D(cupWeight) D(minNet) D(maxNet) D(virtualZero) D(netWeight)

struct WeightCaptureSnapshot {
#define BOOL_VALUE(name) bool name = false;
#define DOUBLE_VALUE(name) double name = 0;
    DECENZA_CAPTURE_FIELDS(BOOL_VALUE, DOUBLE_VALUE)
#undef BOOL_VALUE
#undef DOUBLE_VALUE
    QJsonObject toJson() const;
    static bool parse(const QJsonObject&, WeightCaptureSnapshot*, QString* error);
};

struct PreparationSnapshot {
    QString mode = QStringLiteral("espresso");
    bool milkTimeLocked = true;
    bool milkAttemptOpen = false;
    double sessionMeasuredMilkG = 0;
    double lastOnScaleMilkG = 0;
    bool steamTimeoutScaled = false;
    double lastBeanGrams = 0;
    double lastMilkGrams = 0;
    int lastMilkSeconds = 0;
    qint64 beanCaptureSequence = 0;
    qint64 milkCaptureSequence = 0;
    WeightCaptureSnapshot bean;
    WeightCaptureSnapshot milk;

    static bool isMode(const QString& mode);
    QJsonObject toJson() const;
    static bool parse(const QJsonObject&, PreparationSnapshot*, QString* error = nullptr);
};

class WeightCaptureState final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by PreparationState")
#define BOOL_PROPERTY(name) Q_PROPERTY(bool name READ name NOTIFY changed FINAL)
#define DOUBLE_PROPERTY(name) Q_PROPERTY(double name READ name NOTIFY changed FINAL)
    DECENZA_CAPTURE_FIELDS(BOOL_PROPERTY, DOUBLE_PROPERTY)
#undef BOOL_PROPERTY
#undef DOUBLE_PROPERTY
public:
    explicit WeightCaptureState(QObject* parent = nullptr) : QObject(parent) {}
#define BOOL_GETTER(name) bool name() const { return m_state.name; }
#define DOUBLE_GETTER(name) double name() const { return m_state.name; }
    DECENZA_CAPTURE_FIELDS(BOOL_GETTER, DOUBLE_GETTER)
#undef BOOL_GETTER
#undef DOUBLE_GETTER
    void apply(const WeightCaptureSnapshot& state);
signals:
    void changed();
private:
    WeightCaptureSnapshot m_state;
};

// Both runtimes expose this contract; only the native controller evaluates weight.
class PreparationState : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by MainController")
    Q_PROPERTY(bool available READ available NOTIFY availableChanged FINAL)
    Q_PROPERTY(QString mode READ mode NOTIFY modeChanged FINAL)
    Q_PROPERTY(WeightCaptureState* bean READ bean CONSTANT FINAL)
    Q_PROPERTY(WeightCaptureState* milk READ milk CONSTANT FINAL)
    Q_PROPERTY(double sessionMeasuredMilkG READ sessionMeasuredMilkG NOTIFY changed FINAL)
    Q_PROPERTY(double lastOnScaleMilkG READ lastOnScaleMilkG NOTIFY changed FINAL)
    Q_PROPERTY(bool steamTimeoutScaled READ steamTimeoutScaled NOTIFY changed FINAL)
    Q_PROPERTY(double milkScaleLoadG READ milkScaleLoadG NOTIFY changed FINAL)
    Q_PROPERTY(bool milkTimeLocked READ milkTimeLocked NOTIFY changed FINAL)
    Q_PROPERTY(bool milkAttemptOpen READ milkAttemptOpen NOTIFY changed FINAL)
public:
    explicit PreparationState(QObject* parent = nullptr);
    bool available() const { return m_available; }
    QString mode() const { return m_snapshot.mode; }
    WeightCaptureState* bean() { return &m_bean; }
    WeightCaptureState* milk() { return &m_milk; }
    double sessionMeasuredMilkG() const { return m_snapshot.sessionMeasuredMilkG; }
    double lastOnScaleMilkG() const { return m_snapshot.lastOnScaleMilkG; }
    bool steamTimeoutScaled() const { return m_snapshot.steamTimeoutScaled; }
    double milkScaleLoadG() const;
    bool milkTimeLocked() const { return m_snapshot.milkTimeLocked; }
    bool milkAttemptOpen() const { return m_snapshot.milkAttemptOpen; }
    const PreparationSnapshot& snapshot() const { return m_snapshot; }

    Q_INVOKABLE bool isPreparationMode(const QString& mode) const { return PreparationSnapshot::isMode(mode); }
    Q_INVOKABLE virtual bool selectMode(const QString& mode) = 0;
    Q_INVOKABLE virtual bool startMilkAttempt() = 0;
    Q_INVOKABLE virtual bool cancelMilkAttempt() = 0;
    Q_INVOKABLE virtual bool setMilkTimeLocked(bool locked) = 0;
signals:
    void changed();
    void modeChanged();
    void availableChanged();
    void beanCaptured(double grams);
    void milkCaptured(double grams, int seconds);
    void actionFailed(const QString& action, const QString& message);
protected:
    void applySnapshot(const PreparationSnapshot& snapshot, bool available, bool deliverEvents);
private:
    PreparationSnapshot m_snapshot;
    bool m_available = false;
    WeightCaptureState m_bean;
    WeightCaptureState m_milk;
};
