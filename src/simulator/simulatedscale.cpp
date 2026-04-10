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
