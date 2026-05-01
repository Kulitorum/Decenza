#include "flowscale.h"
#include "../../core/settings.h"
#include <QDateTime>
#include <QDebug>

FlowScale::FlowScale(QObject* parent)
    : ScaleDevice(parent)
{
    // FlowScale is always "connected" since it's virtual
    setConnected(true);
}

void FlowScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    Q_UNUSED(device);
    // No-op - FlowScale doesn't use BLE
}

void FlowScale::setSettings(Settings* settings) {
    m_settings = settings;
}

void FlowScale::setDose(double doseGrams) {
    m_dose = doseGrams;
}

void FlowScale::tare() {
    m_accumulatedWeight = 0.0;
    m_rawFlowIntegral = 0.0;
    setWeight(0.0);
    setFlowRate(0.0);
}

void FlowScale::addFlowSample(double flowRate, double deltaTime) {
    // Skip integration if FlowScale is disabled (redundant on shadow path since MainController checks useFlowScale)
    if (m_settings && !m_settings->useFlowScale())
        return;

    // Integrate flow: raw_integral += flow_rate * time
    // flowRate is in mL/s, deltaTime is in seconds
    if (deltaTime > 0 && deltaTime < 1.0) {  // Sanity check
        double rawIncrement = flowRate * deltaTime;
        m_rawFlowIntegral += rawIncrement;
        emit rawFlowIntegralChanged();

        // Recalculate estimated cup weight
        updateEstimatedWeight();
        setFlowRate(flowRate);
    }
}

void FlowScale::updateEstimatedWeight() {
    // Model: cup_weight = raw_flow_integral - puck_absorption
    // Puck absorption = dose × 0.95 + 6.0
    //
    // Empirical testing with two dose sizes shows puck absorption has two components:
    //   - Fixed base (~6g): water retained by group head, shower screen, basket
    //   - Dose-proportional (~0.95× dose): water absorbed by the coffee puck
    //
    // Validated against:
    //   - 22g dose: predicted 26.9g retention vs 26.6g actual (during active pour)
    //   - 14.5g dose: predicted 19.8g retention vs 19.5g actual (during active pour)
    //   - Gives ±0.5g accuracy during pouring
    double puckAbsorption = m_dose * 0.95 + 6.0;

    double estimatedCupWeight = m_rawFlowIntegral - puckAbsorption;
    if (estimatedCupWeight < 0.0)
        estimatedCupWeight = 0.0;

    m_accumulatedWeight = estimatedCupWeight;
    setWeight(estimatedCupWeight);
}

void FlowScale::reset() {
    m_accumulatedWeight = 0.0;
    m_rawFlowIntegral = 0.0;
    setWeight(0.0);
    setFlowRate(0.0);
}

void FlowScale::resetWeight() {
    tare();  // Same as tare - reset accumulated weight
}

void FlowScale::setSimulatedWeight(double weight) {
    // Directly set weight from simulator (bypasses flow integration)
    m_accumulatedWeight = weight;
    setWeight(weight);

    // Estimate flow rate from weight change (for display)
    // This is approximate but good enough for simulation
    static double lastWeight = 0.0;
    static qint64 lastTime = 0;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (lastTime > 0 && now > lastTime) {
        double dt = (now - lastTime) / 1000.0;
        if (dt > 0 && dt < 1.0) {
            double flowRate = (weight - lastWeight) / dt;
            setFlowRate(qMax(0.0, flowRate));
        }
    }
    lastWeight = weight;
    lastTime = now;
}
