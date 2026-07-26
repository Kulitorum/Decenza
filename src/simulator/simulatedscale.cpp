// Whole file compiles to nothing without DECENZA_SIMULATOR — see the rationale
// in CMakeLists.txt. (moc then reports "No relevant classes found" for the
// headers on those builds; that note is expected, not a fault.)
#ifdef DECENZA_SIMULATOR

#include "simulatedscale.h"
#include <QDebug>
#include <QDateTime>

SimulatedScale::SimulatedScale(QObject* parent)
    : ScaleDevice(parent)
{
}

void SimulatedScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    Q_UNUSED(device);
    // Simulation handles connection via simulateConnection()
}

void SimulatedScale::simulateConnection() {
    qDebug() << "SimulatedScale: Connected";
    setConnected(true);
}

void SimulatedScale::simulateDisconnection() {
    qDebug() << "SimulatedScale: Disconnected";
    m_lastTime = 0;
    setConnected(false);
}

void SimulatedScale::tare() {
    m_tareOffset = m_currentWeight;
    m_lastWeight = 0.0;
    m_lastTime = 0;
    setWeight(0.0);
    setFlowRate(0.0);
    qDebug() << "SimulatedScale: Tared at" << m_tareOffset << "g";
}

void SimulatedScale::setSimulatedWeight(double weight) {
    // Unlike a physical scale — which tares inside the device, so setWeight()
    // always receives an already-correct number — this scale publishes
    // (raw - m_tareOffset), and the RAW value belongs to DE1Simulator, which
    // zeroes its yield accumulator at the start of every operation. If the app's
    // tare captures an offset and the simulator then restarts from zero (the two
    // are driven by independent events and either can land first), every later
    // reading is negative for the whole operation. That is invisible in the flow
    // rate — a derivative, so the constant offset cancels and the graph looks
    // perfect — but the espresso readout and CupFillView both clamp
    // MachineState.scaleWeight at zero, so the cup shows "0.0 g" and never
    // fills while the shot is plainly extracting.
    //
    // A raw value below the offset can only mean that reset happened, because
    // this scale has no cup to lift off: the producer is monotonic within an
    // operation and drops only when it restarts. So treat the drop as the new
    // zero instead of publishing a negative weight.
    if (weight < m_tareOffset) {
        m_tareOffset = weight;
        m_lastWeight = 0.0;
        m_lastTime = 0;
    }

    m_currentWeight = weight;
    double displayWeight = weight - m_tareOffset;

    // Calculate flow rate from weight change
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (m_lastTime > 0 && now > m_lastTime) {
        double dt = (now - m_lastTime) / 1000.0;
        if (dt > 0 && dt < 1.0) {
            double flowRate = (displayWeight - m_lastWeight) / dt;
            setFlowRate(qMax(0.0, flowRate));
        }
    }

    m_lastWeight = displayWeight;
    m_lastTime = now;

    setWeight(displayWeight);
}

#endif  // DECENZA_SIMULATOR
