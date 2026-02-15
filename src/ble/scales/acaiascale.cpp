#include "acaiascale.h"
#include "../protocol/de1characteristics.h"
#include <QDebug>
#include <cmath>

// Helper macro that logs to both qDebug and emits signal for UI/file logging
#define ACAIA_LOG(msg) do { \
    QString _msg = QString("[BLE AcaiaScale] ") + msg; \
    qDebug().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

AcaiaScale::AcaiaScale(ScaleBleTransport* transport, QObject* parent)
    : ScaleDevice(parent)
    , m_transport(transport)
{
    if (m_transport) {
        m_transport->setParent(this);

        connect(m_transport, &ScaleBleTransport::connected,
                this, &AcaiaScale::onTransportConnected);
        connect(m_transport, &ScaleBleTransport::disconnected,
                this, &AcaiaScale::onTransportDisconnected);
        connect(m_transport, &ScaleBleTransport::error,
                this, &AcaiaScale::onTransportError);
        connect(m_transport, &ScaleBleTransport::serviceDiscovered,
                this, &AcaiaScale::onServiceDiscovered);
        connect(m_transport, &ScaleBleTransport::servicesDiscoveryFinished,
                this, &AcaiaScale::onServicesDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicsDiscoveryFinished,
                this, &AcaiaScale::onCharacteristicsDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicChanged,
                this, &AcaiaScale::onCharacteristicChanged);
        // Forward transport logs to scale log
        connect(m_transport, &ScaleBleTransport::logMessage,
                this, &ScaleDevice::logMessage);
    }

    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &AcaiaScale::sendHeartbeat);

    m_initTimer = new QTimer(this);
    connect(m_initTimer, &QTimer::timeout, this, &AcaiaScale::onInitTimer);
}

AcaiaScale::~AcaiaScale() {
    stopAllTimers();
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
}

void AcaiaScale::stopAllTimers() {
    m_heartbeatTimer->stop();
    m_initTimer->stop();
}

void AcaiaScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (!m_transport) {
        emit errorOccurred("No transport available");
        return;
    }

    // Prevent duplicate connection attempts
    if (m_isConnecting) {
        ACAIA_LOG("Already connecting, ignoring duplicate request");
        return;
    }

    // Stop any pending timers from previous connection
    stopAllTimers();

    // Reset state for new connection
    m_isPyxis = false;
    m_pyxisServiceFound = false;
    m_ipsServiceFound = false;
    m_characteristicsReady = false;
    m_receivingNotifications = false;
    m_weightReceived = false;
    m_isConnecting = true;
    m_identRetryCount = 0;
    m_buffer.clear();

    m_name = device.name();
    m_transport->connectToDevice(device);
}

void AcaiaScale::onTransportConnected() {
    ACAIA_LOG("Transport connected, starting service discovery");
    m_transport->discoverServices();
}

void AcaiaScale::onTransportDisconnected() {
    ACAIA_LOG("Transport disconnected");
    stopAllTimers();
    m_weightReceived = false;
    m_characteristicsReady = false;
    m_isConnecting = false;
    setConnected(false);
}

void AcaiaScale::onTransportError(const QString& message) {
    ACAIA_LOG(QString("Transport error: %1").arg(message));
    stopAllTimers();
    m_isConnecting = false;
    emit errorOccurred("Acaia scale connection error");
    setConnected(false);
}

void AcaiaScale::onServiceDiscovered(const QBluetoothUuid& uuid) {
    ACAIA_LOG(QString("Service discovered: %1").arg(uuid.toString()));

    // Check for Pyxis service (newer Lunar 2021, Pyxis, etc.)
    if (uuid == Scale::Acaia::SERVICE) {
        ACAIA_LOG("Found Pyxis service");
        m_pyxisServiceFound = true;
    }
    // Check for IPS service (older Lunar, Pearl)
    else if (uuid == Scale::AcaiaIPS::SERVICE) {
        ACAIA_LOG("Found IPS service");
        m_ipsServiceFound = true;
    }
}

void AcaiaScale::onServicesDiscoveryFinished() {
    ACAIA_LOG("Service discovery finished");

    // Prefer Pyxis protocol if available (newer scales, including Lunar 2021)
    QBluetoothUuid serviceToUse;
    if (m_pyxisServiceFound) {
        m_isPyxis = true;
        serviceToUse = Scale::Acaia::SERVICE;
        ACAIA_LOG("Using Pyxis protocol");
    } else if (m_ipsServiceFound) {
        m_isPyxis = false;
        serviceToUse = Scale::AcaiaIPS::SERVICE;
        ACAIA_LOG("Using IPS protocol");
    } else {
        ACAIA_LOG("WARNING: No compatible service found!");
        emit errorOccurred("No compatible Acaia service found");
        return;
    }

    m_transport->discoverCharacteristics(serviceToUse);
}

void AcaiaScale::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    // Only handle our selected service
    if (m_isPyxis && serviceUuid != Scale::Acaia::SERVICE) return;
    if (!m_isPyxis && serviceUuid != Scale::AcaiaIPS::SERVICE) return;
    if (m_characteristicsReady) {
        ACAIA_LOG("Characteristics already set up, ignoring duplicate callback");
        return;
    }

    ACAIA_LOG(QString("Characteristics discovered, protocol: %1").arg(m_isPyxis ? "Pyxis" : "IPS"));

    m_characteristicsReady = true;
    m_receivingNotifications = false;

    // Start the initialization sequence
    // Pyxis: notifications @ 500ms, then start init timer
    // IPS: notifications @ 100ms, then start init timer
    int notifyDelay = m_isPyxis ? 500 : 100;

    QTimer::singleShot(notifyDelay, this, &AcaiaScale::enableNotifications);
    QTimer::singleShot(notifyDelay + 500, this, &AcaiaScale::startInitSequence);
}

void AcaiaScale::enableNotifications() {
    if (!m_transport || !m_characteristicsReady) return;

    ACAIA_LOG("Enabling notifications");

    if (m_isPyxis) {
        m_transport->enableNotifications(Scale::Acaia::SERVICE, Scale::Acaia::STATUS);
    } else {
        m_transport->enableNotifications(Scale::AcaiaIPS::SERVICE, Scale::AcaiaIPS::CHARACTERISTIC);
    }
}

void AcaiaScale::startInitSequence() {
    if (!m_transport || !m_characteristicsReady) return;

    ACAIA_LOG("Starting init sequence");
    m_identRetryCount = 0;

    // Start recurring timer that sends ident + config
    m_initTimer->start(INIT_TIMER_INTERVAL_MS);

    // Send first ident immediately
    onInitTimer();
}

void AcaiaScale::onInitTimer() {
    // Check if we're receiving notifications (scale responded)
    if (m_receivingNotifications) {
        ACAIA_LOG("Scale responded, stopping init sequence and starting heartbeat");
        m_initTimer->stop();
        m_isConnecting = false;

        // Start heartbeat sequence
        sendConfig();
        QTimer::singleShot(1000, this, [this]() {
            if (m_transport && m_characteristicsReady) {
                sendHeartbeat();
            }
        });
        return;
    }

    // Check retry limit
    if (m_identRetryCount >= MAX_IDENT_RETRIES) {
        ACAIA_LOG(QString("Init sequence failed after %1 retries").arg(MAX_IDENT_RETRIES));
        m_initTimer->stop();
        m_isConnecting = false;
        emit errorOccurred("Scale not responding to ident");
        return;
    }

    // Send ident and config
    sendIdent();
    // Config is sent after a short delay
    QTimer::singleShot(200, this, [this]() {
        if (m_transport && m_characteristicsReady && !m_receivingNotifications) {
            sendConfig();
        }
    });

    m_identRetryCount++;
    ACAIA_LOG(QString("Init attempt %1/%2").arg(m_identRetryCount).arg(MAX_IDENT_RETRIES));
}

void AcaiaScale::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid, const QByteArray& value) {
    // Check if it's from our status characteristic
    if (m_isPyxis && characteristicUuid == Scale::Acaia::STATUS) {
        parseResponse(value);
    } else if (!m_isPyxis && characteristicUuid == Scale::AcaiaIPS::CHARACTERISTIC) {
        parseResponse(value);
    }
}

QByteArray AcaiaScale::encodePacket(uint8_t msgType, const QByteArray& payload) {
    QByteArray packet;
    packet.append(static_cast<char>(0xEF));  // Header 1
    packet.append(static_cast<char>(0xDD));  // Header 2
    packet.append(static_cast<char>(msgType));
    packet.append(payload);
    return packet;
}

void AcaiaScale::sendIdent() {
    ACAIA_LOG(QString("Sending ident, receivingNotifications: %1").arg(m_receivingNotifications ? "true" : "false"));

    // Ident message: type 0x0B with "01234567890123" + checksum
    QByteArray payload = QByteArray::fromHex("3031323334353637383930313233349A6D");
    QByteArray packet = encodePacket(0x0B, payload);
    sendCommand(packet);
    // Note: Timer scheduling is handled by onInitTimer(), not here
}

void AcaiaScale::sendConfig() {
    ACAIA_LOG("Sending config");

    // Config message: type 0x0C with notification settings
    QByteArray payload = QByteArray::fromHex("0900010102020103041106");
    QByteArray packet = encodePacket(0x0C, payload);
    sendCommand(packet);
}

void AcaiaScale::sendHeartbeat() {
    // Heartbeat message: type 0x00 with status bytes
    QByteArray payload = QByteArray::fromHex("02000200");
    QByteArray packet = encodePacket(0x00, payload);
    sendCommand(packet);

    // Only send config during init phase, before first weight received.
    // Once connected and receiving weight, config has done its job.
    if (!m_weightReceived) {
        QTimer::singleShot(1000, this, &AcaiaScale::sendConfig);
    }

    m_heartbeatTimer->start(3000);  // Heartbeat every 3 seconds
}

void AcaiaScale::sendCommand(const QByteArray& command) {
    if (!m_transport || !m_characteristicsReady) return;

    // Write to appropriate characteristic based on protocol
    // CRITICAL: IPS and Pyxis require different write types!
    // - IPS (older Lunar/Pearl): WriteWithoutResponse (fire and forget)
    // - Pyxis (newer Lunar 2021): WriteWithResponse (waits for ack)
    // This matches de1app's ble_write_type_no_response vs ble_write_type_default
    if (m_isPyxis) {
        m_transport->writeCharacteristic(Scale::Acaia::SERVICE, Scale::Acaia::CMD, command,
                                         ScaleBleTransport::WriteType::WithResponse);
    } else {
        // IPS uses same characteristic for read/write, and requires NO_RESPONSE write type
        m_transport->writeCharacteristic(Scale::AcaiaIPS::SERVICE, Scale::AcaiaIPS::CHARACTERISTIC, command,
                                         ScaleBleTransport::WriteType::WithoutResponse);
    }
}

void AcaiaScale::parseResponse(const QByteArray& data) {
    // Append to buffer
    m_buffer.append(data);

    // Need at least 6 bytes for a valid message
    if (m_buffer.size() < 6) return;

    const uint8_t* buf = reinterpret_cast<const uint8_t*>(m_buffer.constData());

    // Find message start (0xEF 0xDD)
    int msgStart = -1;
    for (int i = 0; i < m_buffer.size() - 1; i++) {
        if (buf[i] == 0xEF && buf[i + 1] == 0xDD) {
            msgStart = i;
            break;
        }
    }

    if (msgStart < 0) {
        m_buffer.clear();
        return;
    }

    // Skip bytes before message start
    if (msgStart > 0) {
        m_buffer = m_buffer.mid(msgStart);
        buf = reinterpret_cast<const uint8_t*>(m_buffer.constData());
    }

    // Check if we have enough data for metadata
    if (m_buffer.size() < ACAIA_METADATA_LEN + 1) return;

    uint8_t msgType = buf[2];
    uint8_t length = buf[3];
    uint8_t eventType = buf[4];

    // Mark that we're receiving notifications (not just info messages)
    if (msgType != 7) {
        m_receivingNotifications = true;
    }

    // Check if we have the complete message
    int msgEnd = ACAIA_METADATA_LEN + length;
    if (m_buffer.size() < msgEnd) return;

    // Only process weight messages (msgType 0x0C, eventType 5 or 11)
    if (msgType == 0x0C && (eventType == 5 || eventType == 11)) {
        int payloadOffset = (eventType == 5) ? ACAIA_METADATA_LEN : ACAIA_METADATA_LEN + 3;
        decodeWeight(m_buffer, payloadOffset);
    }

    // Clear buffer after processing
    m_buffer.clear();
}

void AcaiaScale::decodeWeight(const QByteArray& data, int payloadOffset) {
    if (data.size() < payloadOffset + 6) return;

    const uint8_t* payload = reinterpret_cast<const uint8_t*>(data.constData()) + payloadOffset;

    // Weight is 3 bytes, little-endian
    int32_t value = ((payload[2] & 0xFF) << 16) |
                    ((payload[1] & 0xFF) << 8) |
                    (payload[0] & 0xFF);

    // Unit is in payload[4]
    uint8_t unit = payload[4] & 0xFF;
    double weight = value / std::pow(10.0, unit);

    // Sign is in payload[5]
    bool isNegative = payload[5] > 1;
    if (isNegative) {
        weight = -weight;
    }

    // Mark as connected only after receiving first valid weight
    // This ensures the handshake completed successfully
    if (!m_weightReceived) {
        m_weightReceived = true;
        ACAIA_LOG("First weight received, marking as connected");
        setConnected(true);
    }

    setWeight(weight);
}

void AcaiaScale::sendKeepAlive() {
    if (!m_transport || !m_characteristicsReady) return;

    // Re-enable BLE notifications to prevent OS-level subscription expiry
    if (m_isPyxis) {
        m_transport->enableNotifications(Scale::Acaia::SERVICE, Scale::Acaia::STATUS);
    } else {
        m_transport->enableNotifications(Scale::AcaiaIPS::SERVICE, Scale::AcaiaIPS::CHARACTERISTIC);
    }
}

void AcaiaScale::sendTareCommand() {
    // Tare message: type 0x04 with zeros
    QByteArray payload(17, 0);
    QByteArray packet = encodePacket(0x04, payload);
    sendCommand(packet);
}

void AcaiaScale::tare() {
    // Acaia Lunar scales are notoriously unreliable with single tare commands.
    // The Decent app sends 3-4 tares at shot start. We do the same here.
    ACAIA_LOG("Sending multiple tares (Acaia workaround)");

    // Send first tare immediately
    sendTareCommand();

    // Send 2 more tares with 100ms delays
    QTimer::singleShot(100, this, [this]() {
        if (m_transport && m_characteristicsReady) {
            sendTareCommand();
        }
    });
    QTimer::singleShot(200, this, [this]() {
        if (m_transport && m_characteristicsReady) {
            sendTareCommand();
        }
    });
}
