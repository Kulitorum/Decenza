#pragma once

#include <QObject>
#include <QTimer>
#include "../ble/protocol/de1characteristics.h"

class DE1Device;
class ScaleDevice;
class Profile;
class Settings;

class MachineState : public QObject {
    Q_OBJECT

    Q_PROPERTY(Phase phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(bool isFlowing READ isFlowing NOTIFY phaseChanged)
    Q_PROPERTY(bool isHeating READ isHeating NOTIFY phaseChanged)
    Q_PROPERTY(bool isReady READ isReady NOTIFY phaseChanged)
    Q_PROPERTY(double shotTime READ shotTime NOTIFY shotTimeChanged)
    Q_PROPERTY(double targetWeight READ targetWeight WRITE setTargetWeight NOTIFY targetWeightChanged)
    Q_PROPERTY(double scaleWeight READ scaleWeight NOTIFY scaleWeightChanged)
    Q_PROPERTY(double scaleFlowRate READ scaleFlowRate NOTIFY scaleWeightChanged)

public:
    enum class Phase {
        Disconnected,
        Sleep,
        Idle,
        Heating,
        Ready,
        EspressoPreheating,  // Machine is in Espresso state but warming up
        Preinfusion,
        Pouring,
        Ending,
        Steaming,
        HotWater,
        Flushing,
        Refill,
        Descaling,           // Machine is running descale routine
        Cleaning             // Machine is running clean routine
    };
    Q_ENUM(Phase)

    explicit MachineState(DE1Device* device, QObject* parent = nullptr);

    Phase phase() const { return m_phase; }
    QString phaseString() const;
    bool isFlowing() const;
    bool isHeating() const;
    bool isReady() const;
    double shotTime() const;
    double targetWeight() const { return m_targetWeight; }

    void setScale(ScaleDevice* scale);
    void setSettings(Settings* settings);
    void setTargetWeight(double weight);

    // Scale accessors (forward from current scale)
    double scaleWeight() const;
    double scaleFlowRate() const;

    // Called by MainController when shot samples arrive
    void onFlowSample(double flowRate, double deltaTime);

    // Tare the scale (call from MainController when first user frame starts)
    Q_INVOKABLE void tareScale();

signals:
    void phaseChanged();
    void shotTimeChanged();
    void targetWeightChanged();
    void scaleWeightChanged();
    void espressoCycleStarted();  // When entering espresso preheating (clear graph here)
    void shotStarted();           // When extraction actually begins (flow starts)
    void shotEnded();
    void targetWeightReached();
    void tareCompleted();         // Emitted when scale reports ~0g after tare command

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
    void checkStopAtTime();

    DE1Device* m_device = nullptr;
    ScaleDevice* m_scale = nullptr;  // Either FlowScale (fallback) or physical BLE scale
    Settings* m_settings = nullptr;

    Phase m_phase = Phase::Disconnected;
    double m_shotTime = 0.0;
    double m_targetWeight = 36.0;

    QTimer* m_shotTimer = nullptr;
    qint64 m_shotStartTime = 0;
    bool m_stopAtWeightTriggered = false;
    bool m_stopAtTimeTriggered = false;
    bool m_tareCompleted = false;
    bool m_waitingForTare = false;  // True after tare sent, waiting for scale to report ~0g

    // Auto-tare on cup removal detection
    double m_lastIdleWeight = 0.0;
    qint64 m_lastWeightTime = 0;
};
