#pragma once

#include "scaledevice.h"

#include <QObject>
#include <QPointer>
#include <QString>

// A stable QML identity for whichever scale is currently live.
//
// WHY THIS EXISTS
// ---------------
// `ScaleDevice` was the last context property, and the reason it stayed one is that it is not a
// single object: main() re-points the name at a FlowScale, a physical BLE scale, a USB scale or a
// simulated scale as the user connects and disconnects hardware — eleven distinct
// setContextProperty() sites. A context property can be re-pointed; a QML_SINGLETON cannot, since
// a singleton is created once per type.
//
// So the singleton is this proxy, and it is the thing that gets re-pointed. QML says
// `ScaleDevice.connected` exactly as before and neither knows nor cares which driver is behind it.
//
// WHAT CHANGES FOR QML — READ THIS BEFORE ADDING A NULL GUARD
// ----------------------------------------------------------
// The proxy is NEVER null. Existing guards of the shape `ScaleDevice && ScaleDevice.connected`
// still read correctly, because with no target every property returns its disconnected default —
// `connected` false, `batteryLevel` -1, `name` empty. But a guard that tests the OBJECT rather
// than the STATE (`ScaleDevice !== null`, `typeof ScaleDevice !== "undefined"`) is now always
// true and no longer guards anything. Write `ScaleDevice.connected`, not `ScaleDevice`.
//
// This is the same trap QML_GOTCHAS.md records for USBManager, and it is worth repeating here
// because it is the one way this change can break a call site silently.
//
// WHY EVERY SLOT IS FORWARDED, NOT JUST THE ONES QML CALLS TODAY
// --------------------------------------------------------------
// A context property exposed the whole public slot set. Forwarding only the four QML happens to
// call today would silently delete the rest from QML's reach — a functional regression that no
// tool would report, since the calls would parse and simply do nothing. The surface is preserved
// deliberately, and the gate now checks every one of them.
//
// This claim was FALSE when first written, and the #1687 review caught it: three of ScaleDevice's
// twelve public slots — resetFlowCalculation(), addFlowSample() and hasIndependentTimerReset() —
// were not forwarded. No call site broke, because all three are reached only from C++ through a
// direct ScaleDevice*, but the omission is exactly the silent hole the paragraph above warns
// about, and QML_GOTCHAS.md cites this class as the worked example of getting it right. They are
// forwarded now. If you add a public slot to ScaleDevice, add it here in the same edit.
//
// LIFETIME
// --------
// m_target is a QPointer: the concrete scale objects are owned elsewhere and outlive nothing in
// particular — a BLE scale is destroyed on disconnect. QPointer nulls itself, so a stale target
// degrades to "no scale" rather than a dangling read. setTarget() also disconnects the previous
// target explicitly, so a destroyed scale cannot deliver a queued signal through the proxy.
class ScaleDeviceProxy : public QObject {
    Q_OBJECT

    // Mirrors ScaleDevice's own property set. Three of them (name, isFlowScale, isSimulated) are
    // CONSTANT on ScaleDevice and are NOT constant here: they are properties of *which scale is
    // attached*, and that is exactly what this class changes. A CONSTANT here would leave every
    // binding on a scale's name showing the previous scale's.
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool simulationMode READ simulationMode WRITE setSimulationMode NOTIFY simulationModeChanged)
    Q_PROPERTY(double weight READ weight NOTIFY weightChanged)
    Q_PROPERTY(double flowRate READ flowRate NOTIFY flowRateChanged)
    Q_PROPERTY(int batteryLevel READ batteryLevel NOTIFY batteryLevelChanged)
    Q_PROPERTY(bool charging READ charging NOTIFY chargingChanged)
    Q_PROPERTY(QString name READ name NOTIFY targetChanged)
    Q_PROPERTY(bool isFlowScale READ isFlowScale NOTIFY targetChanged)
    Q_PROPERTY(bool isSimulated READ isSimulated NOTIFY targetChanged)

public:
    explicit ScaleDeviceProxy(QObject* parent = nullptr);

    // Re-point at a different scale (or at nothing). Disconnects the previous target, connects the
    // new one, and emits every change signal so bindings re-evaluate against the new device even
    // where the underlying value happens to be unchanged.
    void setTarget(ScaleDevice* target);
    ScaleDevice* target() const { return m_target; }

    bool connected() const { return m_target && m_target->isConnected(); }
    bool simulationMode() const { return m_target && m_target->simulationMode(); }
    double weight() const { return m_target ? m_target->weight() : 0.0; }
    double flowRate() const { return m_target ? m_target->flowRate() : 0.0; }
    // -1 rather than 0: ScaleDevice uses it for "this scale does not report battery", and a
    // proxy with no scale attached is at least as unknown as that.
    int batteryLevel() const { return m_target ? m_target->batteryLevel() : -1; }
    bool charging() const { return m_target && m_target->charging(); }
    QString name() const { return m_target ? m_target->name() : QString(); }
    bool isFlowScale() const { return m_target && m_target->isFlowScale(); }
    bool isSimulated() const { return m_target && m_target->isSimulated(); }

    void setSimulationMode(bool enabled);

public slots:
    // Every one of these is a no-op with no target, which is the same thing QML got before when
    // the context property pointed at a scale that had gone away.
    void tare();
    void startTimer();
    void stopTimer();
    void resetTimer();
    void sleep();
    void wake();
    void disableLcd();
    void sendKeepAlive();
    void disconnectFromScale();
    void resetFlowCalculation();
    void addFlowSample(double flowRate, double deltaTime);

    // Not a command — a query, and the one member here that needs a defined answer with no target.
    // MachineState uses it to decide whether resetTimer() may be sent on its own; with no scale
    // attached there is nothing to send either way, and `true` is ScaleDevice's own base default.
    bool hasIndependentTimerReset() const;

signals:
    void connectedChanged();
    void weightChanged(double weight);
    void weightSampleReceived(double weight);
    void flowRateChanged(double rate);
    void batteryLevelChanged(int level);
    void chargingChanged(bool charging);
    void buttonPressed(int button);
    void errorOccurred(const QString& error);
    void simulationModeChanged();
    void sleepCompleted();
    void logMessage(const QString& message);

    // Fired when the attached scale itself changes. Drives the three properties that are facts
    // about the device rather than about its readings.
    void targetChanged();

private:
    QPointer<ScaleDevice> m_target;
};
