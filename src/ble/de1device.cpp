#include "de1device.h"
#include "protocol/binarycodec.h"
#include "profile/profile.h"
#include "../core/settings.h"

#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
#include "../simulator/de1simulator.h"
#endif
#include <QBluetoothAddress>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>

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
        qWarning() << "DE1Device: Failed to get Android context for address storage";
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
#endif

DE1Device::DE1Device(QObject* parent)
    : QObject(parent)
{
    m_commandTimer.setInterval(50);  // Process queue every 50ms
    m_commandTimer.setSingleShot(true);
    connect(&m_commandTimer, &QTimer::timeout, this, &DE1Device::processCommandQueue);

    // Write timeout timer - detect hung BLE writes (like de1app)
    m_writeTimeoutTimer.setSingleShot(true);
    m_writeTimeoutTimer.setInterval(WRITE_TIMEOUT_MS);
    connect(&m_writeTimeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_writePending) {
            qWarning() << "DE1Device: BLE write TIMEOUT after" << WRITE_TIMEOUT_MS << "ms"
                       << "- uuid:" << m_lastWriteUuid << "data:" << m_lastWriteData.toHex();
            m_writePending = false;
            if (m_lastCommand && m_writeRetryCount < MAX_WRITE_RETRIES) {
                m_writeRetryCount++;
                qWarning() << "DE1Device: Retrying after timeout ("
                           << m_writeRetryCount << "/" << MAX_WRITE_RETRIES << ")";
                QTimer::singleShot(100, this, [this]() {
                    if (m_lastCommand) {
                        m_lastCommand();
                    }
                });
            } else {
                qWarning() << "DE1Device: Write FAILED (timeout) after" << m_writeRetryCount
                           << "retries - uuid:" << m_lastWriteUuid << "data:" << m_lastWriteData.toHex();
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
            qDebug() << "DE1Device: Retry" << m_retryCount << "of" << MAX_RETRIES;
            // Clean up before retry
            if (m_controller) {
                m_controller->disconnectFromDevice();
                delete m_controller;
                m_controller = nullptr;
            }
            // Reconnect
            m_connecting = true;
            emit connectingChanged();
            m_controller = QLowEnergyController::createCentral(m_pendingDevice, this);
            // Use Qt::QueuedConnection for all BLE signals - fixes iOS CoreBluetooth threading
            // issues where callbacks arrive on CoreBluetooth thread and cause re-entrancy/crash
            auto qc = Qt::QueuedConnection;
            connect(m_controller, &QLowEnergyController::connected,
                    this, &DE1Device::onControllerConnected, qc);
            connect(m_controller, &QLowEnergyController::disconnected,
                    this, &DE1Device::onControllerDisconnected, qc);
            connect(m_controller, &QLowEnergyController::errorOccurred,
                    this, &DE1Device::onControllerError, qc);
            connect(m_controller, &QLowEnergyController::serviceDiscovered,
                    this, &DE1Device::onServiceDiscovered, qc);
            connect(m_controller, &QLowEnergyController::discoveryFinished,
                    this, &DE1Device::onServiceDiscoveryFinished, qc);
            m_controller->connectToDevice();
        }
    });
}

DE1Device::~DE1Device() {
    disconnect();
}

bool DE1Device::isConnected() const {
    // In simulation mode, we're "connected" to the simulated machine
    if (m_simulationMode) return true;

    // After service discovery, controller is in DiscoveredState, not ConnectedState
    return m_controller &&
           (m_controller->state() == QLowEnergyController::ConnectedState ||
            m_controller->state() == QLowEnergyController::DiscoveredState) &&
           m_service != nullptr;
}

bool DE1Device::isGuiEnabled() const {
    // GUI is enabled when connected OR in simulation/offline mode
    return isConnected() || m_simulationMode;
}

bool DE1Device::isConnecting() const {
    return m_connecting;
}

void DE1Device::setSimulationMode(bool enabled) {
    if (m_simulationMode == enabled) {
        return;
    }
    m_simulationMode = enabled;
    qDebug() << "DE1Device: Simulation mode" << (enabled ? "ENABLED" : "DISABLED");

    if (enabled) {
        // Set some default simulated state
        m_state = DE1::State::Idle;
        m_subState = DE1::SubState::Ready;
        m_pressure = 0.0;
        m_flow = 0.0;
        m_headTemp = 93.0;
        m_mixTemp = 92.5;
        m_waterLevel = 75.0;
        m_waterLevelMm = 31.25;  // ~75% = (31.25-5)/(40-5)*100
        m_waterLevelMl = 872;    // From lookup table at ~31mm
        m_firmwareVersion = "SIM-1.0";
        emit stateChanged();
        emit subStateChanged();
        emit waterLevelChanged();
        emit firmwareVersionChanged();
    }

    emit simulationModeChanged();
    emit connectedChanged();
    emit guiEnabledChanged();
}

void DE1Device::setSettings(Settings* settings) {
    m_settings = settings;
}

void DE1Device::setIsHeadless(bool headless) {
    if (m_isHeadless != headless) {
        m_isHeadless = headless;
        emit isHeadlessChanged();
    }
}

void DE1Device::setSimulatedState(DE1::State state, DE1::SubState subState) {
    if (!m_simulationMode) return;

    bool stateChanged = (m_state != state);
    bool subStateChanged = (m_subState != subState);

    m_state = state;
    m_subState = subState;

    if (stateChanged) {
        emit this->stateChanged();
    }
    if (subStateChanged) {
        emit this->subStateChanged();
    }
}

void DE1Device::emitSimulatedShotSample(const ShotSample& sample) {
    if (!m_simulationMode) return;

    // Update internal state from sample
    m_pressure = sample.groupPressure;
    m_flow = sample.groupFlow;
    m_headTemp = sample.headTemp;
    m_mixTemp = sample.mixTemp;
    m_steamTemp = sample.steamTemp;

    emit shotSampleReceived(sample);
}

void DE1Device::connectToDevice(const QString& address) {
    QBluetoothDeviceInfo info(QBluetoothAddress(address), QString(), 0);
    connectToDevice(info);
}

void DE1Device::connectToDevice(const QBluetoothDeviceInfo& device) {
    // Don't reconnect if already connected or connecting
    if (isConnected() || m_connecting) {
        return;
    }

    if (m_controller) {
        disconnect();
    }

    // Store device for potential retries and reset counter
    m_pendingDevice = device;
    m_retryCount = 0;
    m_retryTimer.stop();

    m_connecting = true;
    emit connectingChanged();

    m_controller = QLowEnergyController::createCentral(device, this);

    // Use Qt::QueuedConnection for all BLE signals - fixes iOS CoreBluetooth threading
    // issues where callbacks arrive on CoreBluetooth thread and cause re-entrancy/crash
    auto qc = Qt::QueuedConnection;
    connect(m_controller, &QLowEnergyController::connected,
            this, &DE1Device::onControllerConnected, qc);
    connect(m_controller, &QLowEnergyController::disconnected,
            this, &DE1Device::onControllerDisconnected, qc);
    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &DE1Device::onControllerError, qc);
    connect(m_controller, &QLowEnergyController::serviceDiscovered,
            this, &DE1Device::onServiceDiscovered, qc);
    connect(m_controller, &QLowEnergyController::discoveryFinished,
            this, &DE1Device::onServiceDiscoveryFinished, qc);

    m_controller->connectToDevice();
}

void DE1Device::disconnect() {
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

    if (m_controller) {
        m_controller->disconnectFromDevice();
        delete m_controller;
        m_controller = nullptr;
    }

#ifdef Q_OS_ANDROID
    // Clear address from shutdown service
    clearDE1AddressForShutdown();
#endif

    m_characteristics.clear();
    m_connecting = false;
    emit connectingChanged();
    emit connectedChanged();
    emit guiEnabledChanged();
}

void DE1Device::onControllerConnected() {
    m_controller->discoverServices();
}

void DE1Device::onControllerDisconnected() {
#ifdef Q_OS_ANDROID
    // Clear address from shutdown service
    clearDE1AddressForShutdown();
#endif

    m_connecting = false;
    emit connectingChanged();
    emit connectedChanged();
    emit guiEnabledChanged();
}

void DE1Device::onControllerError(QLowEnergyController::Error error) {
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
    qWarning() << "DE1Device: Controller error:" << errorMsg;
    emit errorOccurred(errorMsg);
    m_connecting = false;
    emit connectingChanged();
}

void DE1Device::onServiceDiscovered(const QBluetoothUuid& uuid) {
    if (uuid == DE1::SERVICE_UUID) {
        m_service = m_controller->createServiceObject(uuid, this);
        if (m_service) {
            // Use Qt::QueuedConnection for all service signals - fixes iOS CoreBluetooth
            // threading issues where callbacks arrive on CoreBluetooth thread
            auto qc = Qt::QueuedConnection;
            connect(m_service, &QLowEnergyService::stateChanged,
                    this, &DE1Device::onServiceStateChanged, qc);
            connect(m_service, &QLowEnergyService::characteristicChanged,
                    this, &DE1Device::onCharacteristicChanged, qc);
            connect(m_service, &QLowEnergyService::characteristicRead,
                    this, &DE1Device::onCharacteristicChanged, qc);  // Use same handler for reads
            connect(m_service, &QLowEnergyService::characteristicWritten,
                    this, &DE1Device::onCharacteristicWritten, qc);
            connect(m_service, &QLowEnergyService::errorOccurred,
                    this, [this](QLowEnergyService::ServiceError error) {
                // Log but don't fail on descriptor errors - common on Windows
                if (error != QLowEnergyService::DescriptorReadError &&
                    error != QLowEnergyService::DescriptorWriteError) {
                    qWarning() << "DE1Device: Service error:" << error
                               << "- uuid:" << m_lastWriteUuid << "data:" << m_lastWriteData.toHex();

                    // Handle write errors with retry (like de1app)
                    if (error == QLowEnergyService::CharacteristicWriteError && m_writePending) {
                        m_writePending = false;
                        m_writeTimeoutTimer.stop();  // Cancel timeout - we're handling the error
                        if (m_lastCommand && m_writeRetryCount < MAX_WRITE_RETRIES) {
                            m_writeRetryCount++;
                            qWarning() << "DE1Device: Write ERROR, retrying ("
                                       << m_writeRetryCount << "/" << MAX_WRITE_RETRIES << ")";
                            // Re-execute the last command after a short delay
                            QTimer::singleShot(100, this, [this]() {
                                if (m_lastCommand) {
                                    m_lastCommand();
                                }
                            });
                        } else {
                            qWarning() << "DE1Device: Write FAILED (error) after" << m_writeRetryCount
                                       << "retries - uuid:" << m_lastWriteUuid << "data:" << m_lastWriteData.toHex();
                            m_lastCommand = nullptr;
                            m_writeRetryCount = 0;
                            processCommandQueue();  // Move on to next command
                        }
                    } else {
                        emit errorOccurred(QString("Service error: %1").arg(error));
                    }
                }
            }, qc);
            m_service->discoverDetails();
        } else {
            qWarning() << "DE1Device: Failed to create service object";
        }
    }
}

void DE1Device::onServiceDiscoveryFinished() {
    if (!m_service) {
        // Retry logic - Android sometimes returns wrong/cached services
        m_retryCount++;
        if (m_retryCount <= MAX_RETRIES && m_pendingDevice.isValid()) {
            qWarning() << "DE1Device: Service not found, retry" << m_retryCount << "of" << MAX_RETRIES;
            if (m_controller) {
                m_controller->disconnectFromDevice();
            }
            m_retryTimer.start();
        } else {
            qWarning() << "DE1Device: Max retries exceeded";
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

void DE1Device::onServiceStateChanged(QLowEnergyService::ServiceState state) {
    if (state == QLowEnergyService::RemoteServiceDiscovered) {
        setupService();
        subscribeToNotifications();
        m_connecting = false;
        qDebug() << "DE1Device: Connected";

#ifdef Q_OS_ANDROID
        // Store address for shutdown service (handles swipe-to-kill)
        if (m_controller) {
            storeDE1AddressForShutdown(m_controller->remoteAddress().toString());
        }
#endif

        emit connectingChanged();
        emit connectedChanged();
        emit guiEnabledChanged();
    }
}

void DE1Device::setupService() {
    if (!m_service) return;

    // Cache all characteristics
    const QList<QLowEnergyCharacteristic> chars = m_service->characteristics();
    for (const auto& c : chars) {
        m_characteristics[c.uuid()] = c;
    }
}

void DE1Device::subscribeToNotifications() {
    if (!m_service) return;

    // Helper to subscribe to a characteristic's notifications
    auto subscribe = [this](const QBluetoothUuid& uuid) {
        if (m_characteristics.contains(uuid)) {
            QLowEnergyCharacteristic c = m_characteristics[uuid];
            QLowEnergyDescriptor notification = c.descriptor(
                QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
            if (notification.isValid()) {
                m_service->writeDescriptor(notification, QByteArray::fromHex("0100"));
            }
        }
    };

    // Subscribe to notifications
    subscribe(DE1::Characteristic::STATE_INFO);
    subscribe(DE1::Characteristic::SHOT_SAMPLE);
    subscribe(DE1::Characteristic::WATER_LEVELS);
    subscribe(DE1::Characteristic::READ_FROM_MMR);
    subscribe(DE1::Characteristic::TEMPERATURES);

    // Read initial values
    if (m_characteristics.contains(DE1::Characteristic::VERSION)) {
        m_service->readCharacteristic(m_characteristics[DE1::Characteristic::VERSION]);
    }
    if (m_characteristics.contains(DE1::Characteristic::STATE_INFO)) {
        m_service->readCharacteristic(m_characteristics[DE1::Characteristic::STATE_INFO]);
    }
    if (m_characteristics.contains(DE1::Characteristic::WATER_LEVELS)) {
        m_service->readCharacteristic(m_characteristics[DE1::Characteristic::WATER_LEVELS]);
    }

    // Send Idle state to wake the machine (this is what the tablet app does)
    requestState(DE1::State::Idle);  // Makes fan go quiet
}

void DE1Device::onCharacteristicChanged(const QLowEnergyCharacteristic& c, const QByteArray& value) {
    if (c.uuid() == DE1::Characteristic::STATE_INFO) {
        parseStateInfo(value);
    } else if (c.uuid() == DE1::Characteristic::SHOT_SAMPLE) {
        parseShotSample(value);
    } else if (c.uuid() == DE1::Characteristic::WATER_LEVELS) {
        parseWaterLevel(value);
    } else if (c.uuid() == DE1::Characteristic::VERSION) {
        parseVersion(value);
    } else if (c.uuid() == DE1::Characteristic::READ_FROM_MMR) {
        parseMMRResponse(value);
    }
}

void DE1Device::onCharacteristicWritten(const QLowEnergyCharacteristic& c, const QByteArray& value) {
    // Log all writes for debugging
    QString uuidShort = c.uuid().toString().mid(1, 8);  // Extract xxxx from {0000xxxx-...}
    qDebug() << "DE1Device: Write confirmed to" << uuidShort << "data:" << value.toHex();
    m_writePending = false;
    m_writeTimeoutTimer.stop();  // Cancel timeout - write succeeded
    m_writeRetryCount = 0;       // Reset retry count on successful write
    m_lastCommand = nullptr;     // Clear stored command
    m_lastWriteUuid.clear();
    m_lastWriteData.clear();
    processCommandQueue();
}

void DE1Device::parseStateInfo(const QByteArray& data) {
    if (data.size() < 2) return;

    DE1::State newState = static_cast<DE1::State>(static_cast<uint8_t>(data[0]));
    DE1::SubState newSubState = static_cast<DE1::SubState>(static_cast<uint8_t>(data[1]));

    bool stateChanged = (newState != m_state);
    bool subStateChanged = (newSubState != m_subState);

    // Only log when state actually changes
    if (stateChanged || subStateChanged) {
        qDebug() << "DE1Device: State changed to" << DE1::stateToString(newState)
                 << "/" << DE1::subStateToString(newSubState);
    }

    m_state = newState;
    m_subState = newSubState;

    if (stateChanged) {
        emit this->stateChanged();
    }
    if (subStateChanged) {
        emit this->subStateChanged();
    }
}

void DE1Device::parseShotSample(const QByteArray& data) {
    // DE1 has two BLE specs with different packet formats:
    // Old spec (< 1.0): 17 bytes, pressure/flow are 1 byte each (U8P4)
    // New spec (>= 1.0): 19 bytes, pressure/flow are 2 bytes each (U16P12), temp is 3 bytes

    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.constData());
    ShotSample sample;
    sample.timestamp = QDateTime::currentMSecsSinceEpoch();

    // Detect BLE spec based on packet size
    bool newSpec = (data.size() >= 19);

    if (newSpec) {
        // NEW BLE SPEC (>= 1.0): 19 bytes
        // Bytes 0-1: SampleTime (Short, big-endian, /100 for seconds - actually half-cycles)
        // Bytes 2-3: GroupPressure (Short, /4096.0)
        // Bytes 4-5: GroupFlow (Short, /4096.0)
        // Bytes 6-7: MixTemp (Short, /256.0)
        // Bytes 8-10: HeadTemp (3 bytes, U24P16)
        // Bytes 11-12: SetMixTemp (Short, /256.0)
        // Bytes 13-14: SetHeadTemp (Short, /256.0)
        // Byte 15: SetGroupPressure (char, /16.0)
        // Byte 16: SetGroupFlow (char, /16.0)
        // Byte 17: FrameNumber (char)
        // Byte 18: SteamTemp (char)

        sample.timer = BinaryCodec::decodeShortBE(data, 0) / 100.0;
        sample.groupPressure = BinaryCodec::decodeShortBE(data, 2) / 4096.0;
        sample.groupFlow = BinaryCodec::decodeShortBE(data, 4) / 4096.0;
        sample.mixTemp = BinaryCodec::decodeShortBE(data, 6) / 256.0;
        // HeadTemp is 24-bit: U24P16 format
        sample.headTemp = BinaryCodec::decode3CharToU24P16(d[8], d[9], d[10]);
        sample.setTempGoal = BinaryCodec::decodeShortBE(data, 13) / 256.0;  // SetHeadTemp
        sample.setPressureGoal = d[15] / 16.0;
        sample.setFlowGoal = d[16] / 16.0;
        sample.frameNumber = d[17];
        sample.steamTemp = d[18];
    } else if (data.size() >= 17) {
        // OLD BLE SPEC (< 1.0): 17 bytes
        // Bytes 0-1: SampleTime
        // Byte 2: GroupPressure (U8P4)
        // Byte 3: GroupFlow (U8P4)
        // Bytes 4-5: MixTemp (U16P8)
        // Bytes 6-7: HeadTemp (U16P8)
        // Bytes 8-9: SetMixTemp (U16P8)
        // Bytes 10-11: SetHeadTemp (U16P8)
        // Byte 12: SetGroupPressure (U8P4)
        // Byte 13: SetGroupFlow (U8P4)
        // Byte 14: FrameNumber
        // Bytes 15-16: SteamTemp (U16P8)

        sample.timer = BinaryCodec::decodeShortBE(data, 0) / 100.0;
        sample.groupPressure = d[2] / 16.0;
        sample.groupFlow = d[3] / 16.0;
        sample.mixTemp = BinaryCodec::decodeShortBE(data, 4) / 256.0;
        sample.headTemp = BinaryCodec::decodeShortBE(data, 6) / 256.0;
        sample.setTempGoal = BinaryCodec::decodeShortBE(data, 10) / 256.0;  // SetHeadTemp
        sample.setPressureGoal = d[12] / 16.0;
        sample.setFlowGoal = d[13] / 16.0;
        sample.frameNumber = d[14];
        sample.steamTemp = BinaryCodec::decodeShortBE(data, 15) / 256.0;
    } else {
        qDebug() << "DE1Device: ShotSample too short:" << data.size() << "bytes";
        return;
    }

    // Uncomment for debugging:
    // qDebug() << "DE1Device: ShotSample - headTemp:" << sample.headTemp << "pressure:" << sample.groupPressure;

    // Update internal state
    m_pressure = sample.groupPressure;
    m_flow = sample.groupFlow;
    m_mixTemp = sample.mixTemp;
    m_headTemp = sample.headTemp;
    m_steamTemp = sample.steamTemp;

    // Log steam temp periodically for debugging
    static int steamLogCounter = 0;
    if (++steamLogCounter % 20 == 0) {  // Every ~4 seconds (samples come at ~5Hz)
        QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/steam_debug.log";
        QFile file(logPath);
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&file);
            out << QDateTime::currentDateTime().toString("hh:mm:ss")
                << " STEAM_TEMP=" << m_steamTemp << "\n";
            file.close();
        }
    }

    emit shotSampleReceived(sample);
}

void DE1Device::parseWaterLevel(const QByteArray& data) {
    if (data.size() < 2) return;

    // Convert raw sensor reading to mm (U16P8 format: divide by 256)
    double rawMm = BinaryCodec::decodeU16P8(BinaryCodec::decodeShortBE(data, 0));

    // Apply sensor offset correction (sensor is mounted 5mm above water intake)
    // This matches de1app's water_level_mm_correction = 5
    constexpr double SENSOR_OFFSET = 5.0;
    m_waterLevelMm = rawMm + SENSOR_OFFSET;

    // Use fixed full point matching de1app (water_level_full_point = 40)
    // Refill point comes from user setting (default 5mm, range 3-70)
    constexpr double FULL_POINT = 40.0;
    double refillPoint = m_settings ? static_cast<double>(m_settings->waterRefillPoint()) : 5.0;

    // Calculate percentage: 0% at refill point, 100% at full point
    double range = FULL_POINT - refillPoint;
    if (range <= 0) range = 1.0;  // Safety: avoid division by zero
    m_waterLevel = qBound(0.0, ((m_waterLevelMm - refillPoint) / range) * 100.0, 100.0);

    // Lookup table from de1app CAD data (vars.tcl water_tank_level_to_milliliters)
    // Maps mm (0-65) to ml volume, accounting for non-linear tank geometry
    static const int mmToMl[] = {
        0, 16, 43, 70, 97, 124, 151, 179, 206, 233,      // 0-9mm
        261, 288, 316, 343, 371, 398, 426, 453, 481, 509, // 10-19mm
        537, 564, 592, 620, 648, 676, 704, 732, 760, 788, // 20-29mm
        816, 844, 872, 900, 929, 957, 985, 1013, 1042, 1070, // 30-39mm
        1104, 1138, 1172, 1207, 1242, 1277, 1312, 1347, 1382, 1417, // 40-49mm
        1453, 1488, 1523, 1559, 1594, 1630, 1665, 1701, 1736, 1772, // 50-59mm
        1808, 1843, 1879, 1915, 1951, 1986  // 60-65mm
    };
    constexpr int tableSize = sizeof(mmToMl) / sizeof(mmToMl[0]);

    int index = static_cast<int>(m_waterLevelMm);
    if (index < 0) {
        m_waterLevelMl = 0;
    } else if (index >= tableSize) {
        m_waterLevelMl = mmToMl[tableSize - 1];  // Max value
    } else {
        m_waterLevelMl = mmToMl[index];
    }

    emit waterLevelChanged();
}

void DE1Device::parseVersion(const QByteArray& data) {
    if (data.size() < 10) return;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.constData());

    int bleApi = d[0];
    double bleRelease = BinaryCodec::decodeF8_1_7(d[1]);
    int fwApi = d[5];
    double fwRelease = BinaryCodec::decodeF8_1_7(d[6]);

    m_firmwareVersion = QString("FW %1.%2, BLE %3.%4")
        .arg(fwApi).arg(fwRelease, 0, 'f', 1)
        .arg(bleApi).arg(bleRelease, 0, 'f', 1);
    emit firmwareVersionChanged();

    // Trigger full initialization after version is received (like de1app does)
    sendInitialSettings();
}

void DE1Device::requestGHCStatus() {
    // Request GHC_INFO via MMR read
    QByteArray mmrRead(20, 0);
    mmrRead[0] = 0x00;   // Len = 0 (read 4 bytes)
    mmrRead[1] = 0x80;   // Address high byte
    mmrRead[2] = 0x38;   // Address mid byte
    mmrRead[3] = 0x1C;   // Address low byte (GHC info)

    queueCommand([this, mmrRead]() {
        qDebug() << "DE1Device: Requesting GHC_INFO...";
        writeCharacteristic(DE1::Characteristic::READ_FROM_MMR, mmrRead);
    });
}

void DE1Device::parseMMRResponse(const QByteArray& data) {
    // MMR response format:
    // Byte 0: Length
    // Bytes 1-3: Address (big endian)
    // Bytes 4+: Data (little endian)
    if (data.size() < 5) return;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.constData());

    // Extract address (big endian)
    uint32_t address = (static_cast<uint32_t>(d[1]) << 16) |
                       (static_cast<uint32_t>(d[2]) << 8) |
                       static_cast<uint32_t>(d[3]);

    // Log raw MMR response
    qDebug() << "DE1Device: MMR response - address:" << QString("0x%1").arg(address, 6, 16, QChar('0'))
             << "raw data:" << data.toHex();

    // Check if this is GHC_INFO response (address 0x80381C)
    if (address == 0x80381C) {
        // Log raw GHC byte FIRST before any interpretation
        uint8_t ghcStatus = d[4];
        qDebug() << "DE1Device: GHC_INFO raw byte:" << ghcStatus << QString("(0x%1)").arg(ghcStatus, 2, 16, QChar('0'));

        // GHC_INFO bitmask from DE1:
        // 0 = not installed (headless) - app can start operations
        // 1 = GHC present but unused - app can start operations
        // 2 = GHC installed but inactive - app can start operations
        // 3 = GHC present and active - app CANNOT start, must use GHC buttons
        // 4 = debug mode - app can start operations
        // Other values = GHC required - app CANNOT start
        // See de1app's ghc_required() in vars.tcl for reference

        QString statusName;
        switch (ghcStatus) {
            case 0: statusName = "not installed"; break;
            case 1: statusName = "unused"; break;
            case 2: statusName = "inactive"; break;
            case 3: statusName = "active"; break;
            case 4: statusName = "debug"; break;
            default: statusName = QString("unknown (%1)").arg(ghcStatus); break;
        }

        bool canStartFromApp = (ghcStatus == 0 || ghcStatus == 1 || ghcStatus == 2 || ghcStatus == 4);
        QString logMsg = QString("GHC status: %1 → app %2 start operations")
            .arg(statusName)
            .arg(canStartFromApp ? "CAN" : "CANNOT");

        qDebug() << "DE1Device:" << logMsg;
        emit logMessage(logMsg);

        if (m_isHeadless != canStartFromApp) {
            m_isHeadless = canStartFromApp;
            qDebug() << "DE1Device: isHeadless changed to" << m_isHeadless;
            emit isHeadlessChanged();
        }
    }
}

void DE1Device::writeCharacteristic(const QBluetoothUuid& uuid, const QByteArray& data) {
    if (!m_service || !m_characteristics.contains(uuid)) {
        // Silently ignore in simulation mode
        if (!m_simulationMode) {
            qWarning() << "DE1Device: Cannot write - not connected or characteristic not found:" << uuid.toString();
        }
        return;
    }
    QString uuidShort = uuid.toString().mid(1, 8);  // Extract xxxx from {0000xxxx-...}
    qDebug() << "DE1Device: Writing to" << uuidShort << "data:" << data.toHex();
    m_writePending = true;
    m_lastWriteUuid = uuidShort;   // Store for error logging
    m_lastWriteData = data;        // Store for error logging
    m_writeTimeoutTimer.start();   // Start timeout timer for this write
    m_service->writeCharacteristic(m_characteristics[uuid], data);
}

void DE1Device::queueCommand(std::function<void()> command) {
    m_commandQueue.enqueue(command);
    if (!m_writePending && !m_commandTimer.isActive()) {
        m_commandTimer.start();
    }
}

void DE1Device::processCommandQueue() {
    if (m_writePending || m_commandQueue.isEmpty()) return;

    auto command = m_commandQueue.dequeue();
    m_lastCommand = command;  // Store for potential retry
    command();
}

// Machine control methods
void DE1Device::requestState(DE1::State state) {
    qDebug() << "DE1Device::requestState called with state:" << static_cast<int>(state);

#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
    // In simulation mode, relay to simulator
    if (m_simulationMode && m_simulator) {
        switch (state) {
        case DE1::State::Espresso:
            m_simulator->startEspresso();
            break;
        case DE1::State::Steam:
            m_simulator->startSteam();
            break;
        case DE1::State::HotWater:
            m_simulator->startHotWater();
            break;
        case DE1::State::HotWaterRinse:
            m_simulator->startFlush();
            break;
        case DE1::State::Idle:
            // If waking from sleep, use wakeUp; otherwise stop current operation
            if (m_simulator->state() == DE1::State::Sleep) {
                m_simulator->wakeUp();
            } else {
                m_simulator->stop();
            }
            break;
        case DE1::State::Sleep:
            m_simulator->goToSleep();
            break;
        default:
            qDebug() << "DE1Device: Simulation - unhandled state request:" << static_cast<int>(state);
            break;
        }
        return;
    }
#endif

    qDebug() << "DE1Device: Queueing state change command to" << static_cast<int>(state);
    QByteArray data(1, static_cast<char>(state));
    queueCommand([this, data]() {
        qDebug() << "DE1Device: Executing queued state change command";
        writeCharacteristic(DE1::Characteristic::REQUESTED_STATE, data);
    });
}

void DE1Device::startEspresso() {
    // Re-check GHC status right before starting
    qDebug() << "DE1Device::startEspresso() - current m_isHeadless:" << m_isHeadless << "m_state:" << static_cast<int>(m_state);

    // Set GHC_MODE to 1 (app controls) - this tells the machine we want to start from the app
    // Address 0x803820, value 1 = app controls
    qDebug() << "DE1Device: Setting GHC_MODE to 1 (app controls)";
    writeMMR(DE1::MMR::GHC_MODE, 1);

    // Like de1app: optionally go to Idle first to ensure machine is responsive
    if (m_state != DE1::State::Idle) {
        qDebug() << "DE1Device: Going to Idle before Espresso (current state:" << static_cast<int>(m_state) << ")";
        requestState(DE1::State::Idle);
    }
    requestState(DE1::State::Espresso);
}

void DE1Device::startSteam() {
    qDebug() << "DE1Device: Setting GHC_MODE to 1 (app controls)";
    writeMMR(DE1::MMR::GHC_MODE, 1);

    if (m_state != DE1::State::Idle) {
        qDebug() << "DE1Device: Going to Idle before Steam (current state:" << static_cast<int>(m_state) << ")";
        requestState(DE1::State::Idle);
    }
    requestState(DE1::State::Steam);
}

void DE1Device::startHotWater() {
    qDebug() << "DE1Device: Setting GHC_MODE to 1 (app controls)";
    writeMMR(DE1::MMR::GHC_MODE, 1);

    if (m_state != DE1::State::Idle) {
        qDebug() << "DE1Device: Going to Idle before HotWater (current state:" << static_cast<int>(m_state) << ")";
        requestState(DE1::State::Idle);
    }
    requestState(DE1::State::HotWater);
}

void DE1Device::startFlush() {
    qDebug() << "DE1Device: Setting GHC_MODE to 1 (app controls)";
    writeMMR(DE1::MMR::GHC_MODE, 1);

    if (m_state != DE1::State::Idle) {
        qDebug() << "DE1Device: Going to Idle before Flush (current state:" << static_cast<int>(m_state) << ")";
        requestState(DE1::State::Idle);
    }
    requestState(DE1::State::HotWaterRinse);
}

void DE1Device::startDescale() {
    requestState(DE1::State::Descale);
}

void DE1Device::startClean() {
    requestState(DE1::State::Clean);
}

void DE1Device::stopOperation() {
    qDebug() << "[REFACTOR] DE1Device::stopOperation() - requesting Idle state to stop current operation";
    requestState(DE1::State::Idle);
}

void DE1Device::requestIdle() {
    requestState(DE1::State::Idle);
}

void DE1Device::skipToNextFrame() {
    qDebug() << "[REFACTOR] DE1Device::skipToNextFrame() - sending SkipToNext (0x0E) command to machine";
    requestState(DE1::State::SkipToNext);
}

void DE1Device::goToSleep() {
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
    // In simulation mode, relay to simulator
    if (m_simulationMode && m_simulator) {
        m_simulator->goToSleep();
        return;
    }
#endif

    // Clear pending commands - sleep takes priority
    m_commandQueue.clear();
    m_writePending = false;

    // Send sleep command directly (don't queue it)
    QByteArray data(1, static_cast<char>(DE1::State::Sleep));
    writeCharacteristic(DE1::Characteristic::REQUESTED_STATE, data);
}

void DE1Device::wakeUp() {
    requestState(DE1::State::Idle);
}

void DE1Device::clearCommandQueue() {
    int cleared = m_commandQueue.size();
    m_commandQueue.clear();
    m_writePending = false;
    m_writeTimeoutTimer.stop();  // Cancel any pending timeout
    m_lastCommand = nullptr;     // Clear stored command
    m_writeRetryCount = 0;       // Reset retry count
    m_lastWriteUuid.clear();
    m_lastWriteData.clear();
    if (cleared > 0) {
        qDebug() << "DE1Device::clearCommandQueue: Cleared" << cleared << "pending commands";
    }
}

void DE1Device::uploadProfile(const Profile& profile) {
    qDebug() << "uploadProfile: Uploading profile with" << profile.steps().size() << "frames,"
             << "queue size before:" << m_commandQueue.size();
    for (int i = 0; i < profile.steps().size(); i++) {
        qDebug() << "  BLE Frame" << i << ": temp=" << profile.steps()[i].temperature;
    }

#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
    if (m_simulationMode && m_simulator) {
        m_simulator->setProfile(profile);
    }
#endif

    // Queue header write
    QByteArray header = profile.toHeaderBytes();
    queueCommand([this, header]() {
        writeCharacteristic(DE1::Characteristic::HEADER_WRITE, header);
    });

    // Queue each frame
    QList<QByteArray> frames = profile.toFrameBytes();
    for (const QByteArray& frame : frames) {
        queueCommand([this, frame]() {
            writeCharacteristic(DE1::Characteristic::FRAME_WRITE, frame);
        });
    }

    // Signal completion after queue processes
    queueCommand([this]() {
        emit profileUploaded(true);
    });
}

void DE1Device::uploadProfileAndStartEspresso(const Profile& profile) {
    qDebug() << "uploadProfileAndStartEspresso: Uploading profile with" << profile.steps().size() << "frames, then starting espresso";

#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
    if (m_simulationMode && m_simulator) {
        m_simulator->setProfile(profile);
    }
#endif

    // Queue header write
    QByteArray header = profile.toHeaderBytes();
    queueCommand([this, header]() {
        writeCharacteristic(DE1::Characteristic::HEADER_WRITE, header);
    });

    // Queue each frame
    QList<QByteArray> frames = profile.toFrameBytes();
    for (const QByteArray& frame : frames) {
        queueCommand([this, frame]() {
            writeCharacteristic(DE1::Characteristic::FRAME_WRITE, frame);
        });
    }

    // Queue espresso start AFTER all profile frames - this ensures correct order
    queueCommand([this]() {
        qDebug() << "uploadProfileAndStartEspresso: Profile uploaded, now starting espresso";
        writeCharacteristic(DE1::Characteristic::REQUESTED_STATE,
                           QByteArray(1, static_cast<char>(DE1::State::Espresso)));
    });

    // Signal completion after espresso starts
    queueCommand([this]() {
        emit profileUploaded(true);
    });
}

void DE1Device::writeHeader(const QByteArray& headerData) {
    // Direct header write for direct control mode
    queueCommand([this, headerData]() {
        writeCharacteristic(DE1::Characteristic::HEADER_WRITE, headerData);
    });
}

void DE1Device::writeFrame(const QByteArray& frameData) {
    // Direct frame write for direct control mode
    // This writes a single frame immediately, used for live setpoint updates
    queueCommand([this, frameData]() {
        writeCharacteristic(DE1::Characteristic::FRAME_WRITE, frameData);
    });
}

void DE1Device::writeMMR(uint32_t address, uint32_t value) {
    // MMR Write format (20 bytes):
    // Byte 0: Length (0x04 for 4-byte value)
    // Bytes 1-3: Address (big endian)
    // Bytes 4-7: Value (little endian)
    // Bytes 8-19: Padding (zeros)
    QByteArray data(20, 0);
    data[0] = 0x04;  // Length: 4 bytes
    data[1] = (address >> 16) & 0xFF;  // Address high byte
    data[2] = (address >> 8) & 0xFF;   // Address mid byte
    data[3] = address & 0xFF;          // Address low byte
    data[4] = value & 0xFF;            // Value byte 0 (little endian)
    data[5] = (value >> 8) & 0xFF;     // Value byte 1
    data[6] = (value >> 16) & 0xFF;    // Value byte 2
    data[7] = (value >> 24) & 0xFF;    // Value byte 3

    queueCommand([this, data]() {
        writeCharacteristic(DE1::Characteristic::WRITE_TO_MMR, data);
    });
}

void DE1Device::setUsbChargerOn(bool on, bool force) {
    // IMPORTANT: The DE1 has a 10-minute timeout that automatically turns the charger back ON.
    // We must resend the charger state periodically (every 60 seconds) to overcome this.
    // Use force=true to resend even if state hasn't changed.
    bool stateChanged = (m_usbChargerOn != on);

    if (!stateChanged && !force) {
        return;
    }

    if (stateChanged) {
        m_usbChargerOn = on;
    }

    writeMMR(DE1::MMR::USB_CHARGER, on ? 1 : 0);

    if (stateChanged) {
        emit usbChargerOnChanged();
    }
}

void DE1Device::setWaterRefillLevel(int refillPointMm) {
    // Write to WaterLevels characteristic (A011)
    // Format: Level (U16P8, 2 bytes) + StartFillLevel (U16P8, 2 bytes)
    // Level is set to 0 (read-only field, machine ignores it)
    // StartFillLevel is the refill threshold in mm
    QByteArray data;
    data.append(BinaryCodec::encodeShortBE(BinaryCodec::encodeU16P8(0)));
    data.append(BinaryCodec::encodeShortBE(BinaryCodec::encodeU16P8(static_cast<double>(refillPointMm))));

    qDebug() << "DE1Device: Setting water refill level to" << refillPointMm << "mm";
    queueCommand([this, data]() {
        writeCharacteristic(DE1::Characteristic::WATER_LEVELS, data);
    });
}

void DE1Device::sendInitialSettings() {
    // This mimics de1app's later_new_de1_connection_setup
    // Send a basic profile and shot settings to trigger machine wake-up response

    // Ensure USB charger is ON at startup (safe default like de1app)
    // This prevents the tablet from dying if it was left with charger off
    if (!m_usbChargerOn) {
        m_usbChargerOn = true;
        writeMMR(DE1::MMR::USB_CHARGER, 1);
        emit usbChargerOnChanged();
    }

    // CRITICAL: Set fan temperature threshold via MMR
    // This tells the machine at what temperature the fan should activate
    // Setting this allows the fan to go quiet when temps are stable
    // Default value: 60°C (de1app default from machine.tcl)
    writeMMR(DE1::MMR::FAN_THRESHOLD, 60);

    // Send a basic profile header (5 bytes)
    // HeaderV=1, NumFrames=1, NumPreinfuse=0, MinPressure=0, MaxFlow=6.0
    QByteArray header(5, 0);
    header[0] = 1;   // HeaderV - always 1
    header[1] = 1;   // NumberOfFrames
    header[2] = 0;   // NumberOfPreinfuseFrames  
    header[3] = 0;   // MinimumPressure (U8P4)
    header[4] = 96;  // MaximumFlow (U8P4) = 6.0 * 16

    queueCommand([this, header]() {
        writeCharacteristic(DE1::Characteristic::HEADER_WRITE, header);
    });

    // Send a basic profile frame (8 bytes)
    // Frame 0: 9 bar pressure, 93°C, 30 seconds
    QByteArray frame(8, 0);
    frame[0] = 0;    // FrameToWrite = 0
    frame[1] = 0;    // Flag = 0 (pressure control, no exit condition)
    frame[2] = 144;  // SetVal (U8P4) = 9.0 * 16 = 144 (9 bar)
    frame[3] = 186;  // Temp (U8P1) = 93.0 * 2 = 186 (93°C)
    frame[4] = 62;   // FrameLen (F8_1_7) ~30 seconds encoded
    frame[5] = 0;    // TriggerVal
    frame[6] = 0;    // MaxVol high byte
    frame[7] = 0;    // MaxVol low byte

    queueCommand([this, frame]() {
        writeCharacteristic(DE1::Characteristic::FRAME_WRITE, frame);
    });

    // Send tail frame (required to complete profile upload)
    // FrameToWrite = NumberOfFrames (1), MaxTotalVolume = 0
    QByteArray tailFrame(8, 0);
    tailFrame[0] = 1;    // FrameToWrite = NumberOfFrames
    // Bytes 1-7 are all 0 (no volume limit)

    queueCommand([this, tailFrame]() {
        writeCharacteristic(DE1::Characteristic::FRAME_WRITE, tailFrame);
    });

    // Read GHC (Group Head Controller) info via MMR
    // Write to ReadFromMMR to request a read; response comes as notification
    // Address 0x80381C, Length 0 (4 bytes)
    QByteArray mmrRead(20, 0);
    mmrRead[0] = 0x00;   // Len = 0 (read 4 bytes)
    mmrRead[1] = 0x80;   // Address high byte
    mmrRead[2] = 0x38;   // Address mid byte
    mmrRead[3] = 0x1C;   // Address low byte (GHC info)

    queueCommand([this, mmrRead]() {
        qDebug() << "DE1Device: Requesting GHC_INFO from machine...";
        writeCharacteristic(DE1::Characteristic::READ_FROM_MMR, mmrRead);
    });

    // Send shot settings
    // Default values similar to de1app defaults
    double steamTemp = 0.0;        // Default to off; applyAllSettings() sends user settings 1s later
    int steamDuration = 120;       // Steam timeout in seconds
    double hotWaterTemp = 80.0;    // Hot water temperature
    int hotWaterVolume = 200;      // Hot water volume in ml
    double groupTemp = 93.0;       // Group head temperature

    setShotSettings(steamTemp, steamDuration, hotWaterTemp, hotWaterVolume, groupTemp);

    // Signal that initial settings are complete (after queue processes)
    queueCommand([this]() {
        emit initialSettingsComplete();
    });
}

void DE1Device::setShotSettings(double steamTemp, int steamDuration,
                                double hotWaterTemp, int hotWaterVolume,
                                double groupTemp) {
    QByteArray data(9, 0);
    data[0] = 0;  // SteamSettings flags
    data[1] = BinaryCodec::encodeU8P0(steamTemp);
    data[2] = BinaryCodec::encodeU8P0(steamDuration);
    data[3] = BinaryCodec::encodeU8P0(hotWaterTemp);
    // Volume mode: caller sends actual ml so machine auto-stops via flowmeter
    // Weight mode: caller sends 0, app controls stop via scale instead
    data[4] = BinaryCodec::encodeU8P0(hotWaterVolume);
    data[5] = BinaryCodec::encodeU8P0(60);  // TargetHotWaterLength
    data[6] = BinaryCodec::encodeU8P0(36);  // TargetEspressoVol

    uint16_t groupTempEncoded = BinaryCodec::encodeU16P8(groupTemp);
    data[7] = (groupTempEncoded >> 8) & 0xFF;
    data[8] = groupTempEncoded & 0xFF;

    queueCommand([this, data]() {
        writeCharacteristic(DE1::Characteristic::SHOT_SETTINGS, data);
    });
}
