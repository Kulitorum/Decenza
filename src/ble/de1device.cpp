#include "de1device.h"
#include "de1transport.h"
#include "bletransport.h"
#include "protocol/binarycodec.h"
#include "profile/profile.h"
#include "../core/settings.h"

#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
#include "../simulator/de1simulator.h"
#endif
#include <QBluetoothAddress>
#include <QDateTime>
#include <QDebug>
#include <chrono>
#include <memory>

namespace {
qint64 monotonicMsNow()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
}

DE1Device::DE1Device(QObject* parent)
    : QObject(parent)
{
}

DE1Device::~DE1Device() {
    disconnect();
}

// -- Transport abstraction --

void DE1Device::setTransport(DE1Transport* transport) {
    // Disconnect old transport signals if any
    if (m_transport) {
        QObject::disconnect(m_transport, nullptr, this, nullptr);
    }

    bool wasConnected = isConnected();
    m_transport = transport;

    if (m_transport) {
        connect(m_transport, &DE1Transport::connected,
                this, &DE1Device::onTransportConnected);
        connect(m_transport, &DE1Transport::disconnected,
                this, &DE1Device::onTransportDisconnected);
        connect(m_transport, &DE1Transport::dataReceived,
                this, &DE1Device::onTransportDataReceived);
        connect(m_transport, &DE1Transport::writeComplete,
                this, &DE1Device::onTransportWriteComplete);
        connect(m_transport, &DE1Transport::errorOccurred,
                this, &DE1Device::errorOccurred);
        connect(m_transport, &DE1Transport::logMessage,
                this, &DE1Device::logMessage);
    }

    if (wasConnected != isConnected()) {
        emit connectedChanged();
        emit guiEnabledChanged();
    }
}

QString DE1Device::connectionType() const {
    if (m_simulationMode) return QStringLiteral("Simulation");
    if (!m_transport) return QString();
    return m_transport->transportName();
}

// -- Transport signal handlers --

void DE1Device::onTransportConnected() {
    m_connecting = false;
    emit connectingChanged();
    emit connectedChanged();
    emit guiEnabledChanged();

    // Send Idle state to wake the machine (same as de1app on connect)
    requestState(DE1::State::Idle);
}

void DE1Device::onTransportDisconnected() {
    m_sawStopWritePending = false;
    m_lastSawTriggerMs = 0;
    m_lastSawWriteMs = 0;

    m_connecting = false;
    emit connectingChanged();
    emit connectedChanged();
    emit guiEnabledChanged();
}

void DE1Device::onTransportDataReceived(const QBluetoothUuid& uuid, const QByteArray& data) {
    if (uuid == DE1::Characteristic::STATE_INFO) {
        parseStateInfo(data);
    } else if (uuid == DE1::Characteristic::SHOT_SAMPLE) {
        parseShotSample(data);
    } else if (uuid == DE1::Characteristic::WATER_LEVELS) {
        parseWaterLevel(data);
    } else if (uuid == DE1::Characteristic::VERSION) {
        parseVersion(data);
    } else if (uuid == DE1::Characteristic::READ_FROM_MMR) {
        parseMMRResponse(data);
    }
}

void DE1Device::onTransportWriteComplete(const QBluetoothUuid& uuid, const QByteArray& data) {
    // SAW stop latency instrumentation (worker trigger -> urgent write -> BLE ack)
    if (m_sawStopWritePending
        && uuid == DE1::Characteristic::REQUESTED_STATE
        && data.size() == 1
        && static_cast<uint8_t>(data[0]) == static_cast<uint8_t>(DE1::State::Idle)) {
        qint64 ackMs = monotonicMsNow();
        qint64 dispatchMs = m_lastSawWriteMs - m_lastSawTriggerMs;
        qint64 bleAckMs = ackMs - m_lastSawWriteMs;
        qint64 totalMs = ackMs - m_lastSawTriggerMs;
        qDebug() << "[SAW-Latency] dispatch=" << dispatchMs
                 << "ms, bleAck=" << bleAckMs
                 << "ms, total=" << totalMs << "ms";
        m_sawStopWritePending = false;
        m_lastSawTriggerMs = 0;
        m_lastSawWriteMs = 0;
    }
}

// -- Connection state --

bool DE1Device::isConnected() const {
    if (m_simulationMode) return true;
    return m_transport && m_transport->isConnected();
}

bool DE1Device::isGuiEnabled() const {
    return isConnected() || m_simulationMode;
}

bool DE1Device::isConnecting() const {
    return m_connecting;
}

// -- Simulation mode --

void DE1Device::setSimulationMode(bool enabled) {
    if (m_simulationMode == enabled) {
        return;
    }
    m_simulationMode = enabled;

    if (enabled) {
        m_state = DE1::State::Idle;
        m_subState = DE1::SubState::Ready;
        m_pressure = 0.0;
        m_flow = 0.0;
        m_headTemp = 93.0;
        m_mixTemp = 92.5;
        m_waterLevel = 75.0;
        m_waterLevelMm = 31.25;
        m_waterLevelMl = 872;
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

    m_pressure = sample.groupPressure;
    m_flow = sample.groupFlow;
    m_headTemp = sample.headTemp;
    m_mixTemp = sample.mixTemp;
    m_steamTemp = sample.steamTemp;

    emit shotSampleReceived(sample);
}

// -- Connection management --

void DE1Device::connectToDevice(const QString& address) {
    QBluetoothDeviceInfo info(QBluetoothAddress(address), QString(), 0);
    connectToDevice(info);
}

void DE1Device::connectToDevice(const QBluetoothDeviceInfo& device) {
    // Don't reconnect if already connected or connecting
    if (isConnected() || m_connecting) {
        return;
    }

    // Clean up any existing transport
    if (m_transport) {
        disconnect();
    }

    m_connecting = true;
    emit connectingChanged();

    // Create a new BleTransport and wire it up
    auto* bleTransport = new BleTransport(this);
    setTransport(bleTransport);
    bleTransport->connectToDevice(device);
}

void DE1Device::disconnect() {
    m_profileUploadInProgress = false;
    m_sleepPendingAfterUpload = false;
    m_sawStopWritePending = false;
    m_lastSawTriggerMs = 0;
    m_lastSawWriteMs = 0;

    if (m_transport) {
        // Disconnect signals FIRST to prevent re-entrant emissions
        // (BleTransport::disconnect() emits disconnected(), which would
        // trigger onTransportDisconnected() and double-emit our signals)
        QObject::disconnect(m_transport, nullptr, this, nullptr);
        m_transport->disconnect();
        m_transport->deleteLater();
        m_transport = nullptr;
    }

    m_connecting = false;
    emit connectingChanged();
    emit connectedChanged();
    emit guiEnabledChanged();
}

// -- Parse methods --

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
        return;
    }

    // Update internal state
    m_pressure = sample.groupPressure;
    m_flow = sample.groupFlow;
    m_mixTemp = sample.mixTemp;
    m_headTemp = sample.headTemp;
    m_steamTemp = sample.steamTemp;

    emit shotSampleReceived(sample);
}

void DE1Device::parseWaterLevel(const QByteArray& data) {
    if (data.size() < 2) return;

    // Convert raw sensor reading to mm (U16P8 format: divide by 256)
    double rawMm = BinaryCodec::decodeU16P8(BinaryCodec::decodeShortBE(data, 0));

    // Apply sensor offset correction (sensor is mounted 5mm above water intake)
    constexpr double SENSOR_OFFSET = 5.0;
    m_waterLevelMm = rawMm + SENSOR_OFFSET;

    // Use fixed full point matching de1app (water_level_full_point = 40)
    constexpr double FULL_POINT = 40.0;
    double refillPoint = m_settings ? static_cast<double>(m_settings->waterRefillPoint()) : 5.0;

    // Calculate percentage: 0% at refill point, 100% at full point
    double range = FULL_POINT - refillPoint;
    if (range <= 0) range = 1.0;
    m_waterLevel = qBound(0.0, ((m_waterLevelMm - refillPoint) / range) * 100.0, 100.0);

    // Lookup table from de1app CAD data
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
        m_waterLevelMl = mmToMl[tableSize - 1];
    } else {
        m_waterLevelMl = mmToMl[index];
    }

    // Only emit when water level changes by at least 0.5% or ml changes
    if (qAbs(m_waterLevel - m_lastEmittedWaterLevel) >= 0.5
        || m_waterLevelMl != m_lastEmittedWaterLevelMl) {
        m_lastEmittedWaterLevel = m_waterLevel;
        m_lastEmittedWaterLevelMl = m_waterLevelMl;
        emit waterLevelChanged();
    }
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

    if (!m_transport) return;
    m_transport->write(DE1::Characteristic::READ_FROM_MMR, mmrRead);
}

void DE1Device::parseMMRResponse(const QByteArray& data) {
    if (data.size() < 5) return;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.constData());

    // Extract address (big endian)
    uint32_t address = (static_cast<uint32_t>(d[1]) << 16) |
                       (static_cast<uint32_t>(d[2]) << 8) |
                       static_cast<uint32_t>(d[3]);

    // Check if this is GHC_INFO response (address 0x80381C)
    if (address == 0x80381C) {
        uint8_t ghcStatus = d[4];

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

        emit logMessage(logMsg);

        if (m_isHeadless != canStartFromApp) {
            m_isHeadless = canStartFromApp;
            emit isHeadlessChanged();
        }
    }
    // Check if this is REFILL_KIT response (address 0x80385C)
    else if (address == 0x80385C) {
        uint8_t kitStatus = d[4];

        int detected = (kitStatus > 0) ? 1 : 0;
        QString statusName = detected ? "detected" : "not detected";

        QString logMsg = QString("Refill kit: %1").arg(statusName);
        emit logMessage(logMsg);

        if (m_refillKitDetected != detected) {
            m_refillKitDetected = detected;
            emit refillKitDetectedChanged();
        }
    }
}

// -- Machine control methods (delegate through transport) --

void DE1Device::requestState(DE1::State state) {
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
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
            break;
        }
        return;
    }
#endif

    if (!m_transport) return;
    QByteArray data(1, static_cast<char>(state));
    m_transport->write(DE1::Characteristic::REQUESTED_STATE, data);
}

void DE1Device::startEspresso() {
    writeMMR(DE1::MMR::GHC_MODE, 1);

    if (m_state != DE1::State::Idle) {
        requestState(DE1::State::Idle);
    }
    requestState(DE1::State::Espresso);
}

void DE1Device::startSteam() {
    writeMMR(DE1::MMR::GHC_MODE, 1);

    if (m_state != DE1::State::Idle) {
        requestState(DE1::State::Idle);
    }
    requestState(DE1::State::Steam);
}

void DE1Device::startHotWater() {
    writeMMR(DE1::MMR::GHC_MODE, 1);

    if (m_state != DE1::State::Idle) {
        requestState(DE1::State::Idle);
    }
    requestState(DE1::State::HotWater);
}

void DE1Device::startFlush() {
    writeMMR(DE1::MMR::GHC_MODE, 1);

    if (m_state != DE1::State::Idle) {
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
    requestState(DE1::State::Idle);
}

void DE1Device::stopOperationUrgent() {
    stopOperationUrgent(0);
}

void DE1Device::stopOperationUrgent(qint64 sawTriggerMs) {
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
    if (m_simulationMode && m_simulator) {
        m_simulator->stop();
        return;
    }
#endif
    if (!m_transport) return;
    clearCommandQueue();
    if (sawTriggerMs > 0) {
        m_lastSawTriggerMs = sawTriggerMs;
        m_lastSawWriteMs = monotonicMsNow();
        m_sawStopWritePending = true;
    } else {
        m_sawStopWritePending = false;
        m_lastSawTriggerMs = 0;
        m_lastSawWriteMs = 0;
    }
    QByteArray data(1, static_cast<char>(DE1::State::Idle));
    m_transport->writeUrgent(DE1::Characteristic::REQUESTED_STATE, data);
}

void DE1Device::requestIdle() {
    requestState(DE1::State::Idle);
}

void DE1Device::skipToNextFrame() {
    requestState(DE1::State::SkipToNext);
}

void DE1Device::goToSleep() {
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
    if (m_simulationMode && m_simulator) {
        m_simulator->goToSleep();
        return;
    }
#endif

    // If a profile upload is in progress, defer sleep until it completes.
    if (m_profileUploadInProgress) {
        qDebug() << "DE1Device: Sleep requested during profile upload, deferring until upload completes";
        m_sleepPendingAfterUpload = true;
        return;
    }

    if (!m_transport) return;
    // Clear pending commands - sleep takes priority
    m_transport->clearQueue();

    // Send sleep command directly (don't queue it)
    QByteArray data(1, static_cast<char>(DE1::State::Sleep));
    m_transport->writeUrgent(DE1::Characteristic::REQUESTED_STATE, data);
}

void DE1Device::wakeUp() {
    requestState(DE1::State::Idle);
}

void DE1Device::clearCommandQueue() {
    m_profileUploadInProgress = false;
    m_sleepPendingAfterUpload = false;
    m_sawStopWritePending = false;
    m_lastSawTriggerMs = 0;
    m_lastSawWriteMs = 0;
    if (m_transport) {
        m_transport->clearQueue();
    }
}

void DE1Device::uploadProfile(const Profile& profile) {
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
    if (m_simulationMode && m_simulator) {
        m_simulator->setProfile(profile);
    }
#endif

    if (!m_transport) return;

    m_profileUploadInProgress = true;

    // Queue header write
    QByteArray header = profile.toHeaderBytes();
    m_transport->write(DE1::Characteristic::HEADER_WRITE, header);

    // Queue each frame
    QList<QByteArray> frames = profile.toFrameBytes();
    for (const QByteArray& frame : frames) {
        m_transport->write(DE1::Characteristic::FRAME_WRITE, frame);
    }

    // Track completion by counting writeComplete signals for profile-related UUIDs.
    // Only count HEADER_WRITE and FRAME_WRITE completions to avoid interference
    // from concurrent writes (e.g., MMR writes from other code paths).
    int totalWrites = 1 + frames.size();  // header + frames
    auto* counter = new int(0);
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_transport, &DE1Transport::writeComplete, this,
        [this, totalWrites, counter, conn](const QBluetoothUuid& uuid, const QByteArray& /*data*/) {
            if (uuid == DE1::Characteristic::HEADER_WRITE || uuid == DE1::Characteristic::FRAME_WRITE) {
                (*counter)++;
                if (*counter >= totalWrites) {
                    QObject::disconnect(*conn);
                    delete counter;
                    m_profileUploadInProgress = false;
                    emit profileUploaded(true);
                    if (m_sleepPendingAfterUpload) {
                        m_sleepPendingAfterUpload = false;
                        qDebug() << "DE1Device: Profile upload complete, now sending deferred sleep";
                        goToSleep();
                    }
                }
            }
        });
}

void DE1Device::uploadProfileAndStartEspresso(const Profile& profile) {
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
    if (m_simulationMode && m_simulator) {
        m_simulator->setProfile(profile);
    }
#endif

    if (!m_transport) return;

    m_profileUploadInProgress = true;

    // Queue header write
    QByteArray header = profile.toHeaderBytes();
    m_transport->write(DE1::Characteristic::HEADER_WRITE, header);

    // Queue each frame
    QList<QByteArray> frames = profile.toFrameBytes();
    for (const QByteArray& frame : frames) {
        m_transport->write(DE1::Characteristic::FRAME_WRITE, frame);
    }

    // Queue espresso start AFTER all profile frames
    m_transport->write(DE1::Characteristic::REQUESTED_STATE,
                       QByteArray(1, static_cast<char>(DE1::State::Espresso)));

    // Track completion: header + frames + espresso start command.
    // Count only profile-related UUIDs and the REQUESTED_STATE for espresso start.
    int totalWrites = 1 + frames.size() + 1;
    auto* counter = new int(0);
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_transport, &DE1Transport::writeComplete, this,
        [this, totalWrites, counter, conn](const QBluetoothUuid& uuid, const QByteArray& /*data*/) {
            if (uuid == DE1::Characteristic::HEADER_WRITE ||
                uuid == DE1::Characteristic::FRAME_WRITE ||
                uuid == DE1::Characteristic::REQUESTED_STATE) {
                (*counter)++;
                if (*counter >= totalWrites) {
                    QObject::disconnect(*conn);
                    delete counter;
                    m_profileUploadInProgress = false;
                    emit profileUploaded(true);
                    if (m_sleepPendingAfterUpload) {
                        m_sleepPendingAfterUpload = false;
                        qDebug() << "DE1Device: Profile upload complete, now sending deferred sleep";
                        goToSleep();
                    }
                }
            }
        });
}

void DE1Device::writeHeader(const QByteArray& headerData) {
    if (!m_transport) return;
    m_transport->write(DE1::Characteristic::HEADER_WRITE, headerData);
}

void DE1Device::writeFrame(const QByteArray& frameData) {
    if (!m_transport) return;
    m_transport->write(DE1::Characteristic::FRAME_WRITE, frameData);
}

void DE1Device::writeMMR(uint32_t address, uint32_t value) {
    if (!m_transport) return;
    // MMR Write format (20 bytes):
    // Byte 0: Length (0x04 for 4-byte value)
    // Bytes 1-3: Address (big endian)
    // Bytes 4-7: Value (little endian)
    // Bytes 8-19: Padding (zeros)
    QByteArray data(20, 0);
    data[0] = 0x04;
    data[1] = (address >> 16) & 0xFF;
    data[2] = (address >> 8) & 0xFF;
    data[3] = address & 0xFF;
    data[4] = value & 0xFF;
    data[5] = (value >> 8) & 0xFF;
    data[6] = (value >> 16) & 0xFF;
    data[7] = (value >> 24) & 0xFF;

    m_transport->write(DE1::Characteristic::WRITE_TO_MMR, data);
}

void DE1Device::setUsbChargerOn(bool on, bool force) {
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
    if (!m_transport) return;
    QByteArray data;
    data.append(BinaryCodec::encodeShortBE(BinaryCodec::encodeU16P8(0)));
    data.append(BinaryCodec::encodeShortBE(BinaryCodec::encodeU16P8(static_cast<double>(refillPointMm))));

    m_transport->write(DE1::Characteristic::WATER_LEVELS, data);
}

void DE1Device::setFlowCalibrationMultiplier(double multiplier) {
    uint32_t value = static_cast<uint32_t>(1000.0 * multiplier);
    writeMMR(DE1::MMR::FLOW_CALIBRATION, value);
}

void DE1Device::setRefillKitPresent(int value) {
    writeMMR(DE1::MMR::REFILL_KIT, static_cast<uint32_t>(value));
}

void DE1Device::requestRefillKitStatus() {
    if (!m_transport) return;
    QByteArray mmrRead(20, 0);
    mmrRead[0] = 0x00;
    mmrRead[1] = 0x80;
    mmrRead[2] = 0x38;
    mmrRead[3] = 0x5C;

    m_transport->write(DE1::Characteristic::READ_FROM_MMR, mmrRead);
}

void DE1Device::sendInitialSettings() {
    if (!m_transport) return;

    // Ensure USB charger is ON at startup (safe default like de1app)
    if (!m_usbChargerOn) {
        m_usbChargerOn = true;
        writeMMR(DE1::MMR::USB_CHARGER, 1);
        emit usbChargerOnChanged();
    }

    // CRITICAL: Set fan temperature threshold via MMR
    writeMMR(DE1::MMR::FAN_THRESHOLD, 60);

    // Heater tweaks — matches de1app's set_heater_tweaks()
    if (m_settings) {
        writeMMR(DE1::MMR::PHASE1_FLOW_RATE, m_settings->heaterWarmupFlow());
        writeMMR(DE1::MMR::PHASE2_FLOW_RATE, m_settings->heaterTestFlow());
        writeMMR(DE1::MMR::HOT_WATER_IDLE_TEMP, m_settings->heaterIdleTemp());
        writeMMR(DE1::MMR::ESPRESSO_WARMUP_TIMEOUT, m_settings->heaterWarmupTimeout());
        writeMMR(DE1::MMR::HOT_WATER_FLOW_RATE, m_settings->hotWaterFlowRate());
        writeMMR(DE1::MMR::STEAM_TWO_TAP_STOP, m_settings->steamTwoTapStop() ? 1 : 0);
        writeMMR(DE1::MMR::STEAM_HIGHFLOW_START, 70);
        writeMMR(DE1::MMR::TANK_TEMP_THRESHOLD, 0);
    } else {
        writeMMR(DE1::MMR::PHASE1_FLOW_RATE, 20);
        writeMMR(DE1::MMR::PHASE2_FLOW_RATE, 40);
        writeMMR(DE1::MMR::HOT_WATER_IDLE_TEMP, 990);
        writeMMR(DE1::MMR::ESPRESSO_WARMUP_TIMEOUT, 10);
        writeMMR(DE1::MMR::HOT_WATER_FLOW_RATE, 10);
        writeMMR(DE1::MMR::STEAM_TWO_TAP_STOP, 1);
        writeMMR(DE1::MMR::STEAM_HIGHFLOW_START, 70);
        writeMMR(DE1::MMR::TANK_TEMP_THRESHOLD, 0);
    }

    // Send a basic profile header (5 bytes)
    QByteArray header(5, 0);
    header[0] = 1;   // HeaderV
    header[1] = 1;   // NumberOfFrames
    header[2] = 0;   // NumberOfPreinfuseFrames
    header[3] = 0;   // MinimumPressure (U8P4)
    header[4] = 96;  // MaximumFlow (U8P4) = 6.0 * 16

    m_transport->write(DE1::Characteristic::HEADER_WRITE, header);

    // Send a basic profile frame (8 bytes)
    QByteArray frame(8, 0);
    frame[0] = 0;    // FrameToWrite = 0
    frame[1] = 0;    // Flag
    frame[2] = static_cast<char>(144);  // SetVal (U8P4) = 9.0 * 16 = 144
    frame[3] = static_cast<char>(186);  // Temp (U8P1) = 93.0 * 2 = 186
    frame[4] = 62;   // FrameLen (F8_1_7)
    frame[5] = 0;    // TriggerVal
    frame[6] = 0;    // MaxVol high byte
    frame[7] = 0;    // MaxVol low byte

    m_transport->write(DE1::Characteristic::FRAME_WRITE, frame);

    // Send tail frame
    QByteArray tailFrame(8, 0);
    tailFrame[0] = 1;    // FrameToWrite = NumberOfFrames

    m_transport->write(DE1::Characteristic::FRAME_WRITE, tailFrame);

    // Read GHC info via MMR
    QByteArray mmrRead(20, 0);
    mmrRead[0] = 0x00;
    mmrRead[1] = 0x80;
    mmrRead[2] = 0x38;
    mmrRead[3] = 0x1C;

    m_transport->write(DE1::Characteristic::READ_FROM_MMR, mmrRead);

    // Read refill kit status
    requestRefillKitStatus();

    // Send shot settings
    double steamTemp = 0.0;
    int steamDuration = 120;
    double hotWaterTemp = 80.0;
    int hotWaterVolume = 200;
    double groupTemp = 93.0;

    setShotSettings(steamTemp, steamDuration, hotWaterTemp, hotWaterVolume, groupTemp);

    // Signal that initial settings are complete
    // Use a write-complete counter to detect when all queued writes finish
    // For simplicity, emit immediately — the transport queues ensure ordering,
    // and MainController connects to this signal to apply user settings
    emit initialSettingsComplete();
}

void DE1Device::setShotSettings(double steamTemp, int steamDuration,
                                double hotWaterTemp, int hotWaterVolume,
                                double groupTemp) {
    if (!m_transport) return;
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

    m_transport->write(DE1::Characteristic::SHOT_SETTINGS, data);
}
