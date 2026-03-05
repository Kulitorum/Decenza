#include "difluidscale.h"
#include "../protocol/de1characteristics.h"
#include "scalelogging.h"
#include <QTimer>

#define DIFLUID_LOG(msg)  SCALE_LOG("DifluidScale", msg)
#define DIFLUID_WARN(msg) SCALE_WARN("DifluidScale", msg)

DifluidScale::DifluidScale(ScaleBleTransport* transport, QObject* parent)
    : ScaleDevice(parent)
    , m_transport(transport)
{
    if (m_transport) {
        m_transport->setParent(this);

        connect(m_transport, &ScaleBleTransport::connected,
                this, &DifluidScale::onTransportConnected);
        connect(m_transport, &ScaleBleTransport::disconnected,
                this, &DifluidScale::onTransportDisconnected);
        connect(m_transport, &ScaleBleTransport::error,
                this, &DifluidScale::onTransportError);
        connect(m_transport, &ScaleBleTransport::serviceDiscovered,
                this, &DifluidScale::onServiceDiscovered);
        connect(m_transport, &ScaleBleTransport::servicesDiscoveryFinished,
                this, &DifluidScale::onServicesDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicsDiscoveryFinished,
                this, &DifluidScale::onCharacteristicsDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicChanged,
                this, &DifluidScale::onCharacteristicChanged);
        // Forward transport logs to scale log
        connect(m_transport, &ScaleBleTransport::logMessage,
                this, &ScaleDevice::logMessage);
    }
}

DifluidScale::~DifluidScale() {
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
}

void DifluidScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (!m_transport) {
        emit errorOccurred("No transport available");
        return;
    }

    m_name = device.name();
    m_serviceFound = false;
    m_characteristicsReady = false;

    DIFLUID_LOG(QString("Connecting to %1 (%2)")
                .arg(device.name())
                .arg(device.address().toString()));

    m_transport->connectToDevice(device);
}

void DifluidScale::onTransportConnected() {
    DIFLUID_LOG("Transport connected, starting service discovery");
    m_transport->discoverServices();
}

void DifluidScale::onTransportDisconnected() {
    DIFLUID_WARN("Transport disconnected");
    setConnected(false);
}

void DifluidScale::onTransportError(const QString& message) {
    DIFLUID_WARN(QString("Transport error: %1").arg(message));
    emit errorOccurred("Difluid scale connection error");
    setConnected(false);
}

void DifluidScale::onServiceDiscovered(const QBluetoothUuid& uuid) {
    DIFLUID_LOG(QString("Service discovered: %1").arg(uuid.toString()));
    if (uuid == Scale::DiFluid::SERVICE) {
        DIFLUID_LOG("Found DiFluid service");
        m_serviceFound = true;
    }
}

void DifluidScale::onServicesDiscoveryFinished() {
    DIFLUID_LOG(QString("Service discovery finished, service found: %1").arg(m_serviceFound));
    if (!m_serviceFound) {
        DIFLUID_WARN(QString("DiFluid service %1 not found!").arg(Scale::DiFluid::SERVICE.toString()));
        emit errorOccurred("Difluid service not found");
        return;
    }
    m_transport->discoverCharacteristics(Scale::DiFluid::SERVICE);
}

void DifluidScale::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    if (serviceUuid != Scale::DiFluid::SERVICE) return;
    if (m_characteristicsReady) {
        DIFLUID_LOG("Characteristics already set up, ignoring duplicate callback");
        return;
    }

    DIFLUID_LOG("Characteristics discovered");
    m_characteristicsReady = true;
    setConnected(true);

    // de1app uses 100ms delay for Difluid
    DIFLUID_LOG("Scheduling notification enable in 100ms (de1app timing)");
    QTimer::singleShot(100, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        DIFLUID_LOG("Enabling notifications (100ms)");
        m_transport->enableNotifications(Scale::DiFluid::SERVICE, Scale::DiFluid::CHARACTERISTIC);

        // Enable auto-notifications and set to grams
        DIFLUID_LOG("Sending enable notifications and set grams commands");
        enableNotifications();
        setToGrams();
    });
}

void DifluidScale::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid,
                                           const QByteArray& value) {
    if (characteristicUuid == Scale::DiFluid::CHARACTERISTIC) {
        // Difluid format: header bytes, then hex-encoded weight
        if (value.size() >= 19) {
            // Weight is in bytes 5-8 as hex-encoded integer
            QString hexStr = value.mid(5, 8).toHex();
            bool ok;
            uint32_t weightRaw = hexStr.toUInt(&ok, 16);

            if (ok && weightRaw < 20000) {
                double weight = weightRaw / 10.0;
                setWeight(weight);
            }
        }
    }
}

void DifluidScale::sendCommand(const QByteArray& cmd) {
    if (!m_transport || !m_characteristicsReady) return;
    m_transport->writeCharacteristic(Scale::DiFluid::SERVICE, Scale::DiFluid::CHARACTERISTIC, cmd);
}

void DifluidScale::sendKeepAlive() {
    if (m_transport && m_characteristicsReady)
        m_transport->enableNotifications(Scale::DiFluid::SERVICE, Scale::DiFluid::CHARACTERISTIC);
}

void DifluidScale::enableNotifications() {
    // Enable auto-notifications message
    sendCommand(QByteArray::fromHex("DFDF01000101C1"));
}

void DifluidScale::setToGrams() {
    // Set unit to grams
    sendCommand(QByteArray::fromHex("DFDF01040100C4"));
}

void DifluidScale::tare() {
    sendCommand(QByteArray::fromHex("DFDF03020101C5"));
}

void DifluidScale::startTimer() {
    sendCommand(QByteArray::fromHex("DFDF03020100C4"));
}

void DifluidScale::stopTimer() {
    sendCommand(QByteArray::fromHex("DFDF03010100C3"));
}

void DifluidScale::resetTimer() {
    sendCommand(QByteArray::fromHex("DFDF03020100C4"));
}
