#pragma once

#include "remotemachineuisource.h"
#include <type_traits>

class QQmlEngine;
class QJSEngine;

// Native-name facade for the paired host's PORTAL connection/readout state.
class RemoteBelkaPortal final : public RemoteMachineUiSource
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BelkaPortal)
    QML_SINGLETON

    Q_PROPERTY(QString state READ state NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(bool active READ active NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(bool hasReading READ hasReading NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double temperatureC READ temperatureC NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double ecRaw READ ecRaw NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(bool owned READ owned NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(bool machineBusy READ machineBusy NOTIFY machineUiChanged FINAL)

public:
    static RemoteBelkaPortal* create(QQmlEngine* engine, QJSEngine* jsEngine);

    QString state() const { return machineUi().portalState; }
    bool active() const { return machineUi().portalActive; }
    bool hasReading() const { return machineUi().portalHasReading; }
    double temperatureC() const { return machineUi().portalTemperatureC; }
    double ecRaw() const { return machineUi().portalEcRaw; }
    bool owned() const { return machineUi().portalOwned; }
    bool machineBusy() const { return machineUi().portalMachineBusy; }

private:
    explicit RemoteBelkaPortal(QObject* parent);
};

static_assert(!std::is_default_constructible_v<RemoteBelkaPortal>,
              "The QML singleton must use its engine-owned factory.");
