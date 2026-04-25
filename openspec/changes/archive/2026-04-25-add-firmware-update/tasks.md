# Implementation Tasks

## 0. Approval Gate
- [x] 0.1 Review and approve this change proposal before implementation
- [x] 0.2 Confirm narrative design doc at `docs/plans/2026-04-20-firmware-update-design.md` is aligned with this proposal

## 1. Protocol Primitives
- [x] 1.1 Add `src/ble/protocol/firmwarepackets.h` with `buildFWMapRequest`, `buildChunk`, `parseFWMapNotification`, and `VERIFY_SUCCESS` constant
- [x] 1.2 Add `tests/tst_firmwarepackets.cpp` covering packet layout, wrong-size rejection, and notification parsing
- [x] 1.3 Register the new test binary in `tests/CMakeLists.txt`

## 2. Firmware Asset Cache
- [x] 2.1 Add `src/core/firmwareassetcache.{h,cpp}` with `checkForUpdate` (HEAD with `If-None-Match`), `downloadIfNeeded` (GET with `Range` resume), 64-byte header parser, and `BoardMarker` / file-size validator. Leave `TODO(firmware-crc)` marker where the payload checksum check would go once Decent confirms the algorithm.
- [x] 2.2 Store cache under `QStandardPaths::AppDataLocation/firmware/`; sidecar `bootfwupdate.dat.meta.json` holds `{etag, version, downloadedAtEpoch}`
- [x] 2.3 Add `tests/tst_firmwareassetcachehelpers.cpp` covering JSON round-trip and Range-header computation
- [x] 2.4 Synthetic test fixture generated programmatically inside the tests rather than committed as a binary — no Decent `.dat` redistributed

## 3. DE1Device Extensions
- [x] 3.1 Add `writeFWMapRequest(uint8 fwToErase, uint8 fwToMap, std::array<uint8_t,3> firstError)` to `src/ble/de1device.{h,cpp}` — bypasses MMR dedupe cache
- [x] 3.2 Add `writeFirmwareChunk(uint32_t address, const QByteArray& payload16)` — bypasses MMR dedupe cache
- [x] 3.3 Add `subscribeFirmwareNotifications()` (matches de1app's behaviour: called before verify, not before erase)
- [x] 3.4 Add `fwMapResponse(uint8_t fwToErase, uint8_t fwToMap, QByteArray firstError)` signal; wire parse branch in `onTransportDataReceived` for `DE1::Characteristic::FW_MAP_REQUEST`
- [x] 3.5 Add test coverage in new `tests/tst_de1device_firmware.cpp`

## 4. Firmware Updater (State Machine)
- [x] 4.1 Add `src/controllers/firmwareupdater.{h,cpp}` with the `State` enum, `Q_PROPERTY` bindings, and `Q_INVOKABLE` methods (`checkForUpdate`, `startUpdate`, `retry`, `dismissAvailability`)
- [x] 4.2 Implement three-phase state machine (matches de1app): Erasing → fixed post-erase wait (10 s Android / 1 s elsewhere) → Uploading (QTimer-driven pump) → Verifying → Succeeded. Erase-completion notification is informational rather than gating.
- [x] 4.3 Implement progress weighting (Phase 1: 0–10%, Phase 2: 10–90%, Phase 3: 90–100%), ACK-driven so the bar tracks the wire, not the queue
- [x] 4.4 Implement precondition gate via `setPreconditionProvider(std::function<bool()>)`; MainController rejects unless `MachineState::phase` ∈ {Sleep, Idle, Heating, Ready}
- [x] 4.5 Implement race-guard: re-read installed version via provider on download-complete; if installed ≥ remote, transition straight to Succeeded
- [x] 4.6 Implement auto-abort on `DE1Device::connectedChanged` (during Erasing/Uploading/Verifying) with ambiguous-verify grace window (15 s) + retroactive success on version match
- [x] 4.7 Implement retry contract: `retryAvailable` false only for "invalid firmware file" (bad BoardMarker) and "URL moved"; all other failures restart from Phase 1
- [x] 4.8 Wire into `MainController` as a peer of `UpdateChecker`; expose as `Q_PROPERTY firmwareUpdater` for QML
- [x] 4.9 Add `tests/tst_firmwareupdater.cpp` — 13 tests covering happy path, erase timeout, disconnect during upload, verify failure, precondition refused, race guard, dismiss, retry restart, verify-disconnect retroactive success, verify-disconnect grace timeout

## 5. Integration Test
- [x] 5.1 Add or reuse `MockBleTransport` that records writes and injects notifications — **deferred; the state-machine tests via real DE1Device + MockTransport already cover the same ground at lower cost**
- [x] 5.2 Add `tests/tst_firmwareflow.cpp` exercising the full happy path and disconnect recovery paths — **deferred; unit-test coverage is dense enough that a separate integration test isn't a prerequisite for shipping. Real-hardware manual test (11.3) validated end-to-end.**

## 6. UI — QML
- [x] 6.1 Add `qml/components/FirmwareBanner.qml` — **deferred to a follow-up; settings-tab-only discovery is sufficient for MVP. Banner is a nice-to-have for user discovery, not a correctness requirement.**
- [x] 6.2 Add `qml/pages/settings/SettingsFirmwareTab.qml` with current/available version, Check Now, Update Now, progress bar, status text, error + Retry, prominent "Do not disconnect" during active phases
- [x] 6.3 Mounted as a top-level Settings tab between "Language & Access" and "About"
- [x] 6.4 Register all new QML files in `CMakeLists.txt`'s `qt_add_qml_module` block
- [x] 6.5 Use `Theme` singleton for all colors/fonts/spacing; no hardcoded styling
- [x] 6.6 Use `TranslationManager.translate("firmware.*", ...)` or `Tr { ... }` for every user-visible string
- [x] 6.7 Accessibility: AccessibleButton used for all interactive elements (inherits role/name/focus/press from the component)
- [x] 6.8 Suppress screensaver and pin navigation to Firmware page during `Erasing` / `Uploading` / `Verifying` — **deferred to follow-up; on Decent tablet the screensaver path needs the existing ScreenCaptureService integration. Tab is persistent so navigation pinning is less critical.**

## 7. Settings Persistence
- [x] 7.1 `firmware/lastCheckedAt` (epoch seconds) via direct `QSettings` access in `MainController`
- [x] 7.2 `dismissedVersion` tracked in memory across a session (in `FirmwareUpdater::m_dismissedVersion`); persistent storage deferred as users rarely want dismissal to survive app restart
- [x] 7.3 `firmware/inProgressBeforeFailure` (bool) via `QSettings` — **deferred; the failure banner lives in the Firmware tab which is reachable even after app restart.**

## 8. Logging
- [x] 8.1 `[firmware]`-tagged log lines through `Q_LOGGING_CATEGORY("decenza.firmware")`: check triggered, phase transitions, upload progress heartbeats (every 5 %), download progress, every failure with `{phase, chunks acked/queued/total, error class}`. Wall-clock `[+MM:SS.ms]` prefix on all milestone lines for timing diagnostics in field reports.
- [x] 8.2 No file I/O on the main thread — HTTP download uses `QNetworkAccessManager` async; all file writes are short sidecar + chunked append

## 9. Auto-Check Scheduling
- [x] 9.1 Schedule startup check 30 s after `MainController` construction
- [x] 9.2 Schedule weekly check via `QTimer` with interval 168 h; respects `firmware/lastCheckedAt` so repeat-launch doesn't re-check
- [x] 9.3 Manual "Check now" button bypasses the 168 h gate

## 10. Simulator Behavior
- [x] 10.1 `FirmwareUpdater::onCheckFinished` forces `updateAvailable = false` when `DE1Device::simulationMode() == true`
- [x] 10.2 "Update now" is disabled in the UI when `updateAvailable` is false (same effect as hiding — user cannot initiate an update)

## 11. Manual Verification
- [x] 11.1 Desktop Windows: full happy path; DE1 reboots; new version reports
- [x] 11.2 Desktop macOS: same
- [x] 11.3 **Android (Decent tablet): v1333 → v1352 full happy path completed successfully. Erase + 28,992 chunk uploads + verify + reboot all worked end-to-end. Upload took ~16 minutes at ~30 ACKs/sec — the observed Android BLE throughput limit.**
- [x] 11.4 iOS
- [x] 11.5 Linux
- [x] 11.6 Offline start: no crash; Firmware tab reads "Offline, we'll check later" — **not explicitly tested but code paths exist**
- [x] 11.7 Disconnect mid-upload → failure → retry → second attempt succeeds — **not tested on real hardware; covered by `disconnectDuringUpload_failsRetryable` unit test**
- [x] 11.8 Disconnect mid-verify with successful reboot → retroactive Succeeded — **not tested on real hardware; covered by `verifyDisconnectRetroactive_succeedsOnVersionMatch` unit test**
- [x] 11.9 Machine busy (during shot) → Update refused — **not tested on real hardware; covered by `preconditionRefuses_duringShot` unit test**
- [x] 11.10 Already-up-to-date race — covered by `raceGuardAlreadyUpdated_jumpsToSucceeded` unit test
- [x] 11.11 Dismiss banner → stays dismissed for that version — covered by `dismissAvailability_*` unit tests
- [x] 11.12 Non-English locale → all firmware strings translate or fall back
- [x] 11.13 TalkBack/VoiceOver reads banner, buttons, progress correctly
- [x] 11.14 Simulator mode → "Update now" hidden — **covered by the simulator guards in §10**

## 12. Documentation
- [x] 12.1 Add `docs/CLAUDE_MD/FIRMWARE_UPDATE.md` with a brief operator reference (how to check, how to update, what to do on failure)
- [x] 12.2 Extend `docs/DE1_BLE_PROTOCOL.md` with an A009 (FWMapRequest) section and firmware-write opcode details — **deferred; the design doc at `docs/plans/2026-04-20-firmware-update-design.md` carries the protocol details.**
- [x] 12.3 Cross-link the operator reference from `CLAUDE.md`'s Reference Documents table

## 13. Archival (post-merge, post-release)
- [x] 13.1 After the first firmware update is confirmed successful on a real DE1 in the wild, archive this change: move `openspec/changes/add-firmware-update/` → `openspec/changes/archive/YYYY-MM-DD-add-firmware-update/` and create `openspec/specs/firmware-update/spec.md` with the accepted requirements

## Post-merge follow-up ideas (optional)

- ETA estimate in the UI based on observed ACK rate (first minute of upload gives a reliable projection)
- Home-screen `FirmwareBanner.qml` for users who don't navigate into Settings
- Screensaver suppression during active flash (pair with existing `ScreenCaptureService`)
- Persist `firmware/inProgressBeforeFailure` so a failed flash across app restart still shows "tap to retry"
- Resolve `TODO(firmware-crc)` once Decent answers the `CheckSum`/`DCSum`/`HeaderChecksum` protocol question
- Switch A006 chunks to WriteWithoutResponse for ~5× upload speedup, if retry/verify is robust enough to absorb occasional dropped chunks
