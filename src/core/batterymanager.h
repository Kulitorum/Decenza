#pragma once

#include <QObject>
#include <QTimer>

class DE1Device;
class Settings;

// BatteryManager — controls the DE1's USB charge port to protect the tablet battery.
//
// Hardware setup
// ──────────────
// The tablet (which runs this app) charges through a USB cable plugged into the DE1
// espresso machine. The DE1 has a hardware-controlled USB charge port that can be
// switched on or off via a BLE MMR write (address 0x803854, 1=on, 0=off). This class
// decides when to enable or disable that port based on the tablet's battery level.
//
// The DE1 has a built-in 10-minute safety timeout that automatically re-enables its
// USB port if it hasn't received a command. We resend our decision every 60 seconds
// to stay in control.
//
// Smart charging modes
// ────────────────────
// Off   — DE1 USB port always on. Battery charges freely to 100%.
// On    — Cycles between 55 % and 65 %. Port turns off at 65 %, back on at 55 %.
//         Extends long-term battery lifespan by avoiding constant top-up charging.
// Night — Cycles between 90 % and 95 %. Keeps battery high for morning use while
//         still avoiding 100 % stress.
//
// Mismatch detection
// ──────────────────
// On Android and iOS we read the actual charging status from the OS (independent of
// what we commanded). If we have told the DE1 to enable its USB port but the OS reports
// the tablet is still DISCHARGING, that means the DE1's port isn't delivering power —
// likely because the DE1 went to sleep and cut the port, or a BLE write silently failed.
// We retry the BLE command immediately but wait for 5 consecutive failed 60-second
// checks (~5 min) before alerting the user, since the DE1 can temporarily cut USB
// power for its own hardware needs (e.g. during preheating).
//
// Note: on iOS, UIDevice.batteryState can report Charging/Full/Unplugged but cannot
// distinguish the DE1 USB port from a wall charger. If the tablet is on wall power,
// mismatch detection will see "CHARGING" even when the DE1 port is off — it won't
// produce false alerts, but it also can't detect "wrong power source".

class BatteryManager : public QObject {
    Q_OBJECT

    // Current tablet battery level (0–100), updated every 60 s.
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY batteryPercentChanged)

    // Whether the tablet is actually receiving charging current from the DE1 USB port.
    // On Android and iOS this reflects the real OS charging status, NOT just what we commanded.
    // On desktop it reflects the commanded state (no real battery to read).
    Q_PROPERTY(bool isCharging READ isCharging NOTIFY isChargingChanged)

    // Active smart charging mode (Off / On / Night). Persisted in QSettings.
    Q_PROPERTY(int chargingMode READ chargingMode WRITE setChargingMode NOTIFY chargingModeChanged)

public:
    // Charging modes — stored as int in QSettings under "smartBatteryCharging".
    enum ChargingMode {
        Off   = 0,  // DE1 USB port always on  — battery charges freely to 100 %
        On    = 1,  // Smart charging 55–65 %  — extends battery lifespan
        Night = 2   // Smart charging 90–95 %  — keeps battery high for morning use
    };
    Q_ENUM(ChargingMode)

    explicit BatteryManager(QObject* parent = nullptr);

    // Must be called once at startup before checkBattery() can send any commands.
    // Connects to DE1Device::connectedChanged so charging is applied immediately
    // when the DE1 comes online rather than waiting for the next 60-second tick.
    void setDE1Device(DE1Device* device);

    // Must be called once at startup. Loads chargingMode and the discharging flag
    // from QSettings so the charge/discharge cycle survives app restarts.
    void setSettings(Settings* settings);

    int  batteryPercent() const { return m_batteryPercent; }
    bool isCharging()     const { return m_isCharging; }
    int  chargingMode()   const { return m_chargingMode; }

    // Call on app exit or suspend. Turns the DE1 USB port back on unconditionally so
    // the tablet can charge while the app is not running. Matches de1app behaviour.
    void ensureChargerOn();

public slots:
    // Change the smart charging mode and apply it immediately. Persists to QSettings.
    void setChargingMode(int mode);

    // Read battery level and apply smart charging. Called every 60 s by the internal
    // timer, on DE1 connect, and whenever the charging mode changes.
    void checkBattery();


signals:
    void batteryPercentChanged();
    void isChargingChanged();
    void chargingModeChanged();

    // Emitted when we have commanded the DE1 USB port ON but Android has reported
    // DISCHARGING (port not delivering power) for 5 consecutive 60-second checks (~5 min).
    // Connect in QML to show the user a visible warning.
    void chargingMismatchDetected();

    // Emitted when the above mismatch clears — charging resumed as expected.
    void chargingMismatchResolved();

private:
    // Read the tablet battery level from the platform. On Android and iOS also captures
    // m_androidBatteryStatus and m_androidPlugged so applySmartCharging() can compare
    // commanded vs actual charging state.
    int  readPlatformBatteryPercent();

    // Core decision function — decides whether the DE1 USB port should be on or off,
    // sends the BLE command, logs current state, and runs mismatch detection.
    void applySmartCharging();

    DE1Device* m_device   = nullptr;
    Settings*  m_settings = nullptr;
    QTimer     m_checkTimer;  // Fires every 60 s to run checkBattery()

    int  m_batteryPercent = 100;
    bool m_isCharging     = true;
    int  m_chargingMode   = On;

    // Tracks which half of the charge/discharge cycle we are in.
    // true  = waiting for battery to fall to the lower threshold before re-enabling the port
    // false = port is on (or we just started), charging toward the upper threshold
    // Persisted to QSettings ("battery/discharging") so restarts don't reset the cycle.
    bool m_discharging = false;

    // Actual charging state as reported by the OS (Android intent or iOS UIDevice).
    // These are read independently of what we commanded to detect when the DE1 USB port
    // is not delivering power despite our instructions.
    //
    // m_androidBatteryStatus — uses Android BATTERY_STATUS_* constants as canonical values:
    //   1=UNKNOWN  2=CHARGING  3=DISCHARGING  4=NOT_CHARGING  5=FULL
    //   On iOS, UIDeviceBatteryState is mapped to these same values.
    //   In this hardware setup, DISCHARGING means the DE1 USB port is off (no power).
    //
    // m_androidPlugged — power source type:
    //   0=UNPLUGGED  1=AC  2=USB (from DE1)  4=WIRELESS
    //   On iOS, Charging/Full maps to USB(2) since iOS can't distinguish the source.
    //   UNPLUGGED means the tablet is not receiving external power.
    //
    // Both remain -1 on desktop (no real battery).
    int m_androidBatteryStatus = -1;
    int m_androidPlugged       = -1;

    // Mismatch detection state — how many consecutive 60-second checks have seen
    // shouldChargerBeOn=true but android=DISCHARGING.
    int  m_chargingMismatchCount = 0;
    bool m_chargingMismatch      = false;  // true while mismatch signal is active

    // Throttle the periodic status log to every 5th cycle (~5 min at 60s intervals).
    // State-change logs (threshold crossings, mismatch alerts) always print immediately.
    int m_logCycleCount = 0;
};
