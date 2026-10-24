#pragma once

#include "remotemachineuisource.h"
#include <type_traits>

class QQmlEngine;
class QJSEngine;

class RemoteMachineState final : public RemoteMachineUiSource
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MachineState)
    QML_SINGLETON

    Q_PROPERTY(RemoteMachineState::Phase phase READ phase NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(bool isFlowing READ isFlowing NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(bool isHeating READ isHeating NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(bool isReady READ isReady NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double scaleWeight READ scaleWeight NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double scaleFlowRate READ scaleFlowRate NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double cumulativeVolume READ cumulativeVolume NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double preinfusionVolume READ preinfusionVolume NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double pourVolume READ pourVolume NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double shotTime READ shotTime NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double targetWeight READ targetWeight NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double targetVolume READ targetVolume NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double smoothedScaleFlowRate READ smoothedScaleFlowRate NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(bool canDecreaseTargetWeight READ canDecreaseTargetWeight NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(QString controlContextId READ controlContextId NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(bool standbySwitchOpen READ standbySwitchOpen NOTIFY machineUiChanged FINAL)

public:
    enum class Phase {
#define DECENZA_DECLARE_REMOTE_PHASE(name) name,
        DECENZA_MACHINE_PHASES(DECENZA_DECLARE_REMOTE_PHASE)
#undef DECENZA_DECLARE_REMOTE_PHASE
    };
    Q_ENUM(Phase)
    Phase phase() const { return static_cast<Phase>(machineUi().phase); }
    static RemoteMachineState* create(QQmlEngine* engine, QJSEngine* jsEngine);
    bool isFlowing() const { return machineUi().isFlowing; }
    bool isHeating() const { return machineUi().isHeating; }
    bool isReady() const { return machineUi().isReady; }
    double scaleWeight() const { return machineUi().scaleWeight; }
    double scaleFlowRate() const { return machineUi().scaleFlowRate; }
    double cumulativeVolume() const { return machineUi().cumulativeVolume; }
    double preinfusionVolume() const { return machineUi().preinfusionVolume; }
    double pourVolume() const { return machineUi().pourVolume; }
    double shotTime() const { return machineUi().shotTime; }
    double targetWeight() const { return machineUi().targetWeight; }
    double targetVolume() const { return machineUi().targetVolume; }
    double smoothedScaleFlowRate() const { return machineUi().smoothedScaleFlowRate; }
    bool canDecreaseTargetWeight() const { return machineUi().canDecreaseTargetWeight; }
    QString controlContextId() const { return machineUi().controlContextId; }
    bool standbySwitchOpen() const { return machineUi().standbySwitchOpen; }

signals:
    void scaleWeightChanged();

private:
    explicit RemoteMachineState(QObject* parent);
    double m_lastScaleWeight = 0.0;
};

static_assert(!std::is_default_constructible_v<RemoteMachineState>,
              "The QML singleton must use its engine-owned factory.");
