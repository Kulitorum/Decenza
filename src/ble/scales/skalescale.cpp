#include "skalescale.h"
#include "../protocol/de1characteristics.h"
#include "../protocol/skalescaleprotocol.h"
#include "scalelogging.h"
#include <QDateTime>
#include <QTimer>

#define SKALE_LOG(msg)  SCALE_LOG("SkaleScale", msg)
#define SKALE_INFO(msg) SCALE_INFO("SkaleScale", msg)
#define SKALE_WARN(msg) SCALE_WARN("SkaleScale", msg)

SkaleScale::SkaleScale(ScaleBleTransport* transport, QObject* parent)
    : ScaleDevice(parent)
    , m_transport(transport)
{
    if (m_transport) {
        m_transport->setParent(this);

        connect(m_transport, &ScaleBleTransport::connected,
                this, &SkaleScale::onTransportConnected);
        connect(m_transport, &ScaleBleTransport::disconnected,
                this, &SkaleScale::onTransportDisconnected);
        connect(m_transport, &ScaleBleTransport::error,
                this, &SkaleScale::onTransportError);
        connect(m_transport, &ScaleBleTransport::serviceDiscovered,
                this, &SkaleScale::onServiceDiscovered);
        connect(m_transport, &ScaleBleTransport::servicesDiscoveryFinished,
                this, &SkaleScale::onServicesDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicsDiscoveryFinished,
                this, &SkaleScale::onCharacteristicsDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicChanged,
                this, &SkaleScale::onCharacteristicChanged);
        // Forward transport logs to scale log
        connect(m_transport, &ScaleBleTransport::logMessage,
                this, &ScaleDevice::logMessage);
    }
}

SkaleScale::~SkaleScale() {
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
}

void SkaleScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (!m_transport) {
        SKALE_WARN("connectToDevice called with no transport");
        return;
    }

    m_name = device.name();
    m_serviceFound = false;
    m_characteristicsReady = false;

    SKALE_LOG(QString("Connecting to %1 (%2)")
              .arg(device.name())
              .arg(device.address().toString()));

    m_transport->connectToDevice(device);
}

void SkaleScale::onTransportConnected() {
    SKALE_LOG(DECENZA_BLE_MSG_TRANSPORT_CONNECTED);
    m_transport->discoverServices();
}

void SkaleScale::onTransportDisconnected() {
    SKALE_INFO(DECENZA_BLE_MSG_TRANSPORT_DISCONNECTED);
    // Run end for the frame-shape collapse (core/logcollapse.h).
    for (const auto& [shape, collapsed] :
         m_frameShapeLog.flushAll(QDateTime::currentMSecsSinceEpoch())) {
        SKALE_LOG(shape + LogCollapse::suffix(collapsed));
    }
    setConnected(false);
}

void SkaleScale::onTransportError(const QString& message) {
    SKALE_WARN(QString("Transport error: %1").arg(message));
    setConnected(false);
}

void SkaleScale::onServiceDiscovered(const QBluetoothUuid& uuid) {
    SKALE_LOG(QString("Service discovered: %1").arg(uuid.toString()));
    if (uuid == Scale::Skale::SERVICE) {
        SKALE_LOG("Found Skale service");
        m_serviceFound = true;
    }
}

void SkaleScale::onServicesDiscoveryFinished() {
    SKALE_LOG(QString("Service discovery finished, service found: %1").arg(m_serviceFound));
    if (!m_serviceFound) {
        SKALE_WARN(QString("Skale service %1 not found!").arg(Scale::Skale::SERVICE.toString()));
        return;
    }
    m_transport->discoverCharacteristics(Scale::Skale::SERVICE);
}

void SkaleScale::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    if (serviceUuid != Scale::Skale::SERVICE) return;
    if (m_characteristicsReady) {
        SKALE_LOG(DECENZA_BLE_MSG_DUPLICATE_CHARACTERISTICS);
        return;
    }

    SKALE_LOG("Characteristics discovered");
    m_characteristicsReady = true;
    setConnected(true);

    // Follow de1app sequence exactly:
    // 1. Enable LCD immediately
    // 2. After 1000ms: Enable weight notifications
    // 3. After 2000ms: Enable button notifications
    // 4. After 3000ms: Enable LCD again

    SKALE_LOG("Starting de1app-style wake sequence");
    enableLcd();

    QTimer::singleShot(1000, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        SKALE_LOG("Enabling weight notifications (1000ms)");
        m_transport->enableNotifications(Scale::Skale::SERVICE, Scale::Skale::WEIGHT);
    });

    QTimer::singleShot(2000, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        SKALE_LOG("Enabling button notifications (2000ms)");
        m_transport->enableNotifications(Scale::Skale::SERVICE, Scale::Skale::BUTTON);
    });

    QTimer::singleShot(3000, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        SKALE_LOG("Enabling LCD again (3000ms)");
        enableLcd();
        enableGrams();
        SKALE_LOG("Wake sequence complete, waiting for weight data");
    });
}

void SkaleScale::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid,
                                         const QByteArray& value) {
    if (characteristicUuid == Scale::Skale::WEIGHT) {
        // Dispatch on length, then decode mantissa and exponent — see
        // SkaleScaleProtocol. The old parser accepted any frame of 3 bytes or
        // more and read two of them, so a truncated notification produced a
        // reading and a 5-byte frame's exponent was never consulted.
        if (const auto grams = SkaleScaleProtocol::decodeWeightGrams(value)) {
            setWeight(*grams);
        } else {
            logFrameShapeOnce(QString("Undecodable weight frame, %1 bytes").arg(value.size()),
                              value);
        }
    } else if (characteristicUuid == Scale::Skale::BUTTON) {
        // Button press notification
        if (value.size() >= 1) {
            emit buttonPressed(value[0]);
        }
    }
}

void SkaleScale::logFrameShapeOnce(const QString& shape, const QByteArray& data)
{
    const QString line = scaleFrameShapeLine(m_frameShapeLog, shape, data,
                                             QDateTime::currentMSecsSinceEpoch());
    if (!line.isEmpty())
        SKALE_LOG(line);
}

void SkaleScale::sendCommand(uint8_t cmd) {
    if (!m_transport || !m_characteristicsReady) {
        SKALE_LOG(QString("sendCommand(0x%1) - transport not ready, skipping")
                  .arg(cmd, 2, 16, QChar('0')));
        return;
    }

    SKALE_LOG(QString("sendCommand(0x%1)").arg(cmd, 2, 16, QChar('0')));
    QByteArray packet;
    packet.append(static_cast<char>(cmd));
    m_transport->writeCharacteristic(Scale::Skale::SERVICE, Scale::Skale::CMD, packet);
}

void SkaleScale::sendKeepAlive() {
    // No keep-alive needed — notifications stay active without periodic CCCD re-writes.
    // Re-writing the CCCD risks AuthorizationError disconnects.
}

void SkaleScale::tare() {
    sendCommand(0x10);
}

void SkaleScale::startTimer() {
    sendCommand(0xDD);
}

void SkaleScale::stopTimer() {
    sendCommand(0xD1);
}

void SkaleScale::resetTimer() {
    sendCommand(0xD0);
}

void SkaleScale::enableLcd() {
    sendCommand(0xED);  // Screen on
    sendCommand(0xEC);  // Display weight
}

void SkaleScale::sleep() {
    if (!m_transport || !m_characteristicsReady) {
        emit sleepCompleted();
        return;
    }
    connect(m_transport, &ScaleBleTransport::characteristicWritten,
            this, [this]() { emit sleepCompleted(); },
            Qt::SingleShotConnection);
    disableLcd();
}

void SkaleScale::disableLcd() {
    sendCommand(0xEE);
}

void SkaleScale::enableGrams() {
    sendCommand(0x03);
}
