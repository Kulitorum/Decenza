#include "decentscale.h"
#include "scalelogging.h"
#include "../protocol/de1characteristics.h"
#include "../protocol/decentscaleprotocol.h"
#include <algorithm>
#include <QDateTime>
#include <QTimer>

#define DECENT_LOG(msg)  SCALE_LOG("DecentScale", msg)
#define DECENT_INFO(msg) SCALE_INFO("DecentScale", msg)
#define DECENT_WARN(msg) SCALE_WARN("DecentScale", msg)

namespace {
// Written at two sites — the poll that emits it and the disconnect that closes
// its run — and LogCollapse decides "repeat" by comparing text, so the two must
// stay byte-identical or the collapse silently stops collapsing. It doubles as
// the collapse KEY, which is safe here only because this file has exactly one
// collapsed line; a second one gets its own key rather than sharing this.
inline QString batteryPollText() {
    return QStringLiteral("Polling battery (display-on refresh)");
}
}  // namespace

DecentScale::DecentScale(ScaleBleTransport* transport, QObject* parent)
    : ScaleDevice(parent)
    , m_transport(transport)
{
    if (m_transport) {
        m_transport->setParent(this);

        connect(m_transport, &ScaleBleTransport::connected,
                this, &DecentScale::onTransportConnected);
        connect(m_transport, &ScaleBleTransport::disconnected,
                this, &DecentScale::onTransportDisconnected);
        connect(m_transport, &ScaleBleTransport::error,
                this, &DecentScale::onTransportError);
        connect(m_transport, &ScaleBleTransport::notificationsIssued,
                this, &DecentScale::onNotificationsIssued);
        connect(m_transport, &ScaleBleTransport::serviceDiscovered,
                this, &DecentScale::onServiceDiscovered);
        connect(m_transport, &ScaleBleTransport::servicesDiscoveryFinished,
                this, &DecentScale::onServicesDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicsDiscoveryFinished,
                this, &DecentScale::onCharacteristicsDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicChanged,
                this, &DecentScale::onCharacteristicChanged);
        // Forward transport logs to scale log
        connect(m_transport, &ScaleBleTransport::logMessage,
                this, &ScaleDevice::logMessage);
    }
}

DecentScale::~DecentScale() {
    stopWatchdog();
    stopHeartbeat();
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
}

void DecentScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (!m_transport) {
        DECENT_WARN("connectToDevice called with no transport");
        return;
    }

    // A fresh connect invalidates any previous session's supervision. Without
    // this, a connect issued over a live connection clears
    // m_characteristicsReady while the watchdog keeps running: its guard then
    // stops it mid-session and command writes drop silently while weight
    // still flows.
    stopWatchdog();
    stopHeartbeat();

    m_name = device.name();
    m_serviceFound = false;
    m_characteristicsReady = false;

    m_transport->connectToDevice(device);
}

void DecentScale::onTransportConnected() {
    m_transport->discoverServices();
}

void DecentScale::onTransportDisconnected() {
    DECENT_WARN(DECENZA_BLE_MSG_TRANSPORT_DISCONNECTED);
    stopWatchdog();
    stopHeartbeat();
    // The discovered characteristics don't outlive the link. Clearing
    // m_characteristicsReady blocks writes to a dead transport and keeps
    // wake() (DE1 wake path) from restarting the heartbeat/watchdog on a
    // disconnected scale (#1519); m_serviceFound is cleared alongside so the
    // next discovery starts clean. Both are set again by the next connect's
    // discovery callbacks.
    m_serviceFound = false;
    m_characteristicsReady = false;
    if (m_checksumDisabled) {
        DECENT_LOG("Checksum validation re-enabled on disconnect");
    }
    m_consecutiveChecksumFailures = 0;
    m_checksumDisabled = false;
    // Re-log the firmware version on the next connect — the LED-response
    // packet only arrives periodically, but capturing it fresh per connect
    // is what makes the line useful for triage.
    if (!m_firmwareVersion.isEmpty()) {
        m_firmwareVersion.clear();
        emit firmwareVersionChanged();
    }
    m_lastBatteryByte = -1;
    m_ticksSinceBatteryPoll = 0;
    m_lcdOn = true;
    // Run end for the battery-poll collapse. Reported rather than dropped: the
    // count IS the connection's polling history, and discarding it is the same
    // misattribution as never flushing, only quieter (logcollapse.h).
    {
        const LogCollapse::Collapsed collapsed =
            m_pollLog.flush(batteryPollText(), QDateTime::currentMSecsSinceEpoch());
        if (collapsed.suppressed > 0)
            DECENT_LOG(batteryPollText() + LogCollapse::suffix(collapsed));
    }
    setConnected(false);
}

void DecentScale::onTransportError(const QString& message) {
    DECENT_WARN(QString("Transport error: %1").arg(message));
    // error() covers both fatal link deaths and transient per-operation
    // failures (e.g. a single characteristic-write error on a live link).
    // Only tear down when the transport reports the link is actually gone —
    // a blanket setConnected(false) here parks the app on "disconnected"
    // over a live, streaming link (the scan-based reconnect ladder can't
    // recover that: a connected peripheral doesn't advertise). Don't treat
    // isConnected() as the dead-link detector: both transports' connected
    // flags lag the async disconnect callback, so at error() time this
    // check almost always still reads "connected". The watchdog is the real
    // detector — it supervises the weight feed and forces a propagated
    // disconnect if data actually stopped. Errors before the watchdog is
    // armed (setup phase) are bounded by BLEManager's 20s connection
    // timeout, which tears down a stuck link so retry scans can see the
    // scale again (#1519).
    if (!m_transport || !m_transport->isConnected()) {
        onTransportDisconnected();
    }
}

void DecentScale::onServiceDiscovered(const QBluetoothUuid& uuid) {
    if (uuid == Scale::Decent::SERVICE) {
        m_serviceFound = true;
    }
}

void DecentScale::onServicesDiscoveryFinished() {
    if (!m_serviceFound) {
        DECENT_WARN("Decent Scale service not found");
        m_transport->disconnectFromDevice();
        // The Qt transport's disconnectFromDevice() never emits
        // disconnected(), so run the disconnect handling directly — see the
        // watchdog-exhaustion comment in onWatchdogFired() (#1519).
        onTransportDisconnected();
        return;
    }
    m_transport->discoverCharacteristics(Scale::Decent::SERVICE);
}

void DecentScale::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    if (serviceUuid != Scale::Decent::SERVICE) return;
    if (m_characteristicsReady) {
        DECENT_LOG(DECENZA_BLE_MSG_DUPLICATE_CHARACTERISTICS);
        return;
    }

    DECENT_LOG("Characteristics discovered");
    m_characteristicsReady = true;

    // BEFORE setConnected(), which emits connectedChanged() SYNCHRONOUSLY
    // (scaledevice.cpp) and so hands control to observers while this function is
    // still mid-way through the connect sequence. One of them re-enters us:
    // main.cpp's connectedChanged handler calls wake() when scaleLcdRestorePending
    // is set (the DE1 slept, the link dropped, the scale came back). With the flag
    // still false at that instant, wake()'s guards both pass and it arms the
    // watchdog HERE — about 300 ms before the notify-enable is even submitted, and
    // on a contended radio well over a second before it reaches the dispatcher.
    // kWatchdogFirstTimeoutMs is 1000 ms, so it expires against a question that
    // was never asked, logs "no initial weight data", and re-enables — the very
    // duplicate CCCD write #1885 deleted the 400 ms repeat to avoid. Ten of those
    // force-disconnect a healthy scale.
    //
    // This is the same defect the guard in wake() was written for (arming a
    // "did weight arrive" clock against something that is not an enable); it just
    // reaches it by a path the flag was not yet set to cover. Nothing between here
    // and the wake sequence below reads the flag, so setting it early is free.
    m_watchdogArmPending = true;

    setConnected(true);

    // Start periodic heartbeat to keep connection alive
    startHeartbeat();

    // Follow de1app sequence (temporal order):
    // 1. Heartbeat immediately
    // 2. LCD at 200ms
    // 3. Enable notifications at 300ms
    // 4. LCD at 500ms (in case first was dropped)
    // 5. Heartbeat at 2000ms
    //
    // de1app also enables notifications a second time at 400ms "for
    // reliability". We do not, because under the shared GATT queue that second
    // enable is not a cheap duplicate — it is a second queued CCCD write behind
    // a first that has usually not been dispatched yet. Measured on this tablet
    // with the DE1 connecting concurrently: the two enables dispatched 1161 ms
    // and 1097 ms after being queued, and they are what produced the session's
    // only Bluetooth warning — BleGattQueue's "N Bluetooth operation(s) were
    // delayed because another device was using the radio; the worst (scale
    // enable notifications) waited 1161 ms".
    //
    // Nothing is lost. The failure the blanket retry guards against — an enable
    // that reached the scale and was ignored — is exactly what the watchdog
    // detects (no weight data within kWatchdogFirstTimeoutMs of the enable
    // ISSUING, see onNotificationsIssued) and it re-enables on each retry. That
    // is the evidence-driven version of the same recovery: it fires when weight
    // data actually failed to arrive, rather than every connect regardless.

    DECENT_LOG("Starting de1app-style wake sequence");

    // Heartbeat immediately
    sendHeartbeat();

    // LCD enable at 200ms
    QTimer::singleShot(200, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        DECENT_LOG("Sending wake/LCD command (200ms)");
        wake();
    });

    // Enable BLE notifications at 300ms
    QTimer::singleShot(300, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        enableWeightNotifications("300ms");
        armWatchdogIfEnableNeverIssues();
    });

    // LCD enable again at 500ms (in case first was dropped)
    QTimer::singleShot(500, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        DECENT_LOG("Sending wake/LCD command again (500ms)");
        wake();
    });

    // NOT armed here. Armed when the notify-enable it is timing actually reaches
    // the radio — see onNotificationsIssued().
    //
    // The budget below is "did weight data start flowing after we enabled
    // notifications", and it used to be measured from this point on the
    // assumption that the enables submitted just above go out immediately. Under
    // the shared GATT queue that assumption broke: with the DE1 connecting at
    // the same time the enable is queued behind the machine's connect burst.
    // Measured on a tablet — enableNotifications called at 7.221 s, dispatched
    // at 8.483 s — so the watchdog expired 50 ms BEFORE the scale had been asked
    // anything, reported "no initial weight data" and burned a retry.
    //
    // The interval was never wrong; the moment it started from was. So this
    // arms nothing and the issue callback does — a timer that begins when its
    // subject begins, which is the timers-as-guards rule rather than a tuning
    // change. Restarting an already-running timer on issue would have been the
    // smaller edit and would have left the same bug reachable: an enable that is
    // still queued when the watchdog was never armed at all (a wake sequence cut
    // short) has nothing to restart.
    //
    // Set at the top of this function rather than here — see the comment beside
    // it for the re-entrant observer that reaches wake() before this point.

    // Heartbeat at 2000ms
    QTimer::singleShot(2000, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        DECENT_LOG("Sending heartbeat (2000ms)");
        sendHeartbeat();
    });
}

// The watchdog is armed BY the enable reaching the radio (onNotificationsIssued),
// so an enable that fails BEFORE dispatch arms nothing: m_watchdogArmPending stays
// set, wake()'s own arm is gated on that same flag being clear (and startWatchdog()
// declines a second arm besides), and the watchdog never fires — no re-enable, no retry ladder, no disconnect, with the app still
// believing a scale is connected that will never report a weight. That is the
// #1519 symptom.
//
// Four paths in QtScaleBleTransport::enableNotifications reach failGattOperation()
// without emitting notificationsIssued (link not ready, service missing,
// characteristic invalid, no CCCD; CoreBluetooth has the same shape), and the
// first is live during the DE1's concurrent connect burst. A fifth case is an
// operation the queue never dispatches at all.
//
// The 400 ms duplicate enable used to cover this by accident — a second,
// independent chance to arm. Removing it removed that, so the arm needs a path
// that does not depend on the enable succeeding.
//
// TIMED FROM THE SUBMISSION, and deliberately generous. An earlier version hung
// this off the wake sequence's existing 2000 ms step, which left only ~540 ms of
// margin: the enable is submitted at 300 ms, and this file's own measurement 100
// lines up records 1161 ms of queue wait with the DE1 connecting concurrently, so
// dispatch at ~1461 ms. Tripping early is not harmless — it WARNs that an enable
// still sitting in the queue "never reached the radio", and its retry submits a
// second CCCD write behind the first, which is the exact thing removing the
// duplicate was meant to stop. So the budget is the transport's own operation
// clock (ScaleBleTransport::OPERATION_TIMEOUT_MS, 5 s) plus a margin: past this
// point the queue has either abandoned the operation or is wedged, and both want
// the watchdog running.
void DecentScale::armWatchdogIfEnableNeverIssues() {
    QTimer::singleShot(kEnableIssueBudgetMs, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        if (!m_watchdogArmPending) return;  // the enable issued; nothing to do
        DECENT_WARN(QString("Notify-enable was never issued to the radio within %1 ms — "
                            "arming the watchdog anyway so its retry can re-enable")
                        .arg(kEnableIssueBudgetMs));
        m_watchdogArmPending = false;
        startWatchdog();
    });
}

void DecentScale::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid,
                                          const QByteArray& value) {
    if (characteristicUuid == Scale::Decent::READ) {
        tickleWatchdog();
        parseWeightData(value);
    }
}

void DecentScale::parseWeightData(const QByteArray& data) {
    if (data.size() < 7) return;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.constData());

    uint8_t command = d[1];

    // Validate XOR checksum on all packet types except LED response (0x0A),
    // which uses all 7 bytes for data and has no room for a checksum.
    // See: https://github.com/Kulitorum/Decenza/issues/560
    // Original Decent Scale (v1) does not compute checksums correctly — auto-disable
    // after consecutive failures. See: https://github.com/Kulitorum/Decenza/issues/630
    if (command != 0x0A && !m_checksumDisabled) {
        uint8_t expected = DecentScaleProtocol::calculateXor(data);
        if (expected != d[6]) {
            m_consecutiveChecksumFailures++;
            if (m_consecutiveChecksumFailures >= kChecksumFailureThreshold) {
                m_checksumDisabled = true;
                DECENT_WARN("Checksum validation disabled — scale may be original Decent Scale (non-HDS)");
            } else {
                DECENT_WARN(QString("Invalid checksum on type 0x%1, dropping packet (%2/%3)")
                            .arg(command, 2, 16, QChar('0'))
                            .arg(m_consecutiveChecksumFailures)
                            .arg(kChecksumFailureThreshold));
                return;
            }
        } else {
            m_consecutiveChecksumFailures = 0;
        }
    }

    if (command == 0xCE || command == 0xCA) {
        // Weight data
        int16_t weightRaw = (static_cast<int16_t>(d[2]) << 8) | d[3];
        double weight = weightRaw / 10.0;  // Weight in grams
        setWeight(weight);
    } else if (command == 0x0A && d[0] == 0x03) {
        // LED response packet (openscale/HDS format):
        // [0]=0x03 header, [1]=0x0A type, [2-3]=weight, [4]=battery, [5-6]=firmware version
        // Battery: 0-100 = percentage, 0xFF = charging
        uint8_t battByte = d[4];
        if (battByte <= 100) {
            setCharging(false);
            setBatteryLevel(battByte);
        } else if (battByte == 0xFF) {
            setCharging(true);
            setBatteryLevel(100);  // Keep "100" reporting so existing UI bindings don't regress
        }
        // Log the raw packet plus the byte we're parsing as battery, so a
        // "battery reading looks wrong" report can be diagnosed without
        // inferring from the UI. First LED-response per connect logs once;
        // subsequent packets warn-log only on change. Same shape as the
        // firmware-version log below.
        const int battInt = static_cast<int>(battByte);
        if (m_lastBatteryByte != battInt) {
            const QString packet = QStringLiteral("%1 %2 %3 %4 %5 %6 %7")
                .arg(d[0], 2, 16, QLatin1Char('0'))
                .arg(d[1], 2, 16, QLatin1Char('0'))
                .arg(d[2], 2, 16, QLatin1Char('0'))
                .arg(d[3], 2, 16, QLatin1Char('0'))
                .arg(d[4], 2, 16, QLatin1Char('0'))
                .arg(d[5], 2, 16, QLatin1Char('0'))
                .arg(d[6], 2, 16, QLatin1Char('0'));
            if (m_lastBatteryByte < 0) {
                DECENT_LOG(QString("Battery byte d[4]=0x%1 (%2) — LED response raw: %3")
                           .arg(battByte, 2, 16, QLatin1Char('0'))
                           .arg(battInt)
                           .arg(packet));
            } else {
                // DEBUG: a battery byte ticking 62 -> 61 is the battery
                // discharging, which is what batteries do. It was at WARN, so
                // routine drain read as a fault in every submitted log.
                DECENT_LOG(QString("Battery byte changed: 0x%1 -> 0x%2 (%3 -> %4) — LED response raw: %5")
                            .arg(m_lastBatteryByte, 2, 16, QLatin1Char('0'))
                            .arg(battByte, 2, 16, QLatin1Char('0'))
                            .arg(m_lastBatteryByte)
                            .arg(battInt)
                            .arg(packet));
            }
            m_lastBatteryByte = battInt;
        }
        // Firmware version: bytes [5-6], encoded per openscale (HDS)
        // include/ble.h:730-731 — byte [5] is BCD-packed major (00..99),
        // byte [6] is (minor << 4) | patch where minor and patch are
        // each a nibble (0..15). Source `FW: 3.0.9` → wire 0x03 0x09.
        // Log the parsed triple plus raw bytes for unambiguous triage.
        // Log once per connect; a subsequent packet reporting a different
        // value warn-logs the transition (shouldn't happen on a live
        // scale — a change would itself be diagnostic).
        const QString version = DecentScaleProtocol::decodeHdsFirmwareVersion(d[5], d[6]);
        if (m_firmwareVersion != version) {
            if (m_firmwareVersion.isEmpty()) {
                DECENT_LOG(QString("Firmware version: %1 (raw 0x%2 0x%3)")
                               .arg(version)
                               .arg(d[5], 2, 16, QLatin1Char('0'))
                               .arg(d[6], 2, 16, QLatin1Char('0')));
            } else {
                DECENT_WARN(QString("Firmware version changed mid-connect: %1 -> %2")
                            .arg(m_firmwareVersion, version));
            }
            m_firmwareVersion = version;
            emit firmwareVersionChanged();
        }
    } else if (command == 0xAA) {
        // Button pressed
        int button = d[2];
        emit buttonPressed(button);
    }
}

void DecentScale::sendKeepAlive() {
    // Base class 30s timer still fires, but this override intentionally does nothing.
    // The 1s heartbeat handles keep-alive, and the watchdog handles stale data detection.
}

void DecentScale::onNotificationsIssued(const QBluetoothUuid& characteristicUuid) {
    // The weight stream only: an enable for anything else says nothing about
    // when weight data should start.
    if (characteristicUuid != Scale::Decent::READ) return;

    if (m_watchdogArmPending) {
        // First enable of this wake sequence reaching the radio. startWatchdog()
        // resets the retry counter, which is right exactly once per sequence.
        m_watchdogArmPending = false;
        startWatchdog();
        return;
    }

    // A later enable — today only one the watchdog itself issued on retry (the
    // wake sequence's own 400 ms repeat is gone; see the sequence comment).
    // Restart the countdown so it measures from this attempt, but do NOT go
    // through startWatchdog(): resetting the retry counter here would let the
    // watchdog retry forever.
    if (m_watchdogTimer && m_watchdogTimer->isActive()) {
        m_watchdogTimer->start(m_watchdogUpdatesSeen ? kWatchdogTickleTimeoutMs
                                                     : kWatchdogFirstTimeoutMs);
    }
}

void DecentScale::enableWeightNotifications(const QString& reason) {
    if (!m_transport || !m_characteristicsReady) return;
    DECENT_LOG(QString("Enabling notifications (%1)").arg(reason));
    m_transport->enableNotifications(Scale::Decent::SERVICE, Scale::Decent::READ);
}

void DecentScale::startWatchdog() {
    // Already running: leave it entirely alone. This resets m_watchdogRetries and
    // m_watchdogUpdatesSeen, so a second arm silently restores the whole retry
    // budget and forgets that weight data had been seen — the next lapse is then
    // timed as a first sight (kWatchdogFirstTimeoutMs) rather than as a stall.
    // Build 3574's log shows two arms 198 ms apart: the enable issued at 5.040 s
    // and cleared the pending flag, then the 500 ms wake() armed again at
    // 5.238 s. Before #1885 quietened the connect burst the enable landed after
    // both wakes, so this was unreachable.
    //
    // The guard lives HERE rather than at the caller because startWatchdog() has
    // three callers — wake(), onNotificationsIssued() and
    // armWatchdogIfEnableNeverIssues() — and only one of them was guarded. That
    // left the others correct by an invariant nothing asserts. startHeartbeat()
    // below took the same decision for the same reason.
    //
    // Note this is NOT onNotificationsIssued()'s "later enable" rule, which
    // restarts the countdown while preserving the counters. This restarts
    // nothing: a wake()'s LCD write carries no information about when weight data
    // should arrive, so the window already in flight is the correct one to keep.
    if (m_watchdogTimer && m_watchdogTimer->isActive()) return;

    if (!m_watchdogTimer) {
        m_watchdogTimer = new QTimer(this);
        m_watchdogTimer->setSingleShot(true);
        connect(m_watchdogTimer, &QTimer::timeout, this, &DecentScale::onWatchdogFired);
    }
    m_watchdogUpdatesSeen = false;
    m_watchdogRetries = 0;
    // Initial timeout: verify weight data starts flowing within 1s
    m_watchdogTimer->start(kWatchdogFirstTimeoutMs);
    DECENT_LOG(QString("Watchdog started (initial %1ms timeout)").arg(kWatchdogFirstTimeoutMs));
}

void DecentScale::stopWatchdog() {
    if (m_watchdogTimer) {
        m_watchdogTimer->stop();
    }
    m_watchdogUpdatesSeen = false;
    m_watchdogRetries = 0;
    // A wake sequence torn down before its enable reached the radio must not
    // leave this pending: a later sequence's enable would otherwise arm a
    // watchdog belonging to a connection that is already gone.
    m_watchdogArmPending = false;
}

void DecentScale::tickleWatchdog() {
    // startWatchdog() is the only legitimate arm point. A stray notification
    // arriving after stopWatchdog() (post-disconnect, or sleep()) must not
    // resurrect supervision — a tickle-restarted watchdog on a sleeping scale
    // exhausts its retries against the silent feed and force-disconnects it.
    if (!m_watchdogTimer || !m_watchdogTimer->isActive()) return;
    m_watchdogUpdatesSeen = true;
    m_watchdogRetries = 0;
    // Reset to subsequent timeout: 2s until next expected update
    m_watchdogTimer->start(kWatchdogTickleTimeoutMs);
}

void DecentScale::onWatchdogFired() {
    if (!m_transport || !m_characteristicsReady) {
        DECENT_WARN("Watchdog fired but transport/characteristics not ready — stopping watchdog");
        stopWatchdog();
        return;
    }

    m_watchdogRetries++;

    if (!m_watchdogUpdatesSeen) {
        // Never received any weight data since connection
        DECENT_WARN(QString("Watchdog: no initial weight data (retry %1/%2)")
                    .arg(m_watchdogRetries).arg(kWatchdogMaxRetries));
    } else {
        // Was receiving data but it stopped
        DECENT_WARN(QString("Watchdog: weight data stale for >%1ms (retry %2/%3)")
                    .arg(kWatchdogTickleTimeoutMs).arg(m_watchdogRetries).arg(kWatchdogMaxRetries));
    }

    if (m_watchdogRetries >= kWatchdogMaxRetries) {
        DECENT_WARN("Watchdog: max retries exhausted, disconnecting scale for reconnection");
        stopWatchdog();
        stopHeartbeat();
        m_transport->disconnectFromDevice();
        // The Qt transport's disconnectFromDevice() tears the link down
        // without emitting disconnected() — it severs the controller's
        // signals first (see QtScaleBleTransport::disconnectFromDevice; its
        // connection-priority backoff path compensates likewise, by emitting
        // disconnected() itself after teardown). Run the disconnect handling
        // directly: it drives setConnected(false) → connectedChanged, which
        // the auto-reconnect ladder in main.cpp is gated on. Without this the
        // app keeps believing the scale is connected — no reconnect is ever
        // scheduled and the Connections scan filters the scale out as already
        // known (#1519). The CoreBluetooth transport, by contrast, DOES
        // deliver a late queued disconnected() after its cancel, so on
        // iOS/macOS onTransportDisconnected() runs a second time — it must
        // stay idempotent (setConnected change-guards; the timer stops and
        // flag clears are no-ops on repeat).
        onTransportDisconnected();
        return;
    }

    // Re-enable notifications and restart watchdog
    enableWeightNotifications(QString("watchdog retry %1").arg(m_watchdogRetries));
    if (m_watchdogTimer) {
        m_watchdogTimer->start(m_watchdogUpdatesSeen ? kWatchdogTickleTimeoutMs : kWatchdogFirstTimeoutMs);
    }
}

void DecentScale::sendCommand(const QByteArray& command) {
    if (!m_transport || !m_characteristicsReady) {
        // Not silent: a dropped tare/timer command is invisible in the UI, so
        // leave a trace for triage.
        DECENT_LOG(QString("Command 0x%1 dropped - not connected")
                   .arg(command.isEmpty() ? 0 : static_cast<uint8_t>(command[0]),
                        2, 16, QChar('0')));
        return;
    }

    QByteArray packet(7, 0);
    packet[0] = 0x03;  // Model byte

    for (int i = 0; i < std::min(command.size(), qsizetype(5)); i++) {
        packet[i + 1] = command[i];
    }

    packet[6] = DecentScaleProtocol::calculateXor(packet);

    m_transport->writeCharacteristic(Scale::Decent::SERVICE, Scale::Decent::WRITE, packet);
}

void DecentScale::tare() {
    sendCommand(QByteArray::fromHex("0F0100"));
}

void DecentScale::startTimer() {
    sendCommand(QByteArray::fromHex("0B0300"));
}

void DecentScale::stopTimer() {
    sendCommand(QByteArray::fromHex("0B0000"));
}

void DecentScale::resetTimer() {
    sendCommand(QByteArray::fromHex("0B0200"));
}

void DecentScale::startFirmwareUpdate(const QString& targetVersion) {
    if (!supportsFirmwareUpdate()) {
        DECENT_WARN(DecentScaleProtocol::firmwareUpdateUnknownVersionMessage());
        return;
    }
    // The version is required: a bare command starts the scale's own picker.
    // See DecentScaleProtocol::buildTargetedFirmwareUpdateCommand.
    const QByteArray command = DecentScaleProtocol::buildTargetedFirmwareUpdateCommand(targetVersion);
    if (command.isEmpty()) {
        DECENT_WARN(DecentScaleProtocol::firmwareUpdateBadTargetMessage(targetVersion));
        return;
    }
    DECENT_INFO(DecentScaleProtocol::firmwareUpdateStartingMessage(targetVersion));
    // sendCommand pads to the fixed 7-byte packet, which stays correct here for
    // two independent reasons: the Bluetooth path has no framer at all — one
    // characteristic write goes to one handler, which reads data[2..4] for a
    // targeted 0x1B and stops (openscale include/ble.h) — and that handler's
    // length check is a minimum, not an equality
    // (openscale include/decent_protocol.h, decentRequireLength is `>=`).
    sendCommand(command);
}

void DecentScale::sleep() {
    stopWatchdog();
    stopHeartbeat();
    m_lcdOn = false;
    if (!m_transport || !m_characteristicsReady) {
        emit sleepCompleted();
        return;
    }
    connect(m_transport, &ScaleBleTransport::characteristicWritten,
            this, [this]() { emit sleepCompleted(); },
            Qt::SingleShotConnection);
    // Command 0A 02 00 disables LCD and puts scale to sleep
    sendCommand(QByteArray::fromHex("0A0200"));
}

void DecentScale::wake() {
    // Command 0A 01 01 00 01 enables LCD (grams mode)
    // Must match official de1app: 03 0A 01 01 00 01 [xor]
    sendCommand(QByteArray::fromHex("0A01010001"));
    m_lcdOn = true;

    // Restart heartbeat and watchdog if they were stopped by sleep().
    if (m_characteristicsReady) {
        startHeartbeat();
        // But NOT while an enable is still on its way to the radio.
        //
        // wake() has two callers with opposite needs. After sleep() the scale's
        // notifications were never disabled, so nothing is pending and arming
        // here is right. During the CONNECT wake sequence, wake() is called at
        // 200 ms and 500 ms while the notify-enables sit in the shared queue —
        // and arming there starts a "did weight data arrive" clock against an
        // LCD command, before the scale has been asked for weight at all.
        //
        // Measured on a tablet: armed at 6.412 by the 500 ms wake, expired at
        // 7.412, and the enable did not reach the radio until 7.690. The
        // warning fired 278 ms before the question was asked.
        //
        // A watchdog already running is declined by startWatchdog() itself, not
        // by a second condition here — see the guard at the top of it.
        if (!m_watchdogArmPending) startWatchdog();
    }
}

void DecentScale::disableLcd() {
    // Command 0A 00 00 turns off LCD but keeps scale powered
    // This is different from sleep() which powers off the scale completely
    DECENT_LOG("Disabling LCD (scale stays powered)");
    sendCommand(QByteArray::fromHex("0A0000"));
    m_lcdOn = false;
}

void DecentScale::sendHeartbeat() {
    // Heartbeat command from de1app: 0A 03 FF FF
    // Tells scale we're still connected
    sendCommand(QByteArray::fromHex("0A03FFFF"));
}

void DecentScale::startHeartbeat() {
    // Already ticking: leave it alone. The connect wake sequence calls this
    // once directly and then again from each wake() at 200 ms and 500 ms, which
    // restarted a running timer three times in half a second and logged
    // "Starting heartbeat timer" for each — a line that was not true twice, and
    // that reset the battery-poll tick count along with it. The sleep() → wake()
    // path, where the timer really is stopped, still starts it here.
    if (m_heartbeatTimer && m_heartbeatTimer->isActive()) return;

    if (!m_heartbeatTimer) {
        m_heartbeatTimer = new QTimer(this);
        m_heartbeatTimer->setInterval(1000);  // Every 1 second like de1app
        connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
            // No pause flag: a heartbeat write that raced DE1 characteristic
            // discovery used to fail with CharacteristicWriteError and drop the
            // scale on weaker radios (#1176), and BLEManager forwarded a
            // discovery-active bool through five files to suppress it. Both
            // sides of that race are queued operations now, so the write waits
            // its turn instead of being withheld.
            if (!m_characteristicsReady) return;
            sendHeartbeat();
            // Periodic battery refresh: re-send the display-on command every
            // kBatteryPollHeartbeatTicks (~4 min). The scale replies with an
            // LED-response packet whose d[4] byte is parsed in parseWeightData
            // as battery — heartbeat alone never produces this reply.
            if (++m_ticksSinceBatteryPoll >= kBatteryPollHeartbeatTicks) {
                m_ticksSinceBatteryPoll = 0;
                if (m_lcdOn) {
                    // Collapsed: identical every ~4 min for the connection's
                    // life — see m_pollLog. The COMMAND is unconditional; only
                    // the line about it is suppressed.
                    const QString pollText = batteryPollText();
                    LogCollapse::Collapsed collapsed;
                    if (m_pollLog.shouldLog(pollText, pollText,
                                            QDateTime::currentMSecsSinceEpoch(), &collapsed)) {
                        DECENT_LOG(pollText + LogCollapse::suffix(collapsed));
                    }
                    sendCommand(QByteArray::fromHex("0A01010001"));
                }
                // else: skip poll while LCD is off — see m_lcdOn.
            }
        });
    }
    DECENT_LOG("Starting heartbeat timer");
    m_ticksSinceBatteryPoll = 0;
    m_heartbeatTimer->start();
}

void DecentScale::stopHeartbeat() {
    if (m_heartbeatTimer) {
        DECENT_LOG("Stopping heartbeat timer");
        m_heartbeatTimer->stop();
    }
}

void DecentScale::setLed(int r, int g, int b) {
    QByteArray cmd(5, 0);
    cmd[0] = 0x0A;
    cmd[1] = static_cast<char>(r);
    cmd[2] = static_cast<char>(g);
    cmd[3] = static_cast<char>(b);
    sendCommand(cmd);
}
