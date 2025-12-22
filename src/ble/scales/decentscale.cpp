#include "decentscale.h"
#include "../protocol/de1characteristics.h"
#include <algorithm>

DecentScale::DecentScale(QObject* parent)
    : ScaleDevice(parent)
{
}

void DecentScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (m_controller) {
        disconnect();
    }

    m_name = device.name();
    m_controller = QLowEnergyController::createCentral(device, this);

    connect(m_controller, &QLowEnergyController::connected,
            this, &DecentScale::onControllerConnected);
    connect(m_controller, &QLowEnergyController::disconnected,
            this, &DecentScale::onControllerDisconnected);
    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &DecentScale::onControllerError);
    connect(m_controller, &QLowEnergyController::serviceDiscovered,
            this, &DecentScale::onServiceDiscovered);

    m_controller->connectToDevice();
}

void DecentScale::onControllerConnected() {
    m_controller->discoverServices();
}

void DecentScale::onControllerDisconnected() {
    setConnected(false);
}

void DecentScale::onControllerError(QLowEnergyController::Error error) {
    Q_UNUSED(error)
    emit errorOccurred("Scale connection error");
    setConnected(false);
}

void DecentScale::onServiceDiscovered(const QBluetoothUuid& uuid) {
    if (uuid == Scale::Decent::SERVICE) {
        m_service = m_controller->createServiceObject(uuid, this);
        if (m_service) {
            connect(m_service, &QLowEnergyService::stateChanged,
                    this, &DecentScale::onServiceStateChanged);
            connect(m_service, &QLowEnergyService::characteristicChanged,
                    this, &DecentScale::onCharacteristicChanged);
            m_service->discoverDetails();
        }
    }
}

void DecentScale::onServiceStateChanged(QLowEnergyService::ServiceState state) {
    if (state == QLowEnergyService::RemoteServiceDiscovered) {
        // Get characteristics
        m_readChar = m_service->characteristic(Scale::Decent::READ);
        m_writeChar = m_service->characteristic(Scale::Decent::WRITE);

        // Subscribe to notifications
        if (m_readChar.isValid()) {
            QLowEnergyDescriptor notification = m_readChar.descriptor(
                QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
            if (notification.isValid()) {
                m_service->writeDescriptor(notification, QByteArray::fromHex("0100"));
            }
        }

        setConnected(true);

        // Enable weight notifications
        sendCommand(QByteArray::fromHex("030A0102"));  // Enable weight updates
    }
}

void DecentScale::onCharacteristicChanged(const QLowEnergyCharacteristic& c, const QByteArray& value) {
    if (c.uuid() == Scale::Decent::READ) {
        parseWeightData(value);
    }
}

void DecentScale::parseWeightData(const QByteArray& data) {
    if (data.size() < 7) return;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.constData());

    uint8_t command = d[1];

    if (command == 0xCE || command == 0xCA) {
        // Weight data
        int16_t weightRaw = (static_cast<int16_t>(d[2]) << 8) | d[3];
        double weight = weightRaw / 10.0;  // Weight in grams
        setWeight(weight);
    } else if (command == 0xAA) {
        // Button pressed
        int button = d[2];
        emit buttonPressed(button);
    }
}

void DecentScale::sendCommand(const QByteArray& command) {
    if (!m_service || !m_writeChar.isValid()) return;

    QByteArray packet(7, 0);
    packet[0] = 0x03;  // Model byte

    for (int i = 0; i < std::min(command.size(), qsizetype(5)); i++) {
        packet[i + 1] = command[i];
    }

    packet[6] = calculateXor(packet);

    m_service->writeCharacteristic(m_writeChar, packet);
}

uint8_t DecentScale::calculateXor(const QByteArray& data) {
    uint8_t result = 0;
    for (int i = 0; i < data.size() - 1; i++) {
        result ^= static_cast<uint8_t>(data[i]);
    }
    return result;
}

void DecentScale::tare() {
    sendCommand(QByteArray::fromHex("0F0100"));
}

void DecentScale::startTimer() {
    sendCommand(QByteArray::fromHex("0B0300"));
}

void DecentScale::stopTimer() {
    sendCommand(QByteArray::fromHex("0B0000"));
}

void DecentScale::resetTimer() {
    sendCommand(QByteArray::fromHex("0B0200"));
}

void DecentScale::setLed(int r, int g, int b) {
    QByteArray cmd(5, 0);
    cmd[0] = 0x0A;
    cmd[1] = static_cast<char>(r);
    cmd[2] = static_cast<char>(g);
    cmd[3] = static_cast<char>(b);
    sendCommand(cmd);
}
