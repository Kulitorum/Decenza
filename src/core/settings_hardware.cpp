#include "settings_hardware.h"

#include <QtGlobal>

SettingsHardware::SettingsHardware(QObject* parent)
    : QObject(parent)
    , m_settings("DecentEspresso", "DE1Qt")
{
}

int SettingsHardware::heaterIdleTemp() const {
    int val = m_settings.value("calibration/heaterIdleTemp", 990).toInt();
    return qBound(0, val, 990);
}

void SettingsHardware::setHeaterIdleTemp(int value) {
    if (heaterIdleTemp() != value) {
        m_settings.setValue("calibration/heaterIdleTemp", value);
        emit heaterIdleTempChanged();
    }
}

int SettingsHardware::heaterWarmupFlow() const {
    int val = m_settings.value("calibration/heaterWarmupFlow", 20).toInt();
    return qBound(5, val, 60);
}

void SettingsHardware::setHeaterWarmupFlow(int value) {
    if (heaterWarmupFlow() != value) {
        m_settings.setValue("calibration/heaterWarmupFlow", value);
        emit heaterWarmupFlowChanged();
    }
}

int SettingsHardware::heaterTestFlow() const {
    int val = m_settings.value("calibration/heaterTestFlow", 40).toInt();
    return qBound(5, val, 80);
}

void SettingsHardware::setHeaterTestFlow(int value) {
    if (heaterTestFlow() != value) {
        m_settings.setValue("calibration/heaterTestFlow", value);
        emit heaterTestFlowChanged();
    }
}

int SettingsHardware::heaterWarmupTimeout() const {
    int val = m_settings.value("calibration/heaterWarmupTimeout", 10).toInt();
    return qBound(10, val, 300);
}

void SettingsHardware::setHeaterWarmupTimeout(int value) {
    if (heaterWarmupTimeout() != value) {
        m_settings.setValue("calibration/heaterWarmupTimeout", value);
        emit heaterWarmupTimeoutChanged();
    }
}

int SettingsHardware::hotWaterFlowRate() const {
    int val = m_settings.value("calibration/hotWaterFlowRate", 10).toInt();
    return qBound(5, val, 100);
}

void SettingsHardware::setHotWaterFlowRate(int value) {
    if (hotWaterFlowRate() != value) {
        m_settings.setValue("calibration/hotWaterFlowRate", value);
        emit hotWaterFlowRateChanged();
    }
}

bool SettingsHardware::steamTwoTapStop() const {
    return m_settings.value("calibration/steamTwoTapStop", false).toBool();
}

void SettingsHardware::setSteamTwoTapStop(bool value) {
    if (steamTwoTapStop() != value) {
        m_settings.setValue("calibration/steamTwoTapStop", value);
        emit steamTwoTapStopChanged();
    }
}
