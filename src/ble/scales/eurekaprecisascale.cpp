#include "eurekaprecisascale.h"
#include "../protocol/de1characteristics.h"
#include <QDebug>
#include <QTimer>

// Helper macro that logs to both qDebug and emits signal for UI/file logging
#define EUREKA_LOG(msg) do { \
    QString _msg = QString("[BLE EurekaPrecisaScale] ") + msg; \
    qDebug().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

EurekaPrecisaScale::EurekaPrecisaScale(ScaleBleTransport* transport, QObject* parent)
    : ScaleDevice(parent)
    , m_transport(transport)
{
    if (m_transport) {
        m_transport->setParent(this);

        connect(m_transport, &ScaleBleTransport::connected,
                this, &EurekaPrecisaScale::onTransportConnected);
        connect(m_transport, &ScaleBleTransport::disconnected,
                this, &EurekaPrecisaScale::onTransportDisconnected);
        connect(m_transport, &ScaleBleTransport::error,
                this, &EurekaPrecisaScale::onTransportError);
        connect(m_transport, &ScaleBleTransport::serviceDiscovered,
                this, &EurekaPrecisaScale::onServiceDiscovered);
        connect(m_transport, &ScaleBleTransport::servicesDiscoveryFinished,
                this, &EurekaPrecisaScale::onServicesDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicsDiscoveryFinished,
                this, &EurekaPrecisaScale::onCharacteristicsDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicChanged,
                this, &EurekaPrecisaScale::onCharacteristicChanged);
        // Forward transport logs to scale log
        connect(m_transport, &ScaleBleTransport::logMessage,
                this, &ScaleDevice::logMessage);
    }
}

EurekaPrecisaScale::~EurekaPrecisaScale() {
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
}

void EurekaPrecisaScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (!m_transport) {
        emit errorOccurred("No transport available");
        return;
    }

    m_name = device.name();
    m_serviceFound = false;
    m_characteristicsReady = false;

    EUREKA_LOG(QString("Connecting to %1 (%2)")
               .arg(device.name())
               .arg(device.address().toString()));

    m_transport->connectToDevice(device);
}

void EurekaPrecisaScale::onTransportConnected() {
    EUREKA_LOG("Transport connected, starting service discovery");
    m_transport->discoverServices();
}

void EurekaPrecisaScale::onTransportDisconnected() {
    EUREKA_LOG("Transport disconnected");
    setConnected(false);
}

void EurekaPrecisaScale::onTransportError(const QString& message) {
    EUREKA_LOG(QString("Transport error: %1").arg(message));
    emit errorOccurred("Eureka Precisa scale connection error");
    setConnected(false);
}

void EurekaPrecisaScale::onServiceDiscovered(const QBluetoothUuid& uuid) {
    EUREKA_LOG(QString("Service discovered: %1").arg(uuid.toString()));
    if (uuid == Scale::Generic::SERVICE) {
        EUREKA_LOG("Found Generic service (used by Eureka Precisa)");
        m_serviceFound = true;
    }
}

void EurekaPrecisaScale::onServicesDiscoveryFinished() {
    EUREKA_LOG(QString("Service discovery finished, service found: %1").arg(m_serviceFound));
    if (!m_serviceFound) {
        EUREKA_LOG(QString("Eureka Precisa service %1 not found!").arg(Scale::Generic::SERVICE.toString()));
        emit errorOccurred("Eureka Precisa service not found");
        return;
    }
    m_transport->discoverCharacteristics(Scale::Generic::SERVICE);
}

void EurekaPrecisaScale::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    if (serviceUuid != Scale::Generic::SERVICE) return;
    if (m_characteristicsReady) {
        EUREKA_LOG("Characteristics already set up, ignoring duplicate callback");
        return;
    }

    EUREKA_LOG("Characteristics discovered");
    m_characteristicsReady = true;
    setConnected(true);

    // de1app uses 200ms delay for Eureka Precisa
    EUREKA_LOG("Scheduling notification enable in 200ms (de1app timing)");
    QTimer::singleShot(200, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        EUREKA_LOG("Enabling notifications (200ms)");
        m_transport->enableNotifications(Scale::Generic::SERVICE, Scale::Generic::STATUS);

        // Set unit to grams
        EUREKA_LOG("Setting unit to grams");
        setUnitToGrams();
    });
}

void EurekaPrecisaScale::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid,
                                                  const QByteArray& value) {
    if (characteristicUuid == Scale::Generic::STATUS) {
        // Eureka Precisa format (from de1app binary scan "cucucu cu su cu su"):
        //   Bytes 0-2: header (0xAA, 0x09, 0x41)
        //   Byte 3: timer_running
        //   Bytes 4-5: timer (16-bit little-endian)
        //   Byte 6: sign (1 = negative)
        //   Bytes 7-8: weight (16-bit little-endian, tenths of gram)
        if (value.size() >= 9) {
            const uint8_t* d = reinterpret_cast<const uint8_t*>(value.constData());

            // Check header
            if (d[0] != 0xAA || d[1] != 0x09 || d[2] != 0x41) {
                return;
            }

            // Weight is in bytes 7-8 as little-endian unsigned short (tenths of gram)
            uint16_t weightRaw = d[7] | (d[8] << 8);
            double weight = weightRaw / 10.0;

            // Sign is in byte 6 (1 = negative)
            if (d[6] == 1) {
                weight = -weight;
            }

            setWeight(weight);
        }
    }
}

void EurekaPrecisaScale::sendCommand(const QByteArray& cmd) {
    if (!m_transport || !m_characteristicsReady) return;
    // Eureka Precisa's FFF2 characteristic only supports Write Without Response
    m_transport->writeCharacteristic(Scale::Generic::SERVICE, Scale::Generic::CMD, cmd,
                                     ScaleBleTransport::WriteType::WithoutResponse);
}

void EurekaPrecisaScale::tare() {
    sendCommand(QByteArray::fromHex("AA023131"));
}

void EurekaPrecisaScale::startTimer() {
    sendCommand(QByteArray::fromHex("AA023333"));
}

void EurekaPrecisaScale::stopTimer() {
    sendCommand(QByteArray::fromHex("AA023434"));
}

void EurekaPrecisaScale::resetTimer() {
    sendCommand(QByteArray::fromHex("AA023535"));
}

void EurekaPrecisaScale::setUnitToGrams() {
    sendCommand(QByteArray::fromHex("AA033600"));
}

void EurekaPrecisaScale::turnOff() {
    sendCommand(QByteArray::fromHex("AA023232"));
}

void EurekaPrecisaScale::beepTwice() {
    sendCommand(QByteArray::fromHex("AA023737"));
}
