#pragma once

#include "remotemachineuisource.h"
#include <type_traits>

class QQmlEngine;
class QJSEngine;

class RemoteDE1Device final : public RemoteMachineUiSource
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DE1Device)
    QML_SINGLETON

    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged FINAL)
    Q_PROPERTY(bool guiEnabled READ guiEnabled NOTIFY guiEnabledChanged FINAL)
    Q_PROPERTY(bool simulationMode READ simulationMode NOTIFY simulationModeChanged FINAL)
    Q_PROPERTY(bool isHeadless READ isHeadless NOTIFY isHeadlessChanged FINAL)
    Q_PROPERTY(int state READ stateInt NOTIFY stateChanged FINAL)
    Q_PROPERTY(int subState READ subStateInt NOTIFY subStateChanged FINAL)
    Q_PROPERTY(QString stateString READ stateString NOTIFY stateStringChanged FINAL)
    Q_PROPERTY(QString subStateString READ subStateString NOTIFY subStateStringChanged FINAL)
    Q_PROPERTY(double temperature READ temperature NOTIFY temperatureChanged FINAL)
    Q_PROPERTY(double pressure READ pressure NOTIFY pressureChanged FINAL)
    Q_PROPERTY(double flow READ flow NOTIFY flowChanged FINAL)
    Q_PROPERTY(double steamTemperature READ steamTemperature NOTIFY steamTemperatureChanged FINAL)
    Q_PROPERTY(double waterLevel READ waterLevel NOTIFY waterLevelChanged FINAL)
    Q_PROPERTY(double waterLevelMm READ waterLevelMm NOTIFY waterLevelMmChanged FINAL)
    Q_PROPERTY(int waterLevelMl READ waterLevelMl NOTIFY waterLevelMlChanged FINAL)
    Q_PROPERTY(int refillKitDetected READ refillKitDetected NOTIFY refillKitDetectedChanged FINAL)

public:
    static RemoteDE1Device* create(QQmlEngine* engine, QJSEngine* jsEngine);
    bool connected() const { return machineUi().de1Connected; }
    bool guiEnabled() const { return machineUi().de1GuiEnabled; }
    bool simulationMode() const { return machineUi().simulationMode; }
    bool isHeadless() const { return machineUi().de1IsHeadless; }
    int stateInt() const { return machineUi().de1State; }
    int subStateInt() const { return machineUi().de1SubState; }
    QString stateString() const { return machineUi().stateString; }
    QString subStateString() const { return machineUi().subStateString; }
    double temperature() const { return machineUi().temperature; }
    double pressure() const { return session() ? session()->pressureBar() : 0.0; }
    double flow() const { return session() ? session()->flowMlPerSecond() : 0.0; }
    double steamTemperature() const { return machineUi().steamTemperature; }
    double waterLevel() const { return machineUi().waterLevel; }
    double waterLevelMm() const { return machineUi().waterLevelMm; }
    int waterLevelMl() const { return machineUi().waterLevelMl; }
    int refillKitDetected() const { return machineUi().refillKitDetected; }

signals:
    void connectedChanged();
    void guiEnabledChanged();
    void simulationModeChanged();
    void isHeadlessChanged();
    void stateChanged();
    void subStateChanged();
    void stateStringChanged();
    void subStateStringChanged();
    void temperatureChanged();
    void pressureChanged();
    void flowChanged();
    void steamTemperatureChanged();
    void waterLevelChanged();
    void waterLevelMmChanged();
    void waterLevelMlChanged();
    void refillKitDetectedChanged();

private:
    explicit RemoteDE1Device(QObject* parent);
};

static_assert(!std::is_default_constructible_v<RemoteDE1Device>,
              "The QML singleton must use its engine-owned factory.");
