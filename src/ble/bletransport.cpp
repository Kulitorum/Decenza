#include "bletransport.h"
#include "protocol/de1characteristics.h"

#include <QBluetoothAddress>
#include <QLowEnergyConnectionParameters>
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
        qWarning() << "[BLE DE1] storeDE1AddressForShutdown: Android context is invalid";
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
        qWarning() << "[BLE DE1] clearDE1AddressForShutdown: Android context is invalid";
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
        qWarning() << "[BLE DE1] startBleConnectionService: Android context is invalid";
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
        qWarning() << "[BLE DE1] stopBleConnectionService: Android context is invalid";
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
                log(QString("Write timeout, retrying %1/%2 (uuid=%3)")
                    .arg(m_writeRetryCount).arg(MAX_WRITE_RETRIES).arg(m_lastWriteUuid));
                QTimer::singleShot(WRITE_RETRY_DELAY_MS, this, [this]() {
                    if (m_lastCommand) {
                        m_lastCommand();
                    }
                });
            } else {
                warn(QString("Write FAILED after %1 retries (uuid=%2, %3 bytes)")
                    .arg(MAX_WRITE_RETRIES).arg(m_lastWriteUuid).arg(m_lastWriteData.size()));
                emit errorOccurred(QString("BLE write failed after %1 retries").arg(MAX_WRITE_RETRIES));
                m_lastCommand = nullptr;
                m_writeRetryCount = 0;
                processCommandQueue();  // Move on to next command
                if (!m_writePending && m_commandQueue.isEmpty())
                    emit queueDrained();
            }
        }
    });

    // Retry timer for failed service discovery
    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(RETRY_DELAY_MS);
    connect(&m_retryTimer, &QTimer::timeout, this, [this]() {
        if (m_pendingDevice.isValid()) {
            log(QString("Service discovery retry %1/%2").arg(m_retryCount).arg(MAX_RETRIES));
            // Clean up before retry
            if (m_controller) {
                m_controller->disconnectFromDevice();
                delete m_controller;
                m_controller = nullptr;
            }
            if (!setupController(m_pendingDevice)) {
                warn("Retry abandoned - failed to create BLE controller");
                m_pendingDevice = QBluetoothDeviceInfo();
                return;
            }
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
    // Bypass the 50ms command queue for immediate write. Does NOT clear the queue —
    // callers that need to clear (SAW, sleep) do so explicitly before calling this.
    // This allows ensureChargerOn (app suspend) to write urgently without dropping
    // any pending extraction frames.
    //
    // If a write is already in-flight, prepend to the queue instead of calling
    // writeCharacteristic directly — writeCharacteristic is not re-entrant and would
    // corrupt m_writePending/m_lastWriteUuid/m_writeTimeoutTimer state.
    if (m_writePending) {
        m_commandQueue.prepend([this, uuid, data]() {
            writeCharacteristic(uuid, data);
        });
    } else {
        writeCharacteristic(uuid, data);
    }
}

void BleTransport::read(const QBluetoothUuid& uuid) {
    if (!m_service || !m_characteristics.contains(uuid)) {
        log(QString("read(%1) skipped - %2").arg(uuid.toString().mid(1, 8), !m_service ? "no service" : "unknown characteristic"));
        return;
    }
    m_service->readCharacteristic(m_characteristics[uuid]);
}

void BleTransport::subscribe(const QBluetoothUuid& uuid) {
    if (!m_service || !m_characteristics.contains(uuid)) {
        log(QString("subscribe(%1) skipped - %2").arg(uuid.toString().mid(1, 8), !m_service ? "no service" : "unknown characteristic"));
        return;
    }
    QLowEnergyCharacteristic c = m_characteristics[uuid];
    QLowEnergyDescriptor notification = c.descriptor(
        QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (notification.isValid()) {
        m_service->writeDescriptor(notification, QByteArray::fromHex("0100"));
    } else {
        warn(QString("subscribe(%1) FAILED - CCCD descriptor not found")
            .arg(uuid.toString().mid(1, 8)));
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
    m_characteristicsReady = false;

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
    qsizetype cleared = m_commandQueue.size();
    m_commandQueue.clear();
    m_writePending = false;
    m_writeTimeoutTimer.stop();
    m_lastCommand = nullptr;
    m_writeRetryCount = 0;
    m_lastWriteUuid.clear();
    m_lastWriteData.clear();
    Q_UNUSED(cleared);
}

bool BleTransport::isConnected() const {
    return m_controller &&
           (m_controller->state() == QLowEnergyController::ConnectedState ||
            m_controller->state() == QLowEnergyController::DiscoveredState) &&
           m_service != nullptr &&
           m_characteristicsReady;
}

// -- BLE-specific public API --

void BleTransport::connectToDevice(const QBluetoothDeviceInfo& device) {
    QString deviceId = device.address().isNull()
        ? device.deviceUuid().toString()
        : device.address().toString();

    if (isConnected()) {
        log(QString("connectToDevice(%1) skipped - already connected").arg(deviceId));
        return;
    }

    if (m_controller) {
        log("Cleaning up previous controller before new connection");
        disconnect();
    }

    // Store device for potential retries and reset counter
    m_pendingDevice = device;
    m_retryCount = 0;
    m_retryTimer.stop();

    log(QString("Connecting to DE1 at %1").arg(deviceId));

    if (!setupController(device)) {
        m_pendingDevice = QBluetoothDeviceInfo();
        return;
    }

    m_controller->connectToDevice();
}

// -- Private slots --

void BleTransport::onControllerConnected() {
    log("Controller connected, starting service discovery");

    // Request CONNECTION_PRIORITY_HIGH to reduce BLE connection interval from the
    // default ~30-50ms to 7.5-15ms, which reduces how long Android GC pauses delay
    // BLE notification delivery. See issue #342.
    // On Android, QLowEnergyConnectionParameters with minimumInterval < 30ms maps to
    // BluetoothGatt.CONNECTION_PRIORITY_HIGH. Default params have min=7.5ms.
    QLowEnergyConnectionParameters params;
    m_controller->requestConnectionUpdate(params);

    m_controller->discoverServices();
}

void BleTransport::onControllerDisconnected() {
    log("Controller disconnected");
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
    QString errorName;
    QString userMessage;
    switch (error) {
        case QLowEnergyController::UnknownError:
            errorName = "UnknownError"; userMessage = "Unknown error"; break;
        case QLowEnergyController::UnknownRemoteDeviceError:
            errorName = "UnknownRemoteDeviceError"; userMessage = "Remote device not found"; break;
        case QLowEnergyController::NetworkError:
            errorName = "NetworkError"; userMessage = "Network error"; break;
        case QLowEnergyController::InvalidBluetoothAdapterError:
            errorName = "InvalidBluetoothAdapterError"; userMessage = "Invalid Bluetooth adapter"; break;
        case QLowEnergyController::ConnectionError:
            errorName = "ConnectionError"; userMessage = "Connection error"; break;
        case QLowEnergyController::AdvertisingError:
            errorName = "AdvertisingError"; userMessage = "Advertising error"; break;
        case QLowEnergyController::RemoteHostClosedError:
            errorName = "RemoteHostClosedError"; userMessage = "Remote device closed connection"; break;
        case QLowEnergyController::AuthorizationError:
            errorName = "AuthorizationError"; userMessage = "Authorization error"; break;
        case QLowEnergyController::MissingPermissionsError:
            errorName = "MissingPermissionsError"; userMessage = "Missing Bluetooth permissions"; break;
        default:
            errorName = QString::number(static_cast<int>(error)); userMessage = "Connection error"; break;
    }
    warn(QString("!!! CONTROLLER ERROR: %1 !!!").arg(errorName));
    emit errorOccurred(userMessage);
}

void BleTransport::onServiceDiscovered(const QBluetoothUuid& uuid) {
    log(QString("Service discovered: %1%2").arg(uuid.toString(), uuid == DE1::SERVICE_UUID ? " (DE1)" : ""));
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
                            log(QString("CharacteristicWriteError, retrying %1/%2 (uuid=%3)")
                                .arg(m_writeRetryCount).arg(MAX_WRITE_RETRIES).arg(m_lastWriteUuid));
                            QTimer::singleShot(WRITE_RETRY_DELAY_MS, this, [this]() {
                                if (m_lastCommand) {
                                    m_lastCommand();
                                }
                            });
                        } else {
                            warn(QString("CharacteristicWriteError FAILED after %1 retries (uuid=%2)")
                                .arg(MAX_WRITE_RETRIES).arg(m_lastWriteUuid));
                            emit errorOccurred(QString("BLE write failed after %1 retries").arg(MAX_WRITE_RETRIES));
                            m_lastCommand = nullptr;
                            m_writeRetryCount = 0;
                            processCommandQueue();
                            if (!m_writePending && m_commandQueue.isEmpty())
                                emit queueDrained();
                        }
                    } else {
                        emit errorOccurred(QString("Service error: %1").arg(error));
                    }
                } else {
                    log(QString("Descriptor error (suppressed): %1").arg(static_cast<int>(error)));
                }
            }, qc);
            log("Starting characteristic discovery for DE1 service");
            m_service->discoverDetails();
        } else {
            warn("ERROR: createServiceObject() returned null for DE1 service UUID");
            emit errorOccurred("Failed to initialize DE1 service - try reconnecting");
        }
    }
}

void BleTransport::onServiceDiscoveryFinished() {
    if (!m_service) {
        // Retry logic - Android sometimes returns wrong/cached services
        m_retryCount++;
        if (m_retryCount <= MAX_RETRIES && m_pendingDevice.isValid()) {
            log(QString("DE1 service not found after discovery, scheduling retry %1/%2").arg(m_retryCount).arg(MAX_RETRIES));
            if (m_controller) {
                m_controller->disconnectFromDevice();
            }
            m_retryTimer.start();
        } else {
            log("DE1 service not found after all retries");
            emit errorOccurred("DE1 service not found after " + QString::number(MAX_RETRIES) + " retries. Try toggling Bluetooth off/on.");
            m_pendingDevice = QBluetoothDeviceInfo();
            disconnect();
        }
    } else {
        log("Service discovery complete - DE1 service found");
        // Success - clear pending device
        m_pendingDevice = QBluetoothDeviceInfo();
        m_retryCount = 0;
    }
}

void BleTransport::onServiceStateChanged(QLowEnergyService::ServiceState state) {
    if (state == QLowEnergyService::RemoteServiceDiscovered) {
        setupService();
        m_characteristicsReady = true;
        log(QString("Characteristics ready: %1 registered").arg(m_characteristics.size()));
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

    if (!m_writePending && m_commandQueue.isEmpty())
        emit queueDrained();
}

// -- Private helpers --

void BleTransport::log(const QString& message) {
    QString msg = QString("[BLE DE1] ") + message;
    qDebug().noquote() << msg;
    emit logMessage(msg);
}

void BleTransport::warn(const QString& message) {
    QString msg = QString("[BLE DE1] ") + message;
    qWarning().noquote() << msg;
    emit logMessage(msg);
}

bool BleTransport::setupController(const QBluetoothDeviceInfo& device) {
    m_controller = QLowEnergyController::createCentral(device, this);
    if (!m_controller) {
        warn("ERROR: Failed to create BLE controller!");
        emit errorOccurred("Failed to create BLE controller");
        return false;
    }

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
    // Log all controller state changes for debugging
    connect(m_controller, &QLowEnergyController::stateChanged, this, [this](QLowEnergyController::ControllerState state) {
        QString stateName;
        switch (state) {
            case QLowEnergyController::UnconnectedState: stateName = "Unconnected"; break;
            case QLowEnergyController::ConnectingState: stateName = "Connecting"; break;
            case QLowEnergyController::ConnectedState: stateName = "Connected"; break;
            case QLowEnergyController::DiscoveringState: stateName = "Discovering"; break;
            case QLowEnergyController::DiscoveredState: stateName = "Discovered"; break;
            case QLowEnergyController::ClosingState: stateName = "Closing"; break;
            default: stateName = QString::number(static_cast<int>(state)); break;
        }
        this->log(QString("Controller state: %1").arg(stateName));
    }, qc);

    return true;
}

void BleTransport::setupService() {
    if (!m_service) return;

    const QList<QLowEnergyCharacteristic> chars = m_service->characteristics();
    for (const auto& c : chars) {
        m_characteristics[c.uuid()] = c;
        log(QString("  Char %1 props=0x%2")
            .arg(c.uuid().toString().mid(1, 8))
            .arg(static_cast<int>(c.properties()), 2, 16, QChar('0')));
    }
}

void BleTransport::writeCharacteristic(const QBluetoothUuid& uuid, const QByteArray& data) {
    if (!m_service || !m_characteristics.contains(uuid)) {
        log(QString("writeCharacteristic(%1) skipped - %2").arg(uuid.toString().mid(1, 8), !m_service ? "no service" : "unknown characteristic"));
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
