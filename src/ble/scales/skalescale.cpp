#include "skalescale.h"
#include "../protocol/de1characteristics.h"
#include <QDebug>

SkaleScale::SkaleScale(QObject* parent)
    : ScaleDevice(parent)
{
}

SkaleScale::~SkaleScale() {
    // Disconnect BLE before derived class is destroyed to prevent
    // callbacks to destroyed virtual methods
    disconnectFromScale();
}

void SkaleScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (m_controller) {
        disconnectFromScale();
    }

    m_name = device.name();
    m_controller = QLowEnergyController::createCentral(device, this);

    connect(m_controller, &QLowEnergyController::connected,
            this, &SkaleScale::onControllerConnected);
    connect(m_controller, &QLowEnergyController::disconnected,
            this, &SkaleScale::onControllerDisconnected);
    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &SkaleScale::onControllerError);
    connect(m_controller, &QLowEnergyController::serviceDiscovered,
            this, &SkaleScale::onServiceDiscovered);

    m_controller->connectToDevice();
}

void SkaleScale::onControllerConnected() {
    qDebug() << "SkaleScale: Controller connected, discovering services...";
    connect(m_controller, &QLowEnergyController::discoveryFinished,
            this, [this]() {
        qDebug() << "SkaleScale: Service discovery finished, service found:" << (m_service != nullptr);
        if (!m_service) {
            qWarning() << "SkaleScale: Skale service not found!";
            emit errorOccurred("Skale service not found");
        }
    });
    m_controller->discoverServices();
}

void SkaleScale::onControllerDisconnected() {
    setConnected(false);
}

void SkaleScale::onControllerError(QLowEnergyController::Error error) {
    Q_UNUSED(error)
    emit errorOccurred("Skale connection error");
    setConnected(false);
}

void SkaleScale::onServiceDiscovered(const QBluetoothUuid& uuid) {
    if (uuid == Scale::Skale::SERVICE) {
        m_service = m_controller->createServiceObject(uuid, this);
        if (m_service) {
            connect(m_service, &QLowEnergyService::stateChanged,
                    this, &SkaleScale::onServiceStateChanged);
            connect(m_service, &QLowEnergyService::characteristicChanged,
                    this, &SkaleScale::onCharacteristicChanged);
            m_service->discoverDetails();
        }
    }
}

void SkaleScale::onServiceStateChanged(QLowEnergyService::ServiceState state) {
    if (state == QLowEnergyService::RemoteServiceDiscovered) {
        m_cmdChar = m_service->characteristic(Scale::Skale::CMD);
        m_weightChar = m_service->characteristic(Scale::Skale::WEIGHT);
        m_buttonChar = m_service->characteristic(Scale::Skale::BUTTON);

        // Subscribe to weight notifications
        if (m_weightChar.isValid()) {
            QLowEnergyDescriptor notification = m_weightChar.descriptor(
                QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
            if (notification.isValid()) {
                m_service->writeDescriptor(notification, QByteArray::fromHex("0100"));
            }
        }

        // Subscribe to button notifications
        if (m_buttonChar.isValid()) {
            QLowEnergyDescriptor notification = m_buttonChar.descriptor(
                QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
            if (notification.isValid()) {
                m_service->writeDescriptor(notification, QByteArray::fromHex("0100"));
            }
        }

        setConnected(true);

        // Wake sequence with retries (matching official de1app pattern)
        // - Enable grams and LCD immediately
        // - Retry LCD enable at 200ms and 500ms for reliability
        enableGrams();
        enableLcd();
        QTimer::singleShot(200, this, &SkaleScale::enableLcd);
        QTimer::singleShot(500, this, &SkaleScale::enableLcd);
    }
}

void SkaleScale::onCharacteristicChanged(const QLowEnergyCharacteristic& c, const QByteArray& value) {
    if (c.uuid() == Scale::Skale::WEIGHT) {
        // Skale weight format: byte 0 = type, bytes 1-2 = unsigned short weight (10ths of gram)
        if (value.size() >= 3) {
            const uint8_t* d = reinterpret_cast<const uint8_t*>(value.constData());
            int16_t weightRaw = static_cast<int16_t>((d[2] << 8) | d[1]);
            double weight = weightRaw / 10.0;
            setWeight(weight);
        }
    } else if (c.uuid() == Scale::Skale::BUTTON) {
        // Button press notification
        if (value.size() >= 1) {
            emit buttonPressed(value[0]);
        }
    }
}

void SkaleScale::sendCommand(uint8_t cmd) {
    if (!m_service || !m_cmdChar.isValid()) return;

    QByteArray packet;
    packet.append(static_cast<char>(cmd));
    m_service->writeCharacteristic(m_cmdChar, packet);
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

void SkaleScale::disableLcd() {
    sendCommand(0xEE);
}

void SkaleScale::enableGrams() {
    sendCommand(0x03);
}
