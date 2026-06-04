#include "difluidr1.h"

#include "../protocol/de1characteristics.h"
#include "../transport/scalebletransport.h"
#include "aes128.h"

#include <QtEndian>
#include <array>
#include <cmath>
#include <utility>

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
        if (!m_transport || m_phase != Phase::CharacteristicsReady) return;

        // Enable notifications on 1E01 (measurement), 1E02 (battery),
        // 1E06/1E07 (status), and 1E08 (command ACK). Each call is logged
        // per-UUID so a dropped CCCD write is identifiable from the log.
        using namespace Refractometer::DiFluidR1;
        const std::array<std::pair<QBluetoothUuid, const char*>, 5> notifyChars = {{
            { DATA,    "1E01 (data)" },
            { BATTERY, "1E02 (battery)" },
            { STATUS,  "1E06 (status)" },
            { STATUS2, "1E07 (status2)" },
            { CMD,     "1E08 (cmd ack)" },
        }};
        for (const auto& [uuid, label] : notifyChars) {
            R1_LOG(QString("Enabling notify %1").arg(QString::fromLatin1(label)));
            m_transport->enableNotifications(SERVICE, uuid);
        }

        // Read the salt — characteristicRead() will fire on completion and
        // promote the link to Ready once the AES key is derived.
        R1_LOG("Reading salt (1E03)");
        m_transport->readCharacteristic(SERVICE, SALT);
        m_saltWatchdog.start();
    });

    // Salt-read watchdog: if 5s after characteristic discovery the device
    // hasn't returned the salt, tear the transport down so the UI returns
    // to a known "not connected" state the user can act on. Without this
    // the driver would sit at "Reading salt" forever with no signal.
    m_saltWatchdog.setSingleShot(true);
    m_saltWatchdog.setInterval(5000);
    connect(&m_saltWatchdog, &QTimer::timeout, this, [this]() {
        if (m_phase == Phase::Ready) return;
        R1_WARN("Salt read did not complete within 5s — tearing down transport. "
                "Check whether characteristic 1E03 is exposed and readable.");
        disconnectFromDevice();
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
        connect(m_transport, &ScaleBleTransport::characteristicDiscovered,
                this, [this](const QBluetoothUuid& service,
                             const QBluetoothUuid& ch, int properties) {
            if (service != Refractometer::DiFluidR1::SERVICE) return;
            R1_LOG(QString("  characteristic %1 props=0x%2")
                       .arg(ch.toString(), QString::number(properties, 16)));
        });
        connect(m_transport, &ScaleBleTransport::notificationsEnabled,
                this, [this](const QBluetoothUuid& ch) {
            R1_LOG(QString("Notifications enabled OK on %1").arg(ch.toString()));
        });
        connect(m_transport, &ScaleBleTransport::characteristicWritten,
                this, [this](const QBluetoothUuid& ch) {
            R1_LOG(QString("Write completed on %1").arg(ch.toString()));
        });
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
    return name.startsWith(QStringLiteral("DFT_TDJ"), Qt::CaseInsensitive);
}

std::array<uint8_t, 16> DiFluidR1::deriveKey(const QByteArray& salt) {
    std::array<uint8_t, 16> key{};
    for (int i = 0; i < 6; ++i) {
        const int s = (salt.size() > i) ? static_cast<uint8_t>(salt[i]) : 0;
        key[i] = static_cast<uint8_t>((s - (i >> 1)) & 0xFF);
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
    const qint32  tempRaw   = qFromBigEndian<qint32>(b + 0);
    const qint32  brixRaw   = qFromBigEndian<qint32>(b + 4);
    const quint32 riRaw     = qFromBigEndian<quint32>(b + 8);
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

    const QString newName = device.name();
    const bool nameChange = (newName != m_name);
    m_name = newName;
    m_phase = Phase::Disconnected;
    if (nameChange) emit nameChanged();

    R1_LOG(QString("Connecting to %1 (%2)")
               .arg(m_name,
                    device.address().isNull() ? device.deviceUuid().toString()
                                              : device.address().toString()));

    m_transport->connectToDevice(device);
}

void DiFluidR1::resetLinkState() {
    m_measurementTimer.stop();
    m_initTimer.stop();
    m_saltWatchdog.stop();
    const bool wasReady = (m_phase == Phase::Ready);
    const bool wasMeasuring = m_measuring;
    m_phase = Phase::Disconnected;
    m_measuring = false;
    if (wasReady) emit connectedChanged();
    if (wasMeasuring) emit measuringChanged();
}

void DiFluidR1::disconnectFromDevice() {
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
    resetLinkState();
}

void DiFluidR1::requestMeasurement() {
    if (m_phase != Phase::Ready) {
        R1_WARN("Cannot read — not connected");
        return;
    }

    m_measuring = true;
    emit measuringChanged();
    R1_LOG("Requesting measurement from R1 (doDetect)");

    // doDetect = 01 00. R1 requires ATT Write Request (WriteWithResponse);
    // the transport default already supplies that.
    QByteArray cmd;
    cmd.append(static_cast<char>(0x01));
    cmd.append(static_cast<char>(0x00));
    sendCommand(cmd);
    m_measurementTimer.start();
}

// === Transport callbacks ===

void DiFluidR1::onTransportConnected() {
    R1_LOG("Transport connected, starting service discovery");
    m_phase = Phase::ServiceDiscovery;
    m_transport->discoverServices();
}

void DiFluidR1::onTransportDisconnected() {
    R1_LOG("Transport disconnected");
    resetLinkState();
}

void DiFluidR1::onTransportError(const QString& message) {
    R1_WARN(QString("Transport error: %1").arg(message));
    resetLinkState();
}

void DiFluidR1::onServiceDiscovered(const QBluetoothUuid& uuid) {
    R1_LOG(QString("Service discovered: %1").arg(uuid.toString()));
}

void DiFluidR1::onServicesDiscoveryFinished() {
    using Refractometer::DiFluidR1::SERVICE;
    const bool found = m_transport && m_transport->isConnected();
    R1_LOG(QString("Service discovery finished (transport connected=%1)").arg(found));
    if (!found) return;
    m_transport->discoverCharacteristics(SERVICE);
}

void DiFluidR1::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    if (serviceUuid != Refractometer::DiFluidR1::SERVICE) return;
    if (m_phase >= Phase::CharacteristicsReady) {
        R1_LOG("Characteristics already set up, ignoring duplicate callback");
        return;
    }
    R1_LOG("Characteristics discovered, enabling notifications");
    m_phase = Phase::CharacteristicsReady;
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
        if (value.size() >= 4) {
            const int pct = static_cast<uint8_t>(value[3]);
            R1_LOG(QString("Battery: %1%").arg(pct));
        }
        return;
    }

    if (characteristicUuid == CMD) {
        // ACK channel — `01 FF` after doDetect.
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

    m_saltWatchdog.stop();

    if (value.size() < 6) {
        // Short-salt: warn and tear the transport down so the UI returns to a
        // known not-connected state the user can act on. Without the teardown
        // we'd stay stuck at "Reading salt…" with no recoverable signal.
        R1_WARN(QString("Salt read returned too few bytes (%1): %2 — cannot derive key, "
                        "tearing down transport")
                    .arg(value.size()).arg(QString(value.toHex(' '))));
        disconnectFromDevice();
        return;
    }

    m_key = deriveKey(value);
    // Salt is read from a public BLE characteristic; logging both salt and
    // derived key has no secret to protect and aids cross-checking when a
    // decrypt produces nonsense.
    const QByteArray keyBytes(reinterpret_cast<const char*>(m_key.data()), 16);
    R1_LOG(QString("Salt read (%1 bytes): %2")
               .arg(value.size()).arg(QString(value.toHex(' '))));
    R1_LOG(QString("Derived key: %1").arg(QString(keyBytes.toHex(' '))));

    // Optional model/hardware-version hints from the 12-byte payload. These
    // mappings are reverse-engineered guesses; mark as approximate in the log.
    if (value.size() >= 11) {
        const int hwHi = static_cast<uint8_t>(value[6]) - 3;
        const double hwLo = static_cast<uint8_t>(value[7]) - 3.5;
        const int model = (static_cast<uint8_t>(value[10]) - 5) % 6;
        const char* modelNames[] = { "Basic", "Air", "Pro", "Ultimate", "Coffee", "?" };
        R1_LOG(QString("Device hints (approximate): model≈%1 hwVersion≈%2.%3")
                   .arg(modelNames[model >= 0 && model < 5 ? model : 5])
                   .arg(hwHi).arg(hwLo, 0, 'f', 1));
    }

    if (m_phase != Phase::Ready) {
        m_phase = Phase::Ready;
        emit connectedChanged();
        R1_LOG("Connected and ready for measurements");
    }
}

// === Packet parsing ===

void DiFluidR1::handleMeasurementFrame(const QByteArray& ciphertext) {
    if (ciphertext.size() != 16) {
        R1_LOG(QString("Non-protocol DATA packet (%1 bytes): %2")
                   .arg(ciphertext.size())
                   .arg(QString(ciphertext.left(32).toHex(' '))));
        return;
    }
    if (m_phase != Phase::Ready) {
        R1_WARN(QString("Measurement frame arrived before key derivation — dropping. ct=%1")
                    .arg(QString(ciphertext.toHex(' '))));
        return;
    }

    const QByteArray pt = decryptFrame(ciphertext, m_key);
    double tempC = 0.0, brix = 0.0, ri = 0.0, raw = 0.0;
    if (!parsePlaintext(pt, tempC, brix, ri, raw)) {
        R1_WARN(QString("Decryption produced unparseable plaintext. ct=%1 pt=%2")
                    .arg(QString(ciphertext.toHex(' ')), QString(pt.toHex(' '))));
        return;
    }

    R1_LOG(QString("Frame ct=%1").arg(QString(ciphertext.toHex(' '))));
    R1_LOG(QString("Frame pt=%1").arg(QString(pt.toHex(' '))));
    R1_LOG(QString("Reading: T=%1°C  Brix=%2%  RI=%3  raw=%4")
               .arg(tempC, 0, 'f', 2)
               .arg(brix, 0, 'f', 2)
               .arg(ri, 0, 'f', 5)
               .arg(raw, 0, 'f', 2));

    // RI outside [1.30, 1.50] usually means a bad decrypt key.
    if (ri < 1.30 || ri > 1.50) {
        R1_WARN(QString("Refractive index %1 is outside the plausible band [1.30, 1.50].")
                    .arg(ri, 0, 'f', 5));
    }

    m_temperature = tempC;
    emit temperatureChanged(m_temperature);
    emitTdsResult(brix, tempC);
}

void DiFluidR1::emitTdsResult(double brix, double /*tempC*/) {
    if (brix > MAX_PLAUSIBLE_TDS || brix < -MAX_PLAUSIBLE_TDS) {
        R1_WARN(QString("Brix out of range: %1% — ignoring").arg(brix, 0, 'f', 2));
        emit errorOccurred("R1 reported an out-of-range value");
        m_measurementTimer.stop();
        if (m_measuring) {
            m_measuring = false;
            emit measuringChanged();
        }
        return;
    }

    m_tds = brix;
    emit tdsChanged(m_tds);
    emit measurementComplete();
    m_measurementTimer.stop();
    if (m_measuring) {
        m_measuring = false;
        emit measuringChanged();
    }
}

void DiFluidR1::sendCommand(const QByteArray& cmd) {
    if (!m_transport || m_phase < Phase::CharacteristicsReady) {
        R1_WARN(QString("sendCommand dropped (no transport or phase=%1): %2")
                    .arg(QString::number(static_cast<int>(m_phase)),
                         QString(cmd.toHex(' '))));
        return;
    }
    R1_LOG(QString("Write 1E08 (WithResponse): %1").arg(QString(cmd.toHex(' '))));
    m_transport->writeCharacteristic(Refractometer::DiFluidR1::SERVICE,
                                     Refractometer::DiFluidR1::CMD, cmd);
}
