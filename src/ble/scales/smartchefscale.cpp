#include "smartchefscale.h"
#include "../protocol/de1characteristics.h"
#include "scalelogging.h"
#include <QTimer>

#define SMARTCHEF_LOG(msg)  SCALE_LOG("SmartChefScale", msg)
#define SMARTCHEF_WARN(msg) SCALE_WARN("SmartChefScale", msg)

SmartChefScale::SmartChefScale(ScaleBleTransport* transport, QObject* parent)
    : ScaleDevice(parent)
    , m_transport(transport)
{
    if (m_transport) {
        m_transport->setParent(this);

        connect(m_transport, &ScaleBleTransport::connected,
                this, &SmartChefScale::onTransportConnected);
        connect(m_transport, &ScaleBleTransport::disconnected,
                this, &SmartChefScale::onTransportDisconnected);
        connect(m_transport, &ScaleBleTransport::error,
                this, &SmartChefScale::onTransportError);
        connect(m_transport, &ScaleBleTransport::serviceDiscovered,
                this, &SmartChefScale::onServiceDiscovered);
        connect(m_transport, &ScaleBleTransport::servicesDiscoveryFinished,
                this, &SmartChefScale::onServicesDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicsDiscoveryFinished,
                this, &SmartChefScale::onCharacteristicsDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicChanged,
                this, &SmartChefScale::onCharacteristicChanged);
        // Forward transport logs to scale log
        connect(m_transport, &ScaleBleTransport::logMessage,
                this, &ScaleDevice::logMessage);
    }
}

SmartChefScale::~SmartChefScale() {
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
}

void SmartChefScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (!m_transport) {
        emit errorOccurred("No transport available");
        return;
    }

    m_name = device.name();
    m_serviceFound = false;
    m_characteristicsReady = false;

    SMARTCHEF_LOG(QString("Connecting to %1 (%2)")
                  .arg(device.name())
                  .arg(device.address().toString()));

    m_transport->connectToDevice(device);
}

void SmartChefScale::onTransportConnected() {
    SMARTCHEF_LOG("Transport connected, starting service discovery");
    m_transport->discoverServices();
}

void SmartChefScale::onTransportDisconnected() {
    SMARTCHEF_WARN("Transport disconnected");
    setConnected(false);
}

void SmartChefScale::onTransportError(const QString& message) {
    SMARTCHEF_WARN(QString("Transport error: %1").arg(message));
    emit errorOccurred("SmartChef scale connection error");
    setConnected(false);
}

void SmartChefScale::onServiceDiscovered(const QBluetoothUuid& uuid) {
    SMARTCHEF_LOG(QString("Service discovered: %1").arg(uuid.toString()));
    if (uuid == Scale::Generic::SERVICE) {
        SMARTCHEF_LOG("Found Generic service (used by SmartChef)");
        m_serviceFound = true;
    }
}

void SmartChefScale::onServicesDiscoveryFinished() {
    SMARTCHEF_LOG(QString("Service discovery finished, service found: %1").arg(m_serviceFound));
    if (!m_serviceFound) {
        SMARTCHEF_WARN(QString("SmartChef service %1 not found!").arg(Scale::Generic::SERVICE.toString()));
        emit errorOccurred("SmartChef service not found");
        return;
    }
    m_transport->discoverCharacteristics(Scale::Generic::SERVICE);
}

void SmartChefScale::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    if (serviceUuid != Scale::Generic::SERVICE) return;
    if (m_characteristicsReady) {
        SMARTCHEF_LOG("Characteristics already set up, ignoring duplicate callback");
        return;
    }

    SMARTCHEF_LOG("Characteristics discovered");
    m_characteristicsReady = true;
    setConnected(true);

    // de1app uses 100ms delay for SmartChef
    SMARTCHEF_LOG("Scheduling notification enable in 100ms (de1app timing)");
    QTimer::singleShot(100, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        SMARTCHEF_LOG("Enabling notifications (100ms)");
        m_transport->enableNotifications(Scale::Generic::SERVICE, Scale::Generic::STATUS);
    });
}

void SmartChefScale::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid,
                                             const QByteArray& value) {
    if (characteristicUuid == Scale::Generic::STATUS) {
        // SmartChef format: weight in bytes 5-6 as unsigned short (tenths of gram)
        // Sign determined by byte 3
        if (value.size() >= 7) {
            const uint8_t* d = reinterpret_cast<const uint8_t*>(value.constData());

            int16_t weightRaw = static_cast<int16_t>((d[5] << 8) | d[6]);
            double weight = weightRaw / 10.0;

            // If byte 3 > 10, weight is negative
            if (d[3] > 10) {
                weight = -weight;
            }

            setWeight(weight);
        }
    }
}

void SmartChefScale::sendKeepAlive() {
    if (m_transport && m_characteristicsReady)
        m_transport->enableNotifications(Scale::Generic::SERVICE, Scale::Generic::STATUS);
}

void SmartChefScale::tare() {
    // SmartChef doesn't support software-based taring
    // User must press the tare button on the scale
    SMARTCHEF_LOG("Tare not supported - press button on scale");
}
