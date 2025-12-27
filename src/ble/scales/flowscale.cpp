#include "flowscale.h"
#include "../../core/settings.h"
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

void FlowScale::tare() {
    m_accumulatedWeight = 0.0;
    setWeight(0.0);
    setFlowRate(0.0);
}

void FlowScale::addFlowSample(double flowRate, double deltaTime) {
    // Integrate flow: weight += flow_rate * time * calibration
    // flowRate is in mL/s, deltaTime is in seconds
    // Calibration factor from settings (default 0.78 based on original testing)
    double calibrationFactor = m_settings ? m_settings->flowCalibrationFactor() : 0.78;
    if (deltaTime > 0 && deltaTime < 1.0) {  // Sanity check
        // Track raw (uncalibrated) integral for calibration purposes
        double rawIncrement = flowRate * deltaTime;
        m_rawFlowIntegral += rawIncrement;
        emit rawFlowIntegralChanged();

        // Apply calibration for displayed weight
        m_accumulatedWeight += rawIncrement * calibrationFactor;
        setWeight(m_accumulatedWeight);
        setFlowRate(flowRate * calibrationFactor);
    }
}

void FlowScale::reset() {
    m_accumulatedWeight = 0.0;
    setWeight(0.0);
    setFlowRate(0.0);
}

void FlowScale::resetWeight() {
    tare();  // Same as tare - reset accumulated weight
}
