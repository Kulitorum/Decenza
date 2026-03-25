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
        R2_WARN("Cannot read — not connected");
        return;
    }

    m_measuring = true;
    emit measuringChanged();
    R2_LOG("Requesting single test from R2");

    // Official protocol: Func=3 (Device Action), Cmd=0 (Single Test), DataLen=0
    // Command: DF DF 03 00 00 <checksum>
    QByteArray cmd;
    cmd.append(static_cast<char>(0xDF));  // Header
    cmd.append(static_cast<char>(0xDF));  // Header
    cmd.append(static_cast<char>(0x03));  // Func: Device Action
    cmd.append(static_cast<char>(0x00));  // Cmd: Single Test
    cmd.append(static_cast<char>(0x00));  // DataLen: 0

    // Checksum: sum of all bytes & 0xFF
    uint8_t checksum = 0;
    for (qsizetype i = 0; i < cmd.size(); ++i)
        checksum += static_cast<uint8_t>(cmd[i]);
    cmd.append(static_cast<char>(checksum));

    sendCommand(cmd);

    // Safety timeout
    QTimer::singleShot(15000, this, [this]() {
        if (m_measuring) {
            R2_WARN("Measurement timeout");
            m_measuring = false;
            emit measuringChanged();
        }
    });
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

        // Send "get temperature unit" as init handshake (Func=1, Cmd=0, DataLen=0)
        // This benign query confirms the BLE link is working and may wake the R2
        QTimer::singleShot(200, this, [this]() {
            if (!m_transport || !m_characteristicsReady) return;
            QByteArray initCmd;
            initCmd.append(static_cast<char>(0xDF));
            initCmd.append(static_cast<char>(0xDF));
            initCmd.append(static_cast<char>(0x01));  // Func: Settings
            initCmd.append(static_cast<char>(0x00));  // Cmd: Temperature Unit
            initCmd.append(static_cast<char>(0x00));  // DataLen: 0 (query)
            uint8_t checksum = 0;
            for (qsizetype i = 0; i < initCmd.size(); ++i)
                checksum += static_cast<uint8_t>(initCmd[i]);
            initCmd.append(static_cast<char>(checksum));
            R2_LOG(QString("Sending init query: %1").arg(QString(initCmd.toHex(' '))));
            sendCommand(initCmd);
        });
    });
}

void DiFluidR2::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid,
                                        const QByteArray& value) {
    // Accept data from any characteristic on our service
    handlePacket(value);
}

// === Packet parsing ===

void DiFluidR2::handlePacket(const QByteArray& packet) {
    // Official DiFluid protocol: DF DF <Func> <Cmd> <DataLen> <Data0..DataN> <Checksum>
    // Minimum packet: header(2) + func(1) + cmd(1) + datalen(1) + checksum(1) = 6 bytes
    if (packet.size() < 6) {
        return;
    }

    // Validate header (0xDF 0xDF)
    if (static_cast<uint8_t>(packet[0]) != PACKET_HEADER ||
        static_cast<uint8_t>(packet[1]) != PACKET_HEADER) {
        R2_LOG(QString("Non-protocol packet (%1 bytes): %2")
            .arg(packet.size()).arg(QString(packet.left(8).toHex(' '))));
        return;
    }

    if (!validateChecksum(packet)) {
        R2_WARN(QString("Checksum failed: %1").arg(QString(packet.toHex(' '))));
        return;
    }

    uint8_t func = static_cast<uint8_t>(packet[2]);
    uint8_t cmd = static_cast<uint8_t>(packet[3]);
    uint8_t dataLen = static_cast<uint8_t>(packet[4]);

    // Data starts at byte 5, length = dataLen
    // Verify packet length: 5 (header+func+cmd+datalen) + dataLen + 1 (checksum)
    if (packet.size() < 5 + dataLen + 1) {
        R2_WARN(QString("Packet too short for declared data length"));
        return;
    }

    // Only log action results, not every packet


    // Func 3 = Device Action (test results)
    if (func == 3) {
        if (cmd == 254) {
            // Error response
            uint8_t errClass = dataLen > 0 ? static_cast<uint8_t>(packet[5]) : 0;
            uint8_t errCode = dataLen > 1 ? static_cast<uint8_t>(packet[6]) : 0;
            R2_WARN(QString("R2 error: class=%1 code=%2").arg(errClass).arg(errCode));
            if (errClass == 2 && errCode == 3) emit errorOccurred("No liquid detected");
            else if (errClass == 2 && errCode == 4) emit errorOccurred("Beyond range");
            else emit errorOccurred(QString("R2 error %1/%2").arg(errClass).arg(errCode));
            m_measuring = false;
            emit measuringChanged();
            return;
        }
        if (cmd == 255) {
            R2_WARN("R2 unknown error");
            m_measuring = false;
            emit measuringChanged();
            return;
        }

        // Test result packets: Data0 = package number
        if (dataLen < 1) return;
        uint8_t packNo = static_cast<uint8_t>(packet[5]);

        switch (packNo) {
        case 0: {
            // Status: Data1 = status code
            uint8_t status = dataLen >= 2 ? static_cast<uint8_t>(packet[6]) : 0;
            R2_LOG(QString("Status: %1").arg(status));
            if (status == 0) {
                R2_LOG("Test finished");
            } else if (status == 11) {
                R2_LOG("Test started");
            }
            break;
        }
        case 1: {
            // Temperature: Data1-2 = prism temp * 10, Data3-4 = tank temp * 10
            if (dataLen < 5) return;
            uint16_t prismTemp = static_cast<uint16_t>(
                (static_cast<uint8_t>(packet[6]) << 8) | static_cast<uint8_t>(packet[7]));
            uint16_t tankTemp = static_cast<uint16_t>(
                (static_cast<uint8_t>(packet[8]) << 8) | static_cast<uint8_t>(packet[9]));
            m_temperature = prismTemp / 10.0;
            R2_LOG(QString("Temperature: prism=%1°C tank=%2°C")
                .arg(prismTemp / 10.0, 0, 'f', 1).arg(tankTemp / 10.0, 0, 'f', 1));
            emit temperatureChanged(m_temperature);
            break;
        }
        case 2: {
            // TDS result: Data1-2 = concentration * 100, Data3-6 = refractive index * 100000
            if (dataLen < 3) return;
            uint16_t tdsRaw = static_cast<uint16_t>(
                (static_cast<uint8_t>(packet[6]) << 8) | static_cast<uint8_t>(packet[7]));
            m_tds = tdsRaw / 100.0;
            R2_LOG(QString("TDS: %1% (raw=%2)").arg(m_tds, 0, 'f', 2).arg(tdsRaw));
            emit tdsChanged(m_tds);
            emit measurementComplete();
            m_measuring = false;
            emit measuringChanged();
            break;
        }
        case 3: {
            // Average result: same format as pack 2
            if (dataLen < 3) return;
            uint16_t tdsRaw = static_cast<uint16_t>(
                (static_cast<uint8_t>(packet[6]) << 8) | static_cast<uint8_t>(packet[7]));
            double avgTds = tdsRaw / 100.0;
            R2_LOG(QString("Average TDS: %1% (raw=%2)").arg(avgTds, 0, 'f', 2).arg(tdsRaw));
            // Use average as the final TDS
            m_tds = avgTds;
            emit tdsChanged(m_tds);
            emit measurementComplete();
            m_measuring = false;
            emit measuringChanged();
            break;
        }
        case 4: {
            // Average temp + count info
            R2_LOG(QString("Average temp/count packet"));
            break;
        }
        default:
            R2_LOG(QString("Unknown pack number: %1").arg(packNo));
            break;
        }
    } else {
        // Non-action responses (device info, settings)
        R2_LOG(QString("Response: Func=%1 Cmd=%2").arg(func).arg(cmd));
    }
}

bool DiFluidR2::validateChecksum(const QByteArray& packet) const {
    if (packet.size() < 7) return false;

    // Checksum = sum of all bytes from index 0 to N-2, mod 256
    uint8_t calculated = 0;
    for (qsizetype i = 0; i < packet.size() - 1; ++i) {
        calculated += static_cast<uint8_t>(packet[i]);
    }
    uint8_t received = static_cast<uint8_t>(packet[packet.size() - 1]);
    return calculated == received;
}

void DiFluidR2::sendCommand(const QByteArray& cmd) {
    if (!m_transport || !m_characteristicsReady) return;
    m_transport->writeCharacteristic(Refractometer::DiFluidR2::SERVICE,
                                     Refractometer::DiFluidR2::CHARACTERISTIC, cmd);
}
