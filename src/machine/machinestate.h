#pragma once

#include <QObject>
#include <QTimer>
#include "../ble/protocol/de1characteristics.h"

class DE1Device;
class ScaleDevice;
class Profile;

class MachineState : public QObject {
    Q_OBJECT

    Q_PROPERTY(Phase phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(bool isFlowing READ isFlowing NOTIFY phaseChanged)
    Q_PROPERTY(bool isHeating READ isHeating NOTIFY phaseChanged)
    Q_PROPERTY(bool isReady READ isReady NOTIFY phaseChanged)
    Q_PROPERTY(double shotTime READ shotTime NOTIFY shotTimeChanged)
    Q_PROPERTY(double targetWeight READ targetWeight WRITE setTargetWeight NOTIFY targetWeightChanged)

public:
    enum class Phase {
        Disconnected,
        Sleep,
        Idle,
        Heating,
        Ready,
        Preinfusion,
        Pouring,
        Ending,
        Steaming,
        HotWater,
        Flushing
    };
    Q_ENUM(Phase)

    explicit MachineState(DE1Device* device, QObject* parent = nullptr);

    Phase phase() const { return m_phase; }
    bool isFlowing() const;
    bool isHeating() const;
    bool isReady() const;
    double shotTime() const { return m_shotTime; }
    double targetWeight() const { return m_targetWeight; }

    void setScale(ScaleDevice* scale);
    void setTargetWeight(double weight);

signals:
    void phaseChanged();
    void shotTimeChanged();
    void targetWeightChanged();
    void shotStarted();
    void shotEnded();
    void targetWeightReached();

private slots:
    void onDE1StateChanged();
    void onDE1SubStateChanged();
    void onScaleWeightChanged(double weight);
    void updateShotTimer();

private:
    void updatePhase();
    void startShotTimer();
    void stopShotTimer();
    void checkStopAtWeight(double weight);

    DE1Device* m_device = nullptr;
    ScaleDevice* m_scale = nullptr;

    Phase m_phase = Phase::Disconnected;
    double m_shotTime = 0.0;
    double m_targetWeight = 36.0;

    QTimer* m_shotTimer = nullptr;
    qint64 m_shotStartTime = 0;
    bool m_stopAtWeightTriggered = false;
};
