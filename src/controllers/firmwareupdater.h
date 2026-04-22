#pragma once

#include <QBluetoothUuid>
#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include "core/firmwareassetcache.h"
#include "core/firmwareheader.h"

class DE1Device;

// Orchestrates the three-phase DE1 firmware update (erase → upload → verify)
// over BLE, using DE1Device for the wire writes and FirmwareAssetCache for
// source download + file validation. Lives as a peer of SteamCalibrator and
// UpdateChecker under MainController.
//
// State machine (see docs/plans/2026-04-20-firmware-update-design.md §5):
//   Idle → Checking (checkForUpdate) → Idle (with updateAvailable=true/false)
//   Idle/Ready → Downloading (startUpdate; skipped if cache already valid)
//   Ready → Erasing → Uploading → Verifying → Succeeded
//   any → Failed on disconnect / timeout / verify error
//
// Qt-timing knobs are injectable so tests can fast-forward through what would
// otherwise be a 45-second real-world flash.

class FirmwareUpdater : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(bool isDowngrade READ isDowngrade NOTIFY availabilityChanged)
    // True while wired to the DE1 simulator (no real BLE). The check,
    // download, and version surfaces still run so the page is usable;
    // only the flash itself is blocked.
    Q_PROPERTY(bool isSimulated READ isSimulated NOTIFY isSimulatedChanged)
    // True while the state machine is actively talking to the DE1 over
    // BLE (Erasing, Uploading, or Verifying). Used by the root QML to
    // suppress the sleep countdown / screensaver for the ~15-minute
    // flash duration.
    Q_PROPERTY(bool isFlashing READ isFlashing NOTIFY stateChanged)
    Q_PROPERTY(int availableVersion READ availableVersion NOTIFY availabilityChanged)
    Q_PROPERTY(int installedVersion READ installedVersion NOTIFY installedVersionChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(bool retryAvailable READ retryAvailable NOTIFY stateChanged)
    // True while we need the user to flip the DE1's power switch for the
    // new firmware to take effect. Set as soon as we enter AwaitingReboot
    // (verify succeeded) and cleared on Succeeded/Failed. The UI surfaces
    // a "power-cycle the DE1" instruction when this is true.
    Q_PROPERTY(bool needsManualReboot READ needsManualReboot NOTIFY needsManualRebootChanged)

public:
    enum class State {
        Idle,
        Checking,
        Downloading,
        Ready,
        Erasing,
        Uploading,
        Verifying,
        Succeeded,
        Failed,
        // The bootloader confirmed the new bank verifies, but the DE1 is
        // still running the old firmware. In practice the DE1 never auto-
        // reboots (captured on every flash we've instrumented), so we
        // prompt the user to power-cycle immediately and stay in this
        // state until the DE1 disconnects + reconnects reporting the
        // expected version. Added after #822 — appending rather than
        // inserting so existing numeric values are stable for QML/tests.
        AwaitingReboot
    };
    Q_ENUM(State)

    explicit FirmwareUpdater(DE1Device* device,
                             DE1::Firmware::FirmwareAssetCache* cache,
                             QObject* parent = nullptr);
    ~FirmwareUpdater() override;

    // Dependency injection / test hooks --------------------------------

    // Supplies the current DE1-installed firmware version (from MMR 0x800010
    // typically). Called on every checkForUpdate() / startUpdate() to decide
    // Newer-vs-Same and to race-guard before erase.
    void setInstalledVersionProvider(std::function<uint32_t()> fn);

    // Supplies the "is it OK to start a firmware update right now?" gate.
    // Typical wiring checks MachineState::phase against Sleep/Idle/Heating/
    // Ready and rejects anything else (espresso, steam, flush, descale,
    // clean). Not-set is treated as "yes, allow".
    void setPreconditionProvider(std::function<bool()> fn);

    // Timing knobs (defaults match the spec). Tests set these to small
    // values to avoid minute-long test runs.
    void setPostEraseWaitMs(int ms);          // default 10000 (Android) or 1000 (other)
    void setChunkPumpIntervalMs(int ms);      // default 1
    void setEraseTimeoutMs(int ms);           // default 30000
    void setVerifyTimeoutMs(int ms);          // default 60000
    void setVerifyDisconnectGraceMs(int ms);  // default 180000 (3 min)
    void setPostUploadSettleMs(int ms);       // default 1500

    // Read-only state -----------------------------------------------

    State state() const { return m_state; }
    QString stateText() const;
    bool updateAvailable() const { return m_updateAvailable; }
    // True when the available firmware is *older* than what's installed.
    // Mirrors de1app's "Firmware downgrade available" affordance: the user
    // is allowed to flash it, but the UI should label it as a downgrade.
    bool isDowngrade() const { return m_isDowngrade; }
    bool isSimulated() const;
    bool isFlashing() const {
        return m_state == State::Erasing ||
               m_state == State::Uploading ||
               m_state == State::Verifying ||
               m_state == State::AwaitingReboot;
    }
    bool needsManualReboot() const { return m_needsManualReboot; }
    int availableVersion() const { return static_cast<int>(m_availableVersion); }
    int installedVersion() const;  // logs [firmware] getter call so we can verify QML reads it
    double progress() const { return m_progress; }
    QString errorMessage() const { return m_errorMessage; }
    bool retryAvailable() const { return m_retryAvailable; }

    // User-invocable actions ----------------------------------------

    Q_INVOKABLE void checkForUpdate();
    Q_INVOKABLE void startUpdate();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void dismissAvailability();

    // Reset state + errorMessage if we're currently in Failed. Used before
    // a programmatic re-check (e.g. channel-toggle re-check) so a stale
    // Failed state from a prior attempt doesn't bleed into the new flow
    // as a red "Update failed" strip flashing under the toggle. No-op in
    // any non-Failed state.
    void dismissLingeringFailure();

signals:
    void stateChanged();
    void availabilityChanged();
    void progressChanged();
    void installedVersionChanged();
    void isSimulatedChanged();
    void needsManualRebootChanged();

private slots:
    void onCheckFinished(DE1::Firmware::FirmwareAssetCache::CheckResult result);
    void onDownloadFinished(QString path, DE1::Firmware::Header header);
    void onDownloadFailed(QString reason);
    void onDownloadProgress(qint64 received, qint64 total);
    void onFwMapResponse(uint8_t fwToErase, uint8_t fwToMap, QByteArray firstError);
    void onDeviceConnectionChanged();
    void onDeviceFirmwareVersionChanged();
    void onPostEraseWaitComplete();
    void onChunkPumpTick();
    void onFirmwareWriteAcked(const QBluetoothUuid& uuid, const QByteArray& data);
    void onEraseTimeout();
    void onVerifyTimeout();
    void onVerifyDisconnectGrace();

private:
    void setState(State newState);
    void setProgress(double p);
    void failWith(const QString& reason, bool retryable);
    void beginErasePhase();
    void beginUploadPhase();
    void beginVerifyPhase();
    void completeSuccess();
    void loadCachedPayload();

    DE1Device* m_device = nullptr;
    DE1::Firmware::FirmwareAssetCache* m_cache = nullptr;

    State       m_state            = State::Idle;
    bool        m_updateAvailable  = false;
    bool        m_isDowngrade      = false;
    uint32_t    m_availableVersion = 0;
    uint32_t    m_installedVersion = 0;
    double      m_progress         = 0.0;
    QString     m_errorMessage;
    bool        m_retryAvailable   = false;

    // Cached firmware bytes for the chunk pump. Loaded once at the start
    // of uploading so we don't re-read the ~453 KB file for every chunk.
    //
    // Two counters, because "queued" (what we've handed to BleTransport's
    // command queue) and "acked" (what the DE1 has actually confirmed
    // over BLE) diverge significantly on Android — we can queue 28k chunks
    // in seconds but BLE drains them at ~15 ms/ACK. The progress bar and
    // the verify-trigger gate both use m_chunksAcked so the user sees real
    // progress and verify doesn't race ahead of the wire.
    QByteArray  m_firmwareBytes;
    qsizetype   m_chunksTotal      = 0;
    qsizetype   m_chunksQueued     = 0;   // handed to BleTransport
    qsizetype   m_chunksAcked      = 0;   // confirmed by DE1 via writeComplete

    // Erase state: de1app expects *two* notifications — first fwToErase=1
    // (erase in progress), then fwToErase=0 (erase complete). We only
    // proceed after the second.
    bool        m_eraseInProgressSeen = false;

    // Dismissed-version memory. When the user taps the banner's "x",
    // availableVersion is pinned here; subsequent checks that return
    // the same version don't re-surface the banner. A strictly newer
    // remote version clears the pin via onCheckFinished.
    uint32_t    m_dismissedVersion = 0;

    // Verify-disconnect grace window: when the DE1 disconnects during
    // Verifying or AwaitingReboot, we wait briefly to see if the
    // post-reconnect version matches what we just flashed before
    // classifying the outcome. This is the primary success path under
    // the current model — the DE1 doesn't auto-reboot, so completion
    // runs through a user power-cycle + reconnect, not a direct
    // verify → Succeeded transition.
    bool        m_verifyingAmbiguous    = false;
    QTimer      m_verifyDisconnectGrace;
    // Generous default so a user power-cycle (sees prompt, walks to the
    // machine, flips switch, waits for DE1 BLE stack to come back) doesn't
    // trip a spurious "did not reconnect after verify" failure. Real-world
    // DE1 boot + BLE rediscovery is typically 30–60 s; 180 s leaves room
    // for slow reconnects on Android. Tests override via setter to keep
    // runtime short.
    int         m_verifyDisconnectGraceMs = 180000;

    // True between verify-success notification and the final Succeeded
    // transition. Persists through a spurious Failed state so that if the
    // DE1 eventually comes back on the new firmware (e.g. a boot took
    // longer than the grace window), onDeviceFirmwareVersionChanged can
    // retroactively flip to Succeeded instead of the user re-flashing via
    // Retry.
    bool        m_flashCompleted       = false;

    // Set the moment verify succeeds so the UI prompts the user to
    // power-cycle immediately. Cleared on Succeeded/Failed.
    bool        m_needsManualReboot    = false;

    QTimer      m_postEraseWaitTimer;
    QTimer      m_chunkPumpTimer;
    QTimer      m_eraseTimeoutTimer;
    QTimer      m_verifyTimeoutTimer;

    // Wall clock since the user tapped "Update now". Prefixes [firmware]
    // log lines with "[+MM:SS.ms]" so a later review of logcat can tell
    // at a glance how long each phase took.
    QElapsedTimer m_updateTimer;

    // NB: these inline initializers are the actual defaults. The
    // constexpr DEFAULT_*_MS values in firmwareupdater.cpp are advisory
    // documentation only — they're never used to construct these members.
    int         m_postEraseWaitMs      = 0;       // overridden in ctor based on OS
    int         m_chunkPumpIntervalMs  = 1;
    int         m_eraseTimeoutMs       = 30000;
    int         m_verifyTimeoutMs      = 60000;   // bumped from 10000; see below
    int         m_postUploadSettleMs   = 1500;    // delay after last ACK → verify

    std::function<uint32_t()> m_installedVersionProvider;
    std::function<bool()>     m_preconditionProvider;

    friend class tst_FirmwareUpdater;
};
