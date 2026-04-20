# Implementation Tasks

## 0. Approval Gate
- [ ] 0.1 Review and approve this change proposal before implementation
- [ ] 0.2 Confirm narrative design doc at `docs/plans/2026-04-20-firmware-update-design.md` is aligned with this proposal

## 1. Protocol Primitives
- [ ] 1.1 Add `src/ble/protocol/firmwarepackets.h` with `buildFWMapRequest`, `buildChunk`, `parseFWMapNotification`, and `VERIFY_SUCCESS` constant
- [ ] 1.2 Add `tests/tst_firmwarepackets.cpp` covering packet layout, wrong-size rejection, and notification parsing
- [ ] 1.3 Register the new test binary in `tests/CMakeLists.txt`

## 2. Firmware Asset Cache
- [ ] 2.1 Add `src/core/firmwareassetcache.{h,cpp}` with `checkForUpdate` (HEAD with `If-None-Match`), `downloadIfNeeded` (GET with `Range` resume), header parser, and CRC32 validator
- [ ] 2.2 Store cache under `QStandardPaths::AppDataLocation/firmware/`; sidecar `bootfwupdate.dat.meta.json` holds `{etag, sha256, version, downloadedAt}`
- [ ] 2.3 Add `tests/tst_firmwareassetcache.cpp` covering valid/invalid headers, CRC mismatch, version comparison, resume-after-partial
- [ ] 2.4 Commit synthetic 284-byte test fixture under `tests/data/firmware/` (28-byte header + 256-byte payload, all-zero payload with matching CRC32). Do not redistribute Decent's real `.dat`.

## 3. DE1Device Extensions
- [ ] 3.1 Add `writeFWMapRequest(uint8 fwToErase, uint8 fwToMap, std::array<uint8_t,3> firstError)` to `src/ble/de1device.{h,cpp}` — bypasses MMR dedupe cache
- [ ] 3.2 Add `writeFirmwareChunk(uint32_t address, const QByteArray& payload16)` — bypasses MMR dedupe cache
- [ ] 3.3 Add `subscribeFirmwareNotifications()` / `unsubscribeFirmwareNotifications()` — not always-on
- [ ] 3.4 Add `fwMapResponse(uint8_t fwToErase, uint8_t fwToMap, std::array<uint8_t,3> firstError)` signal; wire parse branch in `onCharacteristicChanged` for `DE1::Characteristic::FW_MAP_REQUEST`
- [ ] 3.5 Add test coverage in new `tests/tst_de1device_firmware.cpp` or extend existing `tst_mmrwrite.cpp`

## 4. Firmware Updater (State Machine)
- [ ] 4.1 Add `src/controllers/firmwareupdater.{h,cpp}` with the `State` enum, `Q_PROPERTY` bindings, and `Q_INVOKABLE` methods (`checkForUpdate`, `startUpdate`, `retry`, `dismissAvailability`)
- [ ] 4.2 Implement three-phase state machine: Erasing → wait for `fwMapResponse(erase=0)` → post-erase pause (10 s on Android, 1 s elsewhere via `QOperatingSystemVersion`) → Uploading (1 ms `QTimer`-driven chunk pump) → Verifying → Succeeded
- [ ] 4.3 Implement progress weighting (Phase 1: 0–10%, Phase 2: 10–90%, Phase 3: 90–100%)
- [ ] 4.4 Implement precondition gate: refuse unless machine phase is Idle or Sleep
- [ ] 4.5 Implement race-guard: re-read `MMR 0x800010` just before erase; if installed ≥ remote, transition straight to Succeeded
- [ ] 4.6 Implement auto-abort on `DE1Device::disconnected` (any phase) and on `MachineState::phaseChanged` out of Idle/Sleep during active update
- [ ] 4.7 Implement retry contract: `retryAvailable` false only for "invalid firmware file" and "URL moved"; all other failures restart from Phase 1
- [ ] 4.8 Wire into `MainController` alongside `SteamCalibrator` and `UpdateChecker`; expose as `Q_PROPERTY` for QML
- [ ] 4.9 Add `tests/tst_firmwareupdater.cpp` with `FakeDE1Device` and mocked clock (covering the 9 test cases in the design doc section 7.1)

## 5. Integration Test
- [ ] 5.1 Add or reuse `MockBleTransport` that records writes and injects notifications
- [ ] 5.2 Add `tests/tst_firmwareflow.cpp` exercising the full happy path and disconnect recovery paths; assert exact byte sequences on A009/A006 and correct subscribe/unsubscribe timing

## 6. UI — QML
- [ ] 6.1 Add `qml/components/FirmwareBanner.qml`; anchor in `qml/main.qml` header area; visible when `firmwareUpdater.updateAvailable && !dismissedForVersion`
- [ ] 6.2 Add `qml/pages/settings/SettingsFirmwareTab.qml` with current/available version, Check Now, Update Now, progress bar, status text, error + Retry, prominent "Do not disconnect" during active phases
- [ ] 6.3 Integrate as a sub-tab of `SettingsMachineTab.qml` or as a sibling tab (follow the existing settings hierarchy)
- [ ] 6.4 Register all new QML files in `CMakeLists.txt`'s `qt_add_qml_module` block
- [ ] 6.5 Use `Theme` singleton for all colors/fonts/spacing; no hardcoded styling
- [ ] 6.6 Use `TranslationManager.translate("firmware.*", ...)` or `Tr { ... }` for every user-visible string
- [ ] 6.7 Accessibility per `docs/CLAUDE_MD/ACCESSIBILITY.md`: every interactive element has `Accessible.role`, `Accessible.name`, `Accessible.focusable`, `Accessible.onPressAction`
- [ ] 6.8 Suppress screensaver, pin navigation to Firmware page, and block app-switching UI during `Erasing` / `Uploading` / `Verifying`

## 7. Settings Persistence
- [ ] 7.1 Add `firmware/lastCheckedAt` (epoch seconds) via `Settings` wrapper
- [ ] 7.2 Add `firmware/dismissedVersion` (int) via `Settings` wrapper
- [ ] 7.3 Add `firmware/inProgressBeforeFailure` (bool) via `Settings` wrapper — used for the persistent "interrupted, tap Retry" banner

## 8. Logging
- [ ] 8.1 Emit `[firmware]`-tagged log lines through the existing `AsyncLogger` for: check triggered, download progress, phase transitions, every failure with `{phase, errorClass, lastChunkOffset, lastFwMapResponse}`
- [ ] 8.2 Ensure no file I/O on the main thread (per `CLAUDE.md` design principles)

## 9. Auto-Check Scheduling
- [ ] 9.1 Schedule startup check 30 s after main window shown
- [ ] 9.2 Schedule weekly check via `QTimer` with interval `qMax(168h - elapsedSinceLastCheck, 30s)`
- [ ] 9.3 Implement manual "Check now" button that bypasses the 168 h gate

## 10. Simulator Behavior
- [ ] 10.1 Force `firmwareUpdater.updateAvailable = false` when `DE1Device::isSimulator() == true`
- [ ] 10.2 Hide the "Update now" button in `SettingsFirmwareTab.qml` under simulator mode

## 11. Manual Verification
- [ ] 11.1 Desktop Windows: full happy path; DE1 reboots; new version reports
- [ ] 11.2 Desktop macOS: same
- [ ] 11.3 Android (Decent tablet): same; verify 10 s post-erase wait in debug-log timestamps
- [ ] 11.4 iOS: same
- [ ] 11.5 Linux: same
- [ ] 11.6 Offline start: no crash; Firmware tab reads "Offline, we'll check later"
- [ ] 11.7 Disconnect mid-upload → failure → retry → second attempt succeeds
- [ ] 11.8 Disconnect mid-verify with successful reboot → retroactive Succeeded
- [ ] 11.9 Machine busy (during shot) → Update refused with tooltip
- [ ] 11.10 Already-up-to-date race (flash via reaprime in background) → "Already up to date" without erasing
- [ ] 11.11 Dismiss banner → stays dismissed for that version; reappears on newer firmware
- [ ] 11.12 Non-English locale → all firmware strings translate or fall back
- [ ] 11.13 TalkBack/VoiceOver reads banner, buttons, progress correctly
- [ ] 11.14 Simulator mode → "Update now" hidden

## 12. Documentation
- [ ] 12.1 Add `docs/CLAUDE_MD/FIRMWARE_UPDATE.md` with a brief operator reference (how to check, how to update, what to do on failure)
- [ ] 12.2 Extend `docs/DE1_BLE_PROTOCOL.md` with an A009 (FWMapRequest) section and firmware-write opcode details
- [ ] 12.3 Cross-link the operator reference from `CLAUDE.md`'s Reference Documents table

## 13. Archival (post-merge, post-release)
- [ ] 13.1 After the first firmware update is confirmed successful on a real DE1 in the wild, archive this change: move `openspec/changes/add-firmware-update/` → `openspec/changes/archive/YYYY-MM-DD-add-firmware-update/` and create `openspec/specs/firmware-update/spec.md` with the accepted requirements
