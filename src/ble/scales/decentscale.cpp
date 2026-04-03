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
        emit errorOccurred("No transport available");
        return;
    }

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
    m_consecutiveChecksumFailures = 0;
    m_checksumDisabled = false;
    setConnected(false);
}

void DecentScale::onTransportError(const QString& message) {
    DECENT_WARN(QString("Transport error: %1").arg(message));
    emit errorOccurred("Scale connection error");
    setConnected(false);
}

void DecentScale::onServiceDiscovered(const QBluetoothUuid& uuid) {
    if (uuid == Scale::Decent::SERVICE) {
        m_serviceFound = true;
    }
}

void DecentScale::onServicesDiscoveryFinished() {
    if (!m_serviceFound) {
        DECENT_WARN("Decent Scale service not found");
        emit errorOccurred("Decent Scale service not found");
        m_transport->disconnectFromDevice();
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
    // Original Decent Scale (v1) may not compute checksums correctly — auto-disable
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
            setBatteryLevel(battByte);
        } else if (battByte == 0xFF) {
            setBatteryLevel(100);  // Charging — report as full
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
    if (!m_watchdogTimer) return;
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
        return;
    }

    // Re-enable notifications and restart watchdog
    enableWeightNotifications(QString("watchdog retry %1").arg(m_watchdogRetries));
    if (m_watchdogTimer) {
        m_watchdogTimer->start(m_watchdogUpdatesSeen ? kWatchdogTickleTimeoutMs : kWatchdogFirstTimeoutMs);
    }
}

void DecentScale::sendCommand(const QByteArray& command) {
    if (!m_transport || !m_characteristicsReady) return;

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
            if (m_characteristicsReady) {
                sendHeartbeat();
            }
        });
    }
    DECENT_LOG("Starting heartbeat timer");
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
