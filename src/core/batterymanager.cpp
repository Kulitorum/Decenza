#include "core/diagnosticlogging.h"
#include "batterymanager.h"
#include "settings.h"
#include "../ble/de1device.h"
#include <QDateTime>
#include <QDebug>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

#ifdef Q_OS_IOS
#include <UIKit/UIKit.h>
#endif

namespace {
// The one rendering of the charging mode in the log; a raw enum read "mode= 1".
QString chargingModeLabel(bool usbChargerEnabled, int mode)
{
    static const char* modeNames[] = {"Off(always-on)", "On(55-65%)", "Night(90-95%)"};
    if (!usbChargerEnabled)
        return QStringLiteral("ChargerDisabled");
    return QLatin1String((mode >= 0 && mode <= 2) ? modeNames[mode] : "Unknown");
}
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

BatteryManager::BatteryManager(QObject* parent)
    : QObject(parent)
{
    // Poll every 60 seconds — same cadence as de1app. This is also the frequency at
    // which we resend the charger command to the DE1. The DE1 has a 10-minute built-in
    // timeout that re-enables its USB port automatically; 60 s gives us a 10× safety
    // margin so we always win the race to keep the port in the state we want.
    m_checkTimer.setInterval(60000);
    connect(&m_checkTimer, &QTimer::timeout, this, &BatteryManager::checkBattery);
    m_checkTimer.start();

    checkBattery();
}

// ─────────────────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────────────────

void BatteryManager::setAppActive(bool active) {
    if (m_appActive == active)
        return;
    m_appActive = active;
    // One line per transition so the gap in the periodic battery telemetry is
    // attributable from the log — a silently gated poll is indistinguishable
    // from a frozen event loop otherwise.
    DIAG_DEBUG(BATTERY, "BatteryManager") << "poll" << (active ? "re-armed (app no longer suspended)"
                                                  : "gated (app suspended)");
}

void BatteryManager::setDE1Device(DE1Device* device) {
    m_device = device;

    // Apply smart charging the moment the DE1 connects rather than waiting up to 60 s
    // for the next timer tick. Without this the DE1 can reconnect after a BLE drop and
    // sit for up to a minute with its USB port in the wrong state — on at 70 % when we
    // want it off, for example. We only act on the rising edge (isConnected == true);
    // the disconnect edge is handled by the early return in applySmartCharging().
    // If the DE1 reconnects while the app is suspended this apply is gated (see
    // m_appActive) — resume re-runs checkBattery(), and the DE1's 10-minute
    // auto-enable keeps the tablet charging in the interim.
    connect(device, &DE1Device::connectedChanged, this, [this]() {
        if (m_device && m_device->isConnected())
            checkBattery();
    });
}

void BatteryManager::setSettings(Settings* settings) {
    if (!settings) {
        DIAG_WARN(BATTERY, "BatteryManager") << "setSettings(nullptr) called — charging mode will use default (On/55-65%)";
        return;
    }
    m_settings = settings;
    m_chargingMode = m_settings->value("smartBatteryCharging", On).toInt();

    // Restore the discharging flag so the charge/discharge cycle survives restarts.
    // Without this, m_discharging always resets to false on startup. That means if
    // the battery was at 60 % and actively discharging toward the 55 % floor, after
    // a restart the app would see 60 % < 65 % and immediately re-enable the port,
    // charging back to 65 % — the lower threshold would effectively never be reached.
    m_discharging = m_settings->value("battery/discharging", false).toBool();
    m_usbChargerEnabled = m_settings->value("battery/usbChargerEnabled", true).toBool();

    DIAG_DEBUG(BATTERY, "BatteryManager") << QStringLiteral("Loaded mode=%1 appCycle=%2 usbChargerEnabled=%3")
        .arg(chargingModeLabel(true, m_chargingMode),
             m_discharging ? QStringLiteral("drain") : QStringLiteral("charge"),
             m_usbChargerEnabled ? QStringLiteral("true") : QStringLiteral("false"));

    emit chargingModeChanged();
    emit usbChargerEnabledChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
// Mode changes
// ─────────────────────────────────────────────────────────────────────────────

void BatteryManager::setChargingMode(int mode) {
    if (mode < Off || mode > Night) {
        DIAG_WARN(BATTERY, "BatteryManager") << "setChargingMode() called with invalid value" << mode << "— ignoring";
        return;
    }

    if (m_chargingMode == mode)
        return;

    m_chargingMode = mode;
    DIAG_DEBUG(BATTERY, "BatteryManager") << "Charging mode set to" << mode;

    // Reset mismatch state on mode change — stale count from the previous mode is
    // meaningless in the new context and could cause a spurious resolved emission.
    m_chargingMismatchCount = 0;
    if (m_chargingMismatch) {
        m_chargingMismatch = false;
        emit chargingMismatchResolved();
    }

    if (m_settings)
        m_settings->setValue("smartBatteryCharging", mode);

    emit chargingModeChanged();

    // Apply the new mode's port state now rather than at the next 60 s tick.
    checkBattery();
}

void BatteryManager::setUsbChargerEnabled(bool enabled) {
    if (m_usbChargerEnabled == enabled)
        return;

    m_usbChargerEnabled = enabled;
    DIAG_INFO(BATTERY, "BatteryManager") << "USB charger" << (enabled ? "enabled" : "disabled") << "by user";

    m_chargingMismatchCount = 0;
    if (m_chargingMismatch) {
        m_chargingMismatch = false;
        emit chargingMismatchResolved();
    }

    if (m_settings)
        m_settings->setValue("battery/usbChargerEnabled", enabled);

    emit usbChargerEnabledChanged();
    checkBattery();
}

// ─────────────────────────────────────────────────────────────────────────────
// Battery reading
// ─────────────────────────────────────────────────────────────────────────────

void BatteryManager::checkBattery() {
    // See m_appActive: nothing useful to poll while suspended, and it keeps
    // JNI battery reads out of the suspend window.
    if (!m_appActive)
        return;

    int newPercent = readPlatformBatteryPercent();

    if (newPercent != m_batteryPercent) {
        m_batteryPercent = newPercent;
        emit batteryPercentChanged();
    }

    applySmartCharging();
}

int BatteryManager::readPlatformBatteryPercent() {
#ifdef Q_OS_ANDROID
    // ACTION_BATTERY_CHANGED is a sticky broadcast — registering a null receiver
    // returns the most recent cached intent without needing a persistent receiver.
    QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");

    if (!context.isValid()) {
        DIAG_WARN(BATTERY, "BatteryManager") << "QtNative.getContext() returned null — JNI context unavailable, battery reading skipped";
        return 100;
    }

    QJniObject intentFilter("android/content/IntentFilter",
        "(Ljava/lang/String;)V",
        QJniObject::fromString("android.intent.action.BATTERY_CHANGED").object<jstring>());

    QJniObject intent = context.callObjectMethod(
        "registerReceiver",
        "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;",
        nullptr,
        intentFilter.object());

    if (!intent.isValid()) {
        DIAG_WARN(BATTERY, "BatteryManager") << "registerReceiver() returned null intent — battery status unavailable";
        return 100;
    }

    // Battery percentage is expressed as level/scale (e.g. 72/100 = 72 %).
    jint level = intent.callMethod<jint>("getIntExtra",
        "(Ljava/lang/String;I)I",
        QJniObject::fromString("level").object<jstring>(), -1);

    jint scale = intent.callMethod<jint>("getIntExtra",
        "(Ljava/lang/String;I)I",
        QJniObject::fromString("scale").object<jstring>(), 100);

    if (level < 0 || scale <= 0)
        return 100;

    // Read the actual charging status and power-source type. These are used by
    // applySmartCharging() to detect whether the DE1 USB port is actually delivering
    // power, independently of what we commanded via BLE.
    //
    // status values (android.os.BatteryManager.BATTERY_STATUS_*):
    //   1=UNKNOWN  2=CHARGING  3=DISCHARGING  4=NOT_CHARGING  5=FULL
    //
    // In this hardware setup "DISCHARGING" means the DE1's USB port is electrically
    // off — the tablet is not receiving current, even if the cable is still plugged in.
    m_androidBatteryStatus = intent.callMethod<jint>("getIntExtra",
        "(Ljava/lang/String;I)I",
        QJniObject::fromString("status").object<jstring>(), -1);

    // plugged values (android.os.BatteryManager.BATTERY_PLUGGED_*):
    //   0=UNPLUGGED  1=AC  2=USB (DE1 port)  4=WIRELESS
    //
    // "UNPLUGGED" here means the DE1 USB port is not providing voltage — either the
    // DE1 turned the port off (our command), the DE1 went to sleep, or the cable
    // was physically removed. If we're commanding the port ON and see UNPLUGGED, that
    // is a mismatch worth investigating.
    m_androidPlugged = intent.callMethod<jint>("getIntExtra",
        "(Ljava/lang/String;I)I",
        QJniObject::fromString("plugged").object<jstring>(), -1);

    return (level * 100) / scale;

#elif defined(Q_OS_IOS)
    [[UIDevice currentDevice] setBatteryMonitoringEnabled:YES];
    float level = [[UIDevice currentDevice] batteryLevel];
    if (level < 0)
        return 100;  // level unknown

    // Read the actual charging state from iOS so mismatch detection works cross-platform.
    // UIDeviceBatteryState values:
    //   0 = UIDeviceBatteryStateUnknown
    //   1 = UIDeviceBatteryStateUnplugged
    //   2 = UIDeviceBatteryStateCharging
    //   3 = UIDeviceBatteryStateFull
    //
    // We map these to the same m_androidBatteryStatus constants used by applySmartCharging():
    //   2=CHARGING  3=DISCHARGING  5=FULL
    // and to m_androidPlugged:
    //   0=UNPLUGGED  2=USB
    // This enables the mismatch detection and log output that was previously Android-only.
    UIDeviceBatteryState iosState = [[UIDevice currentDevice] batteryState];
    switch (iosState) {
    case UIDeviceBatteryStateCharging:
        m_androidBatteryStatus = 2;  // CHARGING
        m_androidPlugged = 2;        // USB (best guess — iOS doesn't distinguish source)
        break;
    case UIDeviceBatteryStateFull:
        m_androidBatteryStatus = 5;  // FULL
        m_androidPlugged = 2;        // USB
        break;
    case UIDeviceBatteryStateUnplugged:
        m_androidBatteryStatus = 3;  // DISCHARGING
        m_androidPlugged = 0;        // UNPLUGGED
        break;
    default:  // UIDeviceBatteryStateUnknown
        m_androidBatteryStatus = 1;  // UNKNOWN
        m_androidPlugged = -1;
        break;
    }

    return static_cast<int>(level * 100);

#else
    return 100;  // Desktop — no real battery
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Smart charging logic
// ─────────────────────────────────────────────────────────────────────────────

void BatteryManager::applySmartCharging() {
    // We can only control the DE1 USB port while BLE is connected.
    // If the DE1 is disconnected we skip the command — the DE1 will auto-enable its
    // port after 10 minutes anyway, so the tablet will eventually charge.
    if (!m_device || !m_device->isConnected()) {
        if (!m_device) {
            DIAG_DEBUG(BATTERY, "BatteryManager") << "DE1 device not yet set, skipping charger command";
        } else {
            // Same key as the poll line: once a minute while the DE1 is away, the
            // first says it and the poll line that follows reconnect carries the tally.
            const QString skipped = QStringLiteral("mode=%1 — DE1 not connected, skipping charger command")
                .arg(chargingModeLabel(m_usbChargerEnabled, m_chargingMode));
            LogCollapse::Collapsed collapsed;
            if (m_pollCollapse.shouldLog(QStringLiteral("poll"), skipped,
                                        QDateTime::currentMSecsSinceEpoch(), &collapsed)) {
                DIAG_DEBUG(BATTERY, "BatteryManager") << QStringLiteral("battery=%1% %2")
                    .arg(m_batteryPercent).arg(skipped) + m_pollCollapse.suffixSimilar(collapsed);
            }
        }
        return;
    }

    // ── Step 1: decide whether the DE1 USB port should be on ─────────────────

    bool shouldChargerBeOn = true;

    if (!m_usbChargerEnabled) {
        // Port off regardless of mode; m_discharging is left alone so the mode's
        // cycle resumes where it was when the charger is switched back on.
        shouldChargerBeOn = false;
    } else switch (m_chargingMode) {

    case Off:
        // Always-on mode — no smart control, just keep the port enabled.
        shouldChargerBeOn = true;
        break;

    case On:
        // Smart charging 55–65 %. We use a two-state machine (m_discharging) to
        // create hysteresis and avoid rapid switching near the boundaries:
        //   m_discharging=false → port is on, charging. Switch port off at 65 %.
        //   m_discharging=true  → port is off, draining. Switch port on at 55 %.
        // m_discharging is persisted so a restart mid-cycle doesn't reset the window.
        if (m_discharging) {
            if (m_batteryPercent <= 55) {
                shouldChargerBeOn = true;
                m_discharging = false;
                if (m_settings) m_settings->setValue("battery/discharging", false);
                DIAG_INFO(BATTERY, "BatteryManager") << QStringLiteral("Battery at %1%, requesting charge enable").arg(m_batteryPercent);
            } else {
                shouldChargerBeOn = false;  // Still draining toward 55 %
            }
        } else {
            if (m_batteryPercent >= 65) {
                shouldChargerBeOn = false;
                m_discharging = true;
                if (m_settings) m_settings->setValue("battery/discharging", true);
                DIAG_INFO(BATTERY, "BatteryManager") << QStringLiteral("Battery at %1%, requesting charge disable").arg(m_batteryPercent);
            } else {
                shouldChargerBeOn = true;   // Still charging toward 65 %
            }
        }
        break;

    case Night:
        // Smart charging 90–95 %. Same two-state machine as On mode, different thresholds.
        // Designed to keep the battery high for morning use without staying at 100 %.
        if (m_discharging) {
            if (m_batteryPercent <= 90) {
                shouldChargerBeOn = true;
                m_discharging = false;
                if (m_settings) m_settings->setValue("battery/discharging", false);
                DIAG_INFO(BATTERY, "BatteryManager") << QStringLiteral("Night mode - battery at %1%, requesting charge enable").arg(m_batteryPercent);
            } else {
                shouldChargerBeOn = false;  // Still draining toward 90 %
            }
        } else {
            if (m_batteryPercent >= 95) {
                shouldChargerBeOn = false;
                m_discharging = true;
                if (m_settings) m_settings->setValue("battery/discharging", true);
                DIAG_INFO(BATTERY, "BatteryManager") << QStringLiteral("Night mode - battery at %1%, requesting charge disable").arg(m_batteryPercent);
            } else {
                shouldChargerBeOn = true;   // Still charging toward 95 %
            }
        }
        break;

    default:
        DIAG_WARN(BATTERY, "BatteryManager") << "Unknown charging mode" << m_chargingMode
                   << "— defaulting to always-on. Check QSettings for corruption.";
        shouldChargerBeOn = true;
        break;
    }

    // ── Step 2: log state changes ─────────────────────────────────────────────
    //
    // Every state change, plus five percentage points of progress, so the battery
    // curve can still be reconstructed when diagnosing drain issues.

    const QString modeName = chargingModeLabel(m_usbChargerEnabled, m_chargingMode);

    // Human-readable OS-reported battery status for the log.
    // Populated on both Android (from sticky intent) and iOS (from UIDevice.batteryState).
    const char* osStatus = "n/a";
    switch (m_androidBatteryStatus) {
    case 1: osStatus = "UNKNOWN";      break;
    case 2: osStatus = "CHARGING";     break;
    case 3: osStatus = "DISCHARGING";  break;
    case 4: osStatus = "NOT-CHARGING"; break;
    case 5: osStatus = "FULL";         break;
    }

    // Human-readable plugged source for the log.
    // On Android, USB confirms the DE1 port is electrically active.
    // On iOS, USB just means "some wired source" — iOS can't distinguish DE1 from wall.
    const char* osPlugged = "n/a";
    switch (m_androidPlugged) {
    case 0: osPlugged = "UNPLUGGED"; break;
    case 1: osPlugged = "AC";        break;
#ifdef Q_OS_IOS
    case 2: osPlugged = "USB";       break;
#else
    case 2: osPlugged = "USB(DE1)";  break;
#endif
    case 4: osPlugged = "WIRELESS";  break;
    }

    // Retain state transitions and five percentage points of progress. Normal
    // one-percent movement otherwise defeats changes-only suppression.
    const QString state = QStringLiteral("mode=%1 requestedCharger=%2 appCycle=%3 status=%4 plugged=%5")
        .arg(modeName, shouldChargerBeOn ? QStringLiteral("ON") : QStringLiteral("OFF"))
        // The app's half of the charge window, not the battery's state: that is status=.
        .arg(m_discharging ? QStringLiteral("drain") : QStringLiteral("charge"))
        .arg(osStatus, osPlugged);
    if (state != m_lastLoggedBatteryState || m_lastLoggedBatteryPercent < 0
        || qAbs(m_batteryPercent - m_lastLoggedBatteryPercent) >= 5)
        m_lastLoggedBatteryPercent = m_batteryPercent;
    const QString gate = state + QStringLiteral(" progress=%1").arg(m_lastLoggedBatteryPercent);
    LogCollapse::Collapsed collapsed;
    if (m_pollCollapse.shouldLog(QStringLiteral("poll"), gate,
                                QDateTime::currentMSecsSinceEpoch(), &collapsed)) {
        m_lastLoggedBatteryState = state;
        DIAG_DEBUG(BATTERY, "BatteryManager") << QStringLiteral("battery=%1% %2")
            .arg(m_batteryPercent).arg(state) + m_pollCollapse.suffixSimilar(collapsed);
    }

    // ── Step 3: send the command to the DE1 ──────────────────────────────────
    //
    // Always send with force=true so we resend even when the decision hasn't changed.
    // The DE1's 10-minute auto-enable timeout means that if we want the port OFF we
    // must actively reassert that every cycle — otherwise the DE1 overrides us.
    //
    // The OS sample step 5 judges was read at the top of this pass, so it reflects
    // the PREVIOUS command, not this one.
    const bool sampleTakenUnderOnCommand = m_portCommandedOn;
    m_device->setUsbChargerOn(shouldChargerBeOn, true);
    m_portCommandedOn = shouldChargerBeOn;

    // ── Step 4: update isCharging to reflect reality, not just our intent ────
    //
    // Previously isCharging was set from shouldChargerBeOn (what we commanded).
    // That caused the UI to show "Charging" even when the DE1 USB port wasn't
    // actually delivering power — misleading the user while the battery drained.
    //
    // On Android and iOS we now use the OS-reported status instead. CHARGING(2) or FULL(5)
    // means current is actually flowing into the battery. On desktop platforms
    // m_androidBatteryStatus stays -1, so we fall back to the commanded state.
    bool actuallyCharging = (m_androidBatteryStatus != -1)
        ? (m_androidBatteryStatus == 2 || m_androidBatteryStatus == 5)
        : shouldChargerBeOn;

    if (m_isCharging != actuallyCharging) {
        m_isCharging = actuallyCharging;
        emit isChargingChanged();
    }

    // ── Step 5: mismatch detection ───────────────────────────────────────────
    //
    // If we commanded the port ON but the OS reports DISCHARGING, the DE1 USB port
    // is not delivering power despite our instruction. Possible causes:
    //   • The DE1 went to sleep and cut the USB port (most common overnight)
    //   • The DE1 temporarily cut USB power to prioritise its own hardware (heater, etc.)
    //   • A BLE write silently failed and the DE1 never received the command
    //   • The physical cable between the tablet and DE1 was disconnected
    //
    // We retry the BLE command immediately on the first failed check, but wait for
    // 5 consecutive mismatched samples before alerting the user. The DE1 can legally
    // cut USB power for short periods (e.g. during preheating), so a single transient
    // DISCHARGING reading is not worth a popup. The threshold counts observations, not measured outage duration.
    //
    // This block runs on Android and iOS; m_androidBatteryStatus stays -1 on desktop.
    if (m_androidBatteryStatus != -1) {
        // The pre-command OS sample indicates discharge or an unplugged power source.
        // Using m_androidPlugged as a second signal catches NOT_CHARGING(4) edge cases
        // where the cable is physically connected but the DE1 cut its USB output.
        // Note: on iOS, mismatch detection works for the primary case (Unplugged maps
        // to DISCHARGING + UNPLUGGED). However, iOS cannot report NOT_CHARGING(4), so
        // the edge case where a cable is connected but delivering no current is not detected.
        const bool osDischarging = (m_androidBatteryStatus == 3);
        const bool portActuallyOff = osDischarging || (m_androidPlugged == 0);

        constexpr int kMismatchAlertThreshold = 5;  // consecutive samples, including resume checks

        // A sample read while the port was deliberately off is not evidence against
        // an ON command sent after it. Without this guard the drain -> charge flip
        // counted one false mismatch and re-sent the command on every cycle.
        if (shouldChargerBeOn && portActuallyOff && sampleTakenUnderOnCommand) {
            m_chargingMismatchCount++;

            // Retry the BLE command every cycle while the mismatch persists.
            m_device->setUsbChargerOn(true, true);

            if (m_chargingMismatchCount < kMismatchAlertThreshold) {
                // DEBUG, not WARN: below the threshold this has not yet gone wrong,
                // and a WARN here would sit above the DEBUG line that resolves it.
                // The escalation below is the event worth a WARN.
                //
                // A "cycle 1 of 5" every ~2.5 h, never reaching 2, was not a
                // transient: it was the drain -> charge flip pass, now excluded by
                // sampleTakenUnderOnCommand (SM-X210 log, 2026-10-25).
                DIAG_DEBUG(BATTERY, "BatteryManager") << "desiredChargerOn=true; pre-command OS sample indicates discharge/unplugged"
                         << "(battery=" << m_batteryPercent << "%, cycle"
                         << m_chargingMismatchCount << "of" << kMismatchAlertThreshold
                         << "). Retrying; not yet reported as a fault.";
            } else {
                // Five or more consecutive mismatched OS samples — alert the user.
                DIAG_WARN(BATTERY, "BatteryManager") << "ALERT - requested charging disagrees with OS samples; consecutiveSamples="
                           << m_chargingMismatchCount << "battery=" << m_batteryPercent << "%";
                if (!m_chargingMismatch) {
                    m_chargingMismatch = true;
                    emit chargingMismatchDetected();
                }
            }
        } else {
            // Charging is working as expected, we deliberately have the port off, or
            // the sample predates our ON command.
            if (m_chargingMismatch) {
                // INFO, up from DEBUG. Not the same tier as the WARN that
                // announced it, and saying so rather than claiming a match: a
                // reader filtered to WARN and above still will not see this line.
                // What they see instead is the ALERT above ceasing — it repeats
                // every cycle for as long as the mismatch holds, so its stopping
                // is the WARN-level signal.
                //
                // INFO is nonetheless the correct tier by audience: a resolution
                // is not a problem, and WARN is reserved for things that are. The
                // defect being fixed is the previous DEBUG, which put the
                // retraction below the connections view that had shown the user
                // the fault in the first place.
                DIAG_INFO(BATTERY, "BatteryManager") << "Charging mismatch alert cleared; priorMismatchedSamples="
                        << m_chargingMismatchCount << "desiredChargerOn=" << shouldChargerBeOn;
                m_chargingMismatch = false;
                emit chargingMismatchResolved();
            } else if (m_chargingMismatchCount > 0) {
                DIAG_DEBUG(BATTERY, "BatteryManager") << "OS sample and desired charging now agree; priorMismatchedSamples="
                         << m_chargingMismatchCount << "(no alert shown; duration of any physical interruption unknown)";
            }
            m_chargingMismatchCount = 0;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// App lifecycle safety
// ─────────────────────────────────────────────────────────────────────────────

void BatteryManager::ensureChargerOn() {
    // Called on app exit or suspend. Re-enables the DE1 USB port so the tablet can
    // charge while the app is not running to manage it. Without this, if smart
    // charging had turned the port off (battery > 65 %, say), the DE1 would keep it
    // off for up to 10 minutes after the app exits. Matches de1app's app_exit.
    // Skipped when the user switched the USB charger off; the DE1 firmware still
    // re-enables its port ~10 min after the last command.
    if (!m_usbChargerEnabled)
        return;
    if (m_device && m_device->isConnected()) {
        DIAG_DEBUG(BATTERY, "BatteryManager") << "Ensuring charger is ON (app exit/suspend safety)";
        // Urgent, meaning FRONT of the shared GATT queue rather than behind whatever
        // else is waiting. Not a bypass: it still waits for any operation already
        // outstanding, on this device or another. See DE1Transport::writeUrgent and
        // the iOS-suspend note at the caller in main.cpp for why one event-loop turn
        // is available here.
        m_device->setUsbChargerOnUrgent(true);
        m_portCommandedOn = true;
    }
}
