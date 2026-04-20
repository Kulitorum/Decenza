# DE1 Firmware Update — Design

**Status:** design, pre-implementation
**Author:** Michael Holm (with Claude)
**Date:** 2026-04-20
**OpenSpec change:** [`add-firmware-update`](../../openspec/changes/add-firmware-update/proposal.md)

## 1. Problem

Decenza aims to be a complete replacement for Decent's original `de1app` (Tcl/Tk) controller. `de1app` ships DE1 firmware updates over BLE; Decenza does not. Users who want to drop the Decent tablet and rely only on Decenza currently cannot receive firmware updates — including the version on `main` of `decentespresso/de1app` that is ~7 KB newer than the last tagged release. This design adds firmware update as a first-class capability.

## 2. Scope

- **In scope:** BLE-based firmware update procedure (three-phase erase / upload / verify), firmware download from Decent's GitHub, detection and UI, retry on failure, all platforms Decenza supports (Windows, macOS, Linux, Android, iOS).
- **Out of scope:** USB-serial firmware update (the reaprime/Streamline path); auto-resume of partial uploads at the protocol level; signing/verification beyond what's in the firmware file header; firmware rollback.

## 3. Architecture

```
                 ┌──────────────────────────────────────────────┐
 QML layer       │ FirmwareBanner.qml         SettingsFirmwareTab.qml
 (user-facing)   │   (home-screen pill)        (progress, logs, retry)
                 └──────┬───────────────────────┬─────────────────┘
                        │ Q_PROPERTY / signals │
 Controller      ┌──────▼───────────────────────▼─────────────────┐
 (state machine) │              FirmwareUpdater                   │
                 │  states: Idle│Checking│Downloading│Ready│      │
                 │          Erasing│Uploading│Verifying│Done│Fail │
                 │  owns: QTimer for Android 10 s post-erase wait,│
                 │        chunk-pump QTimer (1 ms pacing),        │
                 │        persistent "retry pending" banner flag. │
                 └──────┬────────────────────┬──────────┬─────────┘
                        │ manifest/download  │ ble i/o   │ clock/platform
                        ▼                    ▼           ▼
 Support         ┌─────────────────┐  ┌──────────────┐  (QSysInfo /
 services        │ FirmwareAsset   │  │ DE1Device    │   QOperatingSystem
                 │ Cache           │  │  +writeFW-   │   Version)
                 │ (HTTP, header   │  │   MapRequest │
                 │  parse, SHA)    │  │  +writeFW-   │
                 └────────┬────────┘  │   Chunk      │
                          │           │  +fwMap-     │
 Networking       QNetworkAccessMgr   │   Response   │
 (GitHub raw)     / existing pool     │   (signal)   │
                                      └──────┬───────┘
 BLE (unchanged)                             │
                                   BleTransport → QLowEnergyController
                                   (A006 write, A009 write+notify)
```

**Key decisions:**

1. **`FirmwareUpdater` is owned by `MainController`** as a peer to `SteamCalibrator`, `UpdateChecker`, and `BatteryManager`. Exposed to QML as `mainController.firmwareUpdater`.
2. **`FirmwareAssetCache` is independent of BLE.** Pure networking + file I/O, testable in isolation.
3. **`DE1Device` additions are three methods and one signal.** No state-machine logic inside `DE1Device`; it remains a thin BLE I/O layer.
4. **Auto-check is cheap and weekly.** HEAD request with `If-None-Match`; on ETag change fetch only the 64-byte header via `Range: bytes=0-63`; no full-body download until the user confirms.
5. **No BLE write pacing changes.** `DE1Device` / `BleTransport` already handle backpressure; we pump chunks through the existing write queue via a 1 ms `QTimer`.

## 4. Components

### 4.1 `FirmwareUpdater` (new)

Path: `src/controllers/firmwareupdater.{h,cpp}`

```cpp
class FirmwareUpdater : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)      // localised
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(int availableVersion READ availableVersion NOTIFY availabilityChanged)
    Q_PROPERTY(int installedVersion READ installedVersion NOTIFY availabilityChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)       // 0.0..1.0
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(bool retryAvailable READ retryAvailable NOTIFY stateChanged)

public:
    enum class State {
        Idle, Checking, Downloading, Ready,
        Erasing, Uploading, Verifying,
        Succeeded, Failed
    }; Q_ENUM(State)

    Q_INVOKABLE void checkForUpdate();
    Q_INVOKABLE void startUpdate();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void dismissAvailability();

signals:
    void stateChanged();
    void availabilityChanged();
    void progressChanged();
};
```

Internals: `m_currentChunk`, `m_totalChunks`, `m_chunkPumpTimer`, `m_postEraseWaitTimer`, `m_attemptCount`. Listens to `DE1Device::fwMapResponse`, `DE1Device::disconnected`, `MachineState::phaseChanged` (aborts if the user starts a shot).

### 4.2 `FirmwareAssetCache` (new)

Path: `src/core/firmwareassetcache.{h,cpp}`

```cpp
class FirmwareAssetCache : public QObject {
    Q_OBJECT
public:
    struct Header {                                           // 64 bytes on wire
        uint32_t checksum, boardMarker, version, byteCount,   // offsets 0..15
                 cpuBytes, unused, dcSum;                     // offsets 16..27
        std::array<uint8_t, 32> iv;                           // offset 28..59 (opaque to us)
        uint32_t headerChecksum;                              // offset 60..63
    };
    struct CheckResult { enum Kind { Newer, Same, Error } kind; int remoteVersion; };

    void checkForUpdate();                 // HEAD + If-None-Match
    void downloadIfNeeded();               // GET with Range: resume
    QString cachedPath() const;
    std::optional<Header> cachedHeader() const;
    bool validateFile(QString path, QString* err) const;

signals:
    void checkFinished(CheckResult);
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(QString path, Header);
    void downloadFailed(QString reason);
};
```

URL (one constant): `https://raw.githubusercontent.com/decentespresso/de1app/main/de1plus/fw/bootfwupdate.dat`. Sidecar `bootfwupdate.dat.meta.json` under `QStandardPaths::AppDataLocation/firmware/` stores `{etag, sha256, version, downloadedAt}`.

### 4.3 `DE1Device` additions (modifications)

Path: `src/ble/de1device.{h,cpp}`

```cpp
void writeFWMapRequest(uint8_t fwToErase, uint8_t fwToMap);
void writeFirmwareChunk(uint32_t address, const QByteArray& payload16);
void subscribeFirmwareNotifications();
void unsubscribeFirmwareNotifications();

signals:
    void fwMapResponse(uint8_t fwToErase, uint8_t fwToMap, std::array<uint8_t,3> firstError);
```

All three write methods call `m_transport->write(...)` directly with hand-built byte arrays — **no MMR dedupe** (the dedupe cache in `m_lastMMRValues` would grow uselessly large for firmware bytes). Parsing added to `onCharacteristicChanged` at `src/ble/de1device.cpp:155` with a new branch for `DE1::Characteristic::FW_MAP_REQUEST`.

A009 notifications are subscribed on demand (at update start) and unsubscribed on completion — not always-on — because firmware notifications don't fire during normal operation and leaving the subscription active adds nothing.

### 4.4 Packet builders (new, Qt-free unit-testable)

Path: `src/ble/protocol/firmwarepackets.h`

```cpp
namespace DE1::Firmware {
    // Write to A009: [WindowIncrement(u16 BE)=0][FWToErase(u8)][FWToMap(u8)]
    //                [FirstError(3 bytes)] → 7 bytes total
    QByteArray buildFWMapRequest(uint8_t fwToErase, uint8_t fwToMap,
                                 std::array<uint8_t,3> firstError = {0,0,0});

    // Write to A006: [Opcode=0x10][Addr24 LE][Payload 16B] → 20 bytes total
    QByteArray buildChunk(uint32_t address, const QByteArray& payload16);

    struct FWMapNotification {
        uint16_t windowIncrement;          // big-endian on wire
        uint8_t  fwToErase, fwToMap;
        std::array<uint8_t,3> firstError;
    };
    std::optional<FWMapNotification> parseFWMapNotification(const QByteArray&);

    constexpr std::array<uint8_t,3> VERIFY_SUCCESS = {0xFF, 0xFF, 0xFD};
}
```

### 4.5 UI

- `qml/components/FirmwareBanner.qml` — dismissible pill at the top of `main.qml`. Visible when `firmwareUpdater.updateAvailable && !dismissedForVersion`. Tapping opens Firmware settings.
- `qml/pages/settings/SettingsFirmwareTab.qml` — full flow: installed version, available version, "Check now", "Update now" (enabled only when `state == Ready`), progress bar, status text, error + "Retry".
- Localisation via `TranslationManager.translate("firmware.*", ...)` and `Tr` components.
- Accessibility per `docs/CLAUDE_MD/ACCESSIBILITY.md`: every interactive element gets role, name, focusable, onPressAction.

## 5. Data flow

### 5.1 Happy path

```
 ┌─────────┐     ┌───────────┐     ┌───────────┐   ┌────────┐   ┌────────┐
 │  User   │     │ Firmware  │     │ Firmware  │   │ DE1    │   │ DE1    │
 │ (QML)   │     │ Updater   │     │ AssetCache│   │ Device │   │ (BLE)  │
 └────┬────┘     └─────┬─────┘     └─────┬─────┘   └───┬────┘   └───┬────┘
      │           startup +30s, or week timer          │            │
      │                │  checkForUpdate()             │            │
      │                │─────────────────▶│  HEAD raw.githubusercontent
      │                │                  │─────────────────────────▶│
      │                │◀───Newer(v1347)──│             │            │
      │◀───banner──────│                  │             │            │
      │                │                                │            │
      │  tap "Update"  │                  │             │            │
      │───startUpdate()▶│                 │             │            │
      │                │  downloadIfNeeded│             │            │
      │                │─────────────────▶│  GET .dat (If-None-Match)│
      │                │◀──downloadFinished(path, hdr)  │            │
      │                │  validate header, compare MMR 0x800010      │
      │                │                                │            │
      │                │  subscribeFirmwareNotifications│            │
      │                │───────────────────────────────▶│            │
      │  Phase 1 ERASE                                  │            │
      │                │  writeFWMapRequest(FWToErase=1,FWToMap=1)   │
      │                │───────────────────────────────▶│───A009────▶│
      │                │                                │◀ notify ── │ FWToErase=1
      │                │                                │◀ notify ── │ FWToErase=0
      │                │  wait 10 s (Android) / 1 s other            │
      │  Phase 2 UPLOAD (~28,000 chunks × 16 B @ 1 ms)  │            │
      │                │  writeFirmwareChunk(addr=N, 16B)            │
      │                │───────────────────────────────▶│───A006────▶│
      │                │  (QTimer 1 ms; advance N += 16)             │
      │  Phase 3 VERIFY                                 │            │
      │                │  writeFWMapRequest(FWToErase=0,FWToMap=1,   │
      │                │                    FirstError=FF,FF,FF)     │
      │                │───────────────────────────────▶│───A009────▶│
      │                │                                │◀ notify ── │ {FF,FF,FD}
      │                │  unsubscribeFirmwareNotifications            │
      │                │  state = Succeeded                           │
      │◀──DE1 reboots; BLE disconnects; auto-reconnect flow resumes──│
```

### 5.2 Check cadence

| Trigger | When | Action |
|---|---|---|
| App startup | 30 s after main window shown | HEAD check |
| Weekly | once per 168 h since last check (while app running) | HEAD check |
| User "Check now" | button on Firmware tab | HEAD check, bypass cache |

Persistence: `firmware/lastCheckedAt` (epoch seconds) in `QSettings`.

### 5.3 Progress weighting

- Phase 1 (erase): 0 – 10 %
- Phase 2 (upload, linear over chunks): 10 – 90 %
- Phase 3 (verify): 90 – 100 %

Erase and verify each take 2 – 5 s; upload takes ~28 s. Linear-over-chunks alone would look stuck at 0 % and 100 %; weighting restores continuous motion.

## 6. Error handling and recovery

### 6.1 Download-phase failures

| Failure | Detection | User-facing result | State |
|---|---|---|---|
| No internet | `QNetworkReply::NetworkError` | Tab: "Offline — we'll try again later" | `Idle` (no retry banner) |
| GitHub 5xx | HTTP status | Tab: "GitHub temporarily unavailable" | `Idle` |
| GitHub 4xx (URL moved) | HTTP status | Log warning, silently disable update flow until next app restart | `Idle` |
| Download truncated | `bytesReceived < Content-Length` | "Download incomplete — try again" + retry | `Failed` |
| File size < `ByteCount + 64` after download | Size check | Wipe cache, restart download from 0. | `Downloading` (transparent one-shot retry) → `Failed` if repeats |
| Header `boardMarker != 0xDE100001` | Parsed after download | "The firmware file is not valid. Please report this." Disable flow until app restart. | `Failed` (non-retryable) |

### 6.2 Pre-flight failures

| Failure | Detection | Handling |
|---|---|---|
| DE1 disconnected at tap | `DE1Device::isConnected == false` | Button disabled; tooltip "Connect DE1 first" |
| Machine in shot / steam / descale | `MachineState::phase` not Idle/Sleep | Button disabled; tooltip "Finish current operation first" |
| Race: installed ≥ remote on pre-flight MMR re-read | Last-moment read | Jump to `Succeeded` with "Already up to date"; clear banner |
| Low battery (< 20 % on tablet) | `BatteryManager::level` | Soft warning: "Update may take 45 seconds. Continue with low battery?" |

### 6.3 Erase-phase failures

| Failure | Detection | Handling |
|---|---|---|
| No `fwMapResponse` within 30 s | Timeout | `Failed`: "Erase did not start. Retry, or power-cycle the DE1." |
| Erase-start arrives, erase-done never does | 30 s after first notify | Same |
| BLE disconnect mid-erase | `DE1Device::disconnected` | `Failed`: "Disconnected during erase. Reconnect and tap Retry." |

DE1 behavior after interrupted erase: the bootloader flag (`FWToMap=1`) is already set, so on next boot the DE1 will re-accept a fresh erase+upload. Decenza detects this because the post-reboot firmware version reads as `0` or unchanged; the banner stays visible for the next attempt.

### 6.4 Upload-phase failures

| Failure | Detection | Handling |
|---|---|---|
| BLE disconnect | `DE1Device::disconnected` | `Failed`: "Disconnected at N%. Tap Retry to start over." Banner persists. |
| DE1 reboots mid-upload | disconnect + reconnect with older version | Same path as disconnect; full re-erase on retry |
| Write queue stall | `BleTransport` cmdstack warning + 10 s no-progress watchdog | `Failed`: "Update stalled. Tap Retry." |
| App backgrounded on mobile | `QGuiApplication::applicationStateChanged` | Ignored while active; full-screen "Do not switch apps" overlay. If OS kills us → disconnect path. |

### 6.5 Verify-phase failures

| Failure | Detection | Handling |
|---|---|---|
| `firstError != {0xFF, 0xFF, 0xFD}` | Notification parse | `Failed`: "Verification failed at block X. Tap Retry." (3-byte offset in debug log.) |
| No verify notify within 10 s | Timeout | `Failed`: "No response from machine." |
| Disconnect during verify | `DE1Device::disconnected` | Ambiguous — DE1 may have rebooted successfully. After auto-reconnect, re-read `MMR 0x800010`: matches new version → `Succeeded` retroactively; else → `Failed`. |

### 6.6 Retry contract

- `retryAvailable == true` iff the last failure was restartable (everything except "invalid firmware file" and "URL moved").
- After a failure, the home-screen banner persists as "Firmware update interrupted — tap to retry" until the user either succeeds or explicitly dismisses ("Not now"). Persisted via `firmware/inProgressBeforeFailure` in `QSettings`.
- Screensaver / navigation guards stay up across failure → retry. Only success or explicit cancel releases them.
- Every failure writes one `AsyncLogger` line tagged `[firmware]` with timestamp, phase, error class, last chunk offset, last fwMapResponse bytes.

## 7. Testing

### 7.1 Unit tests (no BLE)

**`tests/tst_firmwarepackets.cpp`** (new):

| Test | Asserts |
|---|---|
| `buildFWMapRequest_erase` | `buildFWMapRequest(1, 1)` → exactly 7 bytes `{00 00 01 01 00 00 00}` |
| `buildFWMapRequest_verify` | `buildFWMapRequest(0, 1, {FF,FF,FF})` → `{00 00 00 01 FF FF FF}` |
| `buildChunk_layout` | `buildChunk(0x00123456, 16×0xAA)` → 20 bytes `{10 56 34 12 AA×16}` |
| `buildChunk_rejectsWrongSize` | payload size ≠ 16 → empty result (or assertion) |
| `parseFWMap_success` | `{00 00 00 01 FF FF FD}` → `firstError == VERIFY_SUCCESS` |
| `parseFWMap_erasing` | `{00 00 01 00 00 00 00}` → `fwToErase=1, fwToMap=0` |
| `parseFWMap_tooShort` | 6-byte input → `std::nullopt` |

**`tests/tst_firmwareassetcache.cpp`** (new):

| Test | Asserts |
|---|---|
| `parseHeader_valid` | Canned 64-byte header parses with correct LE values, IV bytes preserved |
| `validateFile_goodBoardMarker` | Synthetic fixture with `BoardMarker = 0xDE100001` and `fileSize == ByteCount + 64` passes |
| `validateFile_badBoardMarker` | Returns false, error mentions "not a DE1 firmware file" |
| `validateFile_truncated` | File smaller than `ByteCount + 64` → validation fails with retryable classification |
| `validateFile_truncated` | File smaller than `ByteCount + 64` → validation fails |
| `versionComparison_strictlyGreater` | v1342 < v1347 → `Newer`; equal → `Same`; remote < installed → not offered |

Fixture: synthetic 320-byte `.dat` (64-byte header + 256-byte payload) under `tests/data/firmware/`. Header has `BoardMarker = 0xDE100001` and `ByteCount = 256`; payload is all-zero. No Decent binary redistributed. The `CheckSum`, `DCSum`, and `HeaderChecksum` fields can stay zero until the `TODO(firmware-crc)` question to Decent is resolved.

**`tests/tst_firmwareupdater.cpp`** (new), using a `FakeDE1Device` and mocked `FirmwareAssetCache`:

| Test | Sequence | Asserts |
|---|---|---|
| `happyPath_endToEnd` | Start → fake erase complete → N chunks → verify success | `Succeeded`; exactly `m_totalChunks` chunk writes; progress monotonic 0 → 1 |
| `eraseTimeout` | Start → no notification | After 30 s (simulated) → `Failed`, `retryAvailable=true` |
| `disconnectDuringUpload` | erase ok → fire 100 chunks → `disconnected` | `Failed`; error mentions 100/totalChunks progress |
| `verifyFailure` | Happy path except verify `{FF,FF,00}` | `Failed`; error includes 3-byte offset |
| `preconditionRefuses_onShot` | `phase=Espresso`, call `startUpdate()` | No BLE writes; error "finish current operation first" |
| `raceGuard_alreadyUpdated` | MMR pre-read shows installed ≥ remote | Straight to `Succeeded`; no erase |
| `disconnectDuringVerify_thenReconnectWithNewVersion` | verify-phase disconnect → reconnect with new version | Retroactive `Succeeded` |
| `retryAfterFailure_restartsFromErase` | happyPath → force failure at chunk 500 → `retry()` | Second run restarts from chunk 0 |
| `badBoardMarker_nonRetryable` | Invalid header | `Failed`, `retryAvailable=false` |

### 7.2 Integration test

**`tests/tst_firmwareflow.cpp`** (new) using `MockBleTransport`:

- Drives `DE1Device` + `FirmwareUpdater` end-to-end against a mocked transport that records writes and injects notifications.
- Asserts exact byte sequences per characteristic, subscribe/unsubscribe timing on A009, and total write count of `1 + totalChunks + 1`.
- Disconnect path cleanly transitions to `Failed`.
- Runtime budget < 500 ms by passing a 0-ms chunk-pump interval in test mode.

### 7.3 Simulator

Hide the "Update now" button when the simulator is active. `firmwareUpdater.updateAvailable` forced to `false` when `DE1Device::isSimulator() == true`. No simulator code changes.

### 7.4 Manual verification

Before merge:

- [ ] Desktop (Windows): full happy path, verify DE1 reboots and reports new version.
- [ ] Desktop (macOS): same.
- [ ] Android (Decent tablet): same; confirm 10 s post-erase wait in debug-log timestamps.
- [ ] iOS: same.
- [ ] Linux: same.
- [ ] Offline start: no crash; tab reads "Offline, we'll check later".
- [ ] Disconnect mid-upload: failure → retry → second attempt succeeds.
- [ ] Disconnect mid-verify: reconnect sees new version → retroactive success (no spurious Failed).
- [ ] Machine busy: start shot → Update refused.
- [ ] Already-up-to-date race: flash via reaprime in background → Update says "Already up to date" without erasing.
- [ ] Dismiss banner: stays dismissed for that version; reappears on newer firmware.
- [ ] Localization: switch locale; all strings translate or fall back.
- [ ] Accessibility: TalkBack/VoiceOver reads banner, buttons, progress correctly.
- [ ] Simulator: "Update now" hidden.

### 7.5 What is not tested automatically

- Actual flash of a real DE1 (would risk bricking a $3000 machine).
- Network download from live GitHub.
- Cross-platform BLE pacing — relies on Qt.

## 8. Open questions

None at design time. Re-evaluate after first implementation pass:
- Whether the 10 s Android post-erase wait needs tuning for current Android versions (de1app's value is from 2021-era hardware).
- Whether GitHub raw CDN rate-limits weekly HEAD requests from a large install base (unlikely at current scale; monitor).

## 9. References

- **de1app source:** `C:\code\de1app\de1plus\de1_comms.tcl:810–997` (state machine), `binary.tcl:379–412` (packet specs), `bluetooth.tcl:2667–2690` (response handling).
- **Decent firmware:** `https://raw.githubusercontent.com/decentespresso/de1app/main/de1plus/fw/bootfwupdate.dat`.
- **Kal Freese's Python reference:** `https://github.com/kalfreese/de1-firmware-updater` — independent implementation of the same BLE protocol, useful for cross-checking packet builders.
- **Decenza existing BLE layer:**
  - `src/ble/protocol/de1characteristics.h` — UUIDs (FW_MAP_REQUEST already declared at line 26)
  - `src/ble/de1device.{h,cpp}` — MMR write/read, firmware version parse
  - `src/ble/bletransport.cpp` — notification subscribe dispatcher
- **Decenza conventions:** `CLAUDE.md`, `docs/CLAUDE_MD/BLE_PROTOCOL.md`, `docs/CLAUDE_MD/ACCESSIBILITY.md`, `docs/CLAUDE_MD/TESTING.md`.
