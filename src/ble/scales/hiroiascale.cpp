#include "hiroiascale.h"
#include "../protocol/de1characteristics.h"
#include <QDebug>
#include <QTimer>

// Helper macro that logs to both qDebug and emits signal for UI/file logging
#define HIROIA_LOG(msg) do { \
    QString _msg = QString("[BLE HiroiaScale] ") + msg; \
    qDebug().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

#define HIROIA_WARN(msg) do { \
    QString _msg = QString("[BLE HiroiaScale] ") + msg; \
    qWarning().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

HiroiaScale::HiroiaScale(ScaleBleTransport* transport, QObject* parent)
    : ScaleDevice(parent)
    , m_transport(transport)
{
    if (m_transport) {
        m_transport->setParent(this);

        connect(m_transport, &ScaleBleTransport::connected,
                this, &HiroiaScale::onTransportConnected);
        connect(m_transport, &ScaleBleTransport::disconnected,
                this, &HiroiaScale::onTransportDisconnected);
        connect(m_transport, &ScaleBleTransport::error,
                this, &HiroiaScale::onTransportError);
        connect(m_transport, &ScaleBleTransport::serviceDiscovered,
                this, &HiroiaScale::onServiceDiscovered);
        connect(m_transport, &ScaleBleTransport::servicesDiscoveryFinished,
                this, &HiroiaScale::onServicesDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicsDiscoveryFinished,
                this, &HiroiaScale::onCharacteristicsDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicChanged,
                this, &HiroiaScale::onCharacteristicChanged);
        // Forward transport logs to scale log
        connect(m_transport, &ScaleBleTransport::logMessage,
                this, &ScaleDevice::logMessage);
    }
}

HiroiaScale::~HiroiaScale() {
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
}

void HiroiaScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (!m_transport) {
        emit errorOccurred("No transport available");
        return;
    }

    m_name = device.name();
    m_serviceFound = false;
    m_characteristicsReady = false;

    HIROIA_LOG(QString("Connecting to %1 (%2)")
               .arg(device.name())
               .arg(device.address().toString()));

    m_transport->connectToDevice(device);
}

void HiroiaScale::onTransportConnected() {
    HIROIA_LOG("Transport connected, starting service discovery");
    m_transport->discoverServices();
}

void HiroiaScale::onTransportDisconnected() {
    HIROIA_LOG("Transport disconnected");
    setConnected(false);
}

void HiroiaScale::onTransportError(const QString& message) {
    HIROIA_WARN(QString("Transport error: %1").arg(message));
    emit errorOccurred("Hiroia Jimmy scale connection error");
    setConnected(false);
}

void HiroiaScale::onServiceDiscovered(const QBluetoothUuid& uuid) {
    HIROIA_LOG(QString("Service discovered: %1").arg(uuid.toString()));
    if (uuid == Scale::HiroiaJimmy::SERVICE) {
        HIROIA_LOG("Found Hiroia Jimmy service");
        m_serviceFound = true;
    }
}

void HiroiaScale::onServicesDiscoveryFinished() {
    HIROIA_LOG(QString("Service discovery finished, service found: %1").arg(m_serviceFound));
    if (!m_serviceFound) {
        HIROIA_WARN(QString("Hiroia Jimmy service %1 not found!").arg(Scale::HiroiaJimmy::SERVICE.toString()));
        emit errorOccurred("Hiroia Jimmy service not found");
        return;
    }
    m_transport->discoverCharacteristics(Scale::HiroiaJimmy::SERVICE);
}

void HiroiaScale::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    if (serviceUuid != Scale::HiroiaJimmy::SERVICE) return;
    if (m_characteristicsReady) {
        HIROIA_LOG("Characteristics already set up, ignoring duplicate callback");
        return;
    }

    HIROIA_LOG("Characteristics discovered");
    m_characteristicsReady = true;
    setConnected(true);

    // de1app uses 200ms delay before enabling Hiroia notifications
    HIROIA_LOG("Scheduling notification enable in 200ms (de1app timing)");
    QTimer::singleShot(200, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        HIROIA_LOG("Enabling notifications (200ms)");
        m_transport->enableNotifications(Scale::HiroiaJimmy::SERVICE, Scale::HiroiaJimmy::STATUS);
    });
}

void HiroiaScale::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid,
                                          const QByteArray& value) {
    if (characteristicUuid == Scale::HiroiaJimmy::STATUS) {
        // Hiroia format: 4 bytes header, then 4 bytes weight (unsigned, tenths of gram)
        if (value.size() >= 7) {
            // Append a zero byte to make it 8 bytes for easy parsing
            QByteArray padded = value;
            padded.append(static_cast<char>(0));

            const uint8_t* d = reinterpret_cast<const uint8_t*>(padded.constData());

            // Weight is in bytes 4-7 as unsigned 32-bit little-endian
            uint32_t weightRaw = d[4] | (d[5] << 8) | (d[6] << 16) | (d[7] << 24);

            // Handle negative values (if >= 8388608, it's negative)
            double weight;
            if (weightRaw >= 8388608) {
                weight = static_cast<double>(0xFFFFFF - weightRaw) * -1.0;
            } else {
                weight = static_cast<double>(weightRaw);
            }

            weight /= 10.0;  // Convert to grams
            setWeight(weight);
        }
    }
}

void HiroiaScale::sendKeepAlive() {
    if (m_transport && m_characteristicsReady)
        m_transport->enableNotifications(Scale::HiroiaJimmy::SERVICE, Scale::HiroiaJimmy::STATUS);
}

void HiroiaScale::tare() {
    if (!m_transport || !m_characteristicsReady) return;

    QByteArray packet = QByteArray::fromHex("0700");
    m_transport->writeCharacteristic(Scale::HiroiaJimmy::SERVICE, Scale::HiroiaJimmy::CMD, packet);
}
