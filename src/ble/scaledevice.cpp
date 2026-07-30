#include "scaledevice.h"

#include "core/logtags.h"
#include "scales/scalelogging.h"
#include <QDebug>

ScaleDevice::ScaleDevice(QObject* parent)
    : QObject(parent)
{
    m_keepAliveTimer.setInterval(30000);
    connect(&m_keepAliveTimer, &QTimer::timeout, this, &ScaleDevice::sendKeepAlive);
}

ScaleDevice::~ScaleDevice() {
    // The subclass is already gone by the time we get here, so anything that
    // reaches back through it is unsafe: name() resolved to the base override,
    // logging `[Scale] "" DISCONNECTED` on every teardown, and
    // connectedChanged/batteryLevelChanged would be delivered to slots that
    // legitimately expect a live scale. m_destroying (an event-based flag set
    // exactly here) makes setConnected() do the bookkeeping and stay quiet.
    m_destroying = true;
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

    // This method IS the deliberate close — every caller reached it by choosing
    // to disconnect (DE1 going to sleep, app exit, the user disconnecting in
    // Settings). Marking here rather than at each call site is what makes the
    // tier correct for all 13 drivers instead of the one that happened to be
    // wired first: markExpectedDisconnect() had a single caller in
    // DecentScaleWifi, so every Bluetooth scale still reported a deliberate
    // DE1-sleep close as a fault, while this class's own comment claimed
    // otherwise.
    //
    // The destructor path also arrives here, and stays silent regardless —
    // setConnected() returns early on m_destroying before any logging.
    markExpectedDisconnect();
    setConnected(false);
}

void ScaleDevice::setConnected(bool connected) {
    if (m_connected != connected) {
        m_connected = connected;
        if (m_destroying) {
            // Destructor path — record the state, drop the timer, emit nothing.
            m_keepAliveTimer.stop();
            return;
        }
        if (connected) {
            // Any pending expected-disconnect mark is stale by definition once we
            // are connected again — it described a close that never happened.
            // Defence in depth: the drivers now hand the flag over immediately
            // before the transition, but a mark orphaned by an early return would
            // otherwise sit until the next drop and downgrade it.
            m_expectedDisconnect = false;
            // qInfo, not qDebug: this is the canonical "the scale is usable now"
            // line for EVERY driver, so it is the one event a user most needs in
            // the connections view — and the view shows INFO and above. Its
            // DISCONNECTED counterpart below is already WARN, so a connect at
            // DEBUG meant the log showed scales dropping and never arriving.
            // "[Scale][ScaleDevice]", not a bare "[Scale]". Every other line in this
            // subsystem carries a source tag, and in a real log this one stood out as
            // the only exception — visibly so, since it sits directly above
            // "[Scale][USB Scale] Polling started" describing the same device. A
            // reader filtering the subsystem down to one source lost precisely the
            // line that says the scale started working.
            //
            // The tag is this base class, deliberately, rather than the driver: the
            // line is emitted here for all 13 of them, and naming the driver would
            // mean either a virtual or a per-driver copy of the wording, which is
            // how "First weight received" and "Scale confirmed working" became two
            // names for one event. The scale's own NAME is in the message.
            SCALE_INFO_STDERR_TAGGED("ScaleDevice",
                QStringLiteral("%1 CONNECTED").arg(name()));
            m_keepAliveTimer.start();
        } else {
            // WARN only when the link dropped on its own. A deliberate close —
            // DE1 going to sleep, app exit, the user disconnecting — is narrative,
            // not a fault, and INFO keeps it in the connections view without
            // spending the tier that means "look here".
            //
            // Validated both ways against real logs: a user's 25,720-line capture
            // has eight disconnects, every one preceded by
            // "CONTROLLER ERROR: ConnectionError" — genuine faults, correctly WARN.
            // A maintainer's log has the opposite case, "WebSocket disconnected
            // (expected) — scale power-off: disabled" followed immediately by this
            // line at WARN. A blanket demotion would have been wrong for the first
            // log; a blanket WARN is wrong for the second. Hence the flag.
            const bool expected = m_expectedDisconnect;
            m_expectedDisconnect = false;
            if (expected) {
                SCALE_INFO_STDERR_TAGGED("ScaleDevice",
                    QStringLiteral("%1 DISCONNECTED (expected)").arg(name()));
            } else {
                SCALE_WARN_STDERR_TAGGED("ScaleDevice",
                    QStringLiteral("%1 DISCONNECTED").arg(name()));
            }
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
