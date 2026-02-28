#include "bletransport.h"
#include "protocol/de1characteristics.h"

#include <QBluetoothAddress>
#include <QDebug>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>

// Store DE1 address in Android SharedPreferences for shutdown service
static void storeDE1AddressForShutdown(const QString& address) {
    QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");

    if (!context.isValid()) {
        return;
    }

    QJniObject::callStaticMethod<void>(
        "io/github/kulitorum/decenza_de1/DeviceShutdownService",
        "setDe1Address",
        "(Landroid/content/Context;Ljava/lang/String;)V",
        context.object(),
        QJniObject::fromString(address).object<jstring>());
}

static void clearDE1AddressForShutdown() {
    QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");

    if (!context.isValid()) {
        return;
    }

    QJniObject::callStaticMethod<void>(
        "io/github/kulitorum/decenza_de1/DeviceShutdownService",
        "clearDe1Address",
        "(Landroid/content/Context;)V",
        context.object());
}

static void startBleConnectionService() {
    QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");

    if (!context.isValid()) {
        return;
    }

    QJniObject::callStaticMethod<void>(
        "io/github/kulitorum/decenza_de1/BleConnectionService",
        "start",
        "(Landroid/content/Context;)V",
        context.object());
}

static void stopBleConnectionService() {
    QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");

    if (!context.isValid()) {
        return;
    }

    QJniObject::callStaticMethod<void>(
        "io/github/kulitorum/decenza_de1/BleConnectionService",
        "stop",
        "(Landroid/content/Context;)V",
        context.object());
}
#endif

BleTransport::BleTransport(QObject* parent)
    : DE1Transport(parent)
{
    m_commandTimer.setInterval(50);  // Process queue every 50ms
    m_commandTimer.setSingleShot(true);
    connect(&m_commandTimer, &QTimer::timeout, this, &BleTransport::processCommandQueue);

    // Write timeout timer - detect hung BLE writes (like de1app)
    m_writeTimeoutTimer.setSingleShot(true);
    m_writeTimeoutTimer.setInterval(WRITE_TIMEOUT_MS);
    connect(&m_writeTimeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_writePending) {
            m_writePending = false;
            if (m_lastCommand && m_writeRetryCount < MAX_WRITE_RETRIES) {
                m_writeRetryCount++;
                QTimer::singleShot(100, this, [this]() {
                    if (m_lastCommand) {
                        m_lastCommand();
                    }
                });
            } else {
                m_lastCommand = nullptr;
                m_writeRetryCount = 0;
                processCommandQueue();  // Move on to next command
            }
        }
    });

    // Retry timer for failed service discovery
    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(RETRY_DELAY_MS);
    connect(&m_retryTimer, &QTimer::timeout, this, [this]() {
        if (m_pendingDevice.isValid()) {
            // Clean up before retry
            if (m_controller) {
                m_controller->disconnectFromDevice();
                delete m_controller;
                m_controller = nullptr;
            }
            m_controller = QLowEnergyController::createCentral(m_pendingDevice, this);
            // Use Qt::QueuedConnection for all BLE signals - fixes iOS CoreBluetooth threading
            auto qc = Qt::QueuedConnection;
            connect(m_controller, &QLowEnergyController::connected,
                    this, &BleTransport::onControllerConnected, qc);
            connect(m_controller, &QLowEnergyController::disconnected,
                    this, &BleTransport::onControllerDisconnected, qc);
            connect(m_controller, &QLowEnergyController::errorOccurred,
                    this, &BleTransport::onControllerError, qc);
            connect(m_controller, &QLowEnergyController::serviceDiscovered,
                    this, &BleTransport::onServiceDiscovered, qc);
            connect(m_controller, &QLowEnergyController::discoveryFinished,
                    this, &BleTransport::onServiceDiscoveryFinished, qc);
            m_controller->connectToDevice();
        }
    });
}

BleTransport::~BleTransport() {
    disconnect();
}

// -- DE1Transport interface implementation --

void BleTransport::write(const QBluetoothUuid& uuid, const QByteArray& data) {
    queueCommand([this, uuid, data]() {
        writeCharacteristic(uuid, data);
    });
}

void BleTransport::writeUrgent(const QBluetoothUuid& uuid, const QByteArray& data) {
    // Bypass the 50ms command queue for faster write (used by SAW stop-at-weight)
    clearQueue();
    writeCharacteristic(uuid, data);
}

void BleTransport::read(const QBluetoothUuid& uuid) {
    if (!m_service || !m_characteristics.contains(uuid)) {
        return;
    }
    m_service->readCharacteristic(m_characteristics[uuid]);
}

void BleTransport::subscribe(const QBluetoothUuid& uuid) {
    if (!m_service || !m_characteristics.contains(uuid)) {
        return;
    }
    QLowEnergyCharacteristic c = m_characteristics[uuid];
    QLowEnergyDescriptor notification = c.descriptor(
        QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (notification.isValid()) {
        m_service->writeDescriptor(notification, QByteArray::fromHex("0100"));
    }
}

void BleTransport::subscribeAll() {
    if (!m_service) return;

    subscribe(DE1::Characteristic::STATE_INFO);
    subscribe(DE1::Characteristic::SHOT_SAMPLE);
    subscribe(DE1::Characteristic::WATER_LEVELS);
    subscribe(DE1::Characteristic::READ_FROM_MMR);
    subscribe(DE1::Characteristic::TEMPERATURES);

    // Read initial values
    read(DE1::Characteristic::VERSION);
    read(DE1::Characteristic::STATE_INFO);
    read(DE1::Characteristic::WATER_LEVELS);
}

void BleTransport::disconnect() {
    m_commandQueue.clear();
    m_writePending = false;
    m_writeTimeoutTimer.stop();
    m_lastCommand = nullptr;
    m_writeRetryCount = 0;
    m_lastWriteUuid.clear();
    m_lastWriteData.clear();

    // Stop any pending retries
    m_retryTimer.stop();
    m_pendingDevice = QBluetoothDeviceInfo();
    m_retryCount = 0;

    if (m_service) {
        delete m_service;
        m_service = nullptr;
    }
    m_characteristics.clear();

    if (m_controller) {
        m_controller->disconnectFromDevice();
        delete m_controller;
        m_controller = nullptr;
    }

#ifdef Q_OS_ANDROID
    clearDE1AddressForShutdown();
    stopBleConnectionService();
#endif

    emit disconnected();
}

void BleTransport::clearQueue() {
    int cleared = m_commandQueue.size();
    m_commandQueue.clear();
    m_writePending = false;
    m_writeTimeoutTimer.stop();
    m_lastCommand = nullptr;
    m_writeRetryCount = 0;
    m_lastWriteUuid.clear();
    m_lastWriteData.clear();
    if (cleared > 0) {
        // qDebug() << "BleTransport::clearQueue: Cleared" << cleared << "pending commands";
    }
}

bool BleTransport::isConnected() const {
    return m_controller &&
           (m_controller->state() == QLowEnergyController::ConnectedState ||
            m_controller->state() == QLowEnergyController::DiscoveredState) &&
           m_service != nullptr;
}

// -- BLE-specific public API --

void BleTransport::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (isConnected()) {
        return;
    }

    if (m_controller) {
        disconnect();
    }

    // Store device for potential retries and reset counter
    m_pendingDevice = device;
    m_retryCount = 0;
    m_retryTimer.stop();

    m_controller = QLowEnergyController::createCentral(device, this);

    // Use Qt::QueuedConnection for all BLE signals - fixes iOS CoreBluetooth threading
    // issues where callbacks arrive on CoreBluetooth thread and cause re-entrancy/crash
    auto qc = Qt::QueuedConnection;
    connect(m_controller, &QLowEnergyController::connected,
            this, &BleTransport::onControllerConnected, qc);
    connect(m_controller, &QLowEnergyController::disconnected,
            this, &BleTransport::onControllerDisconnected, qc);
    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &BleTransport::onControllerError, qc);
    connect(m_controller, &QLowEnergyController::serviceDiscovered,
            this, &BleTransport::onServiceDiscovered, qc);
    connect(m_controller, &QLowEnergyController::discoveryFinished,
            this, &BleTransport::onServiceDiscoveryFinished, qc);

    m_controller->connectToDevice();
}

// -- Private slots --

void BleTransport::onControllerConnected() {
    m_controller->discoverServices();
}

void BleTransport::onControllerDisconnected() {
#ifdef Q_OS_ANDROID
    clearDE1AddressForShutdown();
    stopBleConnectionService();
#endif

    // Clear pending BLE operations to prevent writes against a dead connection,
    // which causes DeadObjectException crashes on Android (issue #189)
    m_commandQueue.clear();
    m_writePending = false;
    m_writeTimeoutTimer.stop();
    m_commandTimer.stop();

    emit disconnected();
}

void BleTransport::onControllerError(QLowEnergyController::Error error) {
    QString errorMsg;
    switch (error) {
        case QLowEnergyController::UnknownError:
            errorMsg = "Unknown error";
            break;
        case QLowEnergyController::UnknownRemoteDeviceError:
            errorMsg = "Remote device not found";
            break;
        case QLowEnergyController::NetworkError:
            errorMsg = "Network error";
            break;
        case QLowEnergyController::InvalidBluetoothAdapterError:
            errorMsg = "Invalid Bluetooth adapter";
            break;
        case QLowEnergyController::ConnectionError:
            errorMsg = "Connection error";
            break;
        case QLowEnergyController::AdvertisingError:
            errorMsg = "Advertising error";
            break;
        case QLowEnergyController::RemoteHostClosedError:
            errorMsg = "Remote host closed connection";
            break;
        case QLowEnergyController::AuthorizationError:
            errorMsg = "Authorization error";
            break;
        default:
            errorMsg = "Bluetooth error";
            break;
    }
    emit errorOccurred(errorMsg);
}

void BleTransport::onServiceDiscovered(const QBluetoothUuid& uuid) {
    if (uuid == DE1::SERVICE_UUID) {
        m_service = m_controller->createServiceObject(uuid, this);
        if (m_service) {
            // Use Qt::QueuedConnection for all service signals - fixes iOS CoreBluetooth
            // threading issues where callbacks arrive on CoreBluetooth thread
            auto qc = Qt::QueuedConnection;
            connect(m_service, &QLowEnergyService::stateChanged,
                    this, &BleTransport::onServiceStateChanged, qc);
            connect(m_service, &QLowEnergyService::characteristicChanged,
                    this, &BleTransport::onCharacteristicChanged, qc);
            connect(m_service, &QLowEnergyService::characteristicRead,
                    this, &BleTransport::onCharacteristicChanged, qc);  // Use same handler for reads
            connect(m_service, &QLowEnergyService::characteristicWritten,
                    this, &BleTransport::onCharacteristicWritten, qc);
            connect(m_service, &QLowEnergyService::errorOccurred,
                    this, [this](QLowEnergyService::ServiceError error) {
                // Log but don't fail on descriptor errors - common on Windows
                if (error != QLowEnergyService::DescriptorReadError &&
                    error != QLowEnergyService::DescriptorWriteError) {
                    // Handle write errors with retry (like de1app)
                    if (error == QLowEnergyService::CharacteristicWriteError && m_writePending) {
                        m_writePending = false;
                        m_writeTimeoutTimer.stop();
                        if (m_lastCommand && m_writeRetryCount < MAX_WRITE_RETRIES) {
                            m_writeRetryCount++;
                            QTimer::singleShot(100, this, [this]() {
                                if (m_lastCommand) {
                                    m_lastCommand();
                                }
                            });
                        } else {
                            m_lastCommand = nullptr;
                            m_writeRetryCount = 0;
                            processCommandQueue();
                        }
                    } else {
                        emit errorOccurred(QString("Service error: %1").arg(error));
                    }
                }
            }, qc);
            m_service->discoverDetails();
        }
    }
}

void BleTransport::onServiceDiscoveryFinished() {
    if (!m_service) {
        // Retry logic - Android sometimes returns wrong/cached services
        m_retryCount++;
        if (m_retryCount <= MAX_RETRIES && m_pendingDevice.isValid()) {
            if (m_controller) {
                m_controller->disconnectFromDevice();
            }
            m_retryTimer.start();
        } else {
            emit errorOccurred("DE1 service not found after " + QString::number(MAX_RETRIES) + " retries. Try toggling Bluetooth off/on.");
            m_pendingDevice = QBluetoothDeviceInfo();
            disconnect();
        }
    } else {
        // Success - clear pending device
        m_pendingDevice = QBluetoothDeviceInfo();
        m_retryCount = 0;
    }
}

void BleTransport::onServiceStateChanged(QLowEnergyService::ServiceState state) {
    if (state == QLowEnergyService::RemoteServiceDiscovered) {
        setupService();
        subscribeAll();

#ifdef Q_OS_ANDROID
        // Store address for shutdown service (handles swipe-to-kill)
        if (m_controller) {
            storeDE1AddressForShutdown(m_controller->remoteAddress().toString());
        }
        // Start foreground service to prevent Samsung/OEM app killing
        startBleConnectionService();
#endif

        emit connected();
    }
}

void BleTransport::onCharacteristicChanged(const QLowEnergyCharacteristic& c, const QByteArray& value) {
    emit dataReceived(c.uuid(), value);
}

void BleTransport::onCharacteristicWritten(const QLowEnergyCharacteristic& c, const QByteArray& value) {
    m_writePending = false;
    m_writeTimeoutTimer.stop();
    m_writeRetryCount = 0;
    m_lastCommand = nullptr;
    m_lastWriteUuid.clear();
    m_lastWriteData.clear();

    emit writeComplete(c.uuid(), value);
    processCommandQueue();
}

// -- Private helpers --

void BleTransport::setupService() {
    if (!m_service) return;

    const QList<QLowEnergyCharacteristic> chars = m_service->characteristics();
    for (const auto& c : chars) {
        m_characteristics[c.uuid()] = c;
    }
}

void BleTransport::writeCharacteristic(const QBluetoothUuid& uuid, const QByteArray& data) {
    if (!m_service || !m_characteristics.contains(uuid)) {
        return;
    }
    m_writePending = true;
    QString uuidShort = uuid.toString().mid(1, 8);
    m_lastWriteUuid = uuidShort;
    m_lastWriteData = data;
    m_writeTimeoutTimer.start();
    m_service->writeCharacteristic(m_characteristics[uuid], data);
}

void BleTransport::queueCommand(std::function<void()> command) {
    m_commandQueue.enqueue(command);
    if (!m_writePending && !m_commandTimer.isActive()) {
        m_commandTimer.start();
    }
}

void BleTransport::processCommandQueue() {
    if (m_writePending || m_commandQueue.isEmpty()) return;

    auto command = m_commandQueue.dequeue();
    m_lastCommand = command;  // Store for potential retry
    command();
}
