#pragma once

// Whole file compiles to nothing without DECENZA_SIMULATOR — see the rationale
// in CMakeLists.txt. (moc then reports "No relevant classes found" for the
// headers on those builds; that note is expected, not a fault.)
#ifdef DECENZA_SIMULATOR

#include "../ble/scaledevice.h"

/**
 * SimulatedScale - A virtual scale for simulation mode
 *
 * Integrates with the scale infrastructure just like physical scales.
 * Receives weight updates from DE1Simulator.
 */
class SimulatedScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit SimulatedScale(QObject* parent = nullptr);

    // ScaleDevice interface
    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return "Simulated Scale"; }
    QString type() const override { return "simulated"; }
    bool isSimulated() const override { return true; }

public slots:
    void tare() override;

    // Called by DE1Simulator to update weight
    void setSimulatedWeight(double weight);

    // Simulate connection (called when simulation starts)
    void simulateConnection();
    void simulateDisconnection();

private:
    double m_currentWeight = 0.0;
    double m_tareOffset = 0.0;
    double m_lastWeight = 0.0;
    qint64 m_lastTime = 0;
};

#endif  // DECENZA_SIMULATOR
