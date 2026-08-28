#include "de1device.h"
#include "bledeviceid.h"
#include "de1logging.h"
#include "machine/sawlogging.h"
#include "../controllers/calibrationlogging.h"
#include "de1transport.h"
#include "bletransport.h"
#include "protocol/binarycodec.h"
#include "protocol/firmwarepackets.h"
#include "profile/profile.h"
#include "../core/settings_hardware.h"

#ifdef DECENZA_SIMULATOR
#include "../simulator/de1simulator.h"
#endif
#include <QBluetoothAddress>
#include <QDateTime>
#include <cmath>
#include <iterator>
#include <QDebug>

// Alias the shared DE1 helpers (src/ble/de1logging.h) — never copy a body.
//
// The tags are the sub-prefixes this file already used, now inside the [DE1]
// marker, so one [DE1] search returns the whole machine. Most per-area greps
// still work by substring — `[MMR]`, `[Phase]`, `[ShotSettings]` are unchanged
// inside `[DE1][MMR]` and so on. TWO DELIBERATELY DO NOT:
//   - `[firmware]` is now the tag "Firmware". A case-sensitive
//     `grep '\[firmware\]'` returns nothing; search `[DE1][Firmware]`.
//   - `[WaterLevels]` (plural, setWaterRefillLevel) and `[WaterLevel]`
//     (singular, parseWaterLevel) were two spellings of one area and are now
//     both "WaterLevel".
// Between those two and `[BLE DE1]`/`[DE1]`, this file carried nine distinct
// prefixes before this change, not five.
//
// DEBUG uses the _STDERR_ variants (no `emit logMessage`) so DEBUG detail stops
// flowing into the connections-page DE1 view. Note what that view is TODAY: an
// unfiltered `de1LogText.text += message` with no level, no cap and no Clear
// button (SettingsConnectionsTab.qml), so every DEBUG line grew a QML string for
// the process lifetime. Sourcing it from the system log at INFO+ is task 5.1/5.2
// of the openspec change; this anticipates that contract rather than waiting for
// it, and shrinks an unbounded accumulation meanwhile.
#define DEVICE_LOG(msg)         DE1_LOG_STDERR_TAGGED("Device", msg)
#define DEVICE_INFO(msg)        DE1_INFO_TAGGED("Device", msg)
#define DEVICE_WARN(msg)        DE1_WARN_TAGGED("Device", msg)
// The two _WARN_STDERR aliases are NOT a tier choice — they are required. Their
// only call sites are the const members dropDeviceWriteIfFirmwareFlash() and
// dropIfFirmwareFlashInProgress(), and `logMessage` is declared non-const
// (de1device.h:361) so `emit logMessage(...)` will not compile in one. (Not a moc
// limitation, which this comment used to claim without a citation — moc emits a
// const signal fine, qtbase/src/tools/moc/generator.cpp:1297-1300.)
#define DEVICE_WARN_STDERR(msg) DE1_WARN_STDERR_TAGGED("Device", msg)
#define MMR_LOG(msg)            DE1_LOG_STDERR_TAGGED("MMR", msg)
#define MMR_WARN(msg)           DE1_WARN_TAGGED("MMR", msg)
#define MMR_WARN_STDERR(msg)    DE1_WARN_STDERR_TAGGED("MMR", msg)
#define FW_LOG(msg)             DE1_LOG_STDERR_TAGGED("Firmware", msg)
#define FW_INFO(msg)            DE1_INFO_TAGGED("Firmware", msg)
#define FW_WARN(msg)            DE1_WARN_TAGGED("Firmware", msg)
#define PHASE_LOG(msg)          DE1_LOG_STDERR_TAGGED("Phase", msg)
#define WATER_LOG(msg)          DE1_LOG_STDERR_TAGGED("WaterLevel", msg)
#define DESCALE_LOG(msg)        DE1_LOG_STDERR_TAGGED("Descale", msg)
#define SHOTSETTINGS_LOG(msg)   DE1_LOG_STDERR_TAGGED("ShotSettings", msg)
#include <QStringList>
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
    // 10 s is plenty: a full upload is header + ~5–10 frames, each a single
    // write-with-response. If we pass this cap, the BLE link is almost
    // certainly wedged and we should surface a failure rather than hang.
    m_uploadTimeoutTimer.setSingleShot(true);
    m_uploadTimeoutTimer.setInterval(10000);
    connect(&m_uploadTimeoutTimer, &QTimer::timeout, this, [this]() {
        finishProfileUpload(false, QStringLiteral("timeout waiting for write ACKs"));
    });

    // Sweeps m_pendingMMRReads for one-shot MMR reads (GHC info, machine
    // identity, heater voltage, refill-kit status) that haven't gotten a
    // response in time. Repeating rather than single-shot: started on the first
    // pending read, stopped once the table drains, so it's inert outside a
    // connect window.
    m_mmrReadRetryTimer.setInterval(250);
    connect(&m_mmrReadRetryTimer, &QTimer::timeout, this, &DE1Device::checkMMRReadTimeouts);

    // Fires the deferred startEspresso() once the profile-upload settle window
    // elapses (see startEspresso()). A stoppable member timer, NOT a fire-and-
    // forget QTimer::singleShot: a disconnect inside the window must be able to
    // CANCEL the pending start (onTransportDisconnected stops it), otherwise the
    // shot would fire on whatever link exists when it elapses — including a
    // fresh reconnect the user never asked to start a shot on.
    m_espressoSettleTimer.setSingleShot(true);
    connect(&m_espressoSettleTimer, &QTimer::timeout, this, [this]() {
        m_espressoStartDeferred = false;
        startEspresso();
    });
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
        connect(m_transport, &DE1Transport::writeAbandoned,
                this, &DE1Device::onWriteAbandoned);
        connect(m_transport, &DE1Transport::writeComplete,
                this, &DE1Device::onTransportWriteComplete);
        connect(m_transport, &DE1Transport::errorOccurred,
                this, &DE1Device::errorOccurred);
        connect(m_transport, &DE1Transport::de1LinkFault,
                this, &DE1Device::de1LinkFault);
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
    // The canonical "the machine is usable now" line, and the counterpart in
    // onTransportDisconnected below. Every transport announces its own
    // connection in its own terms ("Transport connected" for BLE, "Port opened"
    // for serial); neither says the DE1 is up, and reading the INFO set as a
    // story left a hole exactly where a user looks first. Names the transport
    // because which one carried the session is the next question asked.
    DEVICE_INFO(QStringLiteral("DE1 CONNECTED (%1)").arg(connectionType()));
    m_connecting = false;
    emit connectingChanged();
    emit connectedChanged();
    emit guiEnabledChanged();

    // Send Idle state to wake the machine (same as de1app on connect)
    requestState(DE1::State::Idle);

    // Send initial settings once the transport signals a full connection.
    // Previously called from parseVersion(), but on Linux/BlueZ the VERSION
    // characteristic can be readable while the service is still in
    // RemoteServiceDiscovering state, so write characteristics were not yet
    // available and MMR writes silently failed. At this point Qt has reached
    // RemoteServiceDiscovered and all characteristics are populated. Verified
    // on Linux; other platforms (macOS, Windows, Android, iOS) should behave
    // the same since RemoteServiceDiscovered is a Qt-level guarantee, but have
    // not been explicitly regression-tested for this ordering.
    // VERSION read (which triggers parseVersion) is still in the subscribeAll()
    // queue, so firmware version will be populated shortly after.
    sendInitialSettings();
}

void DE1Device::onTransportDisconnected() {
    // The keepalive run ends here. Without this its suppressed tally sat in the
    // table across the whole disconnect, and the first keepalive after a
    // reconnect — possibly hours later — printed carrying the PREVIOUS session's
    // count, annotated with a span measured to the moment a reader is looking at
    // it. Keyed by MMR address, which this function does not enumerate, so it
    // flushes all of them.
    for (const auto& [address, collapsed] :
         m_keepaliveLog.flushAll(QDateTime::currentMSecsSinceEpoch())) {
        MMR_LOG(QString("MMR keepalive 0x%1%2")
                    .arg(address, LogCollapse::suffix(collapsed)));
    }

    // The water-level filter's run ends here too. Re-seeding on the next connect is what stops the
    // reconnect showing the tank refilling from wherever the last session left the average, and
    // clearing the log endpoints stops the first line of the new session claiming a delta measured
    // against the old one.
    m_waterLevelSeeded = false;
    m_lastLoggedWaterLevelMm = -1000.0;
    m_lastLoggedWaterLevelMl = -1;

    // Elided-write runs end here as well. Keyed by the message text, which this function does not
    // enumerate, so flushAll() — and the key IS the line to print, under its own tag.
    const qint64 skipFlushMs = QDateTime::currentMSecsSinceEpoch();
    for (const auto& [text, collapsed] : m_mmrSkipLog.flushAll(skipFlushMs))
        MMR_LOG(text + LogCollapse::suffix(collapsed));
    for (const auto& [text, collapsed] : m_shotSettingsSkipLog.flushAll(skipFlushMs))
        SHOTSETTINGS_LOG(text + LogCollapse::suffix(collapsed));

    // Tier by whether the machine was BUSY, because that is what separates a
    // fault from a shutdown, and nothing else here can.
    //
    // I first wrote this as a flat INFO, reasoning that a bare disconnect fires
    // on every app close so WARN would cry wolf, and that a real fault is warned
    // elsewhere. The second half was wrong — all three backstops I named fail on
    // the path that matters:
    //   - de1LinkFault fires on controller errors, write-retry exhaustion and
    //     zombie links. BLEManager::onDe1LinkFault's own contract says it fires
    //     "never on plain device-absence" — and a machine switched off at the
    //     wall IS plain absence.
    //   - The write-abandoned WARN needs a write in flight. Mid-shot the app is
    //     mostly receiving, so m_writeTimeoutTimer is not even running.
    //   - The reconnect ladder in main.cpp logs entirely at qDebug, with no
    //     marker. It is not a backstop at any tier.
    // And on Apple platforms Qt raises no error to reach onControllerError at
    // all: qlowenergycontroller_darwin.mm's _q_disconnected() emits disconnected()
    // without setError() (RemoteHostClosedError appears only in the bluez and
    // winrt backends). So a mid-shot DE1 drop on macOS/iOS produced ZERO lines
    // above INFO — while pulling the USB cable produced three WARNs.
    //
    // Gating on the state resolves the ambiguity instead of surrendering to it: a
    // drop while the machine is doing something is unambiguously a fault, and an
    // idle/sleeping drop is the shutdown case that must stay quiet.
    // Listed as the QUIET states rather than the busy ones, so a firmware state
    // added later defaults to being reported rather than to silence. Init and
    // NoRequest are in here because they are transient/unknown at startup and a
    // false alarm there would be its own cry-wolf.
    const bool wasResting = (m_state == DE1::State::Sleep
                             || m_state == DE1::State::GoingToSleep
                             || m_state == DE1::State::Idle
                             || m_state == DE1::State::SchedIdle
                             || m_state == DE1::State::Init
                             || m_state == DE1::State::NoRequest);
    if (!wasResting) {
        DEVICE_WARN(QStringLiteral("DE1 DISCONNECTED while %1 — unrequested drop")
                        .arg(DE1::stateToString(m_state)));
    } else {
        DEVICE_INFO(QStringLiteral("DE1 DISCONNECTED (was %1)")
                        .arg(DE1::stateToString(m_state)));
    }
    m_sawStopWritePending = false;
    m_lastSawTriggerMs = 0;
    m_lastSawWriteMs = 0;

    // Restore the permissive default so a GHC machine's "false" doesn't carry
    // into the next connection and block its start buttons until (or unless)
    // the fresh GHC MMR read returns. setIsHeadless only emits on a change.
    setIsHeadless(true);

    // Clear ShotSettings tracking so a reconnect doesn't compare the DE1's
    // post-reconnect indication against a stale commanded value from the
    // previous session (which would log a spurious drift).
    m_commandedSteamTargetC = -1.0;
    m_commandedSteamDurationSec = -1;
    m_commandedHotWaterTempC = -1.0;
    m_commandedHotWaterVolMl = -1;
    m_commandedGroupTargetC = -1.0;
    m_lastShotSettingsWriteMs = 0;
    m_lastShotSettingsPayload.clear();
    // Clear the MMR dedup cache so a reconnect re-writes real values rather
    // than trusting cached values from the previous session (the DE1 may have
    // power-cycled or had its firmware state reset between sessions).
    m_lastMMRValues.clear();
    // Calibration offsets are facts about ONE machine. Keeping them across a
    // reconnect means connecting to a second DE1 and showing the first's values
    // — and the wizard's Apply gate is exactly "has this machine answered", so a
    // stale true opens a write against a baseline this machine never reported.
    clearCalibrationCache();
    // Stop chasing reads for a connection that no longer exists — a reconnect
    // re-issues them fresh via sendInitialSettings().
    m_pendingMMRReads.clear();
    m_mmrReadRetryTimer.stop();
    // Drop any pending profile-upload settle window — it belongs to the dead
    // connection's upload, not the next one's. Stopping the settle timer CANCELS
    // an armed deferred start outright, so it can't fire an unrequested shot on
    // a reconnect that lands within the window.
    m_lastProfileUploadCompleteMs = 0;
    m_espressoStartDeferred = false;
    m_espressoSettleTimer.stop();
    m_deviceSteamTargetC = -1.0;
    m_deviceSteamDurationSec = -1;
    m_deviceHotWaterTempC = -1.0;
    m_deviceHotWaterVolMl = -1;
    m_deviceGroupTargetC = -1.0;
    // Emit the NOTIFY signal so any QML binding on deviceSteamTargetC /
    // deviceGroupTargetC sees the reset and doesn't keep displaying the
    // previous session's values until a fresh indication arrives.
    emit shotSettingsReported(-1.0, -1, -1.0, -1, -1.0);

    // If an upload was in flight when the transport dropped, surface it as
    // a non-retryable "BLE disconnect during upload" failure *now* rather
    // than letting the 10 s m_uploadTimeoutTimer eventually fire with a
    // "timeout waiting for write ACKs" reason. The timeout path is
    // classified retryable, which would incorrectly bump ProfileManager's
    // retry counter after the disconnect has already reset it — leading to
    // the communication-failure dialog appearing after 4 post-reconnect
    // failures instead of 5. The reconnect path (initialSettingsComplete
    // -> applyAllSettings -> uploadCurrentProfile) re-uploads on its own,
    // so we don't need to retry from here.
    if (m_profileUploadInProgress) {
        finishProfileUpload(false, QStringLiteral("BLE disconnect during upload"));
    }

    // Reset substate so a stale value (in particular Error_NoAC) cannot outlive the
    // connection it was reported on. Without this, a machine that dropped while its
    // standby switch was cutting AC would show the no-AC warning forever — even after
    // the switch was flipped back and the app reconnected — since nothing would arrive
    // to change it. m_state is left alone here: it already drives no per-value cleanup
    // the way substate's warning does, and callers read connectedChanged()/guiEnabled
    // rather than m_state to learn the link is down.
    if (m_subState != DE1::SubState::Ready) {
        m_subState = DE1::SubState::Ready;
        emit subStateChanged();
    }

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
    } else if (uuid == DE1::Characteristic::SHOT_SETTINGS) {
        parseShotSettings(data);
    } else if (uuid == DE1::Characteristic::WATER_LEVELS) {
        parseWaterLevel(data);
    } else if (uuid == DE1::Characteristic::VERSION) {
        parseVersion(data);
    } else if (uuid == DE1::Characteristic::READ_FROM_MMR) {
        parseMMRResponse(data);
    } else if (uuid == DE1::Characteristic::CALIBRATION) {
        parseCalibration(data);
    } else if (uuid == DE1::Characteristic::FW_MAP_REQUEST) {
        FW_LOG(QStringLiteral("A009 notify: %1").arg(QString::fromLatin1(data.toHex(' '))));
        auto parsed = DE1::Firmware::parseFWMapNotification(data);
        if (parsed) {
            FW_LOG(QStringLiteral("A009 parsed: windowIncrement=%1 erase=%2 map=%3 firstError=%4")
                       .arg(parsed->windowIncrement)
                       .arg(parsed->fwToErase)
                       .arg(parsed->fwToMap)
                       .arg(QString::fromLatin1(
                           QByteArray(reinterpret_cast<const char*>(parsed->firstError.data()), 3)
                               .toHex(' '))));
            emit fwMapResponse(parsed->windowIncrement, parsed->fwToErase, parsed->fwToMap,
                               QByteArray(reinterpret_cast<const char*>(parsed->firstError.data()), 3));
        } else {
            FW_WARN(QStringLiteral("A009 notify too short to parse: %1 bytes").arg(data.size()));
        }
    }
}

void DE1Device::onTransportWriteComplete(const QBluetoothUuid& uuid, const QByteArray& data) {
    // SAW stop latency instrumentation (worker trigger -> urgent write -> BLE ack).
    // Carries [SAW], not [DE1]: it measures stop-at-weight, which is shot logic
    // rather than the machine. SAW is now a registered subsystem — the open
    // decision this site used to point at (task 2b.9 of the
    // replace-scale-log-with-system-log-filter change) was resolved in favour of
    // registering it, so the exemption that stood here is gone and the line goes
    // through the helper like any other.
    //
    // STDERR form on purpose: this class's logMessage feeds the DE1 connections
    // view, and a SAW line does not belong there.
    if (m_sawStopWritePending
        && uuid == DE1::Characteristic::REQUESTED_STATE
        && data.size() == 1
        && static_cast<uint8_t>(data[0]) == static_cast<uint8_t>(DE1::State::Idle)) {
        qint64 ackMs = monotonicMsNow();
        qint64 dispatchMs = m_lastSawWriteMs - m_lastSawTriggerMs;
        qint64 bleAckMs = ackMs - m_lastSawWriteMs;
        qint64 totalMs = ackMs - m_lastSawTriggerMs;
        // Tier by what happened, not by importance. A normal stop is developer
        // detail and belongs at DEBUG among the rest; a slow one is the single
        // most consequential thing in the log for that shot, and per LOGGING.md
        // a fault whose reader can only find it by scrolling through the normal
        // case is a fault nobody reads.
        //
        // bleAckMs now spans the shared GATT queue, so it includes any wait
        // behind another device's operation — the queue's own FOREIGN_WAIT_WARN
        // line names which one. Read the two together.
        if (bleAckMs >= SAW_SLOW_ACK_WARN_MS) {
            SAW_WARN_STDERR("Latency", QStringLiteral(
                "the stop-at-weight command took %1 ms to reach the machine "
                "(dispatch=%2 ms, total=%3 ms). Bluetooth was busy; the shot may "
                "have run past its target weight. A [Bluetooth][GattQueue] line "
                "just before this names the device that held the radio.")
                    .arg(bleAckMs).arg(dispatchMs).arg(totalMs));
        } else {
            SAW_LOG_STDERR("Latency", QStringLiteral("dispatch=%1 ms, bleAck=%2 ms, total=%3 ms")
                                          .arg(dispatchMs).arg(bleAckMs).arg(totalMs));
        }
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
#ifndef DECENZA_SIMULATOR
    // Belt and braces. Today no caller can get here with `true` on a build
    // without a simulator — main.cpp passes the hard-false settings getter and
    // the Ctrl+D shortcut is disabled — but this is a public Q_PROPERTY WRITE
    // that compiles identically in both configurations, so a future QML or C++
    // writer would reopen the bug with no compile-time signal. Enabling here
    // fabricates a connected machine (synthetic firmware string, water level,
    // head temp) and makes isConnected() true, which is exactly the dead
    // "simulating with no engine" UI this build is meant to be free of.
    if (enabled) {
        DEVICE_WARN(QStringLiteral("Simulation mode requested, but no simulator is compiled "
                                   "into this build — ignoring"));
        return;
    }
#endif
    if (m_simulationMode == enabled) {
        return;
    }
    m_simulationMode = enabled;

    if (enabled) {
        m_state = DE1::State::Idle;
        m_subState = DE1::SubState::Ready;
        m_pressure = 0.0;
        m_flow = 0.0;
        m_goalPressure = 0.0;
        m_goalFlow = 0.0;
        m_goalTemperature = 0.0;
        m_headTemp = 93.0;
        m_mixTemp = 92.5;
        m_waterLevel = 75.0;
        m_waterLevelMm = 31.25;
        m_waterLevelMl = 872;
        m_firmwareVersion = QStringLiteral("BLE v4.0.0, API v4\nFW v10.5.0, API v250\npcb=1.3, model=DE1PRO, firmware=v1342");
        // A simulated machine reports a nominal heater voltage, like a real one.
        // Without it the Heater Calibration popup shows "your machine has not
        // reported one" forever off hardware, which is indistinguishable from
        // the readback being broken — the same hole the calibration reads had.
        // 120 rather than 1120: the simulated machine MEASURED it, it was not
        // told (see bucketHeaterVoltage for what the >1000 forms mean).
        m_heaterVoltage = 120;
        emit heaterVoltageChanged();
        emit stateChanged();
        emit subStateChanged();
        emit waterLevelChanged();
        emit firmwareVersionChanged();
    }

    emit simulationModeChanged();
    emit connectedChanged();
    emit guiEnabledChanged();
}

void DE1Device::setSettings(SettingsHardware* settings) {
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

void DE1Device::setSimulatedIdleSteamTemp(double steamTempC) {
    if (!m_simulationMode) return;
    if (qFuzzyCompare(m_steamTemp, steamTempC)) return;
    m_steamTemp = steamTempC;
    // steamTemperature's NOTIFY is shotSampleReceived, so fire a sample to
    // wake QML bindings. Populate steamTemp + headTemp + mixTemp from our
    // cached members (so those properties aren't clobbered to zero); all
    // other sample fields (pressure, flow, goals, timer, frame) are
    // zero-initialized. The caller (DE1Simulator::setTargetSteamTemp) only
    // invokes this when idle — never during an active Steam/Espresso
    // session — so the zeros won't land on the live graphs.
    ShotSample sample;
    sample.timestamp = QDateTime::currentMSecsSinceEpoch();
    sample.steamTemp = m_steamTemp;
    sample.headTemp = m_headTemp;
    sample.mixTemp = m_mixTemp;
    emit shotSampleReceived(sample);
}

void DE1Device::emitSimulatedShotSample(const ShotSample& sample) {
    if (!m_simulationMode) return;

    m_pressure = sample.groupPressure;
    m_flow = sample.groupFlow;
    m_headTemp = sample.headTemp;
    m_mixTemp = sample.mixTemp;
    m_steamTemp = sample.steamTemp;
    m_goalPressure = sample.setPressureGoal;
    m_goalFlow = sample.setFlowGoal;
    m_goalTemperature = sample.setTempGoal;

    emit shotSampleReceived(sample);
}

// -- Connection management --

void DE1Device::connectToDevice(const QString& address) {
    QBluetoothDeviceInfo info(QBluetoothAddress(address), QString(), 0);
    connectToDevice(info);
}

void DE1Device::connectToDevice(const QBluetoothDeviceInfo& device) {
    // Don't reconnect if already connected or connecting
    if (isConnected()) {
        DEVICE_LOG(QStringLiteral("connectToDevice skipped — already connected"));
        return;
    }
    if (m_connecting) {
        DEVICE_LOG(QStringLiteral("connectToDevice skipped — already connecting"));
        return;
    }

    // Clean up any existing transport
    if (m_transport) {
        disconnect();
    }

    m_connecting = true;
    emit connectingChanged();

    // The one INFO for "an attempt started", at the one layer EVERY caller passes
    // through. BLEManager's scan and direct-wake paths announce themselves before
    // getting here, but the other two callers — the `devices_connect_de1` MCP tool
    // and the connections-page device list — do not, so an attempt initiated by an
    // assistant left nothing at INFO+ for that same assistant to read back.
    // BleTransport's equivalent stays DEBUG; see the note there.
    DEVICE_INFO(QStringLiteral("Connect attempt starting for %1")
                    .arg(getDeviceIdentifier(device)));

    // Create a new BleTransport and wire it up (DE1Device owns it)
    auto* bleTransport = new BleTransport(this);
    setTransport(bleTransport);
    m_ownsTransport = true;
    bleTransport->connectToDevice(device);
}

void DE1Device::disconnect() {
    // Surface any in-flight profile upload as a failure before we tear down
    // the transport — otherwise listeners (ProfileManager, QML) would never
    // see a resolution for this attempt.
    if (m_profileUploadInProgress) {
        finishProfileUpload(false, QStringLiteral("BLE disconnect during upload"));
    }
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
        // Only delete transports we created (connectToDevice). External
        // transports (USB via setTransport) are owned by their creator.
        if (m_ownsTransport) {
            m_transport->deleteLater();
        }
        m_transport = nullptr;
        m_ownsTransport = false;
    }

    m_connecting = false;
    emit connectingChanged();
    emit connectedChanged();
    emit guiEnabledChanged();
}

namespace {

// The DE1 runs each descale step for a FIXED time. Measured on firmware 1358 over
// two full descales, which agreed to the tenth of a second (720.1 s and 720.2 s
// total), with each step landing on a round number:
//
//     DescaleInit       30 s     DescaleGroup   120 s
//     DescaleFillGroup  30 s     DescaleSteam   420 s
//     DescaleReturn    120 s     ------------------------
//                               total          720 s
//
// So descale progress is not an estimate — it is a schedule, and the substate
// boundary resyncs it. Water hardness and tank temperature do not change these
// numbers; the two runs were on different days from different starting levels.
//
// If a machine ever disagrees, the boundaries are logged ([DE1][Descale]) on every
// run, so a submitted log says so directly rather than requiring a repro.
struct DescaleStep {
    DE1::SubState subState;
    int seconds;
};
constexpr DescaleStep kDescaleSchedule[] = {
    {DE1::SubState::DescaleInit,      30},
    {DE1::SubState::DescaleFillGroup, 30},
    {DE1::SubState::DescaleReturn,   120},
    {DE1::SubState::DescaleGroup,    120},
    {DE1::SubState::DescaleSteam,    420},
};
constexpr int kDescaleStepCount = static_cast<int>(std::size(kDescaleSchedule));

// Seconds of the schedule completed before the step at `index` (0-based) begins.
constexpr int descaleSecondsBefore(int index) {
    int total = 0;
    for (int i = 0; i < index && i < kDescaleStepCount; ++i) {
        total += kDescaleSchedule[i].seconds;
    }
    return total;
}
constexpr int kDescaleTotalSeconds = descaleSecondsBefore(kDescaleStepCount);

// 0-based position of a substate in the schedule, or -1 if it is not a descale step.
int descaleStepPosition(DE1::SubState subState) {
    for (int i = 0; i < kDescaleStepCount; ++i) {
        if (kDescaleSchedule[i].subState == subState) return i;
    }
    return -1;
}

// First firmware build that honours a cold maintenance request (descale / clean /
// air purge) on a machine with a GHC. Below it the firmware silently DROPS the
// request while the machine is still heating, so the button appears to do nothing.
// Matches Decaid's _kColdMaintenancePromotionMinFwBuild and de1app's onestep_cold
// workaround (machine.tcl:702-706, "the first (cold) Descale request is sometimes
// refused").
constexpr int kColdMaintenanceMinFirmwareBuild = 1356;

}  // namespace

// -- Parse methods --

void DE1Device::parseStateInfo(const QByteArray& data) {
    if (data.size() < 2) return;

    DE1::State newState = static_cast<DE1::State>(static_cast<uint8_t>(data[0]));
    DE1::SubState newSubState = static_cast<DE1::SubState>(static_cast<uint8_t>(data[1]));

    bool stateChanged = (newState != m_state);
    bool subStateChanged = (newSubState != m_subState);

    if (stateChanged) {
        // Water carried here so a phase change is self-contained. Only the corrected figure: the
        // "raw" half was this same number minus the constant sensor offset, reconstructed inline
        // rather than read from anywhere, which is as clear a demonstration as there could be that
        // it carried nothing.
        PHASE_LOG(QStringLiteral("%1 → %2 (water %3 mm, %4 ml)")
                      .arg(DE1::stateToString(m_state))
                      .arg(DE1::stateToString(newState))
                      .arg(m_waterLevelMm, 0, 'f', 1)
                      .arg(m_waterLevelMl));
    }

    // Descale step boundaries. The DE1 exposes no progress percentage and no
    // expected duration for a descale, so the only way to weight the five steps
    // (DescaleInit 8 .. DescaleSteam 12) is to measure them. Each boundary carries
    // the time in that step and the time since the descale began, which is what a
    // weight table is derived from; water level is carried too because the group
    // steps consume tank water and the steam step does not, so it separates them
    // when a boundary is missed.
    if (newState == DE1::State::Descale) {
        if (stateChanged) {
            m_descaleTimer.start();
            m_descaleStepStartMs = 0;
            m_descaleCycle = 1;
            if (!m_descaleTicker) {
                m_descaleTicker = new QTimer(this);
                m_descaleTicker->setInterval(1000);
                connect(m_descaleTicker, &QTimer::timeout, this, &DE1Device::updateDescaleProgress);
            }
            m_descaleTicker->start();
            DESCALE_LOG(QStringLiteral("start: cycle 1 %1 (water %2 ml)")
                            .arg(DE1::subStateToString(newSubState))
                            .arg(m_waterLevelMl));
        } else if (subStateChanged && m_descaleTimer.isValid()) {
            const qint64 nowMs = m_descaleTimer.elapsed();
            // A step number that does not increase means the firmware went back to an
            // earlier step, i.e. started another cycle. DescaleInit is not re-entered,
            // so the wrap is typically DescaleSteam back to DescaleFillGroup.
            const uint8_t from = static_cast<uint8_t>(m_subState);
            const uint8_t to = static_cast<uint8_t>(newSubState);
            const bool bothDescaleSteps = from >= static_cast<uint8_t>(DE1::SubState::DescaleInit)
                                          && from <= static_cast<uint8_t>(DE1::SubState::DescaleSteam)
                                          && to >= static_cast<uint8_t>(DE1::SubState::DescaleInit)
                                          && to <= static_cast<uint8_t>(DE1::SubState::DescaleSteam);
            // Only a step-to-step move counts. The machine drops to Ready between the
            // last cycle and leaving Descale, and that is an ending, not a restart.
            const bool wrapped = bothDescaleSteps && to <= from;
            if (wrapped) {
                ++m_descaleCycle;
            }
            DESCALE_LOG(QStringLiteral("cycle %1 %2 → %3 after %4 s (t=%5 s, water %6 ml)%7")
                            .arg(m_descaleCycle)
                            .arg(DE1::subStateToString(m_subState))
                            .arg(DE1::subStateToString(newSubState))
                            .arg((nowMs - m_descaleStepStartMs) / 1000.0, 0, 'f', 1)
                            .arg(nowMs / 1000.0, 0, 'f', 1)
                            .arg(m_waterLevelMl)
                            .arg(wrapped ? QStringLiteral(" [new cycle]") : QString()));
            m_descaleStepStartMs = nowMs;
        }
    } else if (stateChanged && m_state == DE1::State::Descale && m_descaleTimer.isValid()) {
        if (m_descaleTicker) m_descaleTicker->stop();
        DESCALE_LOG(QStringLiteral("end: %1 cycles, last step %2 after %3 s, total %4 s (water %5 ml)")
                        .arg(m_descaleCycle)
                        .arg(DE1::subStateToString(m_subState))
                        .arg((m_descaleTimer.elapsed() - m_descaleStepStartMs) / 1000.0, 0, 'f', 1)
                        .arg(m_descaleTimer.elapsed() / 1000.0, 0, 'f', 1)
                        .arg(m_waterLevelMl));
        m_descaleTimer.invalidate();
    }

    m_state = newState;
    m_subState = newSubState;

    // After the new substate is committed, so the progress reflects the step the
    // machine is in now rather than the one it just left.
    if (stateChanged || subStateChanged) {
        updateDescaleProgress();
    }

    // A maintenance request held back because the machine was cold goes out as soon
    // as the machine reports it is no longer heating.
    if (m_pendingMaintenanceState != DE1::State::NoRequest && (stateChanged || subStateChanged)) {
        flushPendingMaintenanceState();
    }

    if (stateChanged) {
        emit this->stateChanged();
    }
    if (subStateChanged) {
        emit this->subStateChanged();
    }
}

int DE1Device::descaleStepCount() const {
    return kDescaleStepCount;
}

void DE1Device::updateDescaleProgress() {
    const double oldProgress = m_descaleProgress;
    const int oldStep = m_descaleStepIndex;
    const int oldRemaining = m_descaleSecondsRemaining;

    const int position = (m_state == DE1::State::Descale) ? descaleStepPosition(m_subState) : -1;
    if (position < 0 || !m_descaleTimer.isValid()) {
        // Not in a descale step: Descale/Ready at the very start or the very end, or
        // not descaling at all. Report no progress rather than a stale figure.
        m_descaleProgress = 0.0;
        m_descaleStepIndex = 0;
        m_descaleSecondsRemaining = 0;
    } else {
        const double stepElapsed =
            qBound(0.0,
                   (m_descaleTimer.elapsed() - m_descaleStepStartMs) / 1000.0,
                   static_cast<double>(kDescaleSchedule[position].seconds));
        const double done = descaleSecondsBefore(position) + stepElapsed;
        // Never reaches 1.0 from the schedule alone. The bar reads 100% only when
        // the machine actually leaves Descale, which is the whole defect this
        // replaces: the old bar showed 100% for the 420 s of the final step.
        m_descaleProgress = qBound(0.0, done / kDescaleTotalSeconds, 0.999);
        m_descaleStepIndex = position + 1;
        m_descaleSecondsRemaining = qMax(0, qRound(kDescaleTotalSeconds - done));
    }

    if (!qFuzzyCompare(oldProgress + 1.0, m_descaleProgress + 1.0)
        || oldStep != m_descaleStepIndex
        || oldRemaining != m_descaleSecondsRemaining) {
        emit descaleProgressChanged();
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
        sample.setMixTempGoal = BinaryCodec::decodeShortBE(data, 11) / 256.0;  // SetMixTemp
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
        sample.setMixTempGoal = BinaryCodec::decodeShortBE(data, 8) / 256.0;  // SetMixTemp
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
    m_goalPressure = sample.setPressureGoal;
    m_goalFlow = sample.setFlowGoal;
    m_goalTemperature = sample.setTempGoal;

    emit shotSampleReceived(sample);
}

void DE1Device::parseWaterLevel(const QByteArray& data) {
    if (data.size() < 2) return;

    // Convert raw sensor reading to mm (U16P8 format: divide by 256)
    double rawMm = BinaryCodec::decodeU16P8(BinaryCodec::decodeShortBE(data, 0));

    // Apply sensor offset correction (sensor is mounted 5mm above water intake)
    constexpr double SENSOR_OFFSET = 5.0;
    const double sampleMm = rawMm + SENSOR_OFFSET;

    // Low-pass the sample before anything reads it. See WATER_LEVEL_SMOOTHING_ALPHA in the header
    // for why this is a filter and not a bigger threshold.
    if (!m_waterLevelSeeded) {
        m_waterLevelMm = sampleMm;
        m_waterLevelSeeded = true;
    } else {
        m_waterLevelMm += WATER_LEVEL_SMOOTHING_ALPHA * (sampleMm - m_waterLevelMm);
    }

    // Lookup table from de1app CAD data (mm index → ml volume)
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

    // Calculate percentage from volume: 0% = empty, 100% = full (40mm = 1104ml)
    // Uses ml (volume) rather than mm (height) so the percentage reflects actual
    // tank fullness and is independent of the refill warning threshold.
    constexpr int FULL_ML = 1104;  // mmToMl[40], matching de1app water_level_full_point
    m_waterLevel = qBound(0.0, (static_cast<double>(m_waterLevelMl) / FULL_ML) * 100.0, 100.0);

    // Only emit when water level changes by at least 0.5% or ml changes
    if (qAbs(m_waterLevel - m_lastEmittedWaterLevel) >= 0.5
        || m_waterLevelMl != m_lastEmittedWaterLevelMl) {
        // Log on a coarser gate than the emit — see m_lastLoggedWaterLevelMm. 2 mm is above the
        // smoothed signal's residual dither and below anything a person would call a change in
        // level: a refill, a shot's worth of draw, or the tank running down all clear it.
        if (qAbs(m_waterLevelMm - m_lastLoggedWaterLevelMm) >= WATER_LEVEL_LOG_HYSTERESIS_MM) {
            // What the line has to answer is "what happened to the water", and neither a bare
            // reading nor a pair of readings answers it — 564 ml is not a fact anyone can act on
            // without knowing it was 732 ml a moment ago and that the machine was pulling a shot.
            // So: the move, and the state that caused it.
            //
            // `raw` is deliberately gone. It was `displayed` minus a compile-time constant
            // (SENSOR_OFFSET), so the two could never disagree and printing both doubled the width
            // of every line to say one thing twice. The Phase line proved it by reconstructing raw
            // as `m_waterLevelMm - 5.0` rather than reading a stored value.
            //
            // The state is the machine's, NOT a classification of the event. The tempting version
            // of this line tags a large positive delta "[refill]" — but that is a guess with a
            // threshold behind it, and a reader given "+616 ml [Idle]" can draw the same conclusion
            // from evidence instead of being told it.
            if (m_lastLoggedWaterLevelMl < 0) {
                WATER_LOG(QStringLiteral("%1 ml (%2 mm) first reading [%3]")
                              .arg(m_waterLevelMl)
                              .arg(m_waterLevelMm, 0, 'f', 1)
                              .arg(DE1::stateToString(m_state)));
            } else {
                const int deltaMl = m_waterLevelMl - m_lastLoggedWaterLevelMl;
                WATER_LOG(QStringLiteral("%1 -> %2 ml (%3%4 ml, %5 -> %6 mm) [%7]")
                              .arg(m_lastLoggedWaterLevelMl)
                              .arg(m_waterLevelMl)
                              .arg(deltaMl >= 0 ? QStringLiteral("+") : QString())
                              .arg(deltaMl)
                              .arg(m_lastLoggedWaterLevelMm, 0, 'f', 1)
                              .arg(m_waterLevelMm, 0, 'f', 1)
                              .arg(DE1::stateToString(m_state)));
            }
            m_lastLoggedWaterLevelMm = m_waterLevelMm;
            m_lastLoggedWaterLevelMl = m_waterLevelMl;
        }
        m_lastEmittedWaterLevel = m_waterLevel;
        m_lastEmittedWaterLevelMl = m_waterLevelMl;
        emit waterLevelChanged();
    }
}

void DE1Device::parseVersion(const QByteArray& data) {
    if (data.size() < 10) return;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.constData());

    // BLE: APIVersion(1), Release(1), Commits(2), Changes(1), Sha(4)
    int bleApi = d[0];
    double bleRelease = BinaryCodec::decodeF8_1_7(d[1]);
    uint16_t bleCommits = (static_cast<uint16_t>(d[2]) << 8) | d[3];
    int bleChanges = d[4];
    // FW: APIVersion(1), Release(1), Commits(2), Changes(1), Sha(4)
    int fwApi = d[5];
    double fwRelease = BinaryCodec::decodeF8_1_7(d[6]);
    uint16_t fwCommits = (static_cast<uint16_t>(d[7]) << 8) | d[8];
    int fwChanges = d[9];

    // Line 1: BLE version (matches de1app format)
    m_firmwareVersion = QString("BLE v%1.%2.%3, API v%4")
        .arg(bleRelease, 0, 'f', 1).arg(bleChanges).arg(bleCommits).arg(bleApi);
    // Line 2: FW version (only if different SHA, matching de1app)
    m_firmwareVersion += QString("\nFW v%1.%2.%3, API v%4")
        .arg(fwRelease, 0, 'f', 1).arg(fwChanges).arg(fwCommits).arg(fwApi);
    emit firmwareVersionChanged();

    // NOTE: sendInitialSettings() is now called from onTransportConnected()
    // instead of here, to ensure write characteristics are available first.
    // On some platforms (Linux/BlueZ), VERSION arrives before the service
    // reaches RemoteServiceDiscovered state, causing MMR writes to fail.
}

void DE1Device::rebuildVersionLine3() {
    // Build the third line: "pcb=1.3, model=DE1PRO, firmware=v1342"
    // Only emit once we have at least the firmware build number
    if (m_firmwareBuildNumber == 0) return;

    // Strip any existing third line
    int nlCount = 0;
    int cutPos = -1;
    for (int i = 0; i < m_firmwareVersion.size(); ++i) {
        if (m_firmwareVersion[i] == QLatin1Char('\n')) {
            nlCount++;
            if (nlCount == 2) { cutPos = i; break; }
        }
    }
    if (cutPos >= 0)
        m_firmwareVersion = m_firmwareVersion.left(cutPos);

    static const QStringList modelNames = {
        QStringLiteral("unknown"),  // 0
        QStringLiteral("DE1"),      // 1
        QStringLiteral("DE1+"),     // 2
        QStringLiteral("DE1PRO"),   // 3
        QStringLiteral("DE1XL"),    // 4
        QStringLiteral("DE1CAFE"),  // 5
        QStringLiteral("DE1XXL"),   // 6
        QStringLiteral("DE1XXXL")   // 7
    };

    QStringList parts;
    if (m_cpuBoardModel > 0) {
        parts << QString("pcb=%1").arg(m_cpuBoardModel / 1000.0, 0, 'f', 1);
    }
    if (m_machineModel > 0 && m_machineModel < modelNames.size()) {
        parts << QString("model=%1").arg(modelNames[m_machineModel]);
    }
    parts << QString("firmware=v%1").arg(m_firmwareBuildNumber);

    m_firmwareVersion += QStringLiteral("\n") + parts.join(QStringLiteral(", "));
    emit firmwareVersionChanged();

    // INFO: firmware version, serial and model are the first thing anyone asks
    // for when triaging a machine report.
    DEVICE_INFO(QStringLiteral("Machine info: %1").arg(parts.join(QStringLiteral(", "))));
}

void DE1Device::sendMMRReadRequest(uint32_t address) const {
    if (!m_transport) return;
    QByteArray req(20, 0);
    req[0] = 0x00;  // Len = 0 means "read 4 bytes"
    req[1] = static_cast<char>((address >> 16) & 0xFF);
    req[2] = static_cast<char>((address >> 8) & 0xFF);
    req[3] = static_cast<char>(address & 0xFF);
    m_transport->write(DE1::Characteristic::READ_FROM_MMR, req);
}

void DE1Device::issueMMRReadWithRetry(uint32_t address, const QString& reason) {
    if (!m_transport) return;
    sendMMRReadRequest(address);
    m_pendingMMRReads.insert(address, PendingMMRRead{
        MMR_READ_MAX_RETRIES, monotonicMsNow() + MMR_READ_TIMEOUT_MS, reason});
    if (!m_mmrReadRetryTimer.isActive()) {
        m_mmrReadRetryTimer.start();
    }
}

void DE1Device::checkMMRReadTimeouts() {
    const qint64 now = monotonicMsNow();

    // Collect first — retrying/expiring while iterating the hash would
    // invalidate the iterator.
    QList<uint32_t> toRetry;
    QList<uint32_t> toExpire;
    for (auto it = m_pendingMMRReads.constBegin(); it != m_pendingMMRReads.constEnd(); ++it) {
        if (now < it.value().deadlineMs) continue;
        if (it.value().attemptsRemaining > 0) toRetry << it.key();
        else toExpire << it.key();
    }

    for (uint32_t address : toRetry) {
        auto it = m_pendingMMRReads.find(address);
        it.value().attemptsRemaining--;
        MMR_WARN(QStringLiteral("read timeout, retrying (%1 left): 0x%2 [%3]")
                     .arg(it.value().attemptsRemaining)
                     .arg(address, 6, 16, QLatin1Char('0'))
                     .arg(it.value().reason));
        sendMMRReadRequest(address);
        it.value().deadlineMs = now + MMR_READ_TIMEOUT_MS;
    }

    for (uint32_t address : toExpire) {
        const QString reason = m_pendingMMRReads.value(address).reason;
        MMR_WARN(QStringLiteral("read FAILED after retries: 0x%1 [%2] — leaving "
                                "existing/default value")
                     .arg(address, 6, 16, QLatin1Char('0'))
                     .arg(reason));

        // GHC_INFO is the one exhausted read with a behavioral (not just
        // display) consequence: isHeadless stays at its permissive default, so
        // the app shows in-app start controls whose availability it could not
        // actually confirm with the machine. Spell that out — the other reads
        // (identity strings, heater voltage, refill-kit) only affect display.
        if (address == DE1::MMR::GHC_INFO) {
            MMR_WARN(QStringLiteral(
                "GHC status unconfirmed after retries — in-app start availability reflects "
                "the permissive default (isHeadless=true), not a confirmed machine state"));
        }

        m_pendingMMRReads.remove(address);
    }

    if (m_pendingMMRReads.isEmpty()) {
        m_mmrReadRetryTimer.stop();
    }
}

void DE1Device::parseMMRResponse(const QByteArray& data) {
    if (data.size() < 5) return;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.constData());

    // Extract address (big endian)
    uint32_t address = (static_cast<uint32_t>(d[1]) << 16) |
                       (static_cast<uint32_t>(d[2]) << 8) |
                       static_cast<uint32_t>(d[3]);

    // Check if this is GHC_INFO response
    if (address == DE1::MMR::GHC_INFO) {
        uint8_t ghcStatus = d[4];

        // de1app does not enumerate ghc_is_installed values either; only its
        // ghc_required() allowlist {0,1,2,4} is authoritative. Value 7 is the
        // common "GHC active" code on modern DE1+ hardware (and the default in
        // de1app's simulation files). Treat it the same as 3 here.
        QString statusName;
        switch (ghcStatus) {
            case 0: statusName = "not installed"; break;
            case 1: statusName = "unused"; break;
            case 2: statusName = "inactive"; break;
            case 3: statusName = "active"; break;
            case 4: statusName = "debug"; break;
            case 7: statusName = "active"; break;
            default: statusName = QString("unknown (%1)").arg(ghcStatus); break;
        }

        bool canStartFromApp = (ghcStatus == 0 || ghcStatus == 1 || ghcStatus == 2 || ghcStatus == 4);
        // INFO: whether the app may start operations at all is the single most
        // consequential thing this device reports about itself.
        const QString logMsg = QStringLiteral("GHC status: %1 → app %2 start operations")
                                   .arg(statusName)
                                   .arg(canStartFromApp ? "CAN" : "CANNOT");

        DEVICE_INFO(logMsg);

        if (m_isHeadless != canStartFromApp) {
            m_isHeadless = canStartFromApp;
            emit isHeadlessChanged();
        }
    }
    // CPU board model (address 0x800008) — e.g. 1300 = pcb 1.3
    else if (address == DE1::MMR::CPU_BOARD_MODEL) {
        if (data.size() >= 8) {
            uint32_t val = (static_cast<uint32_t>(static_cast<uint8_t>(d[7])) << 24) |
                           (static_cast<uint32_t>(static_cast<uint8_t>(d[6])) << 16) |
                           (static_cast<uint32_t>(static_cast<uint8_t>(d[5])) << 8) |
                           static_cast<uint32_t>(static_cast<uint8_t>(d[4]));
            m_cpuBoardModel = val;
            rebuildVersionLine3();
        }
    }
    // Machine model (address 0x80000C) — 1=DE1, 2=DE1+, 3=DE1PRO, etc.
    else if (address == DE1::MMR::MACHINE_MODEL) {
        if (data.size() >= 8) {
            uint32_t val = (static_cast<uint32_t>(static_cast<uint8_t>(d[7])) << 24) |
                           (static_cast<uint32_t>(static_cast<uint8_t>(d[6])) << 16) |
                           (static_cast<uint32_t>(static_cast<uint8_t>(d[5])) << 8) |
                           static_cast<uint32_t>(static_cast<uint8_t>(d[4]));
            m_machineModel = static_cast<int>(val);
            rebuildVersionLine3();
        }
    }
    // Firmware build number (address 0x800010) — e.g. 1342 for "v1342"
    else if (address == DE1::MMR::FIRMWARE_VERSION) {
        if (data.size() >= 8) {
            uint32_t val = (static_cast<uint32_t>(static_cast<uint8_t>(d[7])) << 24) |
                           (static_cast<uint32_t>(static_cast<uint8_t>(d[6])) << 16) |
                           (static_cast<uint32_t>(static_cast<uint8_t>(d[5])) << 8) |
                           static_cast<uint32_t>(static_cast<uint8_t>(d[4]));
            m_firmwareBuildNumber = static_cast<int>(val);
            rebuildVersionLine3();
        }
    }
    // Heater voltage (address 0x803834) — e.g. 110, 120, 220, 230
    else if (address == DE1::MMR::HEATER_VOLTAGE) {
        if (data.size() >= 8) {
            uint32_t val = (static_cast<uint32_t>(static_cast<uint8_t>(d[7])) << 24) |
                           (static_cast<uint32_t>(static_cast<uint8_t>(d[6])) << 16) |
                           (static_cast<uint32_t>(static_cast<uint8_t>(d[5])) << 8) |
                           static_cast<uint32_t>(static_cast<uint8_t>(d[4]));
            int voltage = static_cast<int>(val);
            if (voltage != m_heaterVoltage) {
                m_heaterVoltage = voltage;
                DEVICE_INFO(QStringLiteral("Heater voltage: %1 V").arg(m_heaterVoltage));
                emit heaterVoltageChanged();
            }
        }
    }
    // Check if this is REFILL_KIT response (address 0x80385C)
    else if (address == DE1::MMR::REFILL_KIT) {
        uint8_t kitStatus = d[4];

        int detected = (kitStatus > 0) ? 1 : 0;
        QString statusName = detected ? "detected" : "not detected";

        DEVICE_INFO(QStringLiteral("Refill kit: %1").arg(statusName));

        if (m_refillKitDetected != detected) {
            m_refillKitDetected = detected;
            emit refillKitDetectedChanged();
        }
    }

    // A response for this address arrived (whether or not its content matched
    // what the address-specific handling above expected) — clear the "no
    // response at all" timeout tracking. This sits below every other handler so
    // they still see the response first (additive, no early return).
    auto pendingReadIt = m_pendingMMRReads.find(address);
    if (pendingReadIt != m_pendingMMRReads.end()) {
        m_pendingMMRReads.erase(pendingReadIt);
        if (m_pendingMMRReads.isEmpty()) {
            m_mmrReadRetryTimer.stop();
        }
    }
}

// -- Machine control methods (delegate through transport) --

void DE1Device::requestState(DE1::State state) {
#ifdef DECENZA_SIMULATOR
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
        case DE1::State::Descale:
            m_simulator->startDescale();
            break;
        case DE1::State::Clean:
            m_simulator->startClean();
            break;
        case DE1::State::AirPurge:
            m_simulator->startAirPurge();
            break;
        case DE1::State::SkipToNext:
            m_simulator->skipFrame();
            break;
        default:
            break;
        }
        return;
    }
#endif

    if (!m_transport) return;
    if (dropDeviceWriteIfFirmwareFlash("requestState")) return;
    QByteArray data(1, static_cast<char>(state));
    m_transport->write(DE1::Characteristic::REQUESTED_STATE, data);
}

void DE1Device::startEspresso() {
    // One log for every start surface (profile/recipe pills, MCP, GHC sim): if a
    // shot fails to start, the debug log shows whether the BLE command was even
    // issued and the machine state at the time. The gate that can block BEFORE
    // this point (isHeadless / machine-ready) is logged QML-side.
    DEVICE_LOG(QStringLiteral("startEspresso: state=%1 headless=%2 sim=%3")
                   .arg(DE1::stateToString(m_state))
                   .arg(m_isHeadless ? "yes" : "no")
                   .arg(m_simulationMode ? "yes" : "no"));

    // Settle window after a profile upload before the Espresso state change is
    // allowed through. The DE1 firmware writes the shot descriptor to internal
    // flash on the final frame/tail write and only clears its internal
    // "download in progress" flag when that flash write returns — a
    // state=Espresso request that arrives first makes the firmware abort to
    // HeaterDown right after preinfusion (matches Decaid's profileDownloadGuard
    // and its cited firmware bug report). The flash write is not observable over
    // BLE, so this is a genuine timing gap with no event to key on — a fixed
    // settle window is a deliberate, documented exception to the project's
    // "no timers as guards" rule (see the harden-de1-ble-reliability design,
    // Decision 5). Recipe activation uploads the profile and then immediately
    // calls startEspresso(), so this is the common path, not an edge case.
    // Skip in simulation (no real firmware to race).
    if (!m_simulationMode && !m_espressoStartDeferred && m_lastProfileUploadCompleteMs > 0) {
        const qint64 sinceUploadMs = monotonicMsNow() - m_lastProfileUploadCompleteMs;
        if (sinceUploadMs < PROFILE_UPLOAD_SETTLE_MS) {
            const int remainingMs =
                static_cast<int>(PROFILE_UPLOAD_SETTLE_MS - sinceUploadMs);
            DEVICE_LOG(QStringLiteral("startEspresso: deferring %1 ms for profile-upload settle")
                           .arg(remainingMs));
            // Arm a single deferred start via the stoppable member timer. The
            // flag (not zeroing the timestamp) is what a concurrent second
            // startEspresso() checks below — so a double-tap inside the window
            // can't slip an immediate start past the settle. The timer's handler
            // clears the flag and re-runs; by then the window has elapsed (or,
            // if a newer upload landed, re-defers). A disconnect cancels it (see
            // onTransportDisconnected) so a queued shot can't fire on a fresh
            // reconnect the user never requested a shot on.
            m_espressoStartDeferred = true;
            m_espressoSettleTimer.start(remainingMs);
            return;
        }
    }

    // A deferred start is already armed — don't issue a second immediate start
    // racing it (would defeat the settle window above).
    if (m_espressoStartDeferred) return;

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
    requestMaintenanceState(DE1::State::Descale);
}

void DE1Device::startClean() {
    requestMaintenanceState(DE1::State::Clean);
}

// Descale, Clean and AirPurge share one failure: on a GHC machine running firmware
// below 1356, the firmware DROPS the request while the machine is still heating. The
// button does nothing, reports nothing, and the user is left tapping it. Route all
// three through here so the workaround cannot be added to one and forgotten on the
// others — which is how it stood: three identical one-line bodies, none of them
// handling it.
void DE1Device::requestMaintenanceState(DE1::State state) {
    if (applyColdMaintenanceWorkaround(state)) {
        return;  // Deferred; goes out when the machine reports it has left preheat.
    }
    requestState(state);
}

// True while the machine is still coming up to temperature — the window in which old
// firmware discards a maintenance request. Substate carries this during Heating; the
// Espresso-preheat substate is included because the machine reports it from Idle too.
bool DE1Device::isMachineHeating() const {
    return m_state == DE1::State::Busy
           || m_subState == DE1::SubState::Heating
           || m_subState == DE1::SubState::FinalHeating
           || m_subState == DE1::SubState::Stabilising;
}

// Returns true when the request was DEFERRED. Mirrors de1app's onestep_cold
// (machine.tcl:702-706) and Decaid's _prepareColdMaintenanceWorkaround: load a
// profile whose group target is 1°C and whose tank target is 0, which makes the
// machine stop preheating, then send the state once it has.
//
// de1app and Decaid both then wait a fixed second. We wait for the machine to SAY it
// left preheat instead — same intent, no timer, and a slow machine is not raced.
bool DE1Device::applyColdMaintenanceWorkaround(DE1::State state) {
    // firmwareBuildNumber() is 0 until the MMR identity read returns (MMR::FIRMWARE_VERSION
    // in parseMMRRead) — a few seconds after connect, or never on a machine whose MMR reads
    // fail. Unknown counts as OLD, matching Decaid and de1app.
    //
    // Not a coin flip: a machine new enough to honour a cold maintenance request is new
    // enough to REPORT its build, so a missing build number is itself evidence of an old
    // or unhealthy machine. Applying the workaround is the safe direction.
    //
    // The costs agree. Assuming new when the machine is old drops the request silently —
    // the button does nothing, with no error, which is the defect this function exists to
    // remove. Assuming old when the machine is new uploads a throwaway 1C profile and waits
    // for preheat to end; the descale still runs, and DescalingPage re-uploads the real
    // profile on exit.
    const int build = firmwareBuildNumber();
    const bool firmwareDropsColdRequests = build < kColdMaintenanceMinFirmwareBuild;
    const bool ghcPresent = !isHeadless();
    if (!firmwareDropsColdRequests || !ghcPresent || !isMachineHeating()) {
        return false;
    }

    DEVICE_INFO(QStringLiteral("Cold maintenance (%1) on GHC machine, firmware build %2 < %3: "
                               "loading 1C profile and deferring the request until preheat ends")
                    .arg(DE1::stateToString(state))
                    .arg(build == 0 ? QStringLiteral("unknown") : QString::number(build))
                    .arg(kColdMaintenanceMinFirmwareBuild));

    Profile coldProfile;
    coldProfile.setTitle(QStringLiteral("Decenza cold maintenance"));
    coldProfile.setEspressoTemperature(1.0);
    coldProfile.setTankDesiredWaterTemperature(0.0);
    ProfileFrame frame;
    frame.name = QStringLiteral("cold");
    frame.temperature = 1.0;
    frame.pump = QStringLiteral("flow");
    frame.flow = 0.0;
    frame.seconds = 1.0;
    coldProfile.setSteps({frame});
    uploadProfile(coldProfile);

    m_pendingMaintenanceState = state;
    return true;
}

// The deferred half of applyColdMaintenanceWorkaround. Called from parseStateInfo on
// every state/substate change, so the request goes out on the first packet showing
// the machine is no longer heating.
void DE1Device::flushPendingMaintenanceState() {
    if (m_pendingMaintenanceState == DE1::State::NoRequest || isMachineHeating()) {
        return;
    }
    const DE1::State pending = m_pendingMaintenanceState;
    m_pendingMaintenanceState = DE1::State::NoRequest;
    DEVICE_INFO(QStringLiteral("Machine left preheat — sending deferred %1 request")
                    .arg(DE1::stateToString(pending)));
    requestState(pending);
}

void DE1Device::startAirPurge() {
    requestMaintenanceState(DE1::State::AirPurge);
}

void DE1Device::stopOperation() {
    requestState(DE1::State::Idle);
}

void DE1Device::stopOperationUrgent() {
    stopOperationUrgent(0);
}

void DE1Device::customEvent(QEvent* event) {
    if (event->type() == SawStopEvent::eventType()) {
        auto* e = static_cast<SawStopEvent*>(event);
        stopOperationUrgent(e->sawTriggerMs());
    }
}

void DE1Device::stopOperationUrgent(qint64 sawTriggerMs) {
#ifdef DECENZA_SIMULATOR
    if (m_simulationMode && m_simulator) {
        m_simulator->stop();
        return;
    }
#endif
    if (!m_transport) return;
    if (dropDeviceWriteIfFirmwareFlash("stopOperationUrgent")) return;
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

bool DE1Device::goToSleep() {
#ifdef DECENZA_SIMULATOR
    if (m_simulationMode && m_simulator) {
        m_simulator->goToSleep();
        return true;
    }
#endif

    // Never send sleep during a firmware flash: writing to REQUESTED_STATE
    // mid-flash can disrupt the bootloader and risk bricking. The MMR-write
    // guard catches MMR traffic but sleep uses a direct writeUrgent on
    // REQUESTED_STATE and would otherwise bypass it.
    if (m_firmwareFlashInProgress) {
        DEVICE_WARN(QStringLiteral("Sleep requested during firmware flash, dropping"));
        return false;
    }

    // If a profile upload is in progress, defer sleep until it completes.
    if (m_profileUploadInProgress) {
        DEVICE_LOG(QStringLiteral("Sleep requested during profile upload, deferring until "
                                  "upload completes"));
        m_sleepPendingAfterUpload = true;
        return false;
    }

    if (!m_transport) return false;
    // Clear pending commands - sleep takes priority. Only drop the MMR
    // cache if something was actually queued — an empty queue means the
    // cache still matches what we've sent. Avoids a spurious re-send of
    // steam/flush MMR on every steam end (where flow-begin defensively
    // calls us through clearCommandQueue but nothing is pending).
    const qsizetype dropped = m_transport->clearQueue();
    if (dropped > 0) {
        m_lastMMRValues.clear();
    }

    // Send sleep command directly (don't queue it)
    QByteArray data(1, static_cast<char>(DE1::State::Sleep));
    m_transport->writeUrgent(DE1::Characteristic::REQUESTED_STATE, data);
    return true;
}

void DE1Device::wakeUp() {
    requestState(DE1::State::Idle);
}

void DE1Device::clearCommandQueue() {
    if (m_profileUploadInProgress) {
        finishProfileUpload(false, QStringLiteral("command queue cleared during upload"));
    }
    m_sleepPendingAfterUpload = false;
    m_sawStopWritePending = false;
    m_lastSawTriggerMs = 0;
    m_lastSawWriteMs = 0;
    // Dropping the transport queue discards pending MMR writes whose values
    // are already recorded in m_lastMMRValues, so the cache would silently
    // elide the next retry. Only invalidate the cache if something was
    // actually dropped — the call sites here (flow-begin,
    // onEspressoCycleStarted, stopOperationUrgent) fire defensively whether
    // or not writes are pending, and clearing on every call would cost 3
    // spurious MMR writes per steam/hot-water session.
    if (m_transport) {
        const qsizetype dropped = m_transport->clearQueue();
        if (dropped > 0) {
            m_lastMMRValues.clear();
        }
    }
}

void DE1Device::uploadProfile(const Profile& profile) {
#ifdef DECENZA_SIMULATOR
    if (m_simulationMode && m_simulator) {
        m_simulator->setProfile(profile);
    }
#endif

    if (!m_transport) return;
    if (dropDeviceWriteIfFirmwareFlash("uploadProfile")) {
        // Emit so ProfileManager's m_uploadInFlight gate is always released — without
        // this, the gate stays permanently stuck if the call is dropped here.
        emit profileUploaded(false, QStringLiteral("firmware flash in progress"));
        return;
    }

    // Attach the ACK listener BEFORE queuing writes so we observe every
    // writeComplete for this upload.
    QList<QByteArray> frames = profile.toFrameBytes();
    startProfileUploadTracking(profile.title(), frames);

    m_transport->write(DE1::Characteristic::HEADER_WRITE, profile.toHeaderBytes());
    for (const QByteArray& frame : frames) {
        m_transport->write(DE1::Characteristic::FRAME_WRITE, frame);
    }
    writeTankPreheatForProfile(profile);
}

// Tank preheat follows the active profile: de1app's de1_send_shot_frames
// writes this MMR with every shot-frame send — the profile's
// tank_desired_water_temperature for advanced (settings_2c) profiles, 0 for
// all others — so each upload overrides or clears the previous preheat.
// de1app's gate is an artifact of its UI (only advanced profiles can edit
// the field); we honor the parsed field on every profile type. 0 disables
// preheat; 45 is de1app's range-check ceiling. writeMMR's dedup cache
// elides repeat uploads of the same value.
void DE1Device::writeTankPreheatForProfile(const Profile& profile) {
    const uint32_t tankTemp = static_cast<uint32_t>(
        qBound(0, qRound(profile.tankDesiredWaterTemperature()), 45));
    writeMMR(DE1::MMR::TANK_TEMP_THRESHOLD, tankTemp,
             QStringLiteral("profile tank preheat"));
}

// -- Profile upload frame-ACK verification --
//
// The DE1's write-with-response ACK for FRAME_WRITE echoes the leading byte
// (FrameToWrite) of the frame that was accepted. de1app uses that echo to
// verify frames were not silently dropped or reordered — see
// confirm_de1_send_shot_frames_worked in de1app's de1_comms.tcl. We mirror
// that here: record the expected leading bytes, collect them from each ACK,
// and on completion verify the two lists match exactly.

void DE1Device::onWriteAbandoned(const QBluetoothUuid& uuid, const QByteArray& data) {
    if (uuid != DE1::Characteristic::WRITE_TO_MMR || data.size() < 4) return;

    // m_lastMMRValues is recorded at DISPATCH (writeMMR, below), so it says
    // "what we tried to send", not "what the DE1 has". Those are the same thing
    // right up until a write is abandoned — and then the cache is a standing
    // false claim: the next write of that same value is elided as unchanged
    // (":1566"), so the setting becomes unreachable for the rest of the
    // connection with nothing on screen to say so. Dropping the entry here
    // turns a permanent loss back into a transient one.
    //
    // This matters more than it used to. Read-back verification used to catch
    // exactly this case for the one register that had it and logged
    // "verify FAILED for 0x803828 expected=N"; that is gone, and the retry
    // budget is shorter, so abandonment is both more likely and no longer
    // announced anywhere else.
    const uint32_t address = (static_cast<uint32_t>(static_cast<uint8_t>(data[1])) << 16)
                           | (static_cast<uint32_t>(static_cast<uint8_t>(data[2])) << 8)
                           |  static_cast<uint32_t>(static_cast<uint8_t>(data[3]));

    m_lastMMRValues.remove(address);

    // The transport's own line can only say "20 bytes to a005". Name the
    // register, because this is now the only record that a specific machine
    // setting did not land, and these logs are read by users and their AI
    // assistants who cannot decode an MMR payload from a byte count.
    MMR_WARN(QStringLiteral("write ABANDONED for 0x%1 — the setting did not reach "
                            "the machine. Cached value dropped so a later write of "
                            "the same value is not skipped as unchanged.")
                 .arg(address, 6, 16, QLatin1Char('0')));
}

qsizetype DE1Device::discardQueuedProfileWrites() {
    if (!m_transport) return 0;

    // HEADER_WRITE and FRAME_WRITE carry profile frames and nothing else, and
    // uploadProfile() is now their only producer, so the characteristic alone
    // identifies this operation's work. That was NOT true when this was
    // written: writeHeader()/writeFrame() were public slots writing the same
    // two characteristics outside any upload tracker, which would have made
    // this function entitled to discard work it did not own. They had no
    // callers anywhere and were deleted in the same change, so the invariant
    // is structural rather than a coincidence of the current call graph. Deliberately
    // NOT the tank-preheat MMR write that uploadProfile() issues after the
    // frames: it goes to WRITE_TO_MMR alongside unrelated settings, and it is
    // idempotent — the next upload records its own value in m_lastMMRValues, so
    // a stale queued preheat is at worst one superseded write, never a wrong
    // final state.
    //
    // That is also why this needs no m_lastMMRValues invalidation, which
    // clearCommandQueue() does pair with its clear (:1205-1226). The dedup
    // cache only covers MMR registers, and no MMR write is discarded here.
    const qsizetype dropped = m_transport->discardQueued(
        {DE1::Characteristic::HEADER_WRITE, DE1::Characteristic::FRAME_WRITE});
    if (dropped > 0) {
        DEVICE_LOG(QStringLiteral("Discarded %1 queued profile write(s) from a "
                                  "superseded or failed upload")
                       .arg(dropped));
    }
    return dropped;
}

void DE1Device::startProfileUploadTracking(const QString& profileTitle,
                                           const QList<QByteArray>& frames)
{
    // Cancel any still-in-flight tracker. This should be rare (the guard is
    // primarily for defensive coding against callers that re-issue an upload
    // before the previous one drained), but if it happens we want to surface
    // the earlier attempt as a failure rather than silently drop it.
    if (m_profileUploadInProgress) {
        finishProfileUpload(false, QStringLiteral("superseded by a new upload"));
    }
    // No discard here. The supersede path reaches it through
    // finishProfileUpload(false) on the line above, which is also the failure
    // path, so one call site covers both — and an unconditional discard here
    // would run a queue scan on every upload and, worse, entitle this function
    // to throw away HEADER_WRITE/FRAME_WRITE queued by anything else. Only a
    // producer withdraws its own work (de1app's rule: the removal sits with
    // the code about to enqueue the replacement, never at a shared choke
    // point).

    m_uploadProfileTitle = profileTitle;
    m_uploadExpectedFrameBytes.clear();
    m_uploadExpectedFrameBytes.reserve(frames.size());
    for (const QByteArray& frame : frames) {
        m_uploadExpectedFrameBytes.append(frame.isEmpty()
                                              ? 0
                                              : static_cast<uint8_t>(frame.at(0)));
    }
    m_uploadSeenFrameBytes.clear();
    m_uploadSeenFrameBytes.reserve(frames.size());
    m_uploadHeaderAcked = false;

    m_profileUploadInProgress = true;

    m_uploadConnection = connect(m_transport, &DE1Transport::writeComplete, this,
                                 &DE1Device::onProfileUploadWriteComplete);

    m_uploadTimeoutTimer.start();
}

void DE1Device::onProfileUploadWriteComplete(const QBluetoothUuid& uuid,
                                             const QByteArray& data)
{
    if (uuid == DE1::Characteristic::HEADER_WRITE) {
        // Treat every HEADER_WRITE completion as a barrier. Any FRAME_WRITE
        // acks observed before this point were leftovers from a write batch
        // queued before our upload (notably the basic-profile header+frames
        // in sendInitialSettings(), which run on every BLE (re)connect and
        // drain between when our tracker attaches and when our own header
        // is ACKed). Clearing on the header ensures the seen-sequence only
        // reflects frames that followed our own header.
        m_uploadSeenFrameBytes.clear();
        m_uploadHeaderAcked = true;
    } else if (uuid == DE1::Characteristic::FRAME_WRITE) {
        // Ignore any FRAME_WRITE acks that arrive before the header barrier —
        // those belong to a prior (non-tracked) write batch.
        if (!m_uploadHeaderAcked) return;
        m_uploadSeenFrameBytes.append(data.isEmpty()
                                          ? 0
                                          : static_cast<uint8_t>(data.at(0)));
    } else {
        return;  // Unrelated write (MMR, ShotSettings, etc.)
    }

    const bool allFrames =
        m_uploadSeenFrameBytes.size() >= m_uploadExpectedFrameBytes.size();
    const bool allRequired = m_uploadHeaderAcked && allFrames;
    if (!allRequired) return;

    // All the writes we expected have been ACKed by the BLE stack — now
    // verify the frame sequence came back in order.
    bool sequenceMatches =
        (m_uploadSeenFrameBytes.size() == m_uploadExpectedFrameBytes.size());
    if (sequenceMatches) {
        for (qsizetype i = 0; i < m_uploadExpectedFrameBytes.size(); ++i) {
            if (m_uploadSeenFrameBytes[i] != m_uploadExpectedFrameBytes[i]) {
                sequenceMatches = false;
                break;
            }
        }
    }

    if (!sequenceMatches) {
        auto formatSeq = [](const QList<uint8_t>& seq) {
            QStringList parts;
            parts.reserve(seq.size());
            for (uint8_t b : seq) {
                parts.append(QString::asprintf("0x%02X", b));
            }
            return QStringLiteral("[") + parts.join(QStringLiteral(", "))
                   + QStringLiteral("]");
        };
        finishProfileUpload(
            false,
            QStringLiteral(
                "frame sequence mismatch (expected %1, got %2). Profile \"%3\" "
                "was likely NOT correctly loaded on the DE1.")
                .arg(formatSeq(m_uploadExpectedFrameBytes))
                .arg(formatSeq(m_uploadSeenFrameBytes))
                .arg(m_uploadProfileTitle));
        return;
    }

    finishProfileUpload(true);
}

void DE1Device::finishProfileUpload(bool success, const QString& reason)
{
    if (!m_profileUploadInProgress) return;

    m_uploadTimeoutTimer.stop();
    if (m_uploadConnection) {
        QObject::disconnect(m_uploadConnection);
        m_uploadConnection = {};
    }
    m_profileUploadInProgress = false;

    // A failed upload withdraws its own remaining frames.
    //
    // This is what actually produced the #1466 stacking, and it is NOT the
    // missing retry guard it looked like: ProfileManager already refuses to
    // start an upload while one is in flight (m_uploadInFlight,
    // profilemanager.cpp:2176), and it arms the retry timer only from inside
    // the profileUploaded handler (:247) — so a retry cannot overlap the
    // ATTEMPT. What it can overlap is the attempt's WRITES. m_uploadTimeoutTimer
    // is 10 s (de1device.cpp:79) while a single timing-out write occupied the link far
    // longer than that, so the tracker gave up, released the gate, and the
    // retry was issued into a queue still holding every frame of the attempt
    // that had just been declared failed. Guarding harder upstream would not
    // have helped; withdrawing the dead frames here is what closes it.
    //
    // Only on failure. A successful upload has no frames left to withdraw, and
    // calling it there would only add a queue scan per upload.
    if (!success)
        discardQueuedProfileWrites();

    // Use .noquote() so QString reasons/titles land as plain text (no
    // surrounding quotes), making the messages scannable in the debug log
    // and stable for test-harness filters to match against.
    if (success) {
        // Stamp the completion time so a startEspresso() queued right behind
        // this upload (the recipe-activation path) settles for the firmware's
        // internal flash write before requesting the state change.
        m_lastProfileUploadCompleteMs = monotonicMsNow();
        // INFO both ways: whether the machine took the profile is the difference
        // between "my shot ran the wrong curve" being a bug report and being
        // answerable from the log.
        DEVICE_INFO(QStringLiteral("Profile upload verified — %1 frame(s) ACKed in order for "
                                  "profile %2")
                        .arg(m_uploadExpectedFrameBytes.size())
                        .arg(m_uploadProfileTitle));
    } else {
        DEVICE_WARN(QStringLiteral("Profile upload FAILED — %1")
                        .arg(reason.isEmpty() ? QStringLiteral("unknown reason") : reason));
    }

    // Clear accumulated tracking state so a subsequent upload starts clean.
    m_uploadExpectedFrameBytes.clear();
    m_uploadSeenFrameBytes.clear();
    m_uploadHeaderAcked = false;
    m_uploadProfileTitle.clear();

    emit profileUploaded(success, reason);

    // Deferred sleep only applies on a successful upload; on failure we drop
    // the pending sleep instead of trying to put a DE1 to sleep whose profile
    // state we can't vouch for. The caller can re-issue goToSleep() if needed.
    if (m_sleepPendingAfterUpload) {
        m_sleepPendingAfterUpload = false;
        if (success) {
            DEVICE_LOG(QStringLiteral("Profile upload complete, now sending deferred sleep"));
            goToSleep();
        }
    }
}

QByteArray DE1Device::buildMMRPayload(uint32_t address, uint32_t value) {
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
    return data;
}

void DE1Device::setFirmwareFlashInProgress(bool inProgress) {
    if (m_firmwareFlashInProgress == inProgress) return;
    m_firmwareFlashInProgress = inProgress;
    // DEBUG: the flash's own progress is the story; this is how it is enforced.
    FW_LOG(QStringLiteral("MMR-write guard %1")
               .arg(inProgress ? QStringLiteral("ENGAGED") : QStringLiteral("cleared")));
}

bool DE1Device::dropDeviceWriteIfFirmwareFlash(const char* label) const {
    if (!m_firmwareFlashInProgress) return false;
    DEVICE_WARN_STDERR(QStringLiteral("%1 DROPPED (firmware flash in progress)")
                           .arg(QString::fromLatin1(label)));
    return true;
}

bool DE1Device::dropIfFirmwareFlashInProgress(uint32_t address, uint32_t value,
                                              const QString& reason,
                                              const char* label) const {
    if (!m_firmwareFlashInProgress) return false;
    MMR_WARN_STDERR(QStringLiteral("%1 DROPPED (firmware flash in progress): 0x%2 = %3%4")
                        .arg(QString::fromLatin1(label))
                        .arg(address, 6, 16, QLatin1Char('0'))
                        .arg(value)
                        .arg(reason.isEmpty() ? QString()
                                              : QStringLiteral(" [%1]").arg(reason)));
    return true;
}

void DE1Device::writeMMR(uint32_t address, uint32_t value,
                         const QString& reason, bool force) {
    if (!m_transport) return;

    // Firmware flash active: MMR writes travel on the same BLE
    // characteristic (A006) that carries firmware chunks. An MMR packet
    // (length byte 4) landing mid-stream between firmware chunks
    // (length byte 16) would corrupt the bootloader's address tracking
    // and force a failed verify + full retry. Drop the write noisily so
    // any regression — e.g. a future periodic MMR writer fired from a
    // timer — shows up in the logs rather than silently killing an
    // update. Dual-bank flash makes a corrupted upload recoverable
    // (active bank is untouched), but forcing the user through a second
    // 15-minute upload over BLE is still worth preventing.
    if (dropIfFirmwareFlashInProgress(address, value, reason, "write")) {
        return;
    }

    const QString reasonSuffix = reason.isEmpty()
        ? QString() : QStringLiteral(" [%1]").arg(reason);

    // Dedup: skip the BLE write when this register's cached value matches.
    // Matches the setShotSettings pattern (see #773). Multiple convergent
    // callers — applyFlushSettings/applySteamSettings/applyHotWaterSettings/
    // sendMachineSettings — otherwise produce bursts of identical MMR writes
    // when distinct UI paths (page activation, preset selection, slider
    // commit, GHC-initiated operation entry) each re-apply the same values.
    // `force` opts out for callers with refresh semantics (USB charger's
    // 10-minute auto-enable timeout means we must keep reasserting even when
    // the value is unchanged).
    auto it = m_lastMMRValues.constFind(address);
    const bool valueUnchanged = (it != m_lastMMRValues.constEnd() && it.value() == value);

    if (!force && valueUnchanged) {
        // Once per distinct (register, value, caller) per session — see m_writeSkippedLog.
        const QString text = QStringLiteral("write skipped: 0x%1 unchanged (%2)%3")
                                 .arg(address, 6, 16, QLatin1Char('0'))
                                 .arg(value)
                                 .arg(reasonSuffix);
        LogCollapse::Collapsed collapsed;
        if (m_mmrSkipLog.shouldLog(text, text, QDateTime::currentMSecsSinceEpoch(), &collapsed)) {
            MMR_LOG(text + LogCollapse::suffix(collapsed));
        }
        return;
    }

    // Log the dispatched write. Three tags so grep counts stay accurate:
    //   "[DE1][MMR] write:"       — value changed since the last write to this
    //                                 address.
    //   "[DE1][MMR] retry-write:" — force=true + unchanged, with a "-retry"
    //                                 suffix on the reason. No caller produces
    //                                 this today: it existed for
    //                                 writeMMRVerified's re-write, which is
    //                                 gone. Kept because it is the tag in every
    //                                 historical log and removing it would make
    //                                 those lines fall into "keepalive", which
    //                                 is exactly the miscount the split was
    //                                 added to prevent.
    //   "[DE1][MMR] keepalive:"   — force=true + unchanged from any other caller.
    //                          In practice that's only BatteryManager's 60 s
    //                          USB-charger refresh.
    // Before this split a wedge log showed a 5 s "Write timeout" out of nowhere
    // because the write that actually started the timeout never logged anything
    // (see #1309).
    QString tag;
    if (!valueUnchanged) {
        tag = QStringLiteral("write");
    } else if (reason.endsWith(QStringLiteral("-retry"))) {
        tag = QStringLiteral("retry-write");
    } else {
        tag = QStringLiteral("keepalive");
    }
    const QString msg = QStringLiteral("%1: 0x%2 = %3%4")
        .arg(tag)
        .arg(address, 6, 16, QLatin1Char('0'))
        .arg(value)
        .arg(reasonSuffix);

    // Only the keepalive tag is collapsed. "write" is by definition a value that changed and
    // "retry-write" means something already went wrong — both are rare and both are the lines a
    // wedge investigation reads (#1309), so they always print. The keepalive is the one that says
    // the same thing every minute for as long as the app runs.
    if (tag == QLatin1String("keepalive")) {
        LogCollapse::Collapsed collapsed;
        const QString key = QString::number(address, 16);
        if (m_keepaliveLog.shouldLog(key, msg, QDateTime::currentMSecsSinceEpoch(), &collapsed))
            MMR_LOG(msg + m_keepaliveLog.suffix(collapsed));
    } else {
        MMR_LOG(msg);
    }

    m_lastMMRValues.insert(address, value);
    m_transport->write(DE1::Characteristic::WRITE_TO_MMR, buildMMRPayload(address, value));
}

void DE1Device::writeMMRUrgent(uint32_t address, uint32_t value, const QString& reason) {
    if (!m_transport) return;

    if (dropIfFirmwareFlashInProgress(address, value, reason, "write urgent")) {
        return;
    }

    const QString reasonSuffix = reason.isEmpty()
        ? QString() : QStringLiteral(" [%1]").arg(reason);
    MMR_LOG(QStringLiteral("write urgent: 0x%1 = %2%3")
                .arg(address, 6, 16, QLatin1Char('0'))
                .arg(value)
                .arg(reasonSuffix));

    // Urgent writes always go through (no dedup check), but we still update
    // the cache so a subsequent non-urgent writeMMR with the same value
    // correctly dedups against what we just sent.
    m_lastMMRValues.insert(address, value);
    m_transport->writeUrgent(DE1::Characteristic::WRITE_TO_MMR, buildMMRPayload(address, value));
}

// ----- Firmware update (A009 / A006) -------------------------------------

void DE1Device::writeFWMapRequest(uint8_t fwToErase, uint8_t fwToMap,
                                  std::array<uint8_t, 3> firstError) {
    if (!m_transport) {
        FW_WARN(QStringLiteral("writeFWMapRequest dropped: no transport"));
        return;
    }
    const QByteArray packet = DE1::Firmware::buildFWMapRequest(fwToErase, fwToMap, firstError);
    FW_LOG(QStringLiteral("A009 write FWMapRequest: %1")
               .arg(QString::fromLatin1(packet.toHex(' '))));
    m_transport->write(DE1::Characteristic::FW_MAP_REQUEST, packet);
}

void DE1Device::writeFirmwareChunk(uint32_t address, const QByteArray& payload16) {
    if (!m_transport) return;
    QByteArray packet = DE1::Firmware::buildChunk(address, payload16);
    if (packet.isEmpty()) {
        // buildChunk rejects payload size != 16; drop silently rather than
        // ship a malformed packet. Bug will be caught by the §3 test, not
        // by the DE1 after thousands of other chunks already got through.
        return;
    }
    // Intentionally does NOT touch m_lastMMRValues. See the header comment
    // on writeFirmwareChunk for why.
    m_transport->write(DE1::Characteristic::WRITE_TO_MMR, packet);
}

void DE1Device::subscribeFirmwareNotifications() {
    if (!m_transport) {
        FW_WARN(QStringLiteral("subscribeFirmwareNotifications dropped: no transport"));
        return;
    }
    FW_LOG(QStringLiteral("Subscribing to A009 (FW_MAP_REQUEST) notifications"));
    m_transport->subscribe(DE1::Characteristic::FW_MAP_REQUEST);
}

void DE1Device::setUsbChargerOn(bool on, bool force) {
    bool stateChanged = (m_usbChargerOn != on);

    if (!stateChanged && !force) {
        return;
    }

    if (stateChanged) {
        m_usbChargerOn = on;
    }

    // force=true must bypass writeMMR's per-register dedup — the DE1's 10-min
    // auto-enable timeout requires us to keep reasserting the commanded value
    // even when unchanged, otherwise the DE1 will silently override us.
    writeMMR(DE1::MMR::USB_CHARGER, on ? 1 : 0,
             QStringLiteral("setUsbChargerOn"), force);

    if (stateChanged) {
        emit usbChargerOnChanged();
    }
}

void DE1Device::setUsbChargerOnUrgent(bool on) {
    if (!m_transport) {
        DEVICE_WARN(QStringLiteral("setUsbChargerOnUrgent: no transport, cannot set charger %1")
                        .arg(on ? QStringLiteral("ON") : QStringLiteral("OFF")));
        return;
    }
    bool stateChanged = (m_usbChargerOn != on);
    if (stateChanged) {
        m_usbChargerOn = on;
    }
    writeMMRUrgent(DE1::MMR::USB_CHARGER, on ? 1 : 0,
                   QStringLiteral("setUsbChargerOnUrgent"));
    if (stateChanged) {
        emit usbChargerOnChanged();
    }
}

void DE1Device::setWaterRefillLevel(int refillPointMm) {
    if (!m_transport) return;
    if (dropDeviceWriteIfFirmwareFlash("setWaterRefillLevel")) return;
    QByteArray data;
    data.append(BinaryCodec::encodeShortBE(BinaryCodec::encodeU16P8(0)));
    data.append(BinaryCodec::encodeShortBE(BinaryCodec::encodeU16P8(static_cast<double>(refillPointMm))));

    WATER_LOG(QStringLiteral("write: StartFillLevel = %1 mm").arg(refillPointMm));
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
    issueMMRReadWithRetry(DE1::MMR::REFILL_KIT, QStringLiteral("refill kit status"));
}

// ---- Sensor calibration (A012) --------------------------------------------

namespace {

// Names for the log line, so a submitted log reads as prose rather than as two
// integers a reader has to decode against this file.
QString calibrationTargetName(DE1::Calibration::Target target) {
    switch (target) {
        case DE1::Calibration::Target::Flow:        return QStringLiteral("flow");
        case DE1::Calibration::Target::Pressure:    return QStringLiteral("pressure");
        case DE1::Calibration::Target::Temperature: return QStringLiteral("temperature");
    }
    return QStringLiteral("unknown");
}

QString calibrationCommandName(DE1::Calibration::Command command) {
    switch (command) {
        case DE1::Calibration::Command::ReadCurrent:  return QStringLiteral("read");
        case DE1::Calibration::Command::Write:        return QStringLiteral("write");
        case DE1::Calibration::Command::ResetFactory: return QStringLiteral("restore factory");
        case DE1::Calibration::Command::ReadFactory:  return QStringLiteral("read factory");
    }
    return QStringLiteral("unknown");
}

}  // namespace

bool DE1Device::sendCalibration(DE1::Calibration::Target target,
                                DE1::Calibration::Command command,
                                double reported,
                                double measured) {
#ifdef DECENZA_SIMULATOR
    if (m_simulationMode) {
        simulateCalibrationReply(target, command, reported, measured);
        return true;
    }
#endif
    if (!m_transport) {
        // Loud rather than silent. A calibration wizard whose reads go nowhere
        // sits on "not read yet" with nothing to explain it, and the reader
        // cannot tell a request that was refused from one never made.
        CAL_WARN("Sensor") << "cannot" << calibrationCommandName(command)
                           << calibrationTargetName(target) << "— no transport";
        return false;
    }
    if (dropDeviceWriteIfFirmwareFlash("sendCalibration")) {
        // dropDeviceWriteIfFirmwareFlash logs on [DE1], not [Calibration]. Say it
        // again here so one `grep` for this subsystem returns the whole story of
        // why a correction did not land — the rule CLAUDE.md states about a
        // subsystem whose lines scatter across markers.
        CAL_WARN("Sensor") << "cannot" << calibrationCommandName(command)
                           << calibrationTargetName(target) << "— firmware flash in progress";
        return false;
    }

    DE1::Calibration::Record record;
    // A write needs the firmware's key; a read is sent with 1. Neither is a
    // magic number here — both are named in de1characteristics.h with their
    // de1app source lines.
    record.writeKey = (command == DE1::Calibration::Command::Write)
                          ? DE1::Calibration::WRITE_KEY
                          : DE1::Calibration::READ_KEY;
    record.command  = command;
    record.target   = target;
    record.reported = reported;
    record.measured = measured;

    // INFO, not DEBUG: "why did my pressure calibration change" is a user
    // question, and the connections views filter to INFO. A write recorded only
    // at DEBUG is absent from the log a user actually submits.
    CAL_INFO("Sensor") << calibrationCommandName(command) << calibrationTargetName(target)
                       << "reported=" << reported << "measured=" << measured;

    m_transport->write(DE1::Characteristic::CALIBRATION, DE1::Calibration::packRecord(record));
    return true;
}

bool DE1Device::readCalibration(int target, bool factory) {
    if (!isCalibrationTarget(target)) {
        CAL_WARN("Sensor") << "read refused, target out of range:" << target;
        return false;
    }
    return sendCalibration(static_cast<DE1::Calibration::Target>(target),
                           factory ? DE1::Calibration::Command::ReadFactory
                                   : DE1::Calibration::Command::ReadCurrent,
                           0.0, 0.0);
}

void DE1Device::clearCalibrationCache() {
    bool had = false;
    for (int i = 0; i < kCalibrationTargets; ++i) {
        had = had || m_storedCalibration[i].has_value() || m_factoryCalibration[i].has_value();
        m_storedCalibration[i].reset();
        m_factoryCalibration[i].reset();
    }
    if (!had) return;
    // Worth a line: the wizard's Apply gate is "has the machine answered", so a
    // reader seeing it go unavailable mid-session should find the reason here.
    CAL_INFO("Sensor") << "calibration cache cleared — values belong to one machine";
    ++m_calibrationVersion;
    emit calibrationChanged();
}

#ifdef DECENZA_SIMULATOR
// A simulated machine's answer, enough to exercise the wizard off hardware.
//
// What it does NOT do: feed the offset back into the simulated sensor. The shot
// samples DE1Simulator produces are unaffected by m_simStoredCalibration, so a
// second run measures the same reading, applies the same delta, and the stored
// offset grows rather than converging. The retest-until-they-agree loop
// therefore does not converge in the simulator — an earlier version of this
// comment claimed it did, which was wrong and would have had the next reader
// diagnosing a real bug from simulated divergence.
//
// The stored-value read hands back what the simulated machine holds; the factory
// read returns a constant. Neither is evidence of firmware behaviour — there is
// no firmware source in this tree. Do not cite this function for what the
// machine does.
void DE1Device::simulateCalibrationReply(DE1::Calibration::Target target,
                                         DE1::Calibration::Command command,
                                         double reported,
                                         double measured) {
    const int index = static_cast<int>(target);
    if (!isCalibrationTarget(index)) return;

    if (command == DE1::Calibration::Command::Write) {
        m_simStoredCalibration[index] += (measured - reported);
        CAL_INFO("Sensor") << "simulated machine stored"
                           << calibrationTargetName(target)
                           << "calibration =" << m_simStoredCalibration[index];
    }

    DE1::Calibration::Record reply;
    // WriteKey 0 marks a reply that carries a real value, exactly as the machine
    // marks one — so the simulated path goes through the same demux.
    reply.writeKey = DE1::Calibration::REPLY_VALUE_KEY;
    reply.command  = command;
    reply.target   = target;
    reply.measured = (command == DE1::Calibration::Command::ReadFactory)
                         ? kSimFactoryCalibration
                         : m_simStoredCalibration[index];
    parseCalibration(DE1::Calibration::packRecord(reply));
}
#endif

bool DE1Device::writeCalibration(int target, double reported, double measured) {
    if (!isCalibrationTarget(target)) {
        CAL_WARN("Sensor") << "write refused, target out of range:" << target;
        return false;
    }
    return sendCalibration(static_cast<DE1::Calibration::Target>(target),
                           DE1::Calibration::Command::Write, reported, measured);
}

double DE1Device::storedCalibration(int target) const {
    if (!isCalibrationTarget(target)) return 0.0;
    return m_storedCalibration[target].value_or(0.0);
}

double DE1Device::factoryCalibration(int target) const {
    if (!isCalibrationTarget(target)) return 0.0;
    return m_factoryCalibration[target].value_or(0.0);
}

bool DE1Device::hasStoredCalibration(int target) const {
    return isCalibrationTarget(target) && m_storedCalibration[target].has_value();
}

bool DE1Device::hasFactoryCalibration(int target) const {
    return isCalibrationTarget(target) && m_factoryCalibration[target].has_value();
}

// A012 carries three kinds of traffic: echoes of our reads, echoes of our
// writes, and the machine's real stored value. Only the last has WriteKey == 0
// (de1app's calibration_ble_received, de1plus/bluetooth.tcl:3344), and only the
// last may update anything — an echo of our own write treated as authoritative
// would make a REFUSED write look like it succeeded.
void DE1Device::parseCalibration(const QByteArray& data) {
    const auto record = DE1::Calibration::parseRecord(data);
    if (!record) {
        CAL_WARN("Sensor") << "unparseable calibration reply,"
                           << data.size() << "bytes:" << data.toHex(' ');
        return;
    }

    if (!DE1::Calibration::replyCarriesValue(*record)) {
        // Expected and frequent — every read and write we send comes back this
        // way. DEBUG because it is mechanics, not an outcome a user needs.
        CAL_DETAIL("Sensor") << "echo for" << calibrationTargetName(record->target)
                             << calibrationCommandName(record->command);
        return;
    }

    const int index = static_cast<int>(record->target);
    if (!isCalibrationTarget(index)) return;

    // CalCommand 3 marks the reply as the FACTORY value rather than the stored
    // one; the machine reuses the same field for both.
    auto& slot = (record->command == DE1::Calibration::Command::ReadFactory)
                     ? m_factoryCalibration[index]
                     : m_storedCalibration[index];
    const bool isFactory = (record->command == DE1::Calibration::Command::ReadFactory);

    if (slot.has_value() && qFuzzyCompare(*slot + 1.0, record->measured + 1.0)) {
        // NOT silent. The read-back after a write is this feature's only
        // confirmation, and a firmware-REFUSED write produces exactly this
        // branch: the machine answers with the pre-write value and the dedupe
        // matches. Returning without a word makes that indistinguishable from no
        // reply at all, which is the same hole one layer out.
        CAL_DETAIL("Sensor") << (isFactory ? "factory" : "stored")
                             << calibrationTargetName(record->target)
                             << "confirmed unchanged at" << record->measured;
        return;
    }

    slot = record->measured;
    CAL_INFO("Sensor") << (isFactory ? "factory" : "stored")
                       << calibrationTargetName(record->target)
                       << "calibration =" << record->measured;
    ++m_calibrationVersion;
    emit calibrationChanged();
}

int DE1Device::bucketHeaterVoltage(int raw) {
    // Returns 0 when the readback falls in neither band. That is "we could not
    // classify this", NOT "the machine said it does not know" — what the DE1
    // actually emits when it has not measured is not established here, and an
    // earlier version of this comment stated it as fact on decaid's authority.
    // decaid's sentinel is -1 and is likewise a catch-all, not a firmware claim.
    //
    // The bands and the above-1000 subtraction ARE sourced: decaid's
    // De1HeaterVoltage.fromInt (lib/src/models/device/de1_interface.dart:134),
    // whose own comment explains >1000 as "already set" — i.e. told rather than
    // measured.
    int volts = raw > 1000 ? raw - 1000 : raw;
    if (volts >= 90 && volts <= 150) return 120;
    if (volts >= 180 && volts <= 260) return 230;
    if (raw != 0) {
        // A nonzero value we cannot place renders identically to "nothing
        // reported", so name it here or the two are indistinguishable.
        CAL_WARN("Sensor") << "heater voltage readback" << raw
                           << "falls in neither band — showing as unknown";
    }
    return 0;
}

void DE1Device::setHeaterVoltage(int volts) {
    // Refuse anything else rather than clamping to the nearest: the two legal
    // values are far apart and a caller asking for something between them is
    // confused, not approximating. Running the heater at the wrong nominal
    // voltage is the failure this guard exists for.
    if (volts != 120 && volts != 230) {
        CAL_WARN("Sensor") << "heater voltage refused, expected 120 or 230, got" << volts;
        return;
    }
    // The INFO goes AFTER the transport check, not before the write. writeMMR
    // opens with a bare unlogged `if (!m_transport) return;`, so logging first
    // asserts a completed action for a write that never left the app.
    if (!m_transport) {
        CAL_WARN("Sensor") << "heater voltage" << volts << "not sent — no transport";
        return;
    }
#ifdef DECENZA_SIMULATOR
    if (m_simulationMode) {
        // The simulated machine accepts it and reports it back, so the selected
        // button moves — the same loop the real one runs through writeMMR and a
        // readback.
        CAL_INFO("Sensor") << "heater voltage =" << volts << "(simulated)";
        if (m_heaterVoltage != volts) {
            m_heaterVoltage = volts;
            emit heaterVoltageChanged();
        }
        return;
    }
#endif
    CAL_INFO("Sensor") << "heater voltage =" << volts;
    writeMMR(DE1::MMR::HEATER_VOLTAGE, static_cast<uint32_t>(volts));
    // Read back rather than trusting what we sent — HEATER_VOLTAGE is otherwise
    // only read once at connect, so without this the displayed value and the
    // selected button stay on the OLD voltage for the rest of the session even
    // though the write landed. Same contract the calibration path follows.
    issueMMRReadWithRetry(DE1::MMR::HEATER_VOLTAGE, QStringLiteral("heater voltage"));
}

void DE1Device::sendInitialSettings() {
    if (!m_transport) return;

    // Ensure USB charger is ON at startup (safe default like de1app)
    if (!m_usbChargerOn) {
        m_usbChargerOn = true;
        writeMMR(DE1::MMR::USB_CHARGER, 1);
        emit usbChargerOnChanged();
    }

    // CRITICAL: Set fan temperature threshold via MMR.
    // Default DE1 fan runs continuously; threshold > 0 means fan only runs when
    // internal temp exceeds this value. 0 = always on (DE1 firmware default).
    writeMMR(DE1::MMR::FAN_THRESHOLD, m_settings ? m_settings->fanThreshold() : 60);

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

    // NOTE: de1app's equivalent (later_new_de1_connection_setup →
    // de1_send_shot_frames) writes the user's current profile here, not a
    // stub. We used to write a hardcoded 1-frame 9-bar profile ("to trigger
    // wake-up response") — it served no purpose beyond being overwritten by
    // MainController::applyAllSettings() ~500 ms later, and its leftover
    // FRAME_WRITE acks leaked into the next profile-upload tracker (see
    // onProfileUploadWriteComplete's HEADER_WRITE barrier). The user's real
    // profile is sent after `initialSettingsComplete` fires, so there's
    // nothing to send here.

    // Read GHC info via MMR. Timeout+retry (issueMMRReadWithRetry) guards
    // against the post-connect subscription race dropping this one-shot
    // response silently — see the harden-de1-ble-reliability change.
    issueMMRReadWithRetry(DE1::MMR::GHC_INFO, QStringLiteral("GHC info"));

    // Read machine identity MMRs (CPU board model, machine model, firmware build)
    // These populate the third line of the firmware version string
    issueMMRReadWithRetry(DE1::MMR::CPU_BOARD_MODEL, QStringLiteral("CPU board model"));
    issueMMRReadWithRetry(DE1::MMR::MACHINE_MODEL, QStringLiteral("machine model"));
    issueMMRReadWithRetry(DE1::MMR::FIRMWARE_VERSION, QStringLiteral("firmware build number"));
    issueMMRReadWithRetry(DE1::MMR::HEATER_VOLTAGE, QStringLiteral("heater voltage"));

    // Read refill kit status
    requestRefillKitStatus();

    // NOTE: de1app sends the user's steam/hotwater settings here
    // (de1_send_steam_hotwater_settings). We skip it — the user's real
    // settings are sent by MainController::applyAllSettings() which fires
    // immediately after initialSettingsComplete via signal/slot. Writing
    // hardcoded defaults here would briefly set wrong values on the DE1
    // (steam=0, group=93) and pollute the drift detector's commanded state.

    // Signal that initial settings are complete. MainController connects
    // to this to apply user settings (profile upload, steam/hotwater/flush
    // settings, water refill level, flow calibration).
    emit initialSettingsComplete();
}

void DE1Device::setShotSettings(double steamTemp, int steamDuration,
                                double hotWaterTemp, int hotWaterVolume,
                                double groupTemp,
                                const QString& reason) {
    // In simulation mode, forward the commanded steam target to the simulator
    // so its m_steamTemp reflects what the app asked for (including Off
    // presets where steamTemp=0 means "heater off"). Sim mode has no BLE
    // transport, so the real-path dedupe check, characteristic write, and
    // read-back verification are all skipped. m_lastShotSettingsPayload is
    // intentionally left untouched — drift detection only runs against real
    // DE1 indications, which never fire in sim mode.
#ifdef DECENZA_SIMULATOR
    if (m_simulationMode && m_simulator) {
        m_simulator->setTargetSteamTemp(steamTemp);
        m_commandedSteamTargetC = steamTemp;
        m_commandedSteamDurationSec = steamDuration;
        m_commandedHotWaterTempC = hotWaterTemp;
        m_commandedHotWaterVolMl = hotWaterVolume;
        m_commandedGroupTargetC = groupTemp;
        return;
    }
#endif
    if (!m_transport) return;
    if (dropDeviceWriteIfFirmwareFlash("setShotSettings")) return;
    QByteArray data(9, 0);
    data[0] = 0;  // SteamSettings flags
    data[1] = BinaryCodec::encodeU8P0(steamTemp);
    data[2] = BinaryCodec::encodeU8P0(steamDuration);
    data[3] = BinaryCodec::encodeU8P0(hotWaterTemp);
    data[4] = BinaryCodec::encodeU8P0(hotWaterVolume);
    data[5] = BinaryCodec::encodeU8P0(60);  // TargetHotWaterLength
    data[6] = BinaryCodec::encodeU8P0(200);  // TargetEspressoVol (safety limit, matches de1app)

    uint16_t groupTempEncoded = BinaryCodec::encodeU16P8(groupTemp);
    data[7] = (groupTempEncoded >> 8) & 0xFF;
    data[8] = groupTempEncoded & 0xFF;

    const QString reasonSuffix = reason.isEmpty()
        ? QString() : QStringLiteral(" [%1]").arg(reason);

    // Dedupe: skip writes whose payload exactly matches the last one sent.
    // Multiple QML signals (DE1 state change, SteamPage activation, pitcher
    // clicks, +5s/-5s buttons) fire startSteamHeating() with the same values
    // during a single steam session, producing several identical BLE writes.
    // Resends from the drift auto-heal path go through resendLastShotSettings()
    // and bypass this function, so they're unaffected. The `[reason]` suffix
    // attributes the elided call to its origin so we can see which convergent
    // signal fired.
    if (data == m_lastShotSettingsPayload) {
        // Once per distinct (payload, caller) per session — see m_writeSkippedLog.
        const QString text = QStringLiteral(
            "write skipped: payload unchanged "
            "(steam=%1C duration=%2s hotWater=%3C vol=%4ml groupTemp=%5C)%6")
            .arg(steamTemp, 0, 'f', 1)
            .arg(steamDuration)
            .arg(hotWaterTemp, 0, 'f', 1)
            .arg(hotWaterVolume)
            .arg(groupTemp, 0, 'f', 2)
            .arg(reasonSuffix);
        LogCollapse::Collapsed collapsed;
        if (m_shotSettingsSkipLog.shouldLog(text, text, QDateTime::currentMSecsSinceEpoch(),
                                            &collapsed)) {
            SHOTSETTINGS_LOG(text + LogCollapse::suffix(collapsed));
        }
        return;
    }

    // Record what we're commanding so any setShotSettings() call site (main
    // controller, profile manager, steam calibrator, …) contributes to the
    // drift detector without each one having to remember.
    m_commandedSteamTargetC = steamTemp;
    m_commandedSteamDurationSec = steamDuration;
    m_commandedHotWaterTempC = hotWaterTemp;
    m_commandedHotWaterVolMl = hotWaterVolume;
    m_commandedGroupTargetC = groupTemp;
    m_lastShotSettingsWriteMs = QDateTime::currentMSecsSinceEpoch();
    m_lastShotSettingsPayload = data;

    // Trace every write so the timeline of commanded values is visible in the
    // debug log alongside the DE1-reported values from parseShotSettings().
    // This is what lets us tell apart "we never wrote it" vs "we wrote it and
    // the DE1 ignored us" vs "stale indication crossed our new write".
    SHOTSETTINGS_LOG(QStringLiteral(
        "write: steam=%1C duration=%2s hotWater=%3C vol=%4ml groupTemp=%5C%6")
        .arg(steamTemp, 0, 'f', 1)
        .arg(steamDuration)
        .arg(hotWaterTemp, 0, 'f', 1)
        .arg(hotWaterVolume)
        .arg(groupTemp, 0, 'f', 2)
        .arg(reasonSuffix));

    m_transport->write(DE1::Characteristic::SHOT_SETTINGS, data);

    // Verify the write by reading back. The DE1 firmware does NOT push
    // notifications on the SHOT_SETTINGS characteristic when written (de1app
    // doesn't subscribe to it either — confirmed by inspecting de1_comms.tcl).
    // BleTransport::read() queues the read so it executes after this write
    // completes, returning the actual stored value to parseShotSettings()
    // which feeds MainController::onShotSettingsReported() for drift detection.
    m_transport->read(DE1::Characteristic::SHOT_SETTINGS);
}

void DE1Device::parseShotSettings(const QByteArray& data) {
    // Wire format matches de1app's hotwater_steam_settings_spec:
    //   byte 0   SteamSettings flags (u8)
    //   byte 1   TargetSteamTemp     (u8p0, °C)
    //   byte 2   TargetSteamLength   (u8p0, seconds)
    //   byte 3   TargetHotWaterTemp  (u8p0, °C)
    //   byte 4   TargetHotWaterVol   (u8p0, ml)
    //   byte 5   TargetHotWaterLength(u8p0, seconds)
    //   byte 6   TargetEspressoVol   (u8p0, ml)
    //   bytes 7-8 TargetGroupTemp    (u16p8 big-endian, °C)
    if (data.size() < 9) {
        DEVICE_WARN(QStringLiteral("parseShotSettings: short payload, size=%1").arg(data.size()));
        return;
    }
    const auto d = reinterpret_cast<const uint8_t*>(data.constData());
    const double steamTargetC = BinaryCodec::decodeU8P0(d[1]);
    const int steamDurationSec = static_cast<int>(BinaryCodec::decodeU8P0(d[2]));
    const double hotWaterTempC = BinaryCodec::decodeU8P0(d[3]);
    const int hotWaterVolMl = static_cast<int>(BinaryCodec::decodeU8P0(d[4]));
    const uint16_t groupRaw = BinaryCodec::decodeShortBE(data, 7);
    const double groupTargetC = BinaryCodec::decodeU16P8(groupRaw);

    // Trace every DE1-reported value. Pair with the "[DE1][ShotSettings] write:"
    // lines above to reconstruct the request/response timeline when
    // diagnosing "heater didn't heat" or "steam didn't stop" reports.
    SHOTSETTINGS_LOG(QStringLiteral(
        "reported: steam=%1C duration=%2s hotWater=%3C vol=%4ml group=%5C (%6 bytes)")
        .arg(steamTargetC, 0, 'f', 1)
        .arg(steamDurationSec)
        .arg(hotWaterTempC, 0, 'f', 1)
        .arg(hotWaterVolMl)
        .arg(groupTargetC, 0, 'f', 2)
        .arg(data.size()));

    m_deviceSteamTargetC = steamTargetC;
    m_deviceSteamDurationSec = steamDurationSec;
    m_deviceHotWaterTempC = hotWaterTempC;
    m_deviceHotWaterVolMl = hotWaterVolMl;
    m_deviceGroupTargetC = groupTargetC;

    emit shotSettingsReported(steamTargetC, steamDurationSec, hotWaterTempC, hotWaterVolMl, groupTargetC);
}

void DE1Device::resendLastShotSettings() {
    // Not silent, but DEBUG rather than WARN. A bare return made the log assert a
    // write that never left the process, since MainController logs "resending
    // last ShotSettings payload" before calling here. But an empty payload is
    // also the ordinary state before the first write ever happens — nothing is
    // wrong then, and warning on it is what LOGGING.md means by training readers
    // to skim the tier that means "look here". tst_ShotSettings::
    // resendLastShotSettingsNoOpBeforeFirstWrite pins exactly that case.
    //
    // There is no state that distinguishes "had a payload and lost it" from
    // "never wrote one": disconnect clears the payload AND resets the commanded
    // values together (see onTransportDisconnected). So the tier has to suit the
    // benign reading. MainController's own path cannot reach here empty anyway —
    // it only resends once haveCommanded is true, which requires a prior write.
    if (!m_transport || m_lastShotSettingsPayload.isEmpty()) {
        SHOTSETTINGS_LOG(QStringLiteral(
            "resend requested but nothing was sent — %1")
            .arg(!m_transport ? QStringLiteral("no transport")
                              : QStringLiteral("no payload retained (none written yet, or cleared on disconnect)")));
        return;
    }
    if (dropDeviceWriteIfFirmwareFlash("resendLastShotSettings")) return;
    SHOTSETTINGS_LOG(QStringLiteral(
        "resend: repeating last payload "
        "(steam=%1C duration=%2s hotWater=%3C vol=%4ml group=%5C)")
        .arg(m_commandedSteamTargetC, 0, 'f', 1)
        .arg(m_commandedSteamDurationSec)
        .arg(m_commandedHotWaterTempC, 0, 'f', 1)
        .arg(m_commandedHotWaterVolMl)
        .arg(m_commandedGroupTargetC, 0, 'f', 2));
    m_lastShotSettingsWriteMs = QDateTime::currentMSecsSinceEpoch();
    m_transport->write(DE1::Characteristic::SHOT_SETTINGS, m_lastShotSettingsPayload);
}
