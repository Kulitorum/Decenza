#include "scaledeviceproxy.h"

ScaleDeviceProxy::ScaleDeviceProxy(QObject* parent)
    : QObject(parent)
{
}

void ScaleDeviceProxy::setTarget(ScaleDevice* target)
{
    if (m_target == target)
        return;

    if (m_target)
        disconnect(m_target, nullptr, this, nullptr);

    m_target = target;

    if (m_target) {
        // Re-emit, one for one. Every signal ScaleDevice declares is forwarded, including the ones
        // no QML file listens to today: a Connections block added later must not find that half
        // the scale's signals stop at the proxy.
        //
        // weightSampleReceived is deliberately distinct from weightChanged and must stay so —
        // see the comment on its declaration in scaledevice.h. Collapsing the two here would
        // reintroduce #1176/#1185 (a healthy scale reporting a constant weight reads as a dead
        // feed) one layer up from where they were fixed.
        connect(m_target, &ScaleDevice::connectedChanged, this, &ScaleDeviceProxy::connectedChanged);
        connect(m_target, &ScaleDevice::weightChanged, this, &ScaleDeviceProxy::weightChanged);
        connect(m_target, &ScaleDevice::weightSampleReceived, this, &ScaleDeviceProxy::weightSampleReceived);
        connect(m_target, &ScaleDevice::flowRateChanged, this, &ScaleDeviceProxy::flowRateChanged);
        connect(m_target, &ScaleDevice::batteryLevelChanged, this, &ScaleDeviceProxy::batteryLevelChanged);
        connect(m_target, &ScaleDevice::chargingChanged, this, &ScaleDeviceProxy::chargingChanged);
        connect(m_target, &ScaleDevice::buttonPressed, this, &ScaleDeviceProxy::buttonPressed);
        connect(m_target, &ScaleDevice::errorOccurred, this, &ScaleDeviceProxy::errorOccurred);
        connect(m_target, &ScaleDevice::simulationModeChanged, this, &ScaleDeviceProxy::simulationModeChanged);
        connect(m_target, &ScaleDevice::sleepCompleted, this, &ScaleDeviceProxy::sleepCompleted);
        connect(m_target, &ScaleDevice::logMessage, this, &ScaleDeviceProxy::logMessage);
    }

    // Announce everything, unconditionally. Comparing old and new values to emit selectively would
    // be wrong here: two scales can report the same weight and still be different devices, and a
    // binding that read the old scale's value must re-run against the new one either way.
    emit targetChanged();
    emit connectedChanged();
    emit weightChanged(weight());
    emit flowRateChanged(flowRate());
    emit batteryLevelChanged(batteryLevel());
    emit chargingChanged(charging());
    emit simulationModeChanged();
}

void ScaleDeviceProxy::setSimulationMode(bool enabled)
{
    if (m_target)
        m_target->setSimulationMode(enabled);
}

void ScaleDeviceProxy::tare()                { if (m_target) m_target->tare(); }
void ScaleDeviceProxy::startTimer()          { if (m_target) m_target->startTimer(); }
void ScaleDeviceProxy::stopTimer()           { if (m_target) m_target->stopTimer(); }
void ScaleDeviceProxy::resetTimer()          { if (m_target) m_target->resetTimer(); }
void ScaleDeviceProxy::wake()                { if (m_target) m_target->wake(); }
void ScaleDeviceProxy::disableLcd()          { if (m_target) m_target->disableLcd(); }
void ScaleDeviceProxy::sendKeepAlive()       { if (m_target) m_target->sendKeepAlive(); }
void ScaleDeviceProxy::disconnectFromScale() { if (m_target) m_target->disconnectFromScale(); }
void ScaleDeviceProxy::resetFlowCalculation() { if (m_target) m_target->resetFlowCalculation(); }

void ScaleDeviceProxy::addFlowSample(double flowRate, double deltaTime)
{
    if (m_target)
        m_target->addFlowSample(flowRate, deltaTime);
}

// `true` with no target is ScaleDevice's own base-class default, not a guess: a caller asking
// "may I send resetTimer() alone?" when there is no scale to send it to is answered either way.
bool ScaleDeviceProxy::hasIndependentTimerReset() const
{
    return m_target ? m_target->hasIndependentTimerReset() : true;
}

void ScaleDeviceProxy::sleep()
{
    if (m_target) {
        m_target->sleep();
        return;
    }
    // ScaleDevice::sleep()'s base implementation emits sleepCompleted() immediately when there is
    // no confirmation to wait for. Match that with no target at all, so a caller awaiting the
    // completion signal is not left hanging on a scale that is not there.
    emit sleepCompleted();
}
