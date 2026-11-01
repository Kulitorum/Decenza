#pragma once

#include "remotemachineuisource.h"
#include <type_traits>

class QQmlEngine;
class QJSEngine;

class RemoteScaleDevice final : public RemoteMachineUiSource
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ScaleDevice)
    QML_SINGLETON

    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged FINAL)
    Q_PROPERTY(bool isFlowScale READ isFlowScale NOTIFY isFlowScaleChanged FINAL)
    Q_PROPERTY(QString name READ name NOTIFY nameChanged FINAL)
    Q_PROPERTY(int batteryLevel READ batteryLevel NOTIFY batteryLevelChanged FINAL)
    Q_PROPERTY(bool charging READ charging NOTIFY chargingChanged FINAL)

public:
    static RemoteScaleDevice* create(QQmlEngine* engine, QJSEngine* jsEngine);
    bool connected() const { return machineUi().scaleConnected; }
    bool isFlowScale() const { return machineUi().scaleIsFlowScale; }
    QString name() const { return machineUi().scaleName; }
    int batteryLevel() const { return machineUi().scaleBatteryLevel; }
    bool charging() const { return machineUi().scaleCharging; }

signals:
    void connectedChanged();
    void isFlowScaleChanged();
    void nameChanged();
    void batteryLevelChanged();
    void chargingChanged();

private:
    explicit RemoteScaleDevice(QObject* parent);
};

static_assert(!std::is_default_constructible_v<RemoteScaleDevice>,
              "The QML singleton must use its engine-owned factory.");
