#include "settings_hardware.h"

#include <QtGlobal>
#include <QDebug>

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

int SettingsHardware::fanThreshold() const {
    int val = m_settings.value("calibration/fanThreshold", 60).toInt();
    return qBound(0, val, 60);
}

void SettingsHardware::setFanThreshold(int value) {
    value = qBound(0, value, 60);
    if (fanThreshold() != value) {
        m_settings.setValue("calibration/fanThreshold", value);
        emit fanThresholdChanged();
    }
}

// --- Connection-priority weak-device classification (D9) ---
// Dumb persisted storage under the "connectionPriority/" QSettings group;
// BLEManager owns build-scoped gating + the value invariant. No NOTIFY/QML.

bool SettingsHardware::cpLatched() const {
    return m_settings.value("connectionPriority/latched", false).toBool();
}

QString SettingsHardware::cpTriggerKind() const {
    return m_settings.value("connectionPriority/triggerKind").toString();
}

QString SettingsHardware::cpSetTimeIso() const {
    return m_settings.value("connectionPriority/setTimeIso").toString();
}

int SettingsHardware::cpBuildCode() const {
    return m_settings.value("connectionPriority/buildCode", 0).toInt();
}

void SettingsHardware::setConnectionPriorityLatch(const QString& triggerKind,
                                                  const QString& setTimeIso,
                                                  int buildCode) {
    m_settings.setValue("connectionPriority/latched", true);
    m_settings.setValue("connectionPriority/triggerKind", triggerKind);
    m_settings.setValue("connectionPriority/setTimeIso", setTimeIso);
    m_settings.setValue("connectionPriority/buildCode", buildCode);
    // QSettings::setValue is fire-and-forget (no return, no throw). On
    // read-only storage / full disk the write silently drops and the
    // classification degrades to in-memory-only (re-detects next run) — which
    // would look exactly like "still bad every restart" with NO log trail,
    // the precise diagnostic black hole this whole feature exists to avoid.
    // Force a flush and surface a failure so a field debug.log shows it.
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        qWarning().noquote()
            << "[BLE] Failed to PERSIST connection-priority classification "
               "(QSettings status" << static_cast<int>(m_settings.status())
            << ") — it is in-memory-only this run; the device will re-detect "
               "on the next restart instead of starting at BALANCED";
    }
}

void SettingsHardware::clearConnectionPriorityLatch() {
    // Remove ONLY the four latch sub-keys (not the whole "connectionPriority"
    // group): a sibling key — connectionPriority/policyMode — must survive a
    // latch clear (MCP reset / new-build re-detect), since the policy mode is
    // an explicit operator choice with a different (non-build-scoped) lifetime.
    // Removing the group would silently wipe the mode. Each absent latch key
    // then defaults correctly (cpLatched() ⇒ false, etc.).
    m_settings.remove("connectionPriority/latched");
    m_settings.remove("connectionPriority/triggerKind");
    m_settings.remove("connectionPriority/setTimeIso");
    m_settings.remove("connectionPriority/buildCode");
}

QString SettingsHardware::cpMode() const {
    return m_settings.value("connectionPriority/policyMode").toString();
}

void SettingsHardware::setCpMode(const QString& mode) {
    m_settings.setValue("connectionPriority/policyMode", mode);
    // Mirror setConnectionPriorityLatch()'s durability discipline: a silently
    // dropped write here would look like "mode keeps reverting to enforce"
    // with no trail. Force a flush and surface a persist failure.
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        qWarning().noquote()
            << "[BLE] Failed to PERSIST connection-priority policy mode "
               "(QSettings status" << static_cast<int>(m_settings.status())
            << ") — mode change is in-memory-only this run";
    }
}
