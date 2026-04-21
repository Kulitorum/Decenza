#include "controllers/firmwareupdater.h"

#include <QDebug>
#include <QFile>
#include <QLoggingCategory>
#include <QOperatingSystemVersion>

#include "ble/de1device.h"
#include "ble/de1transport.h"
#include "ble/protocol/firmwarepackets.h"

#include <memory>

// Shared logging category with FirmwareAssetCache (defined there). Field
// bug reports can grep "decenza.firmware" in the AsyncLogger output to
// recover the full timeline of a failed update without sifting through
// unrelated BLE/HTTP traffic.
Q_DECLARE_LOGGING_CATEGORY(firmwareLog)

using namespace DE1::Firmware;

namespace {

constexpr int DEFAULT_POST_ERASE_WAIT_ANDROID_MS = 10000;
constexpr int DEFAULT_POST_ERASE_WAIT_OTHER_MS   = 1000;
constexpr int DEFAULT_CHUNK_PUMP_INTERVAL_MS     = 1;
constexpr int DEFAULT_ERASE_TIMEOUT_MS           = 30000;
constexpr int DEFAULT_VERIFY_TIMEOUT_MS          = 60000;
// Settle delay between the last firmware chunk and the verify request.
// Matches Kal Freese's reference Python (asyncio.sleep(1)) — gives the
// DE1's BLE queue time to drain the upload tail before we ask "did
// it work?". Without this, the verify request lands behind the last
// few chunks still in flight and the DE1 either ignores it or its
// response races our timeout.
constexpr int POST_UPLOAD_SETTLE_MS              = 1500;

// Progress weighting so the bar doesn't sit at 0% and 100% for seconds at
// a time. See docs/plans/2026-04-20-firmware-update-design.md §5.3.
constexpr double PROGRESS_ERASE_MAX  = 0.10;
constexpr double PROGRESS_UPLOAD_MAX = 0.90;

int defaultPostEraseWaitMs() {
    return (QOperatingSystemVersion::currentType() == QOperatingSystemVersion::Android)
        ? DEFAULT_POST_ERASE_WAIT_ANDROID_MS
        : DEFAULT_POST_ERASE_WAIT_OTHER_MS;
}

// Format an elapsed count as "[+MM:SS.ms]" — fits long uploads and is
// trivially greppable with /\[\+\d+:/ to strip back to phase order.
QString formatElapsed(qint64 ms) {
    if (ms < 0) return QStringLiteral("[+--:--.---]");
    const qint64 secs = ms / 1000;
    const qint64 mins = secs / 60;
    return QStringLiteral("[+%1:%2.%3]")
        .arg(mins,       2, 10, QLatin1Char('0'))
        .arg(secs % 60,  2, 10, QLatin1Char('0'))
        .arg(ms % 1000,  3, 10, QLatin1Char('0'));
}

}  // namespace

FirmwareUpdater::FirmwareUpdater(DE1Device* device, FirmwareAssetCache* cache,
                                 QObject* parent)
    : QObject(parent)
    , m_device(device)
    , m_cache(cache)
    , m_postEraseWaitMs(defaultPostEraseWaitMs())
{
    m_postEraseWaitTimer.setSingleShot(true);
    m_eraseTimeoutTimer.setSingleShot(true);
    m_verifyTimeoutTimer.setSingleShot(true);
    m_verifyDisconnectGrace.setSingleShot(true);
    m_chunkPumpTimer.setInterval(m_chunkPumpIntervalMs);

    connect(&m_postEraseWaitTimer, &QTimer::timeout,
            this, &FirmwareUpdater::onPostEraseWaitComplete);
    connect(&m_chunkPumpTimer, &QTimer::timeout,
            this, &FirmwareUpdater::onChunkPumpTick);
    connect(&m_eraseTimeoutTimer, &QTimer::timeout,
            this, &FirmwareUpdater::onEraseTimeout);
    connect(&m_verifyTimeoutTimer, &QTimer::timeout,
            this, &FirmwareUpdater::onVerifyTimeout);
    connect(&m_verifyDisconnectGrace, &QTimer::timeout,
            this, &FirmwareUpdater::onVerifyDisconnectGrace);

    if (m_cache) {
        connect(m_cache, &FirmwareAssetCache::checkFinished,
                this, &FirmwareUpdater::onCheckFinished);
        connect(m_cache, &FirmwareAssetCache::downloadFinished,
                this, &FirmwareUpdater::onDownloadFinished);
        connect(m_cache, &FirmwareAssetCache::downloadFailed,
                this, &FirmwareUpdater::onDownloadFailed);
        connect(m_cache, &FirmwareAssetCache::downloadProgress,
                this, &FirmwareUpdater::onDownloadProgress);
    }

    if (m_device) {
        connect(m_device, &DE1Device::fwMapResponse,
                this, &FirmwareUpdater::onFwMapResponse);
        connect(m_device, &DE1Device::connectedChanged,
                this, &FirmwareUpdater::onDeviceConnectionChanged);
        // Re-pull installedVersion when the DE1 reports a new firmware
        // build number (happens once on connect when MMR 0x800010 is parsed,
        // and again after a successful flash + reboot). Without this, the
        // Settings → Firmware tab would show "—" for the first 30 seconds
        // after app start (until the scheduled check runs) even when the
        // DE1 has long since reported its version.
        connect(m_device, &DE1Device::firmwareVersionChanged,
                this, &FirmwareUpdater::onDeviceFirmwareVersionChanged);
        connect(m_device, &DE1Device::simulationModeChanged,
                this, &FirmwareUpdater::isSimulatedChanged);
        // The writeComplete subscription is deferred to beginUploadPhase —
        // at construction time m_device->transport() is still null (the
        // BLE transport is attached later, when the user connects the DE1),
        // so a connect here would silently no-op and we'd never receive
        // ACK signals.
    }
}

void FirmwareUpdater::onDeviceFirmwareVersionChanged() {
    if (!m_installedVersionProvider) return;
    const uint32_t v = m_installedVersionProvider();
    if (v != m_installedVersion) {
        m_installedVersion = v;
        qCDebug(firmwareLog) << "[firmware] installed version refreshed:" << v;
        emit installedVersionChanged();
    }
}

void FirmwareUpdater::onDeviceConnectionChanged() {
    if (!m_device) return;

    // Reconnect path. If we were in ambiguous-verify (disconnected during
    // the verify phase, which commonly means a successful post-flash
    // reboot rather than a real failure), re-read the installed version
    // and decide: match → retroactive success; mismatch → keep waiting
    // for more version updates; timeout → fail.
    if (m_device->isConnected()) {
        if (m_verifyingAmbiguous) {
            const uint32_t installed = m_installedVersionProvider
                ? m_installedVersionProvider() : m_installedVersion;
            m_installedVersion = installed;
            if (installed >= m_availableVersion) {
                m_verifyingAmbiguous = false;
                m_verifyDisconnectGrace.stop();
                completeSuccess();
            }
        }
        return;
    }

    // Disconnect path. Only acts on phases that are actively talking to the
    // DE1. Idle / Ready / Succeeded / Failed ignore disconnects.
    switch (m_state) {
        case State::Erasing:
        case State::Uploading:
            failWith(QStringLiteral("DE1 disconnected during firmware update"),
                     /*retryable*/ true);
            break;
        case State::Verifying:
            // Ambiguous — don't classify yet. Open a 15 s grace window to
            // see whether the device comes back reporting the new version
            // (successful reboot) or stays away / comes back with the old
            // version (genuine failure).
            m_verifyingAmbiguous = true;
            m_verifyTimeoutTimer.stop();
            m_verifyDisconnectGrace.start(m_verifyDisconnectGraceMs);
            break;
        default:
            break;
    }
}

void FirmwareUpdater::onVerifyDisconnectGrace() {
    if (!m_verifyingAmbiguous) return;
    m_verifyingAmbiguous = false;
    failWith(QStringLiteral("DE1 did not reconnect after verify"),
             /*retryable*/ true);
}

FirmwareUpdater::~FirmwareUpdater() {
    // If we're torn down mid-flash (e.g. app shutdown, test cleanup), the
    // DE1Device likely outlives us — it's owned separately by main.cpp.
    // Leaving m_firmwareFlashInProgress stuck on would silently drop every
    // subsequent MMR write for the life of the process, with no obvious
    // symptom at teardown time.
    if (m_device && m_device->firmwareFlashInProgress()) {
        m_device->setFirmwareFlashInProgress(false);
    }
}

// ---- Injection hooks ----------------------------------------------------

void FirmwareUpdater::setInstalledVersionProvider(std::function<uint32_t()> fn) {
    m_installedVersionProvider = std::move(fn);
    // Pull immediately in case DE1Device already reported its version
    // before this provider was wired up (the firmwareVersionChanged signal
    // fires once and is gone — there's no replay).
    if (m_installedVersionProvider) {
        const uint32_t v = m_installedVersionProvider();
        if (v != m_installedVersion) {
            m_installedVersion = v;
            emit installedVersionChanged();
        }
    }
}

void FirmwareUpdater::setPreconditionProvider(std::function<bool()> fn) {
    m_preconditionProvider = std::move(fn);
}

void FirmwareUpdater::setPostEraseWaitMs(int ms) { m_postEraseWaitMs = ms; }

void FirmwareUpdater::setChunkPumpIntervalMs(int ms) {
    m_chunkPumpIntervalMs = ms;
    m_chunkPumpTimer.setInterval(ms);
}

void FirmwareUpdater::setEraseTimeoutMs(int ms)            { m_eraseTimeoutMs            = ms; }
void FirmwareUpdater::setVerifyTimeoutMs(int ms)           { m_verifyTimeoutMs           = ms; }
void FirmwareUpdater::setVerifyDisconnectGraceMs(int ms)   { m_verifyDisconnectGraceMs   = ms; }
void FirmwareUpdater::setPostUploadSettleMs(int ms)        { m_postUploadSettleMs        = ms; }

// ---- Read-only state helpers -------------------------------------------

int FirmwareUpdater::installedVersion() const {
    return static_cast<int>(m_installedVersion);
}

bool FirmwareUpdater::isSimulated() const {
    return m_device && m_device->simulationMode();
}

QString FirmwareUpdater::stateText() const {
    switch (m_state) {
        case State::Idle:        return QStringLiteral("Idle");
        case State::Checking:    return QStringLiteral("Checking for update");
        case State::Downloading: return QStringLiteral("Downloading firmware");
        case State::Ready:       return QStringLiteral("Ready to install");
        case State::Erasing:     return QStringLiteral("Erasing flash");
        case State::Uploading:   return QStringLiteral("Uploading firmware");
        case State::Verifying:   return QStringLiteral("Verifying");
        case State::Succeeded:   return QStringLiteral("Update complete");
        case State::Failed:      return QStringLiteral("Update failed");
    }
    return QString();
}

void FirmwareUpdater::setState(State newState) {
    if (m_state == newState) return;
    qCDebug(firmwareLog).noquote()
        << formatElapsed(m_updateTimer.isValid() ? m_updateTimer.elapsed() : -1)
        << "[firmware] state:" << stateText() << "->" << [&]{
            const State old = m_state; m_state = newState;
            const QString s = stateText(); m_state = old;
            return s;
        }();
    m_state = newState;
    emit stateChanged();
}

void FirmwareUpdater::setProgress(double p) {
    if (p < 0.0) p = 0.0;
    if (p > 1.0) p = 1.0;
    if (qFuzzyCompare(1.0 + m_progress, 1.0 + p)) return;
    m_progress = p;
    emit progressChanged();
}

// ---- Public actions -----------------------------------------------------

void FirmwareUpdater::checkForUpdate() {
    if (!m_cache) return;
    // Simulator: allow the check so the page populates (installed/available
    // versions, channel state). Only the actual flash is blocked — see
    // startUpdate().
    if (m_state == State::Checking) return;
    if (!m_updateTimer.isValid()) m_updateTimer.start();  // reset for a fresh check
    const uint32_t installed = m_installedVersionProvider
        ? m_installedVersionProvider() : m_installedVersion;
    m_installedVersion = installed;
    qCDebug(firmwareLog).noquote()
        << formatElapsed(m_updateTimer.elapsed())
        << "[firmware] check started, installed=" << installed;
    setState(State::Checking);
    m_cache->checkForUpdate(installed);
}

void FirmwareUpdater::startUpdate() {
    if (!m_cache || !m_device) return;

    // Simulator: refuse any flash so we don't stream fake bytes onto a
    // pretend BLE channel and confuse the state machine. The QML gates
    // the Update button on `isSimulated` so this path is normally
    // unreachable via the UI — but keep the guard as a hard safety net
    // against direct invocation (MCP, tests, remote control).
    if (m_device->simulationMode()) {
        qCDebug(firmwareLog) << "[firmware] startUpdate refused (simulator)";
        return;
    }

    // Precondition: delegate to the caller-supplied predicate. If unset,
    // treat as "yes, allow" so unit tests can skip the check when they're
    // focused on later phases.
    if (m_preconditionProvider && !m_preconditionProvider()) {
        // Precondition failure — user can retry once the shot/steam
        // finishes. Not the same as a permanently-bad firmware file.
        m_errorMessage = QStringLiteral("Finish current operation first");
        m_retryAvailable = true;
        setState(State::Failed);
        return;
    }

    // Reset the elapsed clock so the log prefix for this flash attempt
    // starts at [+00:00.000] — gives reviewers a crisp "how long did
    // each phase take" reading without mental math across sessions.
    m_updateTimer.restart();

    // Download (or short-circuit if already cached and valid).
    setState(State::Downloading);
    m_cache->downloadIfNeeded();
}

void FirmwareUpdater::retry() {
    if (!m_retryAvailable) return;
    m_errorMessage.clear();
    m_retryAvailable = false;
    setProgress(0.0);
    startUpdate();
}

void FirmwareUpdater::dismissLingeringFailure() {
    if (m_state != State::Failed) return;
    m_errorMessage.clear();
    m_retryAvailable = false;
    setProgress(0.0);
    setState(State::Idle);
}

void FirmwareUpdater::dismissAvailability() {
    if (!m_updateAvailable) return;
    // Pin the dismissed version so a subsequent check that returns the
    // same remote version doesn't re-open the banner. A strictly newer
    // version clears the pin in onCheckFinished.
    m_dismissedVersion = m_availableVersion;
    m_updateAvailable = false;
    emit availabilityChanged();
}

// ---- Asset-cache callbacks ---------------------------------------------

void FirmwareUpdater::onCheckFinished(FirmwareAssetCache::CheckResult result) {
    if (m_state != State::Checking && m_state != State::Idle) return;
    m_availableVersion = result.remoteVersion;
    // updateAvailable reflects the pure version comparison: the simulator
    // gate lives in startUpdate() and in the QML (`isSimulated`) so users
    // can still see what *would* be flashable against a real DE1.
    const bool offersFlash =
        (result.kind == FirmwareAssetCache::CheckResult::Newer ||
         result.kind == FirmwareAssetCache::CheckResult::Older);
    m_isDowngrade = (result.kind == FirmwareAssetCache::CheckResult::Older);
    if (offersFlash) {
        // The dismissed-version pin applies to both directions: once the
        // user hides the banner for a given remote version, don't re-open
        // it until the remote version changes (in either direction).
        if (result.remoteVersion != m_dismissedVersion) {
            m_dismissedVersion = 0;
            m_updateAvailable  = true;
        } else {
            m_updateAvailable  = false;
        }
    } else {
        m_updateAvailable = false;
    }
    qCDebug(firmwareLog).noquote()
        << "[firmware] check finished: remote=" << result.remoteVersion
        << " kind=" << (result.kind == FirmwareAssetCache::CheckResult::Newer ? "Newer"
                       : result.kind == FirmwareAssetCache::CheckResult::Older ? "Older"
                       : result.kind == FirmwareAssetCache::CheckResult::Same  ? "Same"
                                                                                : "Error")
        << " updateAvailable=" << m_updateAvailable
        << " isDowngrade=" << m_isDowngrade
        << (result.errorDetail.isEmpty() ? QString() : QStringLiteral(" err=") + result.errorDetail);
    emit availabilityChanged();
    setState(State::Idle);
}

void FirmwareUpdater::onDownloadFinished(QString path, Header header) {
    Q_UNUSED(path);
    Q_UNUSED(header);
    if (m_state != State::Downloading) return;

    // Race guard: re-read installed version. If it *equals* the cached
    // header's version, there's nothing to do — short-circuit to Succeeded.
    // A strict mismatch (newer *or* older) proceeds: de1app allows
    // downgrades (matching channel swap: nightly → stable), so we only
    // bail on true equality, not on "installed >= available".
    const uint32_t currentInstalled = m_installedVersionProvider
        ? m_installedVersionProvider() : m_installedVersion;
    m_installedVersion = currentInstalled;
    if (currentInstalled == header.version) {
        m_updateAvailable = false;
        emit availabilityChanged();
        setState(State::Succeeded);
        return;
    }

    m_availableVersion = header.version;
    setState(State::Ready);
    beginErasePhase();
}

void FirmwareUpdater::onDownloadFailed(QString reason) {
    if (m_state != State::Downloading) return;
    failWith(reason, /*retryable*/ true);
}

void FirmwareUpdater::onDownloadProgress(qint64 received, qint64 total) {
    if (m_state != State::Downloading || total <= 0) return;
    // Download phase is counted as half of pre-erase work. We don't have a
    // visible progress bar for "checking" vs "downloading" in the spec, so
    // any download-phase progress just previews up to the erase phase's
    // starting point.
    const double frac = double(received) / double(total);
    setProgress(frac * PROGRESS_ERASE_MAX);
}

// ---- Phase 1: Erase ----------------------------------------------------

void FirmwareUpdater::beginErasePhase() {
    if (!m_device) {
        failWith(QStringLiteral("No DE1 device configured"), false);
        return;
    }
    m_eraseInProgressSeen = false;
    // Engage the MMR-write guard on the device *before* we subscribe or
    // write anything. Firmware chunks share the WRITE_TO_MMR characteristic
    // with regular MMR writes (distinguished only by the length byte), so a
    // stray MMR write interleaved with chunks would corrupt the flash. The
    // guard is cleared in completeSuccess() and failWith().
    m_device->setFirmwareFlashInProgress(true);
    setState(State::Erasing);
    m_device->subscribeFirmwareNotifications();
    m_device->writeFWMapRequest(/*erase*/ 1, /*map*/ 1);
    setProgress(0.02);  // visible motion as Phase 1 starts

    // Match de1app's behaviour: don't gate the next phase on receiving the
    // erase-complete notification. de1app sends the erase command and waits
    // a fixed OS-appropriate delay (10 s Android / 1 s other) before
    // starting the chunk pump — see de1_comms.tcl:913 / :920. The
    // notifications are informational; if Android BLE drops the CCCD
    // subscription or the DE1 doesn't emit them on this firmware, we
    // would otherwise hang in Erasing forever waiting for a signal that
    // never arrives. Use the post-erase timer as the single source of
    // truth for "erase done, start uploading".
    qCDebug(firmwareLog) << "[firmware] erase command sent, waiting"
                         << m_postEraseWaitMs << "ms before chunk pump";
    if (m_postEraseWaitMs <= 0) {
        onPostEraseWaitComplete();
    } else {
        m_postEraseWaitTimer.start(m_postEraseWaitMs);
    }
    // Belt-and-suspenders: if even the post-erase wait expires without us
    // having moved on (shouldn't happen, but a safety net), the erase
    // timeout still fires.
    m_eraseTimeoutTimer.start(m_eraseTimeoutMs);
}

void FirmwareUpdater::onEraseTimeout() {
    if (m_state != State::Erasing) return;
    failWith(QStringLiteral("Erase did not complete. Retry, or power-cycle the DE1."),
             /*retryable*/ true);
}

// ---- Phase 2: Upload ---------------------------------------------------

void FirmwareUpdater::beginUploadPhase() {
    loadCachedPayload();
    if (m_firmwareBytes.isEmpty()) {
        failWith(QStringLiteral("Firmware file missing or unreadable"), true);
        return;
    }
    m_chunksTotal  = (m_firmwareBytes.size() + 15) / 16;  // ceil to 16-byte blocks
    m_chunksQueued = 0;
    m_chunksAcked  = 0;

    // Subscribe to the transport's per-write ACK now. Deferred to this
    // point because m_device->transport() is not guaranteed to be set at
    // FirmwareUpdater construction time — the transport is attached when
    // the DE1 first connects, which may be after MainController wired us
    // up. By the time we reach beginUploadPhase the transport is alive
    // (we just completed the erase handshake through it). Use a
    // Qt::UniqueConnection so repeated updates (retry, second firmware
    // cycle) don't stack handlers.
    if (auto* t = m_device ? m_device->transport() : nullptr) {
        connect(t, &DE1Transport::writeComplete,
                this, &FirmwareUpdater::onFirmwareWriteAcked,
                Qt::UniqueConnection);
    } else {
        qCWarning(firmwareLog) << "[firmware] beginUploadPhase: no transport — "
                                  "progress/verify will stall";
    }

    setState(State::Uploading);
    setProgress(PROGRESS_ERASE_MAX);
    m_chunkPumpTimer.start();
}

void FirmwareUpdater::loadCachedPayload() {
    if (!m_cache) {
        m_firmwareBytes.clear();
        return;
    }
    QFile f(m_cache->cachePath());
    if (!f.open(QIODevice::ReadOnly)) {
        m_firmwareBytes.clear();
        return;
    }
    m_firmwareBytes = f.readAll();
}

void FirmwareUpdater::onChunkPumpTick() {
    if (m_state != State::Uploading) {
        m_chunkPumpTimer.stop();
        return;
    }
    if (m_chunksQueued >= m_chunksTotal) {
        // All chunks are *queued* into BleTransport. Don't trigger verify
        // here — wait until onFirmwareWriteAcked sees the last ACK arrive
        // (m_chunksAcked == m_chunksTotal). The ACK-driven trigger means
        // the user-visible progress bar keeps climbing smoothly to 90 % as
        // each ACK clears the wire, instead of jumping to 90 % the moment
        // we queue the last chunk and then freezing silently for minutes.
        m_chunkPumpTimer.stop();
        qCDebug(firmwareLog) << "[firmware] upload queued to BLE ("
                             << m_chunksTotal << "chunks), waiting for ACKs";
        return;
    }
    const qsizetype byteOffset = m_chunksQueued * 16;
    QByteArray payload = m_firmwareBytes.mid(byteOffset, 16);
    if (payload.size() < 16) {
        payload.append(QByteArray(16 - payload.size(), char(0xFF)));  // pad tail
    }
    m_device->writeFirmwareChunk(static_cast<uint32_t>(byteOffset), payload);
    m_chunksQueued++;
}

void FirmwareUpdater::onFirmwareWriteAcked(const QBluetoothUuid& uuid,
                                           const QByteArray& data) {
    if (m_state != State::Uploading) return;
    // Filter: only count ACKs for 20-byte WriteToMMR packets carrying the
    // firmware-chunk length byte (16). Skips any other traffic that might
    // share the WRITE_TO_MMR characteristic during the upload window, and
    // avoids double-counting the FWMapRequest writes on A009.
    if (uuid != DE1::Characteristic::WRITE_TO_MMR) return;
    if (data.size() != 20) return;
    if (static_cast<uint8_t>(data[0]) != 16) return;

    m_chunksAcked++;

    // Heartbeat log every 5 % of the upload so `[firmware]` in the log
    // has a visible trail during what can be a 5–10 minute BLE-serialised
    // upload on Android, instead of going silent between "upload queued"
    // and "all ... ACKed".
    const qsizetype fivePercent = qMax<qsizetype>(m_chunksTotal / 20, 1);
    if (m_chunksAcked % fivePercent == 0) {
        // qRound (not int-truncation) so the 5% boundary prints "5%" rather
        // than "4%" (1449/28992 = 4.9985...).
        qCDebug(firmwareLog).noquote()
            << formatElapsed(m_updateTimer.isValid() ? m_updateTimer.elapsed() : -1)
            << "[firmware] upload progress:"
            << m_chunksAcked << "/" << m_chunksTotal
            << "(" << qRound(100.0 * m_chunksAcked / m_chunksTotal) << "%)";
    }

    const double uploadFrac = double(m_chunksAcked) / double(m_chunksTotal);
    setProgress(PROGRESS_ERASE_MAX + uploadFrac * (PROGRESS_UPLOAD_MAX - PROGRESS_ERASE_MAX));

    if (m_chunksAcked >= m_chunksTotal) {
        qCDebug(firmwareLog).noquote()
            << formatElapsed(m_updateTimer.isValid() ? m_updateTimer.elapsed() : -1)
            << "[firmware] all" << m_chunksTotal
            << "chunks ACKed, settling"
            << m_postUploadSettleMs << "ms before verify";
        QTimer::singleShot(m_postUploadSettleMs, this, [this]() {
            if (m_state == State::Uploading) beginVerifyPhase();
        });
    }
}

// ---- Phase 3: Verify ---------------------------------------------------

void FirmwareUpdater::beginVerifyPhase() {
    if (!m_device) return;
    setState(State::Verifying);
    setProgress(PROGRESS_UPLOAD_MAX);

    // Match de1app's exact ordering: re-enable A009 notifications RIGHT
    // before sending the verify request (de1_comms.tcl:962). The heavy
    // upload-write burst can invalidate the CCCD subscription on Android
    // BLE, and the bootloader appears to re-arm its notification handlers
    // after the write phase. Calling subscribe here is the single change
    // that distinguishes "no verify response" from "success notification
    // arrives within seconds". Note that de1app also leaves the original
    // erase-phase subscription disabled (line 876 commented out) — but
    // we keep ours active for diagnostic visibility into the erase-done
    // notification, which has no protocol cost.
    m_device->subscribeFirmwareNotifications();
    m_device->writeFWMapRequest(/*erase*/ 0, /*map*/ 1, {0xFF, 0xFF, 0xFF});
    m_verifyTimeoutTimer.start(m_verifyTimeoutMs);
}

void FirmwareUpdater::onVerifyTimeout() {
    if (m_state != State::Verifying) return;
    failWith(QStringLiteral("No response from DE1 during verify"), true);
}

// ---- fwMapResponse router ----------------------------------------------

void FirmwareUpdater::onFwMapResponse(uint8_t fwToErase, uint8_t fwToMap,
                                     QByteArray firstError) {
    Q_UNUSED(fwToMap);

    qCDebug(firmwareLog).noquote()
        << "[firmware] fwMapResponse received: erase=" << fwToErase
        << "map=" << fwToMap << "firstError=" << firstError.toHex(' ');

    if (m_state == State::Erasing) {
        // Per de1app, these are informational during the erase phase — we
        // don't gate the post-erase wait on them anymore. Logged so we can
        // see whether the DE1 is actually sending them on this firmware
        // generation, and tracked so verify-phase logic can use the flag
        // if needed.
        if (fwToErase == 1) {
            m_eraseInProgressSeen = true;
        }
        return;
    }

    if (m_state == State::Verifying) {
        m_verifyTimeoutTimer.stop();
        const QByteArray expected = QByteArray::fromHex("FFFFFD");
        if (firstError == expected) {
            completeSuccess();
        } else {
            const QString detail = QStringLiteral(
                "Verification failed at block %1.%2.%3"
            ).arg(uint8_t(firstError[0]))
             .arg(uint8_t(firstError[1]))
             .arg(uint8_t(firstError[2]));
            failWith(detail, /*retryable*/ true);
        }
        return;
    }
}

void FirmwareUpdater::onPostEraseWaitComplete() {
    if (m_state != State::Erasing) return;
    beginUploadPhase();
}

void FirmwareUpdater::completeSuccess() {
    if (m_device) m_device->setFirmwareFlashInProgress(false);
    setProgress(1.0);
    setState(State::Succeeded);
    m_updateAvailable = false;
    m_installedVersion = m_availableVersion;
    emit availabilityChanged();
    emit installedVersionChanged();
}

void FirmwareUpdater::failWith(const QString& reason, bool retryable) {
    qCWarning(firmwareLog).noquote()
        << formatElapsed(m_updateTimer.isValid() ? m_updateTimer.elapsed() : -1)
        << "[firmware] FAIL phase=" << stateText()
        << " chunks acked=" << m_chunksAcked
        << " queued=" << m_chunksQueued
        << " total=" << m_chunksTotal
        << " retry=" << (retryable ? "yes" : "no")
        << " reason=" << reason;
    if (m_device) m_device->setFirmwareFlashInProgress(false);
    m_eraseTimeoutTimer.stop();
    m_verifyTimeoutTimer.stop();
    m_postEraseWaitTimer.stop();
    m_chunkPumpTimer.stop();
    m_errorMessage   = reason;
    m_retryAvailable = retryable;
    setState(State::Failed);
}
