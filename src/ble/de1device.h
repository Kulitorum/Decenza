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
#include <QElapsedTimer>
#include <QString>
#include <QTimer>

#include "../core/logcollapse.h"
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

class DE1Simulator;

struct ShotSample {
    qint64 timestamp = 0;
    double timer = 0.0;
    double groupPressure = 0.0;
    double groupFlow = 0.0;
    double mixTemp = 0.0;
    double headTemp = 0.0;
    // The DE1 reports two temperature setpoints at adjacent offsets, both in °C —
    // easy to transpose, and a swap decodes to plausible values either way.
    // setTempGoal is SetHeadTemp (basket target); setMixTempGoal is SetMixTemp,
    // the target for water entering the group, which typically runs above the
    // basket target to offset group heat loss.
    double setTempGoal = 0.0;
    double setMixTempGoal = 0.0;
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
    // Descale progress. The DE1 reports no percentage and no expected duration, so
    // these are derived from the firmware's fixed step schedule (see kDescaleSchedule
    // in the .cpp) resynced at every substate boundary. 0/absent when not descaling.
    Q_PROPERTY(double descaleProgress READ descaleProgress NOTIFY descaleProgressChanged)
    Q_PROPERTY(int descaleStepIndex READ descaleStepIndex NOTIFY descaleProgressChanged)
    Q_PROPERTY(int descaleStepCount READ descaleStepCount CONSTANT)
    Q_PROPERTY(int descaleSecondsRemaining READ descaleSecondsRemaining NOTIFY descaleProgressChanged)
    Q_PROPERTY(int descaleCycle READ descaleCycle NOTIFY descaleProgressChanged)
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

    double descaleProgress() const { return m_descaleProgress; }
    int descaleStepIndex() const { return m_descaleStepIndex; }
    int descaleStepCount() const;
    int descaleSecondsRemaining() const { return m_descaleSecondsRemaining; }
    int descaleCycle() const { return m_descaleCycle; }
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
    // of the erase/upload/verify sequence so writeMMR() / writeMMRUrgent()
    // can drop incoming MMR writes — otherwise a stray
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
    // Deliberately NOT guarded on DECENZA_SIMULATOR, even though the branches
    // that dereference m_simulator are: this setter and the member only need
    // the forward declaration, so callers (including main.cpp's teardown
    // `setSimulator(nullptr)`) compile in every configuration.
    //
    // #1629 is why. Two halves were needed and either alone was harmless: this
    // line carried a `#ifdef QT_DEBUG` guard, and #1629 added an *unguarded*
    // `setSimulator(nullptr)` teardown call in main.cpp. Together they broke the
    // nightly, which builds RelWithDebInfo, and would have broken the next
    // release tag — while every local debug build stayed green because QT_DEBUG
    // was defined there. Keeping the seam unguarded removes that whole class of
    // failure: a stray dereference now fails as an incomplete type instead.
    void setSimulator(DE1Simulator* simulator) { m_simulator = simulator; }

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
    void startAirPurge();         // Transport mode: drains internal water via AirPurge state
    void stopOperation();         // Soft stop (for steam: stops flow, no purge)
    void stopOperationUrgent();   // Front of the GATT queue, for a faster stop (SAW)
    void stopOperationUrgent(qint64 sawTriggerMs);  // Includes SAW trigger timestamp for latency tracing
    void requestIdle();           // Hard stop (requests Idle state, triggers steam purge)
    void skipToNextFrame();   // Skip to next profile frame during extraction (0x0E)
    // Returns true when the sleep command actually reached the transport.
    // False means it was dropped or deferred (flash in progress, profile
    // upload in flight, no transport) — callers that need the machine
    // genuinely asleep must check, not assume. Deliberately not [[nodiscard]]:
    // most callers are fire-and-forget UI actions where a deferred sleep is
    // fine, and only the firmware flash cares.
    bool goToSleep();
    void wakeUp();

    // Profile upload
    void uploadProfile(const Profile& profile);
    void clearCommandQueue();  // Clear all pending BLE commands (use when extraction starts)

    // Direct frame writing (for direct control mode)

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
    // intentionally diverge from Settings.steamTemperature()/the keep-warm setting
    // (e.g. startSteamHeating forces the heater on even when keepWarmWhenIdle
    // is false). Resending via sendMachineSettings() would clobber them.
    void resendLastShotSettings();

    // MMR write (for advanced settings like steam flow). Identical writes to
    // the same register are deduped against m_lastMMRValues, matching the
    // setShotSettings dedup pattern — the session log showed ~30 identical
    // flush-flow MMR bursts in 2.5 s when settings sliders emitted convergent
    // change signals. `reason` is an optional caller tag that appears in the
    // [DE1][MMR] write / write skipped log lines. Pass `force=true` to bypass the
    // dedup check (the DE1's USB-charger register has a 10-minute auto-enable
    // timeout that requires us to keep reasserting the commanded value).
    void writeMMR(uint32_t address, uint32_t value,
                  const QString& reason = QString(),
                  bool force = false);

    // MMR write placed at the FRONT of the shared GATT queue — used for
    // time-critical writes that must go out before the app suspends (e.g.
    // ensureChargerOn on iOS). It jumps everything waiting but not anything
    // already outstanding; nothing can preempt an accepted GATT operation.
    // Always bypasses the dedup check since "urgent" implies "must reach the
    // DE1 now". Still updates m_lastMMRValues so a subsequent non-urgent
    // writeMMR with the same value is correctly elided.
    void writeMMRUrgent(uint32_t address, uint32_t value,
                        const QString& reason = QString());


    // USB charger control (force=true to resend even if state unchanged, needed for DE1's 10-min timeout)
    void setUsbChargerOn(bool on, bool force = false);

    // Like setUsbChargerOn but puts the write at the FRONT of the shared GATT
    // queue rather than behind whatever else is waiting — a position, not a
    // bypass. Used by ensureChargerOn() on app exit and suspend; see
    // DE1Transport::writeUrgent and the two call sites in main.cpp for why the
    // posted dispatch still gets an event-loop turn on both paths.
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
    // Forwarded from the DE1 transport: a DE1-link fault (write retry /
    // write-failed / connection-teardown). Re-emitted here so consumers bind
    // once to the stable DE1Device and survive transport swaps.
    void de1LinkFault(const QString& kind);
    void simulationModeChanged();
    void guiEnabledChanged();
    void usbChargerOnChanged();
    void isHeadlessChanged();
    void refillKitDetectedChanged();
    // One signal for every descale progress property — they are all recomputed
    // together from the same tick, so separate notifies would only fan out the
    // same instant to four bindings.
    void descaleProgressChanged();
    void heaterVoltageChanged();

    // Firmware-update response from the DE1 (A009 notification). Carries
    // the parsed WindowIncrement, fwToErase/fwToMap flags and the 3-byte
    // FirstError. de1app's spec describes two Phase 1 notifications —
    // fwToErase=1 (erase in progress) then fwToErase=0 (erase complete) —
    // but v1333+ sends only the completion one. Phase 3 emits a notification
    // whose firstError == {0xFF,0xFF,0xFD} on success.
    //
    // windowIncrement is carried because it is part of what separates a
    // terminal response from an in-progress one. It is not sufficient alone:
    // FirstError == {0xFF,0xFF,0xFF} is the bootloader's "no error found"
    // value and also what the caller writes into the request, so it comes
    // back on in-progress notifications that are otherwise shaped exactly
    // like a verdict. See FirmwareUpdater::onFwMapResponse for the full
    // predicate.
    void fwMapResponse(uint16_t windowIncrement, uint8_t fwToErase,
                       uint8_t fwToMap, QByteArray firstError);
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

    // Firmware-flash guard shared by writeMMR / writeMMRUrgent. Returns true
    // (and logs a qWarning) when the caller should bail because a flash is in
    // progress; false means "proceed". `label` distinguishes the variant in the
    // log line ("write", "write urgent").
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

    // Generic one-shot MMR read with timeout + bounded retry, covering the
    // post-connect informational reads (GHC info, machine identity, heater
    // voltage, refill-kit status). Guards against a dropped READ_FROM_MMR
    // response notification (e.g. the post-connect subscription race) leaving a
    // value silently missing. See docs/CLAUDE_MD/BLE_PROTOCOL.md and the
    // harden-de1-ble-reliability change for the failure this fixes.
    void issueMMRReadWithRetry(uint32_t address, const QString& reason);
    void sendMMRReadRequest(uint32_t address) const;
    void checkMMRReadTimeouts();

    void sendInitialSettings();

    // Profile upload tracking (frame-ACK verification, modeled on de1app's
    // confirm_de1_send_shot_frames_worked). startProfileUploadTracking() must
    // be called BEFORE queuing the header/frame writes so the listener is
    // attached in time to observe every writeComplete.
    void startProfileUploadTracking(const QString& profileTitle,
                                    const QList<QByteArray>& frames);
    void onProfileUploadWriteComplete(const QBluetoothUuid& uuid,
                                      const QByteArray& data);

    // Drop the dedup-cache entry for an MMR write that never landed, and name
    // the register in the log. Both halves are load-bearing now that read-back
    // verification is gone — see the implementation.
    void onWriteAbandoned(const QBluetoothUuid& uuid, const QByteArray& data);
    void finishProfileUpload(bool success, const QString& reason = QString());

    // Withdraw an upload's own still-queued HEADER_WRITE/FRAME_WRITE commands
    // from the transport queue. Returns how many were dropped. See the call
    // sites for why an upload that has been superseded or has terminally
    // failed must not leave its frames queued ahead of the next attempt.
    qsizetype discardQueuedProfileWrites();

    // Writes the profile's tank_desired_water_temperature (clamped 0-45 °C)
    // to TANK_TEMP_THRESHOLD as part of every profile upload, so the next
    // profile overrides or clears the previous one's preheat. Direct-control
    // mode's raw header/frame write path does not pass through here and
    // leaves the previous profile's preheat active.
    void writeTankPreheatForProfile(const Profile& profile);

    // Descale progress, recomputed from the fixed step schedule plus the elapsed
    // time in the current step. Emits descaleProgressChanged() when a value moved.
    void updateDescaleProgress();
    // Maintenance states (descale/clean/air purge) that the machine refused while
    // cold, deferred until it reports that it has left preheat. See requestMaintenanceState().
    void requestMaintenanceState(DE1::State state);
    bool applyColdMaintenanceWorkaround(DE1::State state);
    bool isMachineHeating() const;
    void flushPendingMaintenanceState();

    // Owned when created internally via connectToDevice(); set externally via setTransport() for USB
    DE1Transport* m_transport = nullptr;
    bool m_ownsTransport = false;  // True when DE1Device created the transport (connectToDevice)

    DE1::State m_state = DE1::State::Sleep;
    DE1::SubState m_subState = DE1::SubState::Ready;

    // Descale step timing. The firmware reports no progress figure and no expected
    // duration, so the descale page's progress bar has to be built from measured
    // step weights — and the substates it would weight (DescaleInit .. DescaleSteam)
    // were never logged, so no submitted log contained a single step boundary.
    // Started when the machine enters Descale, read at each substate change.
    QElapsedTimer m_descaleTimer;
    qint64 m_descaleStepStartMs = 0;
    // Recomputes the progress properties once a second so the bar and the
    // remaining-time readout advance between substate boundaries, which can be
    // seven minutes apart. Periodic UI refresh, not a guard.
    QTimer* m_descaleTicker = nullptr;
    double m_descaleProgress = 0.0;
    int m_descaleStepIndex = 0;
    int m_descaleSecondsRemaining = 0;
    // Firmware runs the five steps more than once per descale, so a step alone does
    // not say how far along the machine is — the same "Descaling steam system" is
    // both a third of the way in and nearly done. Counted from the substate walking
    // BACKWARD (DescaleSteam back to a group step), which is the only signal the
    // firmware gives that a cycle restarted.
    int m_descaleCycle = 0;

    // Set when a maintenance request was held back because the machine was cold on
    // firmware that drops those requests (see applyColdMaintenanceWorkaround). Cleared
    // by the state packet that shows the machine has left preheat — an event, not a
    // timer, so a slow machine waits as long as it needs to.
    DE1::State m_pendingMaintenanceState = DE1::State::NoRequest;
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
    // Keepalive writes re-assert an unchanged value every 60 s forever (BatteryManager's USB-charger
    // refresh — the DE1 auto-disables the port after 10 minutes, so the re-assert is load-bearing
    // and cannot be dropped). Its LOG line, repeated 3,042 times in a 48-hour capture for 15% of the
    // whole log, can be, and now is not printed at all while the value holds. The events that remain
    // are the ones that carry something: a value CHANGE, which prints at once with the count of
    // re-asserts it stood for, and the disconnect flush. A 0 -> 1 or 1 -> 0 on this register is the
    // charger actually being commanded, and it is the only thing here a reader has ever needed.
    // Episodic: a run is one connected session. Flushed in onTransportDisconnected().
    LogCollapse m_keepaliveLog{LogCollapse::kChangesOnly};

    // Elided writes — the MMR per-register cache and the ShotSettings payload compare, both above.
    //
    // The line has one real use, and it is worth keeping exactly once: it answers "I changed that
    // setting and the machine ignored it" by showing the write was dropped as redundant and naming
    // the caller. What has no use is the repeat. Convergent callers re-apply the same values at
    // every phase transition and page activation, so one field session carried 65 of these, in
    // bursts of four, every burst byte-identical to the last — the answer to a question nobody had
    // asked yet, restated until it buried the writes that did go out.
    //
    // Keyed by the message TEXT, so the unit that prints once is the whole tuple (register or
    // payload, value, calling reason). A different caller eliding the same value is a different
    // line and still prints; the fortieth identical one does not.
    //
    // TWO instances, not one keyed across both. The key is the message text and the flush prints
    // its keys, so a shared table would have to pick one helper for lines belonging to two
    // different tags — sending [ShotSettings] text out under [MMR], which is precisely the
    // "one grep returns the whole subsystem" invariant the marker gate exists to hold. Splitting
    // the table is what keeps the flush able to name the right one.
    // Episodic: a run is one connected session. Both flushed in onTransportDisconnected().
    LogCollapse m_mmrSkipLog{LogCollapse::kChangesOnly};
    LogCollapse m_shotSettingsSkipLog{LogCollapse::kChangesOnly};
    // Pending one-shot MMR reads keyed by address — covers both the
    // post-connect informational reads issued via issueMMRReadWithRetry().
    // Cleared when parseMMRResponse() sees a response for that address,
    // when retries are exhausted, and on transport disconnect. A single sweep
    // timer (not one QTimer per address) checks every entry's deadline —
    // simpler than N timers for what is at most a handful of concurrent reads.
    struct PendingMMRRead {
        int attemptsRemaining;
        qint64 deadlineMs;
        QString reason;
    };
    QHash<uint32_t, PendingMMRRead> m_pendingMMRReads;
    QTimer m_mmrReadRetryTimer;
    // Matches Decaid's proven values for this exact DE1 BLE read pattern
    // (subscribe-before-write, 4s timeout, 2 retries) rather than reusing the
    // characteristic-write timeout/retry constants, which are tuned for a
    // different failure mode (a write that never gets a GATT-level ack).
    static constexpr int MMR_READ_TIMEOUT_MS = 4000;
    static constexpr int MMR_READ_MAX_RETRIES = 2;
    double m_waterLevel = 0.0;
    double m_waterLevelMm = 0.0;  // Smoothed mm value (sensor offset applied) — see parseWaterLevel
    int m_waterLevelMl = 0;       // Volume in ml (from CAD lookup table)
    double m_lastEmittedWaterLevel = -1.0;  // Throttle: only emit when change >= 0.5%
    int m_lastEmittedWaterLevelMl = -1;    // Throttle: also emit when ml changes (color thresholds)

    // WHY THE LEVEL IS SMOOTHED AT ALL, and why a hysteresis alone was not enough.
    //
    // Two different disturbances ride on this sensor and only one of them was ever accounted for.
    // The comment here used to describe "roughly +/-1 mm" of continuous dither, which is what the
    // tank does at IDLE, and a 2 mm hysteresis is comfortably above it. Under the pump it sloshes,
    // and a field log measured swings of 5.5 mm to 33.5 mm inside four seconds — every one of them
    // clearing 2 mm, so during the only period anyone reads these lines the gate passed on
    // essentially every sample. 220 lines in one 3.7-hour session, consecutive readings
    // contradicting each other by 10 mm.
    //
    // Raising the threshold is the tempting fix and it is the wrong one: it would have to exceed
    // the slosh amplitude, at which point it also swallows a slow drawdown, which is the one
    // trend worth having. The disturbance differs from the signal in FREQUENCY, not amplitude —
    // slosh is a second or two, a refill or a drawdown is not — so filter on that and leave the
    // threshold where it was. An EMA at this alpha has a time constant near 3 s at the observed
    // ~4 Hz sample rate: slosh is attenuated below the 2 mm gate, a refill still resolves within a
    // few seconds, and a drawdown is untouched.
    //
    // The smoothed value feeds the PROPERTY as well as the log, deliberately. The refill warning
    // and the tank colour band key off it, and both were being driven by a figure that swung a
    // third of the tank twice a second while the machine ran. Stability is what they wanted; the
    // cost is that a genuine step (tank lifted out) is reported ~3 s late, which no reading of a
    // water tank depends on.
    static constexpr double WATER_LEVEL_SMOOTHING_ALPHA = 0.08;
    // Seeded from the first sample rather than ramped from zero — otherwise every connect shows the
    // tank filling from empty over the filter's settling time.
    bool m_waterLevelSeeded = false;

    // Last water level LOGGED, which is not the last one EMITTED: the property follows every
    // smoothed sample, the log speaks only on a 2 mm move. Both endpoints are kept so the line can
    // state the DELTA it represents — a bare instantaneous reading cannot tell a reader whether
    // 168 ml left the tank through the group or was never there.
    double m_lastLoggedWaterLevelMm = -1000.0;
    int m_lastLoggedWaterLevelMl = -1;
    static constexpr double WATER_LEVEL_LOG_HYSTERESIS_MM = 2.0;
    QString m_firmwareVersion;
    int m_firmwareBuildNumber = 0;
    int m_machineModel = 0;
    int m_heaterVoltage = 0;  // 0=unknown, read from MMR HEATER_VOLTAGE
    uint32_t m_cpuBoardModel = 0;

    bool m_connecting = false;
    bool m_simulationMode = false;
    bool m_firmwareFlashInProgress = false;
    DE1Simulator* m_simulator = nullptr;  // For simulation mode
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
    QMetaObject::Connection m_uploadConnection;
    QTimer m_uploadTimeoutTimer;
    // Monotonic ms of the last SUCCESSFUL profile upload completion, or 0.
    // startEspresso() defers by up to PROFILE_UPLOAD_SETTLE_MS from this point
    // so it doesn't race the DE1 firmware's post-upload internal flash write
    // (see startEspresso()). Reset to 0 only on transport disconnect; it is
    // deliberately NOT zeroed when a deferral is armed — re-entrancy during the
    // window is gated by m_espressoStartDeferred, and the elapsed-time check
    // makes a retained (stale) timestamp harmless for a later unrelated start.
    qint64 m_lastProfileUploadCompleteMs = 0;
    // True while a startEspresso() is deferred waiting out the settle window,
    // so a concurrent second startEspresso() can't bypass it (see startEspresso()).
    bool m_espressoStartDeferred = false;
    // Stoppable single-shot timer that fires the deferred startEspresso(). Must
    // be cancellable (not QTimer::singleShot) so a disconnect inside the window
    // can abort the pending start — see onTransportDisconnected().
    QTimer m_espressoSettleTimer;
    // Decaid's proven profileDownloadGuard value for the same DE1 firmware
    // flash-write timing (see startEspresso()).
    static constexpr int PROFILE_UPLOAD_SETTLE_MS = 500;
    bool m_usbChargerOn = true;  // Default on (safe default like de1app)
    // True if the app may start operations (no GHC, or a GHC that is present
    // but inactive). Default TRUE, matching de1app, whose ghc_is_installed
    // defaults to 0 (ghc_required() == 0 → app can start). Only a positive GHC
    // read flips this false (see parseMMRResponse). Defaulting false would
    // brick every start button until the GHC MMR read returns — and forever if
    // that read is ever slow or dropped.
    bool m_isHeadless = true;
    int m_refillKitDetected = -1;  // -1=unknown, 0=not detected, 1=detected

    // SAW stop latency instrumentation (monotonic ms timestamps)
    // A stop-at-weight write that took this long to be acknowledged is reported
    // at WARN rather than DEBUG. 300 ms is roughly 0.6 g at a typical 2 ml/s
    // pour — the point where the overshoot is something a user would taste
    // rather than something lost in scale noise. Not a threshold anything acts
    // on; it only decides which tier the line is written at.
    static constexpr qint64 SAW_SLOW_ACK_WARN_MS = 300;

    bool m_sawStopWritePending = false;
    qint64 m_lastSawTriggerMs = 0;
    qint64 m_lastSawWriteMs = 0;

#ifdef DECENZA_TESTING
    friend class tst_SAV;
    friend class tst_MachineState;
    friend class tst_ProfileManager;
    friend class tst_MachineStatusSnapshot;
    friend class tst_MMRWrite;
    friend class tst_DE1DeviceFirmware;
    friend class tst_ShotSampleDecode;
    friend class tst_DE1DeviceMMRReads;
    friend class tst_DE1DeviceHeadless;
#endif
};
