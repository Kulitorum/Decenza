#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include "../ble/protocol/de1characteristics.h"

class DE1Device;
class ScaleDevice;
class Profile;
class Settings;
class ShotTimingController;

class MachineState : public QObject {
    Q_OBJECT

    Q_PROPERTY(Phase phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(bool isFlowing READ isFlowing NOTIFY phaseChanged)
    Q_PROPERTY(bool isHeating READ isHeating NOTIFY phaseChanged)
    Q_PROPERTY(bool isReady READ isReady NOTIFY phaseChanged)
    Q_PROPERTY(double shotTime READ shotTime NOTIFY shotTimeChanged)
    Q_PROPERTY(double targetWeight READ targetWeight WRITE setTargetWeight NOTIFY targetWeightChanged)
    Q_PROPERTY(double targetVolume READ targetVolume WRITE setTargetVolume NOTIFY targetVolumeChanged)
    Q_PROPERTY(double scaleWeight READ scaleWeight NOTIFY scaleWeightChanged)
    Q_PROPERTY(double scaleFlowRate READ scaleFlowRate NOTIFY scaleWeightChanged)
    Q_PROPERTY(double smoothedScaleFlowRate READ smoothedScaleFlowRate NOTIFY scaleWeightChanged)
    Q_PROPERTY(double cumulativeVolume READ cumulativeVolume NOTIFY cumulativeVolumeChanged)
    Q_PROPERTY(double preinfusionVolume READ preinfusionVolume NOTIFY preinfusionVolumeChanged)
    Q_PROPERTY(double pourVolume READ pourVolume NOTIFY pourVolumeChanged)
    Q_PROPERTY(StopAtType stopAtType READ stopAtType NOTIFY stopAtTypeChanged)

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

    // Stop-at modes (what triggers end of shot)
    // NOTE: Must stay in sync with Profile::StopAtType
    enum class StopAtType {
        Weight,         // Stop when scale reaches target weight (brown curve)
        Volume          // Stop when flow meter reaches target volume (blue curve)
    };
    Q_ENUM(StopAtType)

    explicit MachineState(DE1Device* device, QObject* parent = nullptr);

    Phase phase() const { return m_phase; }
    QString phaseString() const;
    bool isFlowing() const;
    bool isHeating() const;
    bool isReady() const;
    double shotTime() const;
    double targetWeight() const { return m_targetWeight; }
    double targetVolume() const { return m_targetVolume; }
    double cumulativeVolume() const { return m_cumulativeVolume; }
    double preinfusionVolume() const { return m_preinfusionVolume; }
    double pourVolume() const { return m_pourVolume; }
    StopAtType stopAtType() const { return m_stopAtType; }

    void setScale(ScaleDevice* scale);
    void setSettings(Settings* settings);
    void setTimingController(ShotTimingController* controller);
    void setTargetWeight(double weight);
    void setTargetVolume(double volume);
    void setStopAtType(StopAtType type);

    // Scale accessors (forward from current scale)
    double scaleWeight() const;
    double scaleFlowRate() const;
    double smoothedScaleFlowRate() const;
    double smoothedScaleFlowRateShort() const;  // 500ms LSLR window for SOW decisions

    // Called by MainController when shot samples arrive
    void onFlowSample(double flowRate, double deltaTime);

    // Tare the scale (call from MainController when first user frame starts)
    Q_INVOKABLE void tareScale();

signals:
    void phaseChanged();
    void shotTimeChanged();
    void targetWeightChanged();
    void targetVolumeChanged();
    void cumulativeVolumeChanged();
    void preinfusionVolumeChanged();
    void pourVolumeChanged();
    void stopAtTypeChanged();
    void scaleWeightChanged();
    void espressoCycleStarted();  // When entering espresso preheating (clear graph here)
    void shotStarted();           // When extraction actually begins (flow starts)
    void shotEnded();
    void targetWeightReached();
    void targetVolumeReached();
    void tareCompleted();         // Emitted when scale reports ~0g after tare command

private slots:
    void onDE1StateChanged();
    void onDE1SubStateChanged();
    void onScaleWeightChanged(double weight);
    void updateShotTimer();
    void onTimingControllerTareComplete();

private:
    void updatePhase();
    void startShotTimer();
    void stopShotTimer();
    void checkStopAtWeightHotWater(double weight);
    void checkStopAtVolume();
    void checkStopAtTime();

    DE1Device* m_device = nullptr;
    QPointer<ScaleDevice> m_scale;  // Auto-nulls when scale is destroyed (prevents dangling pointer)
    Settings* m_settings = nullptr;
    ShotTimingController* m_timingController = nullptr;

    Phase m_phase = Phase::Disconnected;
    double m_shotTime = 0.0;
    double m_targetWeight = 36.0;
    double m_targetVolume = 36.0;
    double m_cumulativeVolume = 0.0;    // Total volume from flow meter (preinfusion + pour)
    int m_lastEmittedCumulativeVolumeMl = -1;  // Throttle: only emit when rounded ml changes
    double m_preinfusionVolume = 0.0;   // Volume during preinfusion substate (ml)
    double m_pourVolume = 0.0;          // Volume during pouring substate (ml)
    StopAtType m_stopAtType = StopAtType::Weight;

    QTimer* m_shotTimer = nullptr;
    qint64 m_shotStartTime = 0;
    bool m_stopAtWeightTriggered = false;
    bool m_stopAtVolumeTriggered = false;
    bool m_stopAtTimeTriggered = false;
    bool m_tareCompleted = false;
    bool m_waitingForTare = false;  // True after tare sent, waiting for scale to report ~0g

    // Weight flow rate: computed as slope of weight over a 1-second window
    // (avoids differentiating noisy scale data — differentiation amplifies BLE timing jitter)
    struct WeightSample {
        qint64 timestamp;
        double weight;
    };
    QList<WeightSample> m_weightSamples;
    void recordWeightSample();

    // Auto-tare on cup removal detection
    double m_lastIdleWeight = 0.0;
    qint64 m_lastWeightTime = 0;
};
