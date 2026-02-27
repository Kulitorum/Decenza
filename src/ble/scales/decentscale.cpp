#include "decentscale.h"
#include "../protocol/de1characteristics.h"
#include <algorithm>
#include <QDateTime>
#include <QTimer>

// Helper macro that logs to both qDebug and emits signal for UI/file logging
#define DECENT_LOG(msg) do { \
    QString _msg = QString("[BLE DecentScale] ") + msg; \
    qDebug().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

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
    stopHeartbeat();
    m_lastNotificationEnableMs = 0;
    m_lastScalePacketMs = 0;
    setConnected(false);
}

void DecentScale::onTransportError(const QString& message) {
    Q_UNUSED(message)
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
        emit errorOccurred("Decent Scale service not found");
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
    m_lastScalePacketMs = QDateTime::currentMSecsSinceEpoch();
    setConnected(true);

    // Start periodic heartbeat to keep connection alive
    startHeartbeat();

    // Follow de1app sequence EXACTLY:
    // 1. Heartbeat immediately
    // 2. Heartbeat at 2000ms
    // 3. LCD at 200ms
    // 4. Enable notifications at 300ms
    // 5. Enable notifications at 400ms (again for reliability)
    // 6. LCD at 500ms (in case first was dropped)

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
        enableWeightNotifications("300ms", true);
    });

    // Enable BLE notifications again at 400ms (de1app does this twice for reliability)
    QTimer::singleShot(400, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        enableWeightNotifications("400ms retry", true);
    });

    // LCD enable again at 500ms (in case first was dropped)
    QTimer::singleShot(500, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        DECENT_LOG("Sending wake/LCD command again (500ms)");
        wake();
    });

    // Heartbeat at 2000ms
    QTimer::singleShot(2000, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        DECENT_LOG("Sending heartbeat (2000ms)");
        sendHeartbeat();
    });
}

void DecentScale::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid,
                                          const QByteArray& value) {
    m_lastScalePacketMs = QDateTime::currentMSecsSinceEpoch();
    if (characteristicUuid == Scale::Decent::READ) {
        parseWeightData(value);
    }
}

void DecentScale::parseWeightData(const QByteArray& data) {
    if (data.size() < 7) return;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.constData());

    uint8_t command = d[1];

    if (command == 0xCE || command == 0xCA) {
        // Weight data
        int16_t weightRaw = (static_cast<int16_t>(d[2]) << 8) | d[3];
        double weight = weightRaw / 10.0;  // Weight in grams
        setWeight(weight);
    } else if (command == 0xAA) {
        // Button pressed
        int button = d[2];
        emit buttonPressed(button);
    }
}

void DecentScale::sendKeepAlive() {
    enableWeightNotifications("periodic keepalive");
}

void DecentScale::enableWeightNotifications(const QString& reason, bool force) {
    if (!m_transport || !m_characteristicsReady) return;

    constexpr qint64 kMinRefreshMs = 5 * 60 * 1000;  // throttle routine re-enables to every 5 minutes
    constexpr qint64 kStaleDataMs = 45 * 1000;       // refresh sooner if packets appear stale
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    const bool dataStale = (m_lastScalePacketMs > 0) && ((now - m_lastScalePacketMs) >= kStaleDataMs);
    const bool recentRefresh = (m_lastNotificationEnableMs > 0) && ((now - m_lastNotificationEnableMs) < kMinRefreshMs);

    if (!force && recentRefresh && !dataStale) {
        return;
    }

    DECENT_LOG(QString("Enabling notifications (%1)").arg(reason));
    m_transport->enableNotifications(Scale::Decent::SERVICE, Scale::Decent::READ);
    m_lastNotificationEnableMs = now;
}

void DecentScale::sendCommand(const QByteArray& command) {
    if (!m_transport || !m_characteristicsReady) return;

    QByteArray packet(7, 0);
    packet[0] = 0x03;  // Model byte

    for (int i = 0; i < std::min(command.size(), qsizetype(5)); i++) {
        packet[i + 1] = command[i];
    }

    packet[6] = calculateXor(packet);

    m_transport->writeCharacteristic(Scale::Decent::SERVICE, Scale::Decent::WRITE, packet);
}

uint8_t DecentScale::calculateXor(const QByteArray& data) {
    uint8_t result = 0;
    for (int i = 0; i < data.size() - 1; i++) {
        result ^= static_cast<uint8_t>(data[i]);
    }
    return result;
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
    // Command 0A 02 00 disables LCD and puts scale to sleep
    sendCommand(QByteArray::fromHex("0A0200"));
}

void DecentScale::wake() {
    // Command 0A 01 01 00 01 enables LCD (grams mode)
    // Must match official de1app: 03 0A 01 01 00 01 [xor]
    sendCommand(QByteArray::fromHex("0A01010001"));
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
