#include "difluidr2.h"
#include "../protocol/de1characteristics.h"
#include "../transport/scalebletransport.h"
#include <QTimer>

// Logging macros — same pattern as scale drivers but emits logMessage() directly
#define R2_LOG(msg) do { \
    QString _msg = QString("[BLE DiFluidR2] ") + msg; \
    qDebug().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

#define R2_WARN(msg) do { \
    QString _msg = QString("[BLE DiFluidR2] ") + msg; \
    qWarning().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

// Protocol constants
static constexpr uint8_t PACKET_HEADER = 0xDF;
static constexpr int PACKET_MIN_LENGTH = 5;

DiFluidR2::DiFluidR2(ScaleBleTransport* transport, QObject* parent)
    : QObject(parent)
    , m_transport(transport)
{
    if (m_transport) {
        m_transport->setParent(this);

        connect(m_transport, &ScaleBleTransport::connected,
                this, &DiFluidR2::onTransportConnected);
        connect(m_transport, &ScaleBleTransport::disconnected,
                this, &DiFluidR2::onTransportDisconnected);
        connect(m_transport, &ScaleBleTransport::error,
                this, &DiFluidR2::onTransportError);
        connect(m_transport, &ScaleBleTransport::serviceDiscovered,
                this, &DiFluidR2::onServiceDiscovered);
        connect(m_transport, &ScaleBleTransport::servicesDiscoveryFinished,
                this, &DiFluidR2::onServicesDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicsDiscoveryFinished,
                this, &DiFluidR2::onCharacteristicsDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicChanged,
                this, &DiFluidR2::onCharacteristicChanged);
        connect(m_transport, &ScaleBleTransport::logMessage,
                this, &DiFluidR2::logMessage);
    }
}

DiFluidR2::~DiFluidR2() {
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
}

bool DiFluidR2::isR2Device(const QString& name) {
    QString lower = name.toLower();
    // Match "R2 Extract", "DiFluid R2", etc.
    // Exclude plain "difluid" (that's the Microbalance scale)
    return lower.contains("r2 extract") || lower.contains("r2extract")
        || (lower.contains("difluid") && lower.contains("r2"));
}

void DiFluidR2::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (!m_transport) {
        emit errorOccurred("No transport available");
        return;
    }

    m_name = device.name();
    m_serviceFound = false;
    m_characteristicsReady = false;

    R2_LOG(QString("Connecting to %1 (%2)")
               .arg(device.name())
               .arg(device.address().isNull() ? device.deviceUuid().toString()
                                              : device.address().toString()));

    m_transport->connectToDevice(device);
}

void DiFluidR2::disconnectFromDevice() {
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
    m_measuring = false;
    m_connected = false;
    m_serviceFound = false;
    m_characteristicsReady = false;
    emit connectedChanged();
    emit measuringChanged();
}

void DiFluidR2::requestMeasurement() {
    if (!m_connected || !m_characteristicsReady) {
        R2_WARN("Cannot request measurement — not connected");
        return;
    }

    m_measuring = true;
    emit measuringChanged();
    R2_LOG("Requesting TDS measurement");

    // Command: DF DF 01 00 01 <checksum>
    // Function 0x01, command 0x00, data 0x01 = request measurement
    QByteArray command;
    command.append(static_cast<char>(PACKET_HEADER));
    command.append(static_cast<char>(PACKET_HEADER));
    command.append(static_cast<char>(0x01));  // Function
    command.append(static_cast<char>(0x00));  // Command
    command.append(static_cast<char>(0x01));  // Data

    // XOR checksum of bytes 2..N-1
    uint8_t checksum = 0;
    for (qsizetype i = 2; i < command.size(); ++i) {
        checksum ^= static_cast<uint8_t>(command[i]);
    }
    command.append(static_cast<char>(checksum));

    sendCommand(command);
}

// === Transport callbacks ===

void DiFluidR2::onTransportConnected() {
    R2_LOG("Transport connected, starting service discovery");
    m_transport->discoverServices();
}

void DiFluidR2::onTransportDisconnected() {
    R2_LOG("Transport disconnected");
    m_connected = false;
    m_characteristicsReady = false;
    m_serviceFound = false;
    emit connectedChanged();
}

void DiFluidR2::onTransportError(const QString& message) {
    R2_WARN(QString("Transport error: %1").arg(message));
    emit errorOccurred("DiFluid R2 connection error");
    m_connected = false;
    emit connectedChanged();
}

void DiFluidR2::onServiceDiscovered(const QBluetoothUuid& uuid) {
    R2_LOG(QString("Service discovered: %1").arg(uuid.toString()));
    if (uuid == Refractometer::DiFluidR2::SERVICE) {
        R2_LOG("Found DiFluid R2 service");
        m_serviceFound = true;
    }
}

void DiFluidR2::onServicesDiscoveryFinished() {
    R2_LOG(QString("Service discovery finished, service found: %1").arg(m_serviceFound));
    if (!m_serviceFound) {
        R2_WARN(QString("DiFluid R2 service %1 not found!")
                    .arg(Refractometer::DiFluidR2::SERVICE.toString()));
        emit errorOccurred("DiFluid R2 service not found");
        return;
    }
    m_transport->discoverCharacteristics(Refractometer::DiFluidR2::SERVICE);
}

void DiFluidR2::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    if (serviceUuid != Refractometer::DiFluidR2::SERVICE) return;
    if (m_characteristicsReady) {
        R2_LOG("Characteristics already set up, ignoring duplicate callback");
        return;
    }

    R2_LOG("Characteristics discovered, enabling notifications");
    m_characteristicsReady = true;

    // Same 100ms delay as DifluidScale (de1app timing)
    QTimer::singleShot(100, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        m_transport->enableNotifications(Refractometer::DiFluidR2::SERVICE,
                                         Refractometer::DiFluidR2::CHARACTERISTIC);
        m_connected = true;
        emit connectedChanged();
        R2_LOG("Connected and ready for measurements");
    });
}

void DiFluidR2::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid,
                                        const QByteArray& value) {
    if (characteristicUuid == Refractometer::DiFluidR2::CHARACTERISTIC) {
        handlePacket(value);
    }
}

// === Packet parsing ===

void DiFluidR2::handlePacket(const QByteArray& packet) {
    if (packet.size() < PACKET_MIN_LENGTH) {
        R2_WARN(QString("Packet too short: %1 bytes").arg(packet.size()));
        return;
    }

    // Validate header (0xDF 0xDF)
    if (static_cast<uint8_t>(packet[0]) != PACKET_HEADER ||
        static_cast<uint8_t>(packet[1]) != PACKET_HEADER) {
        R2_WARN("Invalid packet header");
        return;
    }

    if (!validateChecksum(packet)) {
        R2_WARN("Checksum validation failed");
        return;
    }

    // Function byte at index 2 determines packet type
    uint8_t packageType = static_cast<uint8_t>(packet[2]);

    switch (packageType) {
    case 0x00: {
        // Package 0: Status — measurement ready / no liquid / out of range
        if (packet.size() < 4) return;
        uint8_t status = static_cast<uint8_t>(packet[3]);
        if (status == 0x00) {
            R2_LOG("Measurement ready");
        } else if (status == 0x01) {
            R2_WARN("No liquid detected");
            emit errorOccurred("No liquid detected");
        } else if (status == 0x02) {
            R2_WARN("Measurement beyond range");
            emit errorOccurred("Measurement beyond range");
        }
        m_measuring = false;
        emit measuringChanged();
        break;
    }
    case 0x01: {
        // Package 1: Temperature — bytes 3-4 as big-endian uint16, / 100.0 for °C
        if (packet.size() < 5) return;
        uint16_t tempRaw = static_cast<uint16_t>(
            (static_cast<uint8_t>(packet[3]) << 8) | static_cast<uint8_t>(packet[4]));
        m_temperature = tempRaw / 100.0;
        R2_LOG(QString("Temperature: %1 C").arg(m_temperature, 0, 'f', 1));
        emit temperatureChanged(m_temperature);
        break;
    }
    case 0x02: {
        // Package 2: TDS — bytes 3-4 as big-endian uint16, / 100.0 for TDS%
        if (packet.size() < 5) return;
        uint16_t tdsRaw = static_cast<uint16_t>(
            (static_cast<uint8_t>(packet[3]) << 8) | static_cast<uint8_t>(packet[4]));
        m_tds = tdsRaw / 100.0;
        R2_LOG(QString("TDS: %1%").arg(m_tds, 0, 'f', 2));
        emit tdsChanged(m_tds);
        emit measurementComplete();
        m_measuring = false;
        emit measuringChanged();
        break;
    }
    default:
        R2_LOG(QString("Unknown package type: 0x%1").arg(packageType, 2, 16, QChar('0')));
        break;
    }
}

bool DiFluidR2::validateChecksum(const QByteArray& packet) const {
    if (packet.size() < PACKET_MIN_LENGTH) return false;

    // XOR of bytes 2..N-2 should equal byte N-1
    uint8_t calculated = 0;
    for (qsizetype i = 2; i < packet.size() - 1; ++i) {
        calculated ^= static_cast<uint8_t>(packet[i]);
    }
    uint8_t received = static_cast<uint8_t>(packet[packet.size() - 1]);
    return calculated == received;
}

void DiFluidR2::sendCommand(const QByteArray& cmd) {
    if (!m_transport || !m_characteristicsReady) return;
    m_transport->writeCharacteristic(Refractometer::DiFluidR2::SERVICE,
                                     Refractometer::DiFluidR2::CHARACTERISTIC, cmd);
}
