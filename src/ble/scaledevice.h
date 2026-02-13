#pragma once

#include <QObject>
#include <QBluetoothDeviceInfo>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QList>

class ScaleDevice : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(bool simulationMode READ simulationMode WRITE setSimulationMode NOTIFY simulationModeChanged)
    Q_PROPERTY(double weight READ weight NOTIFY weightChanged)
    Q_PROPERTY(double flowRate READ flowRate NOTIFY flowRateChanged)
    Q_PROPERTY(int batteryLevel READ batteryLevel NOTIFY batteryLevelChanged)
    Q_PROPERTY(QString name READ name CONSTANT)

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

    bool simulationMode() const { return m_simulationMode; }
    void setSimulationMode(bool enabled);

public slots:
    virtual void tare() = 0;
    virtual void startTimer() {}
    virtual void stopTimer() {}
    virtual void resetTimer() {}
    virtual void sleep() {}  // Put scale to sleep (battery power saving - full power off)
    virtual void wake() {}   // Wake scale from sleep (enable LCD)
    virtual void disableLcd() {}  // Turn off LCD but keep scale powered (for screensaver)
    virtual void disconnectFromScale();  // Disconnect BLE from scale
    void resetFlowCalculation();  // Call after tare to avoid flow rate spikes

    // Flow sample input (used by FlowScale to integrate flow into weight)
    // Physical scales ignore this - they get weight directly from the device
    virtual void addFlowSample(double flowRate, double deltaTime) { Q_UNUSED(flowRate); Q_UNUSED(deltaTime); }

signals:
    void connectedChanged();
    void weightChanged(double weight);
    void flowRateChanged(double rate);
    void batteryLevelChanged(int level);
    void buttonPressed(int button);
    void errorOccurred(const QString& error);
    void simulationModeChanged();
    void logMessage(const QString& message);  // For debug logging to UI/file

protected:
    void setConnected(bool connected);
    void setWeight(double weight);
    void setFlowRate(double rate);
    void setBatteryLevel(int level);
    void calculateFlowRate(double newWeight);

    QLowEnergyController* m_controller = nullptr;
    QLowEnergyService* m_service = nullptr;

private:
    bool m_connected = false;
    bool m_simulationMode = false;
    double m_weight = 0.0;
    double m_flowRate = 0.0;
    int m_batteryLevel = 100;

    // Flow rate calculation
    double m_prevWeight = 0.0;
    qint64 m_prevTimestamp = 0;
    QList<double> m_flowHistory;
    static const int FLOW_HISTORY_SIZE = 5;
};
