// Whole file compiles to nothing without DECENZA_SIMULATOR — see the rationale
// in CMakeLists.txt. (moc then reports "No relevant classes found" for the
// headers on those builds; that note is expected, not a fault.)
#ifdef DECENZA_SIMULATOR

#include "simulatedscale.h"
#include "ble/scales/scalelogging.h"
#include <QDebug>
#include <QDateTime>

// Alias the shared scale helpers — never copy a body. Same reasoning as
// de1simulator.cpp: the simulated scale IS the scale as far as the app is
// concerned, so it belongs under [Scale] beside the real drivers, with
// "Simulator" naming which one it is. These were three bare
// `qDebug() << "SimulatedScale: ..."` lines that no [Scale] search returned, so a
// sim session's scale view showed connects and tares from real hardware and
// nothing from the scale it was actually using.
//
// STDERR variants: ScaleDevice has a logMessage signal, but these three fire from
// contexts where the paired-emit adds nothing the marker does not already give.
#define SIMSCALE_INFO(msg) SCALE_INFO_STDERR_TAGGED("Simulator", msg)
#define SIMSCALE_LOG(msg)  SCALE_LOG_STDERR_TAGGED("Simulator", msg)

SimulatedScale::SimulatedScale(QObject* parent)
    : ScaleDevice(parent)
{
}

void SimulatedScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    Q_UNUSED(device);
    // Simulation handles connection via simulateConnection()
}

void SimulatedScale::simulateConnection() {
    SIMSCALE_INFO(QStringLiteral("Simulated scale connected"));
    setConnected(true);
}

void SimulatedScale::simulateDisconnection() {
    SIMSCALE_INFO(QStringLiteral("Simulated scale disconnected"));
    m_lastTime = 0;
    setConnected(false);
}

void SimulatedScale::tare() {
    m_tareOffset = m_currentWeight;
    m_lastWeight = 0.0;
    m_lastTime = 0;
    setWeight(0.0);
    setFlowRate(0.0);
    SIMSCALE_LOG(QStringLiteral("Tared at %1 g").arg(m_tareOffset, 0, 'f', 2));
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
