# Change: Add DE1 firmware update capability

## Why

Decenza aims to be a complete replacement for Decent's original `de1app` (Tcl/Tk) controller. `de1app` ships DE1 firmware updates over BLE; Decenza does not. Users who want to drop the Decent tablet and rely only on Decenza currently cannot receive firmware updates — including the version on `main` of `decentespresso/de1app` that is ~7 KB newer than the last tagged release. Without firmware update support, Decenza is an incomplete replacement.

## What Changes

- Add a **firmware update capability**: detect, download, validate, and flash new DE1 firmware over BLE
- Add `FirmwareUpdater` controller that owns the three-phase state machine (erase → upload → verify) as a peer of `SteamCalibrator` and `UpdateChecker` under `MainController`
- Add `FirmwareAssetCache` service that downloads the firmware binary from Decent's update CDN (`fast.decentespresso.com`, the same host Tcl de1app uses), validates its 64-byte header (`BoardMarker == 0xDE100001` plus file-size ≥ `ByteCount + 64`), and caches it under `QStandardPaths::AppDataLocation/firmware/`. Two channels are exposed: **Stable** (`de1plus`, default) and **Nightly** (`de1nightly`, opt-in). Decent's `de1beta` channel is omitted because it is not updated reliably. Payload-level checksum validation is deferred pending a protocol question to Decent (`TODO(firmware-crc)` marker); the DE1's own verify-phase response is the authoritative correctness check
- Add packet builders (`src/ble/protocol/firmwarepackets.h`) for `FWMapRequest` (A009) and firmware chunks (A006, opcode `0x10`)
- Extend `DE1Device` with `writeFWMapRequest()`, `writeFirmwareChunk()`, on-demand A009 subscribe/unsubscribe, and an `fwMapResponse` signal — **no state-machine logic inside `DE1Device`**
- Add home-screen **`FirmwareBanner.qml`** (dismissible, per-version) and **`SettingsFirmwareTab.qml`** (full flow with progress, retry, error surfaces)
- Add weekly + startup auto-check cadence (HEAD with `If-None-Match`; on ETag change fetch only the 64-byte header via `Range: bytes=0-63`); no full-body download until user confirms
- Add failure-recovery UX: any interruption produces a clean full-retry with a persistent banner, screensaver suppressed until the user succeeds or explicitly cancels
- Add unit tests for packet builders, asset cache, and state machine (using a `FakeDE1Device`), plus a mocked-BLE integration test

## Impact

- **Affected specs:** NEW capability `firmware-update`
- **Affected code:**
  - `src/controllers/firmwareupdater.{h,cpp}` (new)
  - `src/core/firmwareassetcache.{h,cpp}` (new)
  - `src/ble/protocol/firmwarepackets.h` (new)
  - `src/ble/de1device.{h,cpp}` (extend)
  - `src/ble/bletransport.cpp` (on-demand A009 subscription plumbing)
  - `src/controllers/maincontroller.{h,cpp}` (wire `FirmwareUpdater`)
  - `qml/main.qml` (banner anchor)
  - `qml/components/FirmwareBanner.qml` (new)
  - `qml/pages/settings/SettingsFirmwareTab.qml` (new)
  - `qml/pages/settings/SettingsMachineTab.qml` (add Firmware sub-tab)
  - `CMakeLists.txt` (register new sources and QML files)
  - `tests/tst_firmwarepackets.cpp`, `tests/tst_firmwareassetcache.cpp`, `tests/tst_firmwareupdater.cpp`, `tests/tst_firmwareflow.cpp` (new)
  - `tests/data/firmware/` (synthetic test fixture, not Decent's real `.dat`)
  - `docs/CLAUDE_MD/FIRMWARE_UPDATE.md` (new operator reference)
  - `docs/DE1_BLE_PROTOCOL.md` (extend with A009 section)
- **Non-breaking:** zero changes to existing shot processing, profile system, or BLE flows.
- **New outbound network dependency:** `fast.decentespresso.com` (for firmware download). Worth noting in any privacy / release documentation.
- **Binary size:** unchanged — firmware is downloaded on demand, not bundled.
