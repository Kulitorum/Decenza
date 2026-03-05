#include "felicitascale.h"
#include "../protocol/de1characteristics.h"
#include <QDebug>
#include <QTimer>

// Helper macro that logs to both qDebug and emits signal for UI/file logging
#define FELICITA_LOG(msg) do { \
    QString _msg = QString("[BLE FelicitaScale] ") + msg; \
    qDebug().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

#define FELICITA_WARN(msg) do { \
    QString _msg = QString("[BLE FelicitaScale] ") + msg; \
    qWarning().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

FelicitaScale::FelicitaScale(ScaleBleTransport* transport, QObject* parent)
    : ScaleDevice(parent)
    , m_transport(transport)
{
    if (m_transport) {
        m_transport->setParent(this);

        connect(m_transport, &ScaleBleTransport::connected,
                this, &FelicitaScale::onTransportConnected);
        connect(m_transport, &ScaleBleTransport::disconnected,
                this, &FelicitaScale::onTransportDisconnected);
        connect(m_transport, &ScaleBleTransport::error,
                this, &FelicitaScale::onTransportError);
        connect(m_transport, &ScaleBleTransport::serviceDiscovered,
                this, &FelicitaScale::onServiceDiscovered);
        connect(m_transport, &ScaleBleTransport::servicesDiscoveryFinished,
                this, &FelicitaScale::onServicesDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicsDiscoveryFinished,
                this, &FelicitaScale::onCharacteristicsDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicChanged,
                this, &FelicitaScale::onCharacteristicChanged);
        // Forward transport logs to scale log
        connect(m_transport, &ScaleBleTransport::logMessage,
                this, &ScaleDevice::logMessage);
    }
}

FelicitaScale::~FelicitaScale() {
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
}

void FelicitaScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (!m_transport) {
        emit errorOccurred("No transport available");
        return;
    }

    m_name = device.name();
    m_serviceFound = false;
    m_characteristicsReady = false;

    FELICITA_LOG(QString("Connecting to %1 (%2)")
                 .arg(device.name())
                 .arg(device.address().toString()));

    m_transport->connectToDevice(device);
}

void FelicitaScale::onTransportConnected() {
    FELICITA_LOG("Transport connected, starting service discovery");
    m_transport->discoverServices();
}

void FelicitaScale::onTransportDisconnected() {
    FELICITA_LOG("Transport disconnected");
    setConnected(false);
}

void FelicitaScale::onTransportError(const QString& message) {
    FELICITA_WARN(QString("Transport error: %1").arg(message));
    emit errorOccurred("Felicita scale connection error");
    setConnected(false);
}

void FelicitaScale::onServiceDiscovered(const QBluetoothUuid& uuid) {
    FELICITA_LOG(QString("Service discovered: %1").arg(uuid.toString()));
    if (uuid == Scale::Felicita::SERVICE) {
        FELICITA_LOG("Found Felicita service");
        m_serviceFound = true;
    }
}

void FelicitaScale::onServicesDiscoveryFinished() {
    FELICITA_LOG(QString("Service discovery finished, service found: %1").arg(m_serviceFound));
    if (!m_serviceFound) {
        FELICITA_WARN(QString("Felicita service %1 not found!").arg(Scale::Felicita::SERVICE.toString()));
        emit errorOccurred("Felicita service not found");
        return;
    }
    m_transport->discoverCharacteristics(Scale::Felicita::SERVICE);
}

void FelicitaScale::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    if (serviceUuid != Scale::Felicita::SERVICE) return;
    if (m_characteristicsReady) {
        FELICITA_LOG("Characteristics already set up, ignoring duplicate callback");
        return;
    }

    FELICITA_LOG("Characteristics discovered");
    m_characteristicsReady = true;
    setConnected(true);

    // de1app waits 2000ms before enabling Felicita notifications
    FELICITA_LOG("Scheduling notification enable in 2000ms (de1app timing)");
    QTimer::singleShot(2000, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        FELICITA_LOG("Enabling notifications (2000ms)");
        m_transport->enableNotifications(Scale::Felicita::SERVICE, Scale::Felicita::CHARACTERISTIC);
    });
}

void FelicitaScale::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid,
                                            const QByteArray& value) {
    if (characteristicUuid == Scale::Felicita::CHARACTERISTIC) {
        parseResponse(value);
    }
}

void FelicitaScale::parseResponse(const QByteArray& data) {
    // Felicita format: header1 header2 sign weight[6] ... battery
    if (data.size() < 9) return;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.constData());

    // Check headers
    if (d[0] != 0x01 || d[1] != 0x02) return;

    // Sign is at byte 2 ('+' or '-')
    char sign = static_cast<char>(d[2]);

    // Weight is 6 ASCII digits starting at byte 3
    QByteArray weightStr = data.mid(3, 6);
    bool ok;
    int weightInt = weightStr.toInt(&ok);
    if (!ok) return;

    double weight = weightInt / 100.0;  // Weight in grams with 2 decimal places
    if (sign == '-') {
        weight = -weight;
    }

    setWeight(weight);

    // Battery level is at byte 15 if available
    if (data.size() >= 16) {
        uint8_t battery = d[15];
        // Battery formula from de1app: ((battery - 129) / 29.0) * 100
        int battLevel = static_cast<int>(((battery - 129) / 29.0) * 100);
        battLevel = qBound(0, battLevel, 100);
        setBatteryLevel(battLevel);
    }
}

void FelicitaScale::sendCommand(uint8_t cmd) {
    if (!m_transport || !m_characteristicsReady) return;

    QByteArray packet;
    packet.append(static_cast<char>(cmd));
    m_transport->writeCharacteristic(Scale::Felicita::SERVICE, Scale::Felicita::CHARACTERISTIC, packet);
}

void FelicitaScale::sendKeepAlive() {
    if (m_transport && m_characteristicsReady)
        m_transport->enableNotifications(Scale::Felicita::SERVICE, Scale::Felicita::CHARACTERISTIC);
}

void FelicitaScale::tare() {
    sendCommand(0x54);  // 'T'
}

void FelicitaScale::startTimer() {
    sendCommand(0x52);  // 'R'
}

void FelicitaScale::stopTimer() {
    sendCommand(0x53);  // 'S'
}

void FelicitaScale::resetTimer() {
    sendCommand(0x43);  // 'C'
}
