#pragma once

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
    Q_PROPERTY(int availableVersion READ availableVersion NOTIFY availabilityChanged)
    Q_PROPERTY(int installedVersion READ installedVersion NOTIFY installedVersionChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(bool retryAvailable READ retryAvailable NOTIFY stateChanged)

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
        Failed
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
    void setVerifyTimeoutMs(int ms);          // default 10000
    void setVerifyDisconnectGraceMs(int ms);  // default 15000

    // Read-only state -----------------------------------------------

    State state() const { return m_state; }
    QString stateText() const;
    bool updateAvailable() const { return m_updateAvailable; }
    int availableVersion() const { return static_cast<int>(m_availableVersion); }
    int installedVersion() const { return static_cast<int>(m_installedVersion); }
    double progress() const { return m_progress; }
    QString errorMessage() const { return m_errorMessage; }
    bool retryAvailable() const { return m_retryAvailable; }

    // User-invocable actions ----------------------------------------

    Q_INVOKABLE void checkForUpdate();
    Q_INVOKABLE void startUpdate();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void dismissAvailability();

signals:
    void stateChanged();
    void availabilityChanged();
    void progressChanged();
    void installedVersionChanged();

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
    uint32_t    m_availableVersion = 0;
    uint32_t    m_installedVersion = 0;
    double      m_progress         = 0.0;
    QString     m_errorMessage;
    bool        m_retryAvailable   = false;

    // Cached firmware bytes for the chunk pump. Loaded once at the start
    // of uploading so we don't re-read the ~453 KB file for every chunk.
    QByteArray  m_firmwareBytes;
    qsizetype   m_chunksTotal      = 0;
    qsizetype   m_chunksSent       = 0;

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
    // Verifying (which commonly means a successful reboot rather than a
    // failure), we wait briefly to see if the post-reboot version matches
    // what we just flashed before classifying the outcome.
    bool        m_verifyingAmbiguous    = false;
    QTimer      m_verifyDisconnectGrace;
    int         m_verifyDisconnectGraceMs = 15000;

    QTimer      m_postEraseWaitTimer;
    QTimer      m_chunkPumpTimer;
    QTimer      m_eraseTimeoutTimer;
    QTimer      m_verifyTimeoutTimer;

    int         m_postEraseWaitMs      = 0;   // set in ctor based on OS
    int         m_chunkPumpIntervalMs  = 1;
    int         m_eraseTimeoutMs       = 30000;
    int         m_verifyTimeoutMs      = 10000;

    std::function<uint32_t()> m_installedVersionProvider;
    std::function<bool()>     m_preconditionProvider;

    friend class tst_FirmwareUpdater;
};
