#pragma once

#include <QObject>
#include <QBluetoothDeviceInfo>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QTimer>

class ScaleBleTransport;

class ScaleDevice : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(bool simulationMode READ simulationMode WRITE setSimulationMode NOTIFY simulationModeChanged)
    Q_PROPERTY(double weight READ weight NOTIFY weightChanged)
    Q_PROPERTY(double flowRate READ flowRate NOTIFY flowRateChanged)
    Q_PROPERTY(int batteryLevel READ batteryLevel NOTIFY batteryLevelChanged)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(bool isFlowScale READ isFlowScale CONSTANT)
    Q_PROPERTY(bool isSimulated READ isSimulated CONSTANT)

public:
    explicit ScaleDevice(QObject* parent = nullptr);
    virtual ~ScaleDevice();

    virtual void connectToDevice(const QBluetoothDeviceInfo& device) = 0;

    bool isConnected() const;
    double weight() const { return m_weight; }
    double flowRate() const { return m_flowRate; }
    int batteryLevel() const { return m_batteryLevel; }
    virtual QString name() const { return QString(); }
    virtual QString type() const { return QString(); }
    virtual bool isFlowScale() const { return false; }
    virtual bool isSimulated() const { return false; }

    bool simulationMode() const { return m_simulationMode; }
    void setSimulationMode(bool enabled);

    // The underlying BLE transport, when this scale is BLE-backed (null for
    // FlowScale / USB / simulated). Set once by ScaleFactory at creation —
    // the single scale-agnostic chokepoint, so no per-driver code is needed.
    // Lets the connection-priority detection (wired in main.cpp) reach the
    // shared transport without exposing each driver's private member.
    ScaleBleTransport* bleTransport() const { return m_bleTransport; }
    void setBleTransport(ScaleBleTransport* transport) { m_bleTransport = transport; }

public slots:
    virtual void tare() = 0;
    virtual void startTimer() {}
    virtual void stopTimer() {}
    virtual void resetTimer() {}
    // Whether resetTimer() is a true no-side-effect reset (distinct from startTimer/tare).
    // False for scales where resetTimer() has side effects (e.g., DiFluid sends same bytes
    // as startTimer, Eclair delegates to tare). When false, MachineState sends reset+start
    // together at extraction start instead of splitting them across the preheating phase.
    virtual bool hasIndependentTimerReset() const { return true; }
    virtual void sleep() { emit sleepCompleted(); }  // Put scale to sleep (battery power saving - full power off)
    virtual void wake() {}   // Wake scale from sleep (enable LCD)
    virtual void disableLcd() {}  // Turn off LCD but keep scale powered (for screensaver)
    virtual void sendKeepAlive() {}  // Override to send BLE keepalive (e.g., re-enable notifications)
    virtual void disconnectFromScale();  // Disconnect BLE from scale
    void resetFlowCalculation();  // Call after tare to avoid flow rate spikes

    // Flow sample input (used by FlowScale to integrate flow into weight)
    // Physical scales ignore this - they get weight directly from the device
    virtual void addFlowSample(double flowRate, double deltaTime) { Q_UNUSED(flowRate); Q_UNUSED(deltaTime); }

signals:
    void connectedChanged();
    void weightChanged(double weight);
    // Liveness signal: emitted for every weight sample that passes through
    // setWeight() — including ones whose value equals the previous reading.
    // (Synthetic resets that emit weightChanged directly, e.g.
    // setSimulationMode(), are not device samples and deliberately do not emit
    // this.) weightChanged is intentionally deduped (drives the `weight`
    // Q_PROPERTY / QML bindings, and onScaleWeightChanged which feeds MQTT — a
    // constant value must not churn those). But the scale-feed stall detector
    // and SAW
    // de-jitter need sample *arrival*, not value *change*: a healthy scale
    // reporting a constant weight (a static cup through DE1 preheat) is
    // otherwise indistinguishable from a dead feed and trips a false stall →
    // mid-shot connection-priority backoff / ruined shot. See #1176, #1185.
    // WeightProcessor::processWeight is wired to THIS, never weightChanged.
    void weightSampleReceived(double weight);
    void flowRateChanged(double rate);
    void batteryLevelChanged(int level);
    void buttonPressed(int button);
    void errorOccurred(const QString& error);
    void simulationModeChanged();
    void sleepCompleted();  // Emitted when the sleep BLE write completes (or immediately if no confirmation possible)
    void logMessage(const QString& message);  // For debug logging to UI/file

protected:
    void setConnected(bool connected);
    void setWeight(double weight);
    void setFlowRate(double rate);
    void setBatteryLevel(int level);

    QLowEnergyController* m_controller = nullptr;
    QLowEnergyService* m_service = nullptr;

private:
    ScaleBleTransport* m_bleTransport = nullptr;  // Not owned (lives inside the concrete driver)
    bool m_connected = false;
    bool m_simulationMode = false;
    double m_weight = 0.0;
    double m_flowRate = 0.0;
    int m_batteryLevel = -1;  // -1 = not reported by this scale
    QTimer m_keepAliveTimer;
};
