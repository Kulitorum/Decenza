#include "qtscalebletransport.h"
#include <QDebug>

QtScaleBleTransport::QtScaleBleTransport(QObject* parent)
    : ScaleBleTransport(parent)
{
}

QtScaleBleTransport::~QtScaleBleTransport() {
    disconnectFromDevice();
}

void QtScaleBleTransport::connectToDevice(const QString& address, const QString& name) {
    if (m_controller) {
        disconnectFromDevice();
    }

    m_deviceAddress = address;
    m_deviceName = name;

    // Create device info from address
    QBluetoothDeviceInfo deviceInfo(QBluetoothAddress(address), name, 0);
    m_controller = QLowEnergyController::createCentral(deviceInfo, this);

    connect(m_controller, &QLowEnergyController::connected,
            this, &QtScaleBleTransport::onControllerConnected);
    connect(m_controller, &QLowEnergyController::disconnected,
            this, &QtScaleBleTransport::onControllerDisconnected);
    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &QtScaleBleTransport::onControllerError);
    connect(m_controller, &QLowEnergyController::serviceDiscovered,
            this, &QtScaleBleTransport::onServiceDiscovered);
    connect(m_controller, &QLowEnergyController::discoveryFinished,
            this, &QtScaleBleTransport::onServiceDiscoveryFinished);

    m_controller->connectToDevice();
}

void QtScaleBleTransport::disconnectFromDevice() {
    // Clean up services
    for (auto* service : m_services) {
        service->disconnect();
        service->deleteLater();
    }
    m_services.clear();

    if (m_controller) {
        m_controller->disconnect();
        if (m_controller->state() == QLowEnergyController::ConnectedState ||
            m_controller->state() == QLowEnergyController::DiscoveringState) {
            m_controller->disconnectFromDevice();
        }
        m_controller->deleteLater();
        m_controller = nullptr;
    }

    m_connected = false;
}

void QtScaleBleTransport::discoverServices() {
    if (m_controller && m_controller->state() == QLowEnergyController::ConnectedState) {
        m_controller->discoverServices();
    }
}

void QtScaleBleTransport::discoverCharacteristics(const QBluetoothUuid& serviceUuid) {
    QLowEnergyService* service = getOrCreateService(serviceUuid);
    if (service) {
        service->discoverDetails();
    }
}

void QtScaleBleTransport::enableNotifications(const QBluetoothUuid& serviceUuid,
                                              const QBluetoothUuid& characteristicUuid) {
    QLowEnergyService* service = m_services.value(serviceUuid);
    if (!service) {
        emit error("Service not found for enabling notifications");
        return;
    }

    QLowEnergyCharacteristic characteristic = service->characteristic(characteristicUuid);
    if (!characteristic.isValid()) {
        emit error("Characteristic not found for enabling notifications");
        return;
    }

    QLowEnergyDescriptor cccd = characteristic.descriptor(
        QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);

    if (cccd.isValid()) {
        service->writeDescriptor(cccd, QByteArray::fromHex("0100"));
    } else {
        emit error("CCCD descriptor not found");
    }
}

void QtScaleBleTransport::writeCharacteristic(const QBluetoothUuid& serviceUuid,
                                              const QBluetoothUuid& characteristicUuid,
                                              const QByteArray& data,
                                              WriteType writeType) {
    QLowEnergyService* service = m_services.value(serviceUuid);
    if (!service) {
        emit error("Service not found for write");
        return;
    }

    QLowEnergyCharacteristic characteristic = service->characteristic(characteristicUuid);
    if (!characteristic.isValid()) {
        emit error("Characteristic not found for write");
        return;
    }

    // Map our WriteType to Qt's WriteMode
    QLowEnergyService::WriteMode mode = (writeType == WriteType::WithoutResponse)
        ? QLowEnergyService::WriteWithoutResponse
        : QLowEnergyService::WriteWithResponse;

    service->writeCharacteristic(characteristic, data, mode);
}

void QtScaleBleTransport::readCharacteristic(const QBluetoothUuid& serviceUuid,
                                             const QBluetoothUuid& characteristicUuid) {
    QLowEnergyService* service = m_services.value(serviceUuid);
    if (!service) {
        emit error("Service not found for read");
        return;
    }

    QLowEnergyCharacteristic characteristic = service->characteristic(characteristicUuid);
    if (!characteristic.isValid()) {
        emit error("Characteristic not found for read");
        return;
    }

    service->readCharacteristic(characteristic);
}

bool QtScaleBleTransport::isConnected() const {
    return m_connected;
}

void QtScaleBleTransport::onControllerConnected() {
    m_connected = true;
    emit connected();
}

void QtScaleBleTransport::onControllerDisconnected() {
    m_connected = false;
    emit disconnected();
}

void QtScaleBleTransport::onControllerError(QLowEnergyController::Error err) {
    QString msg = QString("BLE Controller error: %1").arg(static_cast<int>(err));
    emit error(msg);
}

void QtScaleBleTransport::onServiceDiscovered(const QBluetoothUuid& uuid) {
    emit serviceDiscovered(uuid);
}

void QtScaleBleTransport::onServiceDiscoveryFinished() {
    emit servicesDiscoveryFinished();
}

void QtScaleBleTransport::onServiceStateChanged(QLowEnergyService::ServiceState state) {
    QLowEnergyService* service = qobject_cast<QLowEnergyService*>(sender());
    if (!service) return;

    if (state == QLowEnergyService::RemoteServiceDiscovered) {
        QBluetoothUuid serviceUuid = service->serviceUuid();

        // Emit discovered characteristics
        const QList<QLowEnergyCharacteristic> chars = service->characteristics();
        for (const QLowEnergyCharacteristic& c : chars) {
            emit characteristicDiscovered(serviceUuid, c.uuid(),
                                         static_cast<int>(c.properties()));
        }

        emit characteristicsDiscoveryFinished(serviceUuid);
    }
}

void QtScaleBleTransport::onCharacteristicChanged(const QLowEnergyCharacteristic& c,
                                                   const QByteArray& value) {
    emit characteristicChanged(c.uuid(), value);
}

void QtScaleBleTransport::onCharacteristicRead(const QLowEnergyCharacteristic& c,
                                                const QByteArray& value) {
    emit characteristicRead(c.uuid(), value);
}

void QtScaleBleTransport::onCharacteristicWritten(const QLowEnergyCharacteristic& c) {
    emit characteristicWritten(c.uuid());
}

void QtScaleBleTransport::onServiceError(QLowEnergyService::ServiceError err) {
    // Log but don't fail - some scales (like Bookoo) reject CCCD writes
    // but still work via setCharacteristicNotification
    qDebug() << "QtScaleBleTransport: Service error:" << err << "(may be expected)";
    emit error(QString("Service error: %1 (may be expected)").arg(static_cast<int>(err)));
}

QLowEnergyService* QtScaleBleTransport::getOrCreateService(const QBluetoothUuid& serviceUuid) {
    if (m_services.contains(serviceUuid)) {
        return m_services.value(serviceUuid);
    }

    if (!m_controller) {
        return nullptr;
    }

    QLowEnergyService* service = m_controller->createServiceObject(serviceUuid, this);
    if (service) {
        connectServiceSignals(service);
        m_services.insert(serviceUuid, service);
    }

    return service;
}

void QtScaleBleTransport::connectServiceSignals(QLowEnergyService* service) {
    connect(service, &QLowEnergyService::stateChanged,
            this, &QtScaleBleTransport::onServiceStateChanged);
    connect(service, &QLowEnergyService::characteristicChanged,
            this, &QtScaleBleTransport::onCharacteristicChanged);
    connect(service, &QLowEnergyService::characteristicRead,
            this, &QtScaleBleTransport::onCharacteristicRead);
    connect(service, &QLowEnergyService::characteristicWritten,
            this, &QtScaleBleTransport::onCharacteristicWritten);
    connect(service, &QLowEnergyService::errorOccurred,
            this, &QtScaleBleTransport::onServiceError);
}
