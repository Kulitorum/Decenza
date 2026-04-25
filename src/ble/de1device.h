#pragma once

#include <array>
#include <cstdint>

#include <QObject>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QByteArray>
#include <QEvent>
#include <QHash>
#include <QList>
#include <QString>
#include <QTimer>

#include "protocol/de1characteristics.h"

// High-priority custom event posted by WeightProcessor to bypass the normal
// QueuedConnection queue on slow devices. Delivered via Qt::HighEventPriority
// so it jumps ahead of pending D-Flow setpoint events on the main thread.
class SawStopEvent : public QEvent {
public:
    static QEvent::Type eventType() {
        static int type = QEvent::registerEventType();
        return static_cast<QEvent::Type>(type);
    }
    explicit SawStopEvent(qint64 sawTriggerMs)
        : QEvent(eventType()), m_sawTriggerMs(sawTriggerMs) {}
    qint64 sawTriggerMs() const { return m_sawTriggerMs; }
private:
    qint64 m_sawTriggerMs;
};

class Profile;
class SettingsHardware;
class DE1Transport;
class BleTransport;

#ifdef QT_DEBUG
class DE1Simulator;
#endif

struct ShotSample {
    qint64 timestamp = 0;
    double timer = 0.0;
    double groupPressure = 0.0;
    double groupFlow = 0.0;
    double mixTemp = 0.0;
    double headTemp = 0.0;
    double setTempGoal = 0.0;
    double setFlowGoal = 0.0;
    double setPressureGoal = 0.0;
    int frameNumber = 0;
    double steamTemp = 0.0;
};

class DE1Device : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(bool connecting READ isConnecting NOTIFY connectingChanged)
    Q_PROPERTY(bool simulationMode READ simulationMode WRITE setSimulationMode NOTIFY simulationModeChanged)
    Q_PROPERTY(bool guiEnabled READ isGuiEnabled NOTIFY guiEnabledChanged)
    Q_PROPERTY(int state READ stateInt NOTIFY stateChanged)
    Q_PROPERTY(int subState READ subStateInt NOTIFY subStateChanged)
    Q_PROPERTY(QString stateString READ stateString NOTIFY stateChanged)
    Q_PROPERTY(QString subStateString READ subStateString NOTIFY subStateChanged)
    Q_PROPERTY(double pressure READ pressure NOTIFY shotSampleReceived)
    Q_PROPERTY(double flow READ flow NOTIFY shotSampleReceived)
    Q_PROPERTY(double temperature READ temperature NOTIFY shotSampleReceived)
    Q_PROPERTY(double goalPressure READ goalPressure NOTIFY shotSampleReceived)
    Q_PROPERTY(double goalFlow READ goalFlow NOTIFY shotSampleReceived)
    Q_PROPERTY(double goalTemperature READ goalTemperature NOTIFY shotSampleReceived)
    Q_PROPERTY(double steamTemperature READ steamTemperature NOTIFY shotSampleReceived)
    // DE1-reported ShotSettings targets (from indications/reads of the
    // SHOT_SETTINGS characteristic). -1.0 (double) or -1 (int) until first
    // report. Used by MainController to detect drift between what we
    // commanded and what the DE1 actually stored.
    Q_PROPERTY(double deviceSteamTargetC READ deviceSteamTargetC NOTIFY shotSettingsReported)
    Q_PROPERTY(double deviceGroupTargetC READ deviceGroupTargetC NOTIFY shotSettingsReported)
    Q_PROPERTY(double waterLevel READ waterLevel NOTIFY waterLevelChanged)
    Q_PROPERTY(double waterLevelMm READ waterLevelMm NOTIFY waterLevelChanged)
    Q_PROPERTY(int waterLevelMl READ waterLevelMl NOTIFY waterLevelChanged)
    Q_PROPERTY(QString firmwareVersion READ firmwareVersion NOTIFY firmwareVersionChanged)
    Q_PROPERTY(bool usbChargerOn READ usbChargerOn NOTIFY usbChargerOnChanged)
    Q_PROPERTY(bool isHeadless READ isHeadless NOTIFY isHeadlessChanged)
    Q_PROPERTY(int refillKitDetected READ refillKitDetected NOTIFY refillKitDetectedChanged)
    Q_PROPERTY(QString connectionType READ connectionType NOTIFY connectedChanged)
    Q_PROPERTY(int machineModel READ machineModel NOTIFY firmwareVersionChanged)
    Q_PROPERTY(int heaterVoltage READ heaterVoltage NOTIFY heaterVoltageChanged)

public:
    explicit DE1Device(QObject* parent = nullptr);
    ~DE1Device();

    bool isConnected() const;
    bool isConnecting() const;
    bool isGuiEnabled() const;

    DE1::State state() const { return m_state; }
    DE1::SubState subState() const { return m_subState; }
    int stateInt() const { return static_cast<int>(m_state); }
    int subStateInt() const { return static_cast<int>(m_subState); }
    QString stateString() const { return DE1::stateToString(m_state); }
    QString subStateString() const { return DE1::subStateToString(m_subState); }

    double pressure() const { return m_pressure; }
    double flow() const { return m_flow; }
    double temperature() const { return m_headTemp; }
    double goalPressure() const { return m_goalPressure; }
    double goalFlow() const { return m_goalFlow; }
    double goalTemperature() const { return m_goalTemperature; }
    double mixTemperature() const { return m_mixTemp; }
    double steamTemperature() const { return m_steamTemp; }
    double deviceSteamTargetC() const { return m_deviceSteamTargetC; }
    int deviceSteamDurationSec() const { return m_deviceSteamDurationSec; }
    double deviceHotWaterTempC() const { return m_deviceHotWaterTempC; }
    int deviceHotWaterVolMl() const { return m_deviceHotWaterVolMl; }
    double deviceGroupTargetC() const { return m_deviceGroupTargetC; }
    // Last ShotSettings values we actually wrote over BLE (-1 if none yet).
    // Used by MainController's drift check to distinguish "DE1 dropped the
    // write" from "DE1 reported its power-on state before we wrote anything".
    double commandedSteamTargetC() const { return m_commandedSteamTargetC; }
    int commandedSteamDurationSec() const { return m_commandedSteamDurationSec; }
    double commandedHotWaterTempC() const { return m_commandedHotWaterTempC; }
    int commandedHotWaterVolMl() const { return m_commandedHotWaterVolMl; }
    double commandedGroupTargetC() const { return m_commandedGroupTargetC; }
    qint64 lastShotSettingsWriteMs() const { return m_lastShotSettingsWriteMs; }
    double waterLevel() const { return m_waterLevel; }
    double waterLevelMm() const { return m_waterLevelMm; }
    int waterLevelMl() const { return m_waterLevelMl; }
    QString firmwareVersion() const { return m_firmwareVersion; }
    bool usbChargerOn() const { return m_usbChargerOn; }
    bool isHeadless() const { return m_isHeadless; }
    Q_INVOKABLE void setIsHeadless(bool headless);  // Debug toggle
    int refillKitDetected() const { return m_refillKitDetected; }  // -1=unknown, 0=not detected, 1=detected
    int machineModel() const { return m_machineModel; }  // 0=unknown, 1=DE1, 2=DE1+, 3=PRO, 4=XL, 5=CAFE, 6=XXL, 7=XXXL
    int firmwareBuildNumber() const { return m_firmwareBuildNumber; }  // 0 = unknown, otherwise build number (e.g. 1347)
    int heaterVoltage() const { return m_heaterVoltage; }  // 0=unknown, otherwise volts (e.g. 110, 220)

    // Transport abstraction
    void setTransport(DE1Transport* transport);
    DE1Transport* transport() const { return m_transport; }
    QString connectionType() const;

    // Simulation mode for GUI development without hardware
    bool simulationMode() const { return m_simulationMode; }
    void setSimulationMode(bool enabled);

    // Firmware-flash guard. Set to true by FirmwareUpdater for the duration
    // of the erase/upload/verify sequence so writeMMR() / writeMMRUrgent() /
    // writeMMRVerified() can drop incoming MMR writes — otherwise a stray
    // MMR packet on the WRITE_TO_MMR characteristic (length byte 4) would
    // interleave with in-flight firmware chunks (length byte 16) on A006
    // and force a failed verify + full retry. Analogous to de1app's
    // `currently_erasing_firmware` gate in `mmr_write`; we gate all three
    // phases (erase/upload/verify) rather than erase-only since chunk
    // writes and MMR writes share the same characteristic during upload.
    bool firmwareFlashInProgress() const { return m_firmwareFlashInProgress; }
    void setFirmwareFlashInProgress(bool inProgress);

    // For simulator integration - allows external code to set state and emit signals
    void setSimulatedState(DE1::State state, DE1::SubState subState);
    void emitSimulatedShotSample(const ShotSample& sample);
    // Push an idle steam-temperature update from the simulator (e.g. when the
    // app commands a new steam target via setShotSettings). Emits a minimal
    // shot sample so QML bindings on DE1Device.steamTemperature re-evaluate.
    void setSimulatedIdleSteamTemp(double steamTempC);
#ifdef QT_DEBUG
    void setSimulator(DE1Simulator* simulator) { m_simulator = simulator; }
#endif

    // Hardware settings (heater calibration sent to firmware)
    void setSettings(SettingsHardware* settings);

public slots:
    void connectToDevice(const QString& address);
    void connectToDevice(const QBluetoothDeviceInfo& device);
    void disconnect();

    // Machine control
    void requestState(DE1::State state);
    void startEspresso();
    void startSteam();
    void startHotWater();
    void startFlush();
    void startDescale();
    void startClean();
    void stopOperation();         // Soft stop (for steam: stops flow, no purge)
    void stopOperationUrgent();   // Bypasses command queue for faster stop (SOW)
    void stopOperationUrgent(qint64 sawTriggerMs);  // Includes SAW trigger timestamp for latency tracing
    void requestIdle();           // Hard stop (requests Idle state, triggers steam purge)
    void skipToNextFrame();   // Skip to next profile frame during extraction (0x0E)
    void goToSleep();
    void wakeUp();

    // Profile upload
    void uploadProfile(const Profile& profile);
    void uploadProfileAndStartEspresso(const Profile& profile);  // Upload then start in correct order
    void clearCommandQueue();  // Clear all pending BLE commands (use when extraction starts)

    // Direct frame writing (for direct control mode)
    void writeHeader(const QByteArray& headerData);
    void writeFrame(const QByteArray& frameData);

    // Settings. `reason` is an optional caller tag that appears in the
    // [ShotSettings] write: / write skipped: log lines, so redundant calls
    // from convergent signals can be attributed to their origin.
    void setShotSettings(double steamTemp, int steamDuration,
                        double hotWaterTemp, int hotWaterVolume,
                        double groupTemp,
                        const QString& reason = QString());

    // Re-send the last ShotSettings payload exactly as last commanded. Used
    // by the drift auto-heal path to re-assert what we intended WITHOUT
    // re-deriving the value from current Settings — critical because code
    // paths like startSteamHeating() or softStopSteam() write values that
    // intentionally diverge from Settings.steamTemperature()/keepSteamHeaterOn
    // (e.g. startSteamHeating forces the heater on even when keepSteamHeaterOn
    // is false). Resending via sendMachineSettings() would clobber them.
    void resendLastShotSettings();

    // MMR write (for advanced settings like steam flow). Identical writes to
    // the same register are deduped against m_lastMMRValues, matching the
    // setShotSettings dedup pattern — the session log showed ~30 identical
    // flush-flow MMR bursts in 2.5 s when settings sliders emitted convergent
    // change signals. `reason` is an optional caller tag that appears in the
    // [MMR] write / write skipped log lines. Pass `force=true` to bypass the
    // dedup check (the DE1's USB-charger register has a 10-minute auto-enable
    // timeout that requires us to keep reasserting the commanded value).
    void writeMMR(uint32_t address, uint32_t value,
                  const QString& reason = QString(),
                  bool force = false);

    // MMR write bypassing the BLE command queue — used for time-critical writes
    // that must complete before the app suspends (e.g. ensureChargerOn on iOS).
    // Always bypasses the dedup check since "urgent" implies "must reach the
    // DE1 now". Still updates m_lastMMRValues so a subsequent non-urgent
    // writeMMR with the same value is correctly elided.
    void writeMMRUrgent(uint32_t address, uint32_t value,
                        const QString& reason = QString());

    // MMR write with read-back verification: write, then read the register
    // back ~50ms later, retry up to maxRetries times if the read doesn't
    // return the value we wrote. Used for SteamFlow during steaming as
    // defensive insurance — on-device testing showed zero retries needed
    // in practice, but verify-and-retry catches any silent drop (e.g. a
    // BLE write lost during high traffic) without costing a retry budget
    // on the happy path. A subsequent call for the same address replaces
    // (cancels) the prior verification — newest write wins.
    void writeMMRVerified(uint32_t address, uint32_t value,
                          const QString& reason = QString(),
                          int maxRetries = 5);

    // USB charger control (force=true to resend even if state unchanged, needed for DE1's 10-min timeout)
    void setUsbChargerOn(bool on, bool force = false);

    // Like setUsbChargerOn but bypasses the BLE command queue for immediate send.
    // Used by ensureChargerOn() on app suspend where the 50ms queue delay could
    // race with iOS suspension.
    void setUsbChargerOnUrgent(bool on);

    // Water refill level (write StartFillLevel to machine via WaterLevels characteristic)
    void setWaterRefillLevel(int refillPointMm);

    // Flow calibration multiplier (MMR 0x80383C: value = int(1000 * multiplier))
    void setFlowCalibrationMultiplier(double multiplier);

    // Refill kit control (MMR 0x80385C: 0=off, 1=on, 2=auto-detect)
    void setRefillKitPresent(int value);
    void requestRefillKitStatus();

    // ---- Firmware update (BLE A009 / A006) --------------------------------
    // These three writers talk directly to the transport and deliberately
    // bypass the per-register MMR dedupe cache (m_lastMMRValues): firmware
    // uploads stream ~28,000 unique 16-byte chunks, populating the cache
    // with those would grow the hash uselessly and risk colliding with
    // real MMR register addresses. See tasks.md §3 and the firmware-update
    // design doc for the wire formats.

    // Write the 7-byte FWMapRequest to A009 (firmware-update control).
    // `fwToErase=1, fwToMap=1` starts Phase 1 (erase). A second call with
    // `fwToErase=0, fwToMap=1, firstError={0xFF,0xFF,0xFF}` starts Phase 3
    // (verify-and-activate). See `DE1::Firmware::buildFWMapRequest`.
    void writeFWMapRequest(uint8_t fwToErase, uint8_t fwToMap,
                           std::array<uint8_t, 3> firstError = {0, 0, 0});

    // Stream one 16-byte firmware chunk to A006 (WRITE_TO_MMR) with opcode
    // 0x10 and a 24-bit little-endian address. Caller is responsible for
    // pacing (de1app uses ~1 ms between chunks). A zero-size or
    // non-16-byte payload is silently dropped so a caller bug can't ship
    // a malformed packet to the DE1.
    void writeFirmwareChunk(uint32_t address, const QByteArray& payload16);

    // Subscribe to FW_MAP_REQUEST notifications. Done on demand at the
    // start of a firmware update rather than always-on, because A009 is
    // silent during normal operation and leaving a handler active adds
    // nothing. There is no unsubscribe on the transport; the user power-
    // cycling the DE1 at the end of verify disconnects BLE and implicitly
    // terminates the subscription.
    void subscribeFirmwareNotifications();

signals:
    void connectedChanged();
    void connectingChanged();
    void stateChanged();
    void subStateChanged();
    void shotSampleReceived(const ShotSample& sample);
    void waterLevelChanged();
    void firmwareVersionChanged();
    // Emitted when a profile upload attempt completes. On failure, `reason`
    // carries a short human-readable string explaining why (matching the text
    // in the qWarning log line) so listeners can distinguish retryable
    // transients (frame sequence mismatch, ACK timeout) from non-retryable
    // events (supersede, queue clear, BLE disconnect). Empty on success.
    void profileUploaded(bool success, const QString& reason = QString());
    void initialSettingsComplete();
    void errorOccurred(const QString& error);
    void simulationModeChanged();
    void guiEnabledChanged();
    void usbChargerOnChanged();
    void isHeadlessChanged();
    void refillKitDetectedChanged();
    void heaterVoltageChanged();

    // Firmware-update response from the DE1 (A009 notification). Carries
    // the parsed fwToErase/fwToMap flags and the 3-byte FirstError. During
    // Phase 1 the DE1 emits two notifications: first with fwToErase=1
    // (erase in progress), then fwToErase=0 (erase complete). Phase 3 emits
    // one notification whose firstError == {0xFF,0xFF,0xFD} on success.
    void fwMapResponse(uint8_t fwToErase, uint8_t fwToMap,
                       QByteArray firstError);
    // Emitted after the DE1 reports its stored ShotSettings (either from our
    // initial read on connect or from an indication after a write). Values
    // are the DE1's current targets; 0 means the heater/setting is off.
    void shotSettingsReported(double deviceSteamTargetC, int deviceSteamDurationSec,
                              double deviceHotWaterTempC, int deviceHotWaterVolMl,
                              double deviceGroupTargetC);
    void logMessage(const QString& message);

protected:
    void customEvent(QEvent* event) override;

private:
    // Build the 20-byte MMR payload without sending it (shared by writeMMR/writeMMRUrgent)
    static QByteArray buildMMRPayload(uint32_t address, uint32_t value);

    // Firmware-flash guard shared by writeMMR / writeMMRUrgent /
    // writeMMRVerified. Returns true (and logs a qWarning) when the caller
    // should bail because a flash is in progress; false means "proceed".
    // `label` distinguishes the variant in the log line ("write", "write urgent",
    // "write verified").
    bool dropIfFirmwareFlashInProgress(uint32_t address, uint32_t value,
                                       const QString& reason,
                                       const char* label) const;

    // Generic "drop this DE1 write mid-flash" guard for command paths that
    // don't go through writeMMR (state changes, profile upload, shot
    // settings, water levels, etc.). Returns true (and logs a qWarning)
    // when a flash is in progress so the caller should bail.
    bool dropDeviceWriteIfFirmwareFlash(const char* label) const;

    // Transport signal handlers
    void onTransportConnected();
    void onTransportDisconnected();
    void onTransportDataReceived(const QBluetoothUuid& uuid, const QByteArray& data);
    void onTransportWriteComplete(const QBluetoothUuid& uuid, const QByteArray& data);

    // Parse methods (dispatch from onTransportDataReceived)
    void parseStateInfo(const QByteArray& data);
    void parseShotSample(const QByteArray& data);
    void parseShotSettings(const QByteArray& data);
    void parseWaterLevel(const QByteArray& data);
    void parseVersion(const QByteArray& data);
    void parseMMRResponse(const QByteArray& data);
    void rebuildVersionLine3();
    void requestGHCStatus();

    // writeMMRVerified helpers
    void scheduleMMRVerifyRead(uint32_t address);
    void retryMMRVerify(uint32_t address, const QString& cause);

    void sendInitialSettings();

    // Profile upload tracking (frame-ACK verification, modeled on de1app's
    // confirm_de1_send_shot_frames_worked). startProfileUploadTracking() must
    // be called BEFORE queuing the header/frame writes so the listener is
    // attached in time to observe every writeComplete.
    void startProfileUploadTracking(const QString& profileTitle,
                                    const QList<QByteArray>& frames,
                                    bool expectEspressoStart);
    void onProfileUploadWriteComplete(const QBluetoothUuid& uuid,
                                      const QByteArray& data);
    void finishProfileUpload(bool success, const QString& reason = QString());

    // Owned when created internally via connectToDevice(); set externally via setTransport() for USB
    DE1Transport* m_transport = nullptr;
    bool m_ownsTransport = false;  // True when DE1Device created the transport (connectToDevice)

    DE1::State m_state = DE1::State::Sleep;
    DE1::SubState m_subState = DE1::SubState::Ready;
    double m_pressure = 0.0;
    double m_flow = 0.0;
    double m_mixTemp = 0.0;
    double m_headTemp = 0.0;
    double m_goalPressure = 0.0;
    double m_goalFlow = 0.0;
    double m_goalTemperature = 0.0;
    double m_steamTemp = 0.0;
    // DE1-reported ShotSettings targets (from SHOT_SETTINGS indications/reads).
    // -1 until first report so MainController can distinguish "never heard
    // back" from "DE1 says target is 0".
    double m_deviceSteamTargetC = -1.0;
    int m_deviceSteamDurationSec = -1;
    double m_deviceHotWaterTempC = -1.0;
    int m_deviceHotWaterVolMl = -1;
    double m_deviceGroupTargetC = -1.0;
    // Last ShotSettings values we wrote (tracked here so every setShotSettings
    // caller — MainController, ProfileManager, etc. — is covered without each
    // one having to remember). -1 until first write.
    double m_commandedSteamTargetC = -1.0;
    int m_commandedSteamDurationSec = -1;
    double m_commandedHotWaterTempC = -1.0;
    int m_commandedHotWaterVolMl = -1;
    double m_commandedGroupTargetC = -1.0;
    qint64 m_lastShotSettingsWriteMs = 0;
    // Raw 9-byte payload of the most recent ShotSettings write, used by
    // resendLastShotSettings() to re-emit the exact bytes we originally sent.
    // This is the only way to resend bytes 5-6 (TargetHotWaterLength,
    // TargetEspressoVol) which are hardcoded in setShotSettings() and have
    // no corresponding commanded-value members.
    QByteArray m_lastShotSettingsPayload;
    // Per-register cache of the last value written to each MMR address.
    // writeMMR() skips the BLE write when the cached value matches, eliding
    // redundant traffic from convergent callers (flush/steam/hot-water slider
    // changes fan out into the same MMR). Cleared on transport disconnect so
    // a reconnect re-writes real values rather than trusting stale ones.
    QHash<uint32_t, uint32_t> m_lastMMRValues;
    // Pending writeMMRVerified() entries keyed by address. Each tracks the
    // expected value and remaining retry budget; cleared when a read-back
    // matches or retries are exhausted, and on transport disconnect.
    struct PendingMMRVerify {
        uint32_t expectedValue;
        int attemptsRemaining;
        QString reason;
    };
    QHash<uint32_t, PendingMMRVerify> m_pendingMMRVerifies;
    double m_waterLevel = 0.0;
    double m_waterLevelMm = 0.0;  // Raw mm value (with sensor offset applied)
    int m_waterLevelMl = 0;       // Volume in ml (from CAD lookup table)
    double m_lastEmittedWaterLevel = -1.0;  // Throttle: only emit when change >= 0.5%
    int m_lastEmittedWaterLevelMl = -1;    // Throttle: also emit when ml changes (color thresholds)
    QString m_firmwareVersion;
    int m_firmwareBuildNumber = 0;
    int m_machineModel = 0;
    int m_heaterVoltage = 0;  // 0=unknown, read from MMR HEATER_VOLTAGE
    uint32_t m_cpuBoardModel = 0;

    bool m_connecting = false;
    bool m_simulationMode = false;
    bool m_firmwareFlashInProgress = false;
#ifdef QT_DEBUG
    DE1Simulator* m_simulator = nullptr;  // For simulation mode
#endif
    SettingsHardware* m_settings = nullptr;  // Heater calibration sent to firmware
    bool m_profileUploadInProgress = false;  // True while profile header+frames are being sent
    bool m_sleepPendingAfterUpload = false;  // Sleep requested during profile upload

    // Frame-ACK verification state for the in-flight profile upload (cleared
    // by finishProfileUpload()). m_uploadExpectedFrameBytes is the leading
    // byte of every frame we queued to FRAME_WRITE, in queue order; each
    // FRAME_WRITE ACK appends its leading byte to m_uploadSeenFrameBytes, and
    // we compare the two sequences when all expected ACKs have arrived.
    QString m_uploadProfileTitle;
    QList<uint8_t> m_uploadExpectedFrameBytes;
    QList<uint8_t> m_uploadSeenFrameBytes;
    bool m_uploadHeaderAcked = false;
    bool m_uploadEspressoStartAcked = false;
    bool m_uploadExpectEspressoStart = false;
    QMetaObject::Connection m_uploadConnection;
    QTimer m_uploadTimeoutTimer;
    bool m_usbChargerOn = true;  // Default on (safe default like de1app)
    bool m_isHeadless = false;   // True if app can start operations (GHC not installed or inactive)
    int m_refillKitDetected = -1;  // -1=unknown, 0=not detected, 1=detected

    // SAW stop latency instrumentation (monotonic ms timestamps)
    bool m_sawStopWritePending = false;
    qint64 m_lastSawTriggerMs = 0;
    qint64 m_lastSawWriteMs = 0;

#ifdef DECENZA_TESTING
    friend class tst_SAV;
    friend class tst_MachineState;
    friend class tst_ProfileManager;
    friend class tst_MMRWrite;
    friend class tst_DE1DeviceFirmware;
#endif
};
