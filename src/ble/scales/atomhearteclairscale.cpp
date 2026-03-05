#include "atomhearteclairscale.h"
#include "../protocol/de1characteristics.h"
#include "scalelogging.h"
#include <QTimer>

#define ECLAIR_LOG(msg)  SCALE_LOG("AtomheartEclairScale", msg)
#define ECLAIR_WARN(msg) SCALE_WARN("AtomheartEclairScale", msg)

AtomheartEclairScale::AtomheartEclairScale(ScaleBleTransport* transport, QObject* parent)
    : ScaleDevice(parent)
    , m_transport(transport)
{
    if (m_transport) {
        m_transport->setParent(this);

        connect(m_transport, &ScaleBleTransport::connected,
                this, &AtomheartEclairScale::onTransportConnected);
        connect(m_transport, &ScaleBleTransport::disconnected,
                this, &AtomheartEclairScale::onTransportDisconnected);
        connect(m_transport, &ScaleBleTransport::error,
                this, &AtomheartEclairScale::onTransportError);
        connect(m_transport, &ScaleBleTransport::serviceDiscovered,
                this, &AtomheartEclairScale::onServiceDiscovered);
        connect(m_transport, &ScaleBleTransport::servicesDiscoveryFinished,
                this, &AtomheartEclairScale::onServicesDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicsDiscoveryFinished,
                this, &AtomheartEclairScale::onCharacteristicsDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicChanged,
                this, &AtomheartEclairScale::onCharacteristicChanged);
        // Forward transport logs to scale log
        connect(m_transport, &ScaleBleTransport::logMessage,
                this, &ScaleDevice::logMessage);
    }
}

AtomheartEclairScale::~AtomheartEclairScale() {
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
}

void AtomheartEclairScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (!m_transport) {
        emit errorOccurred("No transport available");
        return;
    }

    m_name = device.name();
    m_serviceFound = false;
    m_characteristicsReady = false;

    ECLAIR_LOG(QString("Connecting to %1 (%2)")
               .arg(device.name())
               .arg(device.address().toString()));

    m_transport->connectToDevice(device);
}

void AtomheartEclairScale::onTransportConnected() {
    ECLAIR_LOG("Transport connected, starting service discovery");
    m_transport->discoverServices();
}

void AtomheartEclairScale::onTransportDisconnected() {
    ECLAIR_LOG("Transport disconnected");
    setConnected(false);
}

void AtomheartEclairScale::onTransportError(const QString& message) {
    ECLAIR_WARN(QString("Transport error: %1").arg(message));
    emit errorOccurred("Atomheart Eclair scale connection error");
    setConnected(false);
}

void AtomheartEclairScale::onServiceDiscovered(const QBluetoothUuid& uuid) {
    ECLAIR_LOG(QString("Service discovered: %1").arg(uuid.toString()));
    if (uuid == Scale::AtomheartEclair::SERVICE) {
        ECLAIR_LOG("Found Atomheart Eclair service");
        m_serviceFound = true;
    }
}

void AtomheartEclairScale::onServicesDiscoveryFinished() {
    ECLAIR_LOG(QString("Service discovery finished, service found: %1").arg(m_serviceFound));
    if (!m_serviceFound) {
        ECLAIR_WARN(QString("Atomheart Eclair service %1 not found!").arg(Scale::AtomheartEclair::SERVICE.toString()));
        emit errorOccurred("Atomheart Eclair service not found");
        return;
    }
    m_transport->discoverCharacteristics(Scale::AtomheartEclair::SERVICE);
}

void AtomheartEclairScale::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    if (serviceUuid != Scale::AtomheartEclair::SERVICE) return;
    if (m_characteristicsReady) {
        ECLAIR_LOG("Characteristics already set up, ignoring duplicate callback");
        return;
    }

    ECLAIR_LOG("Characteristics discovered");
    m_characteristicsReady = true;
    setConnected(true);

    // de1app uses 200ms delay for Atomheart Eclair
    ECLAIR_LOG("Scheduling notification enable in 200ms (de1app timing)");
    QTimer::singleShot(200, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        ECLAIR_LOG("Enabling notifications (200ms)");
        m_transport->enableNotifications(Scale::AtomheartEclair::SERVICE, Scale::AtomheartEclair::STATUS);
    });
}

bool AtomheartEclairScale::validateXor(const QByteArray& data) {
    if (data.size() < 2) return false;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.constData());

    // XOR all bytes except header and last (XOR) byte
    uint8_t xorResult = 0;
    for (int i = 1; i < data.size() - 1; i++) {
        xorResult ^= d[i];
    }

    return xorResult == d[data.size() - 1];
}

void AtomheartEclairScale::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid,
                                                    const QByteArray& value) {
    if (characteristicUuid == Scale::AtomheartEclair::STATUS) {
        // Atomheart Eclair format: 'W' (0x57) header, 4-byte weight in milligrams, 4-byte timer, XOR byte
        if (value.size() >= 9) {
            const uint8_t* d = reinterpret_cast<const uint8_t*>(value.constData());

            // Check header is 'W' (0x57)
            if (d[0] != 0x57) return;

            // Validate XOR checksum
            if (!validateXor(value)) {
                ECLAIR_WARN("XOR checksum failed");
                return;
            }

            // Weight is 4-byte signed int32 in milligrams (little-endian)
            int32_t weightMg = d[1] | (d[2] << 8) | (d[3] << 16) | (d[4] << 24);
            double weight = weightMg / 1000.0;  // Convert to grams

            setWeight(weight);
        }
    }
}

void AtomheartEclairScale::sendCommand(const QByteArray& cmd) {
    if (!m_transport || !m_characteristicsReady) return;
    m_transport->writeCharacteristic(Scale::AtomheartEclair::SERVICE, Scale::AtomheartEclair::CMD, cmd);
}

void AtomheartEclairScale::sendKeepAlive() {
    if (m_transport && m_characteristicsReady)
        m_transport->enableNotifications(Scale::AtomheartEclair::SERVICE, Scale::AtomheartEclair::STATUS);
}

void AtomheartEclairScale::tare() {
    sendCommand(QByteArray::fromHex("540101"));
}

void AtomheartEclairScale::startTimer() {
    sendCommand(QByteArray::fromHex("430101"));
}

void AtomheartEclairScale::stopTimer() {
    sendCommand(QByteArray::fromHex("430000"));
}

void AtomheartEclairScale::resetTimer() {
    // Eclair resets timer on tare
    tare();
}
