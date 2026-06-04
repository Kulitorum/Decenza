#include "difluidr1.h"

#include "../protocol/de1characteristics.h"
#include "../transport/scalebletransport.h"
#include "aes128.h"

#include <QtEndian>
#include <cmath>

#define R1_LOG(msg) do { \
    QString _msg = QString("[BLE DiFluidR1] ") + msg; \
    qDebug().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

#define R1_WARN(msg) do { \
    QString _msg = QString("[BLE DiFluidR1] ") + msg; \
    qWarning().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

// R1 has no documented out-of-range sentinel, but the same physical
// plausibility ceiling we use for R2 still applies — brewed coffee never
// approaches it, so anything above is a hardware fault.
static constexpr double MAX_PLAUSIBLE_TDS = 35.0;

DiFluidR1::DiFluidR1(ScaleBleTransport* transport, QObject* parent)
    : RefractometerDevice(parent)
    , m_transport(transport)
{
    m_measurementTimer.setSingleShot(true);
    m_measurementTimer.setInterval(15000);
    connect(&m_measurementTimer, &QTimer::timeout, this, [this]() {
        if (m_measuring) {
            R1_WARN("Measurement timeout");
            m_measuring = false;
            emit measuringChanged();
        }
    });

    // 100ms init delay mirrors R2 (and de1app) — Qt's BLE layer has no
    // "ready after characteristic discovery" signal and CCCD writes need
    // a brief settling period on Android/iOS before they reliably take.
    m_initTimer.setSingleShot(true);
    m_initTimer.setInterval(100);
    connect(&m_initTimer, &QTimer::timeout, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;

        // Per spec: enable notifications on 1E01 (measurement), 1E02 (battery),
        // 1E06/1E07 (status), and 1E08 (command ACK). Platform GATT stacks
        // serialize these through a single ATT pipeline so submitting them
        // back-to-back is safe; the spec suggests a 120ms stagger but reports
        // it's not strictly required.
        using namespace Refractometer::DiFluidR1;
        m_transport->enableNotifications(SERVICE, DATA);
        m_transport->enableNotifications(SERVICE, BATTERY);
        m_transport->enableNotifications(SERVICE, STATUS);
        m_transport->enableNotifications(SERVICE, STATUS2);
        m_transport->enableNotifications(SERVICE, CMD);

        // Read the salt — characteristicRead() will fire on completion and
        // mark the link "connected" once the AES key is derived.
        m_transport->readCharacteristic(SERVICE, SALT);
        R1_LOG("Notifications enabled, reading salt");
    });

    if (m_transport) {
        m_transport->setParent(this);

        connect(m_transport, &ScaleBleTransport::connected,
                this, &DiFluidR1::onTransportConnected);
        connect(m_transport, &ScaleBleTransport::disconnected,
                this, &DiFluidR1::onTransportDisconnected);
        connect(m_transport, &ScaleBleTransport::error,
                this, &DiFluidR1::onTransportError);
        connect(m_transport, &ScaleBleTransport::serviceDiscovered,
                this, &DiFluidR1::onServiceDiscovered);
        connect(m_transport, &ScaleBleTransport::servicesDiscoveryFinished,
                this, &DiFluidR1::onServicesDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicsDiscoveryFinished,
                this, &DiFluidR1::onCharacteristicsDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicChanged,
                this, &DiFluidR1::onCharacteristicChanged);
        connect(m_transport, &ScaleBleTransport::characteristicRead,
                this, &DiFluidR1::onCharacteristicRead);
        connect(m_transport, &ScaleBleTransport::logMessage,
                this, &DiFluidR1::logMessage);
    }
}

DiFluidR1::~DiFluidR1() {
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
}

bool DiFluidR1::isR1Device(const QString& name) {
    // R1 advertises with names starting `DFT_TDJ_<serial>`. The official app
    // matches the literal prefix; we do the same (case-insensitive to survive
    // platform name normalization).
    return name.startsWith(QStringLiteral("DFT_TDJ"), Qt::CaseInsensitive);
}

std::array<uint8_t, 16> DiFluidR1::deriveKey(const QByteArray& salt) {
    std::array<uint8_t, 16> key{};
    // key[0..5] = (salt[i] - floor(i/2)) & 0xFF
    // key[6..15] = 0xAA
    for (int i = 0; i < 6; ++i) {
        const int s = (salt.size() > i) ? static_cast<uint8_t>(salt[i]) : 0;
        key[i] = static_cast<uint8_t>((s - (i / 2)) & 0xFF);
    }
    for (int i = 6; i < 16; ++i) {
        key[i] = 0xAA;
    }
    return key;
}

QByteArray DiFluidR1::decryptFrame(const QByteArray& ciphertext,
                                   const std::array<uint8_t, 16>& key) {
    if (ciphertext.size() != 16) return {};
    QByteArray pt(16, Qt::Uninitialized);
    decenza::aes128::decryptBlock(
        key,
        reinterpret_cast<const uint8_t*>(ciphertext.constData()),
        reinterpret_cast<uint8_t*>(pt.data()));
    return pt;
}

bool DiFluidR1::parsePlaintext(const QByteArray& pt,
                               double& outTempC, double& outBrix,
                               double& outRi, double& outRawSample) {
    if (pt.size() != 16) return false;
    const auto* b = reinterpret_cast<const uchar*>(pt.constData());
    const qint32  tempRaw  = qFromBigEndian<qint32>(b + 0);
    const qint32  brixRaw  = qFromBigEndian<qint32>(b + 4);
    const quint32 riRaw    = qFromBigEndian<quint32>(b + 8);
    const qint32  sampleRaw = qFromBigEndian<qint32>(b + 12);
    outTempC     = tempRaw   / 100.0;
    outBrix      = brixRaw   / 100.0;
    outRi        = riRaw     / 100000.0;
    outRawSample = sampleRaw / 100.0;
    return true;
}

QByteArray DiFluidR1::buildDoWriteFrame(double value, const QByteArray& label,
                                        const std::array<uint8_t, 16>& key) {
    // Plaintext layout (16 bytes, zero-padded):
    //   bytes 0..3: int32 big-endian of round(value * 100)
    //   byte  4   : ASCII length of label
    //   bytes 5.. : ASCII label
    //   rest      : zero padding
    if (label.size() > 11) return {};
    const qint32 scaled = static_cast<qint32>(std::llround(value * 100.0));
    QByteArray pt(16, '\0');
    auto* p = reinterpret_cast<uchar*>(pt.data());
    qToBigEndian<qint32>(scaled, p);
    p[4] = static_cast<uchar>(label.size());
    for (int i = 0; i < label.size(); ++i) {
        p[5 + i] = static_cast<uchar>(label[i]);
    }
    QByteArray ct(16, Qt::Uninitialized);
    decenza::aes128::encryptBlock(
        key,
        reinterpret_cast<const uint8_t*>(pt.constData()),
        reinterpret_cast<uint8_t*>(ct.data()));
    QByteArray frame;
    frame.reserve(17);
    frame.append(static_cast<char>(0x06));
    frame.append(ct);
    return frame;
}

void DiFluidR1::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (!m_transport) {
        R1_WARN("connectToDevice called with no transport");
        return;
    }

    m_name = device.name();
    m_serviceFound = false;
    m_characteristicsReady = false;
    m_keyReady = false;

    R1_LOG(QString("Connecting to %1 (%2)")
               .arg(device.name(),
                    device.address().isNull() ? device.deviceUuid().toString()
                                              : device.address().toString()));

    m_transport->connectToDevice(device);
}

void DiFluidR1::disconnectFromDevice() {
    m_measurementTimer.stop();
    m_initTimer.stop();
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
    m_measuring = false;
    m_connected = false;
    m_serviceFound = false;
    m_characteristicsReady = false;
    m_keyReady = false;
    emit connectedChanged();
    emit measuringChanged();
}

void DiFluidR1::requestMeasurement() {
    if (!m_connected || !m_keyReady) {
        R1_WARN("Cannot read — not connected");
        return;
    }

    m_measuring = true;
    emit measuringChanged();
    R1_LOG("Requesting measurement from R1 (doDetect)");

    // doDetect = 01 00. Spec: must be ATT Write Request (WriteWithResponse).
    QByteArray cmd;
    cmd.append(static_cast<char>(0x01));
    cmd.append(static_cast<char>(0x00));
    sendCommand(cmd);
    m_measurementTimer.start();
}

// === Transport callbacks ===

void DiFluidR1::onTransportConnected() {
    R1_LOG("Transport connected, starting service discovery");
    m_transport->discoverServices();
}

void DiFluidR1::onTransportDisconnected() {
    R1_LOG("Transport disconnected");
    m_measurementTimer.stop();
    m_initTimer.stop();
    m_connected = false;
    m_keyReady = false;
    m_characteristicsReady = false;
    m_serviceFound = false;
    m_measuring = false;
    emit connectedChanged();
    emit measuringChanged();
}

void DiFluidR1::onTransportError(const QString& message) {
    R1_WARN(QString("Transport error: %1").arg(message));
    m_measurementTimer.stop();
    m_initTimer.stop();
    m_connected = false;
    m_keyReady = false;
    m_characteristicsReady = false;
    m_serviceFound = false;
    m_measuring = false;
    emit connectedChanged();
    emit measuringChanged();
}

void DiFluidR1::onServiceDiscovered(const QBluetoothUuid& uuid) {
    R1_LOG(QString("Service discovered: %1").arg(uuid.toString()));
    if (uuid == Refractometer::DiFluidR1::SERVICE) {
        R1_LOG("Found DiFluid R1 service");
        m_serviceFound = true;
    }
}

void DiFluidR1::onServicesDiscoveryFinished() {
    R1_LOG(QString("Service discovery finished, service found: %1").arg(m_serviceFound));
    if (!m_serviceFound) {
        R1_WARN(QString("DiFluid R1 service %1 not found!")
                    .arg(Refractometer::DiFluidR1::SERVICE.toString()));
        return;
    }
    m_transport->discoverCharacteristics(Refractometer::DiFluidR1::SERVICE);
}

void DiFluidR1::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    if (serviceUuid != Refractometer::DiFluidR1::SERVICE) return;
    if (m_characteristicsReady) {
        R1_LOG("Characteristics already set up, ignoring duplicate callback");
        return;
    }
    R1_LOG("Characteristics discovered, enabling notifications");
    m_characteristicsReady = true;
    m_initTimer.start();
}

void DiFluidR1::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid,
                                        const QByteArray& value) {
    using namespace Refractometer::DiFluidR1;

    if (characteristicUuid == DATA) {
        handleMeasurementFrame(value);
        return;
    }

    if (characteristicUuid == BATTERY) {
        // 4 bytes, almost always `00 00 00 NN` — battery percent in the last byte.
        if (value.size() >= 4) {
            const int pct = static_cast<uint8_t>(value[3]);
            R1_LOG(QString("Battery: %1%").arg(pct));
        }
        return;
    }

    if (characteristicUuid == CMD) {
        // ACK channel — `01 FF` after doDetect, `02 FF` after doCalibration.
        if (value.size() >= 2) {
            const uint8_t a = static_cast<uint8_t>(value[0]);
            const uint8_t b = static_cast<uint8_t>(value[1]);
            R1_LOG(QString("CMD ack: %1 %2")
                       .arg(QString::number(a, 16).rightJustified(2, '0'),
                            QString::number(b, 16).rightJustified(2, '0')));
        }
        return;
    }

    if (characteristicUuid == STATUS || characteristicUuid == STATUS2) {
        // The official app treats this as an error-code index but in practice
        // `00 01` arrives as a "command received" signal — log only.
        R1_LOG(QString("Status (%1): %2")
                   .arg(characteristicUuid.toString(), QString(value.toHex(' '))));
        return;
    }

    R1_LOG(QString("Unhandled notify on %1: %2")
               .arg(characteristicUuid.toString(), QString(value.toHex(' '))));
}

void DiFluidR1::onCharacteristicRead(const QBluetoothUuid& characteristicUuid,
                                     const QByteArray& value) {
    if (characteristicUuid != Refractometer::DiFluidR1::SALT) return;

    if (value.size() < 6) {
        R1_WARN(QString("Salt read returned too few bytes: %1").arg(value.size()));
        return;
    }

    m_key = deriveKey(value);
    m_keyReady = true;
    R1_LOG(QString("Salt read (%1 bytes): %2 — key derived")
               .arg(value.size()).arg(QString(value.toHex(' '))));

    if (!m_connected) {
        m_connected = true;
        emit connectedChanged();
        R1_LOG("Connected and ready for measurements");
    }
}

// === Packet parsing ===

void DiFluidR1::handleMeasurementFrame(const QByteArray& ciphertext) {
    if (ciphertext.size() != 16) {
        R1_LOG(QString("Non-protocol DATA packet (%1 bytes)").arg(ciphertext.size()));
        return;
    }
    if (!m_keyReady) {
        R1_WARN("Measurement frame arrived before key derivation — dropping");
        return;
    }

    const QByteArray pt = decryptFrame(ciphertext, m_key);
    double tempC = 0.0, brix = 0.0, ri = 0.0, raw = 0.0;
    if (!parsePlaintext(pt, tempC, brix, ri, raw)) {
        R1_WARN("Decryption produced unparseable plaintext");
        return;
    }

    R1_LOG(QString("Reading: T=%1°C  Brix=%2%  RI=%3  raw=%4")
               .arg(tempC, 0, 'f', 2)
               .arg(brix, 0, 'f', 2)
               .arg(ri, 0, 'f', 5)
               .arg(raw, 0, 'f', 2));

    m_temperature = tempC;
    emit temperatureChanged(m_temperature);
    emitTdsResult(brix, tempC);
}

void DiFluidR1::emitTdsResult(double brix, double /*tempC*/) {
    if (brix > MAX_PLAUSIBLE_TDS || brix < -MAX_PLAUSIBLE_TDS) {
        R1_WARN(QString("Brix out of range: %1% — ignoring").arg(brix, 0, 'f', 2));
        emit errorOccurred("R1 reported an out-of-range value");
        m_measurementTimer.stop();
        m_measuring = false;
        emit measuringChanged();
        return;
    }

    m_tds = brix;
    emit tdsChanged(m_tds);
    emit measurementComplete();
    m_measurementTimer.stop();
    m_measuring = false;
    emit measuringChanged();
}

void DiFluidR1::sendCommand(const QByteArray& cmd) {
    if (!m_transport || !m_characteristicsReady) return;
    // Default WriteType::WithResponse — required by the R1 (it drops Write
    // Commands silently). Match the existing transport default rather than
    // re-specifying it so we don't accidentally diverge from R2's path.
    m_transport->writeCharacteristic(Refractometer::DiFluidR1::SERVICE,
                                     Refractometer::DiFluidR1::CMD, cmd);
}
