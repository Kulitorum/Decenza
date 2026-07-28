#include "refractometerproxy.h"

RefractometerProxy::RefractometerProxy(QObject* parent)
    : QObject(parent)
{
}

void RefractometerProxy::setTarget(RefractometerDevice* target)
{
    if (m_target == target)
        return;

    if (m_target)
        disconnect(m_target, nullptr, this, nullptr);

    m_target = target;

    if (m_target) {
        connect(m_target, &RefractometerDevice::connectedChanged, this, &RefractometerProxy::connectedChanged);
        connect(m_target, &RefractometerDevice::tdsChanged, this, &RefractometerProxy::tdsChanged);
        connect(m_target, &RefractometerDevice::temperatureChanged, this, &RefractometerProxy::temperatureChanged);
        connect(m_target, &RefractometerDevice::measuringChanged, this, &RefractometerProxy::measuringChanged);
        connect(m_target, &RefractometerDevice::nameChanged, this, &RefractometerProxy::nameChanged);
        connect(m_target, &RefractometerDevice::autoTestChanged, this, &RefractometerProxy::autoTestChanged);
        connect(m_target, &RefractometerDevice::measurementComplete, this, &RefractometerProxy::measurementComplete);
        connect(m_target, &RefractometerDevice::averageProgress, this, &RefractometerProxy::averageProgress);
        connect(m_target, &RefractometerDevice::errorOccurred, this, &RefractometerProxy::errorOccurred);
        connect(m_target, &RefractometerDevice::logMessage, this, &RefractometerProxy::logMessage);
    }

    // Announce everything: a device arriving or leaving changes every reading at once, and a
    // binding that read the previous device must re-run even where the value is coincidentally
    // the same.
    emit targetChanged();
    emit connectedChanged();
    emit tdsChanged(tds());
    emit temperatureChanged(temperature());
    emit measuringChanged();
    emit nameChanged();
    emit autoTestChanged();
}

void RefractometerProxy::setAutoTest(bool enabled)
{
    if (m_target)
        m_target->setAutoTest(enabled);
}

void RefractometerProxy::setDeviceTestCount(int count)
{
    if (m_target)
        m_target->setDeviceTestCount(count);
}

void RefractometerProxy::disconnectFromDevice()
{
    if (m_target)
        m_target->disconnectFromDevice();
}

void RefractometerProxy::requestMeasurement()
{
    if (m_target)
        m_target->requestMeasurement();
}

void RefractometerProxy::requestAveragedMeasurement(int testCount)
{
    if (m_target)
        m_target->requestAveragedMeasurement(testCount);
}
