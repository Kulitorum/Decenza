#include "scaledevice.h"
#include <QDebug>

ScaleDevice::ScaleDevice(QObject* parent)
    : QObject(parent)
{
    m_keepAliveTimer.setInterval(30000);
    connect(&m_keepAliveTimer, &QTimer::timeout, this, &ScaleDevice::sendKeepAlive);
}

ScaleDevice::~ScaleDevice() {
    disconnectFromScale();
}

bool ScaleDevice::isConnected() const {
    // Simulation mode always reports connected
    if (m_simulationMode) {
        return true;
    }
    return m_connected;
}

void ScaleDevice::setSimulationMode(bool enabled) {
    if (m_simulationMode == enabled) {
        return;
    }
    m_simulationMode = enabled;

    if (enabled) {
        // Set some default simulated state
        m_weight = 0.0;
        m_flowRate = 0.0;
        m_batteryLevel = 85;
        // Keep the liveness contract intact: every weight value this layer
        // publishes also goes out on weightSampleReceived (the stall detector
        // and SAW path listen only to that, never weightChanged). #1176.
        emit weightSampleReceived(m_weight);
        emit weightChanged(m_weight);
        emit flowRateChanged(m_flowRate);
        emit batteryLevelChanged(m_batteryLevel);
    }

    emit simulationModeChanged();
    emit connectedChanged();
}

void ScaleDevice::disconnectFromScale() {
    m_keepAliveTimer.stop();
    if (m_service) {
        // Disconnect signals first to prevent callbacks during deletion
        m_service->disconnect();
        // Use deleteLater() for safe cleanup when called from signal handlers
        m_service->deleteLater();
        m_service = nullptr;
    }

    if (m_controller) {
        // Disconnect signals first to prevent callbacks during deletion
        m_controller->disconnect();
        // Only try to disconnect if controller is in a connected state
        // Avoid calling methods on an errored controller (e.g., GATT error 133)
        if (m_controller->state() == QLowEnergyController::ConnectedState ||
            m_controller->state() == QLowEnergyController::DiscoveringState) {
            m_controller->disconnectFromDevice();
        }
        // Use deleteLater() for safe cleanup when called from signal handlers
        m_controller->deleteLater();
        m_controller = nullptr;
    }

    setConnected(false);
}

void ScaleDevice::setConnected(bool connected) {
    if (m_connected != connected) {
        m_connected = connected;
        if (connected) {
            qDebug() << "[Scale]" << name() << "CONNECTED";
            m_keepAliveTimer.start();
        } else {
            qWarning() << "[Scale]" << name() << "DISCONNECTED";
            m_keepAliveTimer.stop();
            setBatteryLevel(-1);   // Clear stale reading for reconnect
            setCharging(false);    // Mirror — the next status frame will re-assert if still charging
        }
        emit connectedChanged();
    }
}

void ScaleDevice::setWeight(double weight) {
    // Unconditional: a sample arrived. Drives the scale-feed stall detector and
    // SAW de-jitter, which must track sample arrival, not value change (#1176).
    emit weightSampleReceived(weight);
    // Deduped: only on a genuine value change. Drives the `weight` Q_PROPERTY
    // and QML bindings (and onScaleWeightChanged, which feeds MQTT) — a
    // constant reading must not churn those.
    if (m_weight != weight) {
        m_weight = weight;
        emit weightChanged(weight);
    }
}

void ScaleDevice::setFlowRate(double rate) {
    if (m_flowRate != rate) {
        m_flowRate = rate;
        emit flowRateChanged(rate);
    }
}

void ScaleDevice::setBatteryLevel(int level) {
    if (m_batteryLevel != level) {
        m_batteryLevel = level;
        emit batteryLevelChanged(level);
    }
}

void ScaleDevice::setCharging(bool charging) {
    if (m_charging != charging) {
        m_charging = charging;
        emit chargingChanged(charging);
    }
}

void ScaleDevice::resetFlowCalculation() {
    // Flow rate is now computed centrally via LSLR in MachineState::smoothedScaleFlowRate().
    // This method is kept for callers that reset after tare.
    setFlowRate(0.0);
}
