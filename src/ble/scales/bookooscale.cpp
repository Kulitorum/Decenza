#include "bookooscale.h"
#include "../protocol/de1characteristics.h"
#include "scalelogging.h"
#include <QTimer>

#define BOOKOO_LOG(msg)  SCALE_LOG("BookooScale", msg)
#define BOOKOO_WARN(msg) SCALE_WARN("BookooScale", msg)

BookooScale::BookooScale(ScaleBleTransport* transport, QObject* parent)
    : ScaleDevice(parent)
    , m_transport(transport)
{
    if (m_transport) {
        m_transport->setParent(this);

        connect(m_transport, &ScaleBleTransport::connected,
                this, &BookooScale::onTransportConnected);
        connect(m_transport, &ScaleBleTransport::disconnected,
                this, &BookooScale::onTransportDisconnected);
        connect(m_transport, &ScaleBleTransport::error,
                this, &BookooScale::onTransportError);
        connect(m_transport, &ScaleBleTransport::serviceDiscovered,
                this, &BookooScale::onServiceDiscovered);
        connect(m_transport, &ScaleBleTransport::servicesDiscoveryFinished,
                this, &BookooScale::onServicesDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicsDiscoveryFinished,
                this, &BookooScale::onCharacteristicsDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicChanged,
                this, &BookooScale::onCharacteristicChanged);
        connect(m_transport, &ScaleBleTransport::notificationsEnabled,
                this, &BookooScale::onNotificationsEnabled);
        // Forward transport logs to scale log
        connect(m_transport, &ScaleBleTransport::logMessage,
                this, &ScaleDevice::logMessage);
    }
}

BookooScale::~BookooScale() {
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
}

void BookooScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (!m_transport) {
        emit errorOccurred("No transport available");
        return;
    }

    m_name = device.name();
    m_serviceFound = false;
    m_characteristicsReady = false;

    // Log device identifier (UUID on iOS, address on other platforms)
    QString deviceId = device.address().isNull()
        ? device.deviceUuid().toString()
        : device.address().toString();
    BOOKOO_LOG(QString("Connecting to %1 (%2)").arg(device.name(), deviceId));

    m_transport->connectToDevice(device);
}

void BookooScale::onTransportConnected() {
    BOOKOO_LOG("Transport connected, starting service discovery");
    m_transport->discoverServices();
}

void BookooScale::onTransportDisconnected() {
    BOOKOO_LOG("Transport disconnected");
    setConnected(false);
}

void BookooScale::onTransportError(const QString& message) {
    // Log but don't fail - Bookoo rejects CCCD writes but may still work.
    // Qt transport already swallows DescriptorWriteError before it reaches here,
    // and CoreBluetooth uses different error strings, so we cannot reliably
    // filter by message content. Keep the connection alive for all errors.
    BOOKOO_WARN(QString("Transport error: %1 (may be expected)").arg(message));
}

void BookooScale::onServiceDiscovered(const QBluetoothUuid& uuid) {
    BOOKOO_LOG(QString("Service discovered: %1").arg(uuid.toString()));

    if (uuid == Scale::Bookoo::SERVICE) {
        BOOKOO_LOG("Found Bookoo service");
        m_serviceFound = true;
    }
}

void BookooScale::onServicesDiscoveryFinished() {
    BOOKOO_LOG(QString("Service discovery finished, service found: %1").arg(m_serviceFound));

    if (!m_serviceFound) {
        BOOKOO_WARN(QString("Service %1 not found!").arg(Scale::Bookoo::SERVICE.toString()));
        emit errorOccurred("Bookoo service not found");
        return;
    }

    // Discover characteristics for the Bookoo service
    m_transport->discoverCharacteristics(Scale::Bookoo::SERVICE);
}

void BookooScale::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    if (serviceUuid != Scale::Bookoo::SERVICE) return;
    if (m_characteristicsReady) {
        BOOKOO_LOG("Characteristics already set up, ignoring duplicate callback");
        return;
    }

    BOOKOO_LOG("Characteristics discovered");
    m_characteristicsReady = true;
    setConnected(true);

    // Enable notifications with retry pattern (like DecentScale)
    // iOS CoreBluetooth needs more time for CCCD operations
    BOOKOO_LOG("Scheduling notification enable at 300ms and 500ms (iOS reliability)");

    // First attempt at 300ms
    QTimer::singleShot(300, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        BOOKOO_LOG("Enabling notifications (300ms - first attempt)");
        m_transport->enableNotifications(Scale::Bookoo::SERVICE, Scale::Bookoo::STATUS);
    });

    // Retry at 500ms for reliability (matches DecentScale pattern)
    QTimer::singleShot(500, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        BOOKOO_LOG("Enabling notifications (500ms - retry)");
        m_transport->enableNotifications(Scale::Bookoo::SERVICE, Scale::Bookoo::STATUS);
    });
}

void BookooScale::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid,
                                          const QByteArray& value) {
    if (characteristicUuid == Scale::Bookoo::STATUS) {
        parseWeightData(value);
    }
}

void BookooScale::onNotificationsEnabled(const QBluetoothUuid& characteristicUuid) {
    BOOKOO_LOG(QString("Notifications enabled for %1").arg(characteristicUuid.toString()));
}

void BookooScale::parseWeightData(const QByteArray& data) {
    // Bookoo 20-byte weight notification (from BooKooCode/OpenSource protocol docs):
    // [0]=0x03, [1]=0x0B, [2-4]=timer ms, [5]=unit, [6]=sign, [7-9]=weight*100,
    // [10]=flow sign, [11-12]=flow*100, [13]=battery%, [14-15]=standby min,
    // [16]=buzzer, [17]=flow smooth, [18]=auto-mode condition, [19]=checksum
    // Note: protocol doc describes XOR checksum but the documented command examples
    // don't match a simple XOR. Checksum validation is skipped until the algorithm
    // can be verified against real hardware.
    if (data.size() < 10) return;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.constData());

    char sign = static_cast<char>(d[6]);

    // Weight is 3 bytes big-endian in hundredths of gram
    uint32_t weightRaw = (static_cast<uint32_t>(d[7]) << 16)
                       | (static_cast<uint32_t>(d[8]) << 8)
                       | d[9];
    double weight = weightRaw / 100.0;

    if (sign == '-') {
        weight = -weight;
    }

    setWeight(weight);

    // Battery at byte 13 (0-100%)
    if (data.size() >= 14) {
        uint8_t battery = d[13];
        if (battery <= 100) {
            setBatteryLevel(battery);
        }
    }
}

void BookooScale::sendCommand(const QByteArray& cmd) {
    if (!m_transport || !m_characteristicsReady) return;
    m_transport->writeCharacteristic(Scale::Bookoo::SERVICE, Scale::Bookoo::CMD, cmd);
}

void BookooScale::sendKeepAlive() {
    if (m_transport && m_characteristicsReady)
        m_transport->enableNotifications(Scale::Bookoo::SERVICE, Scale::Bookoo::STATUS);
}

void BookooScale::tare() {
    sendCommand(QByteArray::fromHex("030A01000008"));
}

void BookooScale::startTimer() {
    sendCommand(QByteArray::fromHex("030A0400000A"));
}

void BookooScale::stopTimer() {
    sendCommand(QByteArray::fromHex("030A0500000D"));
}

void BookooScale::resetTimer() {
    sendCommand(QByteArray::fromHex("030A0600000C"));
}

void BookooScale::sleep() {
    // Bookoo protocol has no instant power-off command. Emit sleepCompleted immediately
    // so the app exit sequence and remote sleep don't block waiting for BLE confirmation.
    // (Matches Eureka pattern for scales without power-off confirmation.)
    emit sleepCompleted();
}
