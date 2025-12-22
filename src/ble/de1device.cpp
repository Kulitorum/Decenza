#include "de1device.h"
#include "protocol/binarycodec.h"
#include "profile/profile.h"
#include <QBluetoothAddress>
#include <QDateTime>
#include <QDebug>

DE1Device::DE1Device(QObject* parent)
    : QObject(parent)
{
    m_commandTimer.setInterval(50);  // Process queue every 50ms
    m_commandTimer.setSingleShot(true);
    connect(&m_commandTimer, &QTimer::timeout, this, &DE1Device::processCommandQueue);
}

DE1Device::~DE1Device() {
    disconnect();
}

bool DE1Device::isConnected() const {
    // After service discovery, controller is in DiscoveredState, not ConnectedState
    return m_controller &&
           (m_controller->state() == QLowEnergyController::ConnectedState ||
            m_controller->state() == QLowEnergyController::DiscoveredState) &&
           m_service != nullptr;
}

bool DE1Device::isConnecting() const {
    return m_connecting;
}

void DE1Device::connectToDevice(const QString& address) {
    QBluetoothDeviceInfo info(QBluetoothAddress(address), QString(), 0);
    connectToDevice(info);
}

void DE1Device::connectToDevice(const QBluetoothDeviceInfo& device) {
    qDebug() << "DE1Device::connectToDevice" << device.name() << device.address();

    // Don't reconnect if already connected or connecting
    if (isConnected()) {
        qDebug() << "DE1Device: Already connected, ignoring";
        return;
    }
    if (m_connecting) {
        qDebug() << "DE1Device: Already connecting, ignoring";
        return;
    }

    if (m_controller) {
        disconnect();
    }

    m_connecting = true;
    emit connectingChanged();

    m_controller = QLowEnergyController::createCentral(device, this);

    connect(m_controller, &QLowEnergyController::connected,
            this, &DE1Device::onControllerConnected);
    connect(m_controller, &QLowEnergyController::disconnected,
            this, &DE1Device::onControllerDisconnected);
    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &DE1Device::onControllerError);
    connect(m_controller, &QLowEnergyController::serviceDiscovered,
            this, &DE1Device::onServiceDiscovered);
    connect(m_controller, &QLowEnergyController::discoveryFinished,
            this, &DE1Device::onServiceDiscoveryFinished);

    qDebug() << "DE1Device: Initiating connection...";
    m_controller->connectToDevice();
}

void DE1Device::disconnect() {
    m_commandQueue.clear();
    m_writePending = false;

    if (m_service) {
        delete m_service;
        m_service = nullptr;
    }

    if (m_controller) {
        m_controller->disconnectFromDevice();
        delete m_controller;
        m_controller = nullptr;
    }

    m_characteristics.clear();
    m_connecting = false;
    emit connectingChanged();
    emit connectedChanged();
}

void DE1Device::onControllerConnected() {
    qDebug() << "DE1Device: Controller connected, discovering services...";
    m_controller->discoverServices();
}

void DE1Device::onControllerDisconnected() {
    qDebug() << "DE1Device: Controller disconnected";
    m_connecting = false;
    emit connectingChanged();
    emit connectedChanged();
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
    qDebug() << "DE1Device: Controller error:" << errorMsg << "(code:" << error << ")";
    emit errorOccurred(errorMsg);
    m_connecting = false;
    emit connectingChanged();
}

void DE1Device::onServiceDiscovered(const QBluetoothUuid& uuid) {
    qDebug() << "DE1Device: Service discovered:" << uuid.toString();

    if (uuid == DE1::SERVICE_UUID) {
        qDebug() << "DE1Device: Found DE1 service, creating service object...";
        m_service = m_controller->createServiceObject(uuid, this);
        if (m_service) {
            connect(m_service, &QLowEnergyService::stateChanged,
                    this, &DE1Device::onServiceStateChanged);
            connect(m_service, &QLowEnergyService::characteristicChanged,
                    this, &DE1Device::onCharacteristicChanged);
            connect(m_service, &QLowEnergyService::characteristicRead,
                    this, &DE1Device::onCharacteristicChanged);  // Use same handler for reads
            connect(m_service, &QLowEnergyService::characteristicWritten,
                    this, &DE1Device::onCharacteristicWritten);
            connect(m_service, &QLowEnergyService::errorOccurred,
                    this, [this](QLowEnergyService::ServiceError error) {
                qDebug() << "DE1Device: Service error:" << error;
                // Log but don't fail on descriptor errors - common on Windows
                if (error == QLowEnergyService::DescriptorReadError ||
                    error == QLowEnergyService::DescriptorWriteError) {
                    qWarning() << "Descriptor error (often benign on Windows):" << error;
                } else {
                    emit errorOccurred(QString("Service error: %1").arg(error));
                }
            });
            qDebug() << "DE1Device: Discovering service details...";
            m_service->discoverDetails();
        } else {
            qDebug() << "DE1Device: Failed to create service object!";
        }
    }
}

void DE1Device::onServiceDiscoveryFinished() {
    qDebug() << "DE1Device: Service discovery finished, service found:" << (m_service != nullptr);
    if (!m_service) {
        qDebug() << "DE1Device: DE1 service NOT found!";
        emit errorOccurred("DE1 service not found");
        disconnect();
    }
}

void DE1Device::onServiceStateChanged(QLowEnergyService::ServiceState state) {
    qDebug() << "DE1Device: Service state changed:" << state;
    if (state == QLowEnergyService::RemoteServiceDiscovered) {
        qDebug() << "DE1Device: Service details discovered, setting up...";
        setupService();
        subscribeToNotifications();
        m_connecting = false;
        qDebug() << "DE1Device: Connection complete!";
        qDebug() << "  - m_controller:" << (m_controller != nullptr);
        qDebug() << "  - controller state:" << (m_controller ? m_controller->state() : -1);
        qDebug() << "  - m_service:" << (m_service != nullptr);
        qDebug() << "  - isConnected:" << isConnected();
        emit connectingChanged();
        emit connectedChanged();
    }
}

void DE1Device::setupService() {
    if (!m_service) return;

    // Cache all characteristics
    const QList<QLowEnergyCharacteristic> chars = m_service->characteristics();
    qDebug() << "DE1Device: Found" << chars.size() << "characteristics";
    for (const auto& c : chars) {
        qDebug() << "  -" << c.uuid().toString();
        m_characteristics[c.uuid()] = c;
    }
}

void DE1Device::subscribeToNotifications() {
    if (!m_service) return;

    qDebug() << "DE1Device: Subscribing to notifications...";

    // Subscribe to StateInfo notifications
    if (m_characteristics.contains(DE1::Characteristic::STATE_INFO)) {
        qDebug() << "DE1Device: Subscribing to StateInfo";
        QLowEnergyCharacteristic c = m_characteristics[DE1::Characteristic::STATE_INFO];
        QLowEnergyDescriptor notification = c.descriptor(
            QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
        if (notification.isValid()) {
            m_service->writeDescriptor(notification, QByteArray::fromHex("0100"));
        } else {
            qDebug() << "DE1Device: StateInfo CCCD not valid";
        }
    } else {
        qDebug() << "DE1Device: StateInfo characteristic NOT FOUND";
    }

    // Subscribe to ShotSample notifications
    if (m_characteristics.contains(DE1::Characteristic::SHOT_SAMPLE)) {
        qDebug() << "DE1Device: Subscribing to ShotSample";
        QLowEnergyCharacteristic c = m_characteristics[DE1::Characteristic::SHOT_SAMPLE];
        QLowEnergyDescriptor notification = c.descriptor(
            QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
        if (notification.isValid()) {
            m_service->writeDescriptor(notification, QByteArray::fromHex("0100"));
        } else {
            qDebug() << "DE1Device: ShotSample CCCD not valid";
        }
    } else {
        qDebug() << "DE1Device: ShotSample characteristic NOT FOUND";
    }

    // Subscribe to WaterLevels notifications
    if (m_characteristics.contains(DE1::Characteristic::WATER_LEVELS)) {
        qDebug() << "DE1Device: Subscribing to WaterLevels";
        QLowEnergyCharacteristic c = m_characteristics[DE1::Characteristic::WATER_LEVELS];
        QLowEnergyDescriptor notification = c.descriptor(
            QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
        if (notification.isValid()) {
            m_service->writeDescriptor(notification, QByteArray::fromHex("0100"));
        } else {
            qDebug() << "DE1Device: WaterLevels CCCD not valid";
        }
    } else {
        qDebug() << "DE1Device: WaterLevels characteristic NOT FOUND";
    }

    // Subscribe to ReadFromMMR notifications (required for GHC status, etc.)
    if (m_characteristics.contains(DE1::Characteristic::READ_FROM_MMR)) {
        qDebug() << "DE1Device: Subscribing to ReadFromMMR";
        QLowEnergyCharacteristic c = m_characteristics[DE1::Characteristic::READ_FROM_MMR];
        QLowEnergyDescriptor notification = c.descriptor(
            QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
        if (notification.isValid()) {
            m_service->writeDescriptor(notification, QByteArray::fromHex("0100"));
        } else {
            qDebug() << "DE1Device: ReadFromMMR CCCD not valid";
        }
    } else {
        qDebug() << "DE1Device: ReadFromMMR characteristic NOT FOUND";
    }

    // Read version
    // Read version
    if (m_characteristics.contains(DE1::Characteristic::VERSION)) {
        qDebug() << "DE1Device: Reading firmware version";
        m_service->readCharacteristic(m_characteristics[DE1::Characteristic::VERSION]);
    } else {
        qDebug() << "DE1Device: Version characteristic NOT FOUND";
    }

    // Read initial state - machine expects this to complete connection handshake
    if (m_characteristics.contains(DE1::Characteristic::STATE_INFO)) {
        qDebug() << "DE1Device: Reading initial state";
        m_service->readCharacteristic(m_characteristics[DE1::Characteristic::STATE_INFO]);
    }

    // Read water level
    if (m_characteristics.contains(DE1::Characteristic::WATER_LEVELS)) {
        qDebug() << "DE1Device: Reading water level";
        m_service->readCharacteristic(m_characteristics[DE1::Characteristic::WATER_LEVELS]);
    }

    // Send Idle state to wake the machine (this is what the tablet app does)
    qDebug() << "DE1Device: Sending Idle to wake machine";
    requestState(DE1::State::Idle);  // Makes fan go quiet
}

void DE1Device::onCharacteristicChanged(const QLowEnergyCharacteristic& c, const QByteArray& value) {
    // Uncomment for debugging:
    // qDebug() << "DE1Device: Characteristic changed:" << c.uuid().toString() << "size:" << value.size();

    if (c.uuid() == DE1::Characteristic::STATE_INFO) {
        parseStateInfo(value);
    } else if (c.uuid() == DE1::Characteristic::SHOT_SAMPLE) {
        parseShotSample(value);
    } else if (c.uuid() == DE1::Characteristic::WATER_LEVELS) {
        parseWaterLevel(value);
    } else if (c.uuid() == DE1::Characteristic::VERSION) {
        parseVersion(value);
    }
}

void DE1Device::onCharacteristicWritten(const QLowEnergyCharacteristic& c, const QByteArray& value) {
    Q_UNUSED(c)
    Q_UNUSED(value)
    m_writePending = false;
    processCommandQueue();
}

void DE1Device::parseStateInfo(const QByteArray& data) {
    if (data.size() < 2) return;

    DE1::State newState = static_cast<DE1::State>(static_cast<uint8_t>(data[0]));
    DE1::SubState newSubState = static_cast<DE1::SubState>(static_cast<uint8_t>(data[1]));

    bool stateChanged = (newState != m_state);
    bool subStateChanged = (newSubState != m_subState);

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
    if (data.size() < 12) {
        qDebug() << "DE1Device: ShotSample too short:" << data.size();
        return;
    }

    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.constData());

    ShotSample sample;
    sample.timestamp = QDateTime::currentMSecsSinceEpoch();

    // Timer: 2 bytes big-endian
    sample.timer = BinaryCodec::decodeShortBE(data, 0) / 100.0;

    // Group pressure and flow
    sample.groupPressure = BinaryCodec::decodeU8P4(d[2]);
    sample.groupFlow = BinaryCodec::decodeU8P4(d[3]);

    // Temperatures - both are U16P8 (2 bytes each)
    sample.mixTemp = BinaryCodec::decodeU16P8(BinaryCodec::decodeShortBE(data, 4));
    sample.headTemp = BinaryCodec::decodeU16P8(BinaryCodec::decodeShortBE(data, 6));

    // Goals (SetMixTemp, SetHeadTemp as U8P1, then pressure/flow as U8P4)
    sample.setTempGoal = BinaryCodec::decodeU8P1(d[8]);
    sample.setFlowGoal = BinaryCodec::decodeU8P4(d[10]);
    sample.setPressureGoal = BinaryCodec::decodeU8P4(d[11]);

    // Frame and steam temp
    sample.frameNumber = d[12];
    sample.steamTemp = d[13];

    // Uncomment for debugging:
    // qDebug() << "DE1Device: ShotSample - headTemp:" << sample.headTemp << "pressure:" << sample.groupPressure;

    // Update internal state
    m_pressure = sample.groupPressure;
    m_flow = sample.groupFlow;
    m_mixTemp = sample.mixTemp;
    m_headTemp = sample.headTemp;

    emit shotSampleReceived(sample);
}

void DE1Device::parseWaterLevel(const QByteArray& data) {
    if (data.size() < 2) return;

    m_waterLevel = BinaryCodec::decodeU16P8(BinaryCodec::decodeShortBE(data, 0));
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
    qDebug() << "DE1Device: Triggering post-connection initialization";
    sendInitialSettings();
}

void DE1Device::writeCharacteristic(const QBluetoothUuid& uuid, const QByteArray& data) {
    if (!m_service || !m_characteristics.contains(uuid)) return;

    m_writePending = true;
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
    command();
}

// Machine control methods
void DE1Device::requestState(DE1::State state) {
    QByteArray data(1, static_cast<char>(state));
    queueCommand([this, data]() {
        writeCharacteristic(DE1::Characteristic::REQUESTED_STATE, data);
    });
}

void DE1Device::startEspresso() {
    requestState(DE1::State::Espresso);
}

void DE1Device::startSteam() {
    requestState(DE1::State::Steam);
}

void DE1Device::startHotWater() {
    requestState(DE1::State::HotWater);
}

void DE1Device::startFlush() {
    requestState(DE1::State::HotWaterRinse);
}

void DE1Device::stopOperation() {
    requestState(DE1::State::Idle);
}

void DE1Device::goToSleep() {
    requestState(DE1::State::Sleep);
}

void DE1Device::wakeUp() {
    requestState(DE1::State::Idle);
}

void DE1Device::uploadProfile(const Profile& profile) {
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

void DE1Device::sendInitialSettings() {
    // This mimics de1app's later_new_de1_connection_setup
    // Send a basic profile and shot settings to trigger machine wake-up response
    qDebug() << "DE1Device: Sending initial profile and settings";

    // Send a basic profile header (5 bytes)
    // HeaderV=1, NumFrames=1, NumPreinfuse=0, MinPressure=0, MaxFlow=6.0
    QByteArray header(5, 0);
    header[0] = 1;   // HeaderV - always 1
    header[1] = 1;   // NumberOfFrames
    header[2] = 0;   // NumberOfPreinfuseFrames  
    header[3] = 0;   // MinimumPressure (U8P4)
    header[4] = 96;  // MaximumFlow (U8P4) = 6.0 * 16

    queueCommand([this, header]() {
        qDebug() << "DE1Device: Writing profile header";
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
        qDebug() << "DE1Device: Writing profile frame";
        writeCharacteristic(DE1::Characteristic::FRAME_WRITE, frame);
    });

    // Send tail frame (required to complete profile upload)
    // FrameToWrite = NumberOfFrames (1), MaxTotalVolume = 0
    QByteArray tailFrame(8, 0);
    tailFrame[0] = 1;    // FrameToWrite = NumberOfFrames
    // Bytes 1-7 are all 0 (no volume limit)

    queueCommand([this, tailFrame]() {
        qDebug() << "DE1Device: Writing profile tail frame";
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
        qDebug() << "DE1Device: Reading GHC info via MMR";
        writeCharacteristic(DE1::Characteristic::READ_FROM_MMR, mmrRead);
    });

    // Send shot settings
    // Default values similar to de1app defaults
    double steamTemp = 160.0;      // Steam temperature
    int steamDuration = 120;       // Steam timeout in seconds
    double hotWaterTemp = 80.0;    // Hot water temperature
    int hotWaterVolume = 200;      // Hot water volume in ml
    double groupTemp = 93.0;       // Group head temperature

    setShotSettings(steamTemp, steamDuration, hotWaterTemp, hotWaterVolume, groupTemp);
}

void DE1Device::setShotSettings(double steamTemp, int steamDuration,
                                double hotWaterTemp, int hotWaterVolume,
                                double groupTemp) {
    QByteArray data(9, 0);
    data[0] = 0;  // SteamSettings flags
    data[1] = BinaryCodec::encodeU8P0(steamTemp);
    data[2] = BinaryCodec::encodeU8P0(steamDuration);
    data[3] = BinaryCodec::encodeU8P0(hotWaterTemp);
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
