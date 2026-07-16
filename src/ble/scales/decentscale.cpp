#include "decentscale.h"
#include "scalelogging.h"
#include "../protocol/de1characteristics.h"
#include "../protocol/decentscaleprotocol.h"
#include <algorithm>
#include <QTimer>

#define DECENT_LOG(msg)  SCALE_LOG("DecentScale", msg)
#define DECENT_WARN(msg) SCALE_WARN("DecentScale", msg)

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
    DECENT_WARN("Transport disconnected");
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
    m_firmwareVersion.clear();
    m_lastBatteryByte = -1;
    m_ticksSinceBatteryPoll = 0;
    m_lcdOn = true;
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
        DECENT_LOG("Characteristics already set up, ignoring duplicate callback");
        return;
    }

    DECENT_LOG("Characteristics discovered");
    m_characteristicsReady = true;
    setConnected(true);

    // Start periodic heartbeat to keep connection alive
    startHeartbeat();

    // Follow de1app sequence EXACTLY (temporal order):
    // 1. Heartbeat immediately
    // 2. LCD at 200ms
    // 3. Enable notifications at 300ms
    // 4. Enable notifications at 400ms (again for reliability)
    // 5. LCD at 500ms (in case first was dropped)
    // 6. Heartbeat at 2000ms

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
    });

    // Enable BLE notifications again at 400ms (de1app does this twice for reliability)
    QTimer::singleShot(400, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        enableWeightNotifications("400ms retry");
    });

    // LCD enable again at 500ms (in case first was dropped)
    QTimer::singleShot(500, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        DECENT_LOG("Sending wake/LCD command again (500ms)");
        wake();
    });

    // Start watchdog (1s timeout allows pending 300/400ms notification enables to trigger data flow)
    startWatchdog();

    // Heartbeat at 2000ms
    QTimer::singleShot(2000, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        DECENT_LOG("Sending heartbeat (2000ms)");
        sendHeartbeat();
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
                DECENT_WARN(QString("Battery byte changed: 0x%1 -> 0x%2 (%3 -> %4) — LED response raw: %5")
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
        const int major = ((d[5] >> 4) & 0x0F) * 10 + (d[5] & 0x0F);
        const int minor = (d[6] >> 4) & 0x0F;
        const int patch = d[6] & 0x0F;
        const QString version = QStringLiteral("%1.%2.%3 (raw 0x%4 0x%5)")
            .arg(major).arg(minor).arg(patch)
            .arg(d[5], 2, 16, QLatin1Char('0'))
            .arg(d[6], 2, 16, QLatin1Char('0'));
        if (m_firmwareVersion != version) {
            if (m_firmwareVersion.isEmpty()) {
                DECENT_LOG(QString("Firmware version: %1").arg(version));
            } else {
                DECENT_WARN(QString("Firmware version changed mid-connect: %1 -> %2")
                            .arg(m_firmwareVersion, version));
            }
            m_firmwareVersion = version;
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

void DecentScale::enableWeightNotifications(const QString& reason) {
    if (!m_transport || !m_characteristicsReady) return;
    DECENT_LOG(QString("Enabling notifications (%1)").arg(reason));
    m_transport->enableNotifications(Scale::Decent::SERVICE, Scale::Decent::READ);
}

void DecentScale::startWatchdog() {
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

    // Restart heartbeat and watchdog if they were stopped by sleep()
    if (m_characteristicsReady) {
        startHeartbeat();
        startWatchdog();
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
    if (!m_heartbeatTimer) {
        m_heartbeatTimer = new QTimer(this);
        m_heartbeatTimer->setInterval(1000);  // Every 1 second like de1app
        connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
            if (!m_characteristicsReady || m_heartbeatsPaused) return;
            sendHeartbeat();
            // Periodic battery refresh: re-send the display-on command every
            // kBatteryPollHeartbeatTicks (~4 min). The scale replies with an
            // LED-response packet whose d[4] byte is parsed in parseWeightData
            // as battery — heartbeat alone never produces this reply.
            if (++m_ticksSinceBatteryPoll >= kBatteryPollHeartbeatTicks) {
                m_ticksSinceBatteryPoll = 0;
                if (m_lcdOn) {
                    DECENT_LOG("Polling battery (display-on refresh)");
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

void DecentScale::setHeartbeatsPaused(bool paused) {
    if (m_heartbeatsPaused == paused) return;
    m_heartbeatsPaused = paused;
    // Lifted out of the macro arg: SCALE_LOG concatenates with `+`, which binds
    // tighter than `?:`, so an inline ternary would parse as
    // `(QString + bool) ? "..." : "..."` and fail to compile.
    const QString msg = paused
        ? QStringLiteral("Pausing heartbeats — DE1 BLE discovery in progress")
        : QStringLiteral("Resuming heartbeats — DE1 BLE discovery complete");
    DECENT_LOG(msg);
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
