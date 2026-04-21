# Firmware Update Capability

## ADDED Requirements

### Requirement: Firmware availability detection

The system SHALL periodically check for new DE1 firmware on Decent's update CDN and compare the remote firmware version against the connected DE1's installed version (read from `MMR 0x800010`). Two channels are supported:

- **Stable** (default): `https://fast.decentespresso.com/download/sync/de1plus/fw/bootfwupdate.dat`
- **Nightly** (opt-in via the `firmware/nightlyChannel` setting): `https://fast.decentespresso.com/download/sync/de1nightly/fw/bootfwupdate.dat`

The check SHALL be performed at app startup (30 s after the main window is shown) and once per 168 hours thereafter while the app is running. The check SHALL minimise bandwidth: an `HTTP HEAD` with `If-None-Match` SHALL be issued first, and only when the `ETag` has changed SHALL the system fetch the 64-byte firmware header via `Range: bytes=0-63` to read the remote `BoardMarker` and `Version`. The full firmware payload SHALL NOT be downloaded until the user initiates an update.

#### Scenario: Newer firmware available

- **WHEN** the scheduled check runs and the remote file's header version is strictly greater than the installed version
- **THEN** the system shows a dismissible home-screen banner indicating an update is available
- **AND** `firmwareUpdater.updateAvailable` evaluates to `true`

#### Scenario: Same version on remote

- **WHEN** the remote file's header version equals the installed version
- **THEN** no banner is shown
- **AND** no user-visible state changes

#### Scenario: Older firmware on remote (downgrade offered)

- **WHEN** the remote file's header version is strictly less than the installed version (for example, after the user toggles the channel from nightly to stable)
- **THEN** `firmwareUpdater.updateAvailable` evaluates to `true`
- **AND** `firmwareUpdater.isDowngrade` evaluates to `true`
- **AND** the UI labels the action as a downgrade and displays both the installed and the available versions so the user understands what flashing will do

#### Scenario: Network unavailable during check

- **WHEN** the check fails due to no internet connectivity or an HTTP error
- **THEN** the failure is logged through `AsyncLogger` with the `[firmware]` tag
- **AND** no user-facing error is shown
- **AND** the next scheduled check proceeds normally at its next trigger time

#### Scenario: Weekly cadence honoured

- **GIVEN** `firmware/lastCheckedAt` in `QSettings` is less than 168 hours before now
- **WHEN** the app starts
- **THEN** no automatic HEAD check is performed at startup
- **AND** the next check is scheduled for the 168-hour mark

#### Scenario: User dismisses banner for current version

- **WHEN** the user taps the dismiss control on the availability banner
- **THEN** `firmware/dismissedVersion` in `QSettings` is set to the current remote version
- **AND** the banner does not reappear until a strictly newer remote version is detected

#### Scenario: Channel switch invalidates cache

- **GIVEN** the cache holds a firmware blob downloaded from the previously-selected channel
- **WHEN** the user toggles `firmware/nightlyChannel`
- **THEN** the cached firmware file and its `.meta.json` sidecar are deleted
- **AND** the next availability check contacts the newly-selected channel's URL
- **AND** the `ETag`/`Version` from the old channel is not reused against the new channel

### Requirement: Firmware download and validation

The system SHALL download the firmware file only when the user initiates an update and SHALL validate the file's 64-byte header before any BLE write to the DE1. Validation SHALL parse the seven `u32` header fields in little-endian, confirm that `BoardMarker` at offset 4 equals `0xDE100001`, and confirm that the on-disk file size is at least `ByteCount + 64`. The DE1's own verify-phase response (`FirstError == {0xFF, 0xFF, 0xFD}`) is the authoritative correctness check for the written firmware; client-side validation over the encrypted payload (`CheckSum` / `DCSum` / `HeaderChecksum` algorithms) is deferred pending a protocol question to Decent, tracked by the `TODO(firmware-crc)` marker in `FirmwareAssetCache`. The system SHALL support resuming partially-downloaded files via HTTP `Range` requests.

#### Scenario: Successful download and validation

- **WHEN** the user taps "Update now" and the file downloads fully
- **THEN** the system parses the 64-byte header
- **AND** confirms `BoardMarker == 0xDE100001`
- **AND** confirms the on-disk file size is ≥ `ByteCount + 64`
- **AND** enters the `Ready` state

#### Scenario: Download resume

- **GIVEN** a prior download was interrupted and a partial file exists in the cache
- **WHEN** the user re-initiates the download
- **THEN** the system issues an HTTP `GET` with a `Range: bytes=X-` header (where X is the partial file size)
- **AND** continues writing from that offset on a `206 Partial Content` response
- **AND** falls back to a full download if the server returns `200 OK` without `Content-Range`

#### Scenario: Invalid firmware file — bad board marker

- **WHEN** the downloaded file's `BoardMarker` header field does not equal `0xDE100001`
- **THEN** the flow enters `Failed` with `retryAvailable = false`
- **AND** the user sees "The firmware file is not valid. Please report this."
- **AND** further automatic checks are disabled until the next app restart

#### Scenario: Invalid firmware file — truncated payload

- **WHEN** the on-disk file size is less than `ByteCount + 64`
- **THEN** the flow enters `Failed` with `retryAvailable = true`
- **AND** the cached file is deleted so a subsequent retry re-downloads from scratch

### Requirement: Three-phase firmware flash procedure

The system SHALL execute a three-phase flash procedure (erase, upload, verify) against the DE1 over BLE. Phase 1 SHALL write a `FWMapRequest` to characteristic `0000A009-…` with `FWToErase=1` and `FWToMap=1` and SHALL wait for the DE1 to notify `FWToErase=0` plus an OS-dependent post-erase delay (10 s on Android, 1 s on all other platforms). Phase 2 SHALL stream the firmware payload as 16-byte chunks to characteristic `0000A006-…` with opcode `0x10` and a 24-bit little-endian address field, paced by a 1 ms timer. Phase 3 SHALL write a `FWMapRequest` with `FWToErase=0`, `FWToMap=1`, `FirstError={0xFF, 0xFF, 0xFF}` and SHALL interpret the response's `firstError` field equal to `{0xFF, 0xFF, 0xFD}` as success.

#### Scenario: Successful end-to-end flash

- **GIVEN** the DE1 is connected and in `Idle` or `Sleep` state
- **AND** a validated firmware file is cached
- **WHEN** the user initiates the update
- **THEN** the system subscribes to A009 notifications
- **AND** writes the erase request
- **AND** waits for the erase-complete notification plus the OS-dependent post-erase delay
- **AND** streams all firmware chunks in 16-byte blocks
- **AND** writes the verify request
- **AND** receives a notification with `firstError == {0xFF, 0xFF, 0xFD}`
- **AND** transitions to `Succeeded`
- **AND** unsubscribes from A009 notifications

#### Scenario: Precondition — machine busy

- **WHEN** the user attempts to start an update and `MachineState::phase` is not `Idle` and not `Sleep`
- **THEN** no BLE writes are issued
- **AND** no firmware download is initiated
- **AND** the user is shown the message "Finish current operation first"
- **AND** the state does not advance to `Erasing`

#### Scenario: Precondition — version race

- **WHEN** the pre-flight re-read of `MMR 0x800010` shows the installed version exactly equals the downloaded version
- **THEN** the flow transitions to `Succeeded` without issuing an erase
- **AND** the banner is cleared
- **NOTE:** an installed version that is *greater* than the downloaded version is a deliberate downgrade and SHALL proceed with the flash.
- **AND** the outcome is logged as `race` to aid future debugging

#### Scenario: Progress reporting

- **WHEN** the flash is in progress
- **THEN** `firmwareUpdater.progress` reports values in `[0.0, 1.0]` weighted 10 % for erase, 80 % for upload, 10 % for verify
- **AND** the value is strictly non-decreasing across the update

### Requirement: Failure recovery

The system SHALL treat any interruption during flash as a non-destructive failure and SHALL offer the user a one-tap retry that restarts the full erase-upload-verify sequence from scratch. Screensaver suppression and navigation guards SHALL remain in effect across failure → retry until either a successful update completes or the user explicitly cancels. The system SHALL distinguish a BLE disconnect during the verify phase from a genuine verify failure by inspecting the firmware version reported by the DE1 after the subsequent auto-reconnect.

#### Scenario: BLE disconnect during upload

- **WHEN** the DE1 disconnects while firmware chunks are being uploaded
- **THEN** the flow enters `Failed` with `retryAvailable = true`
- **AND** the home-screen banner reads "Firmware update interrupted — tap to retry"
- **AND** the banner persists across app restarts until the user succeeds or explicitly cancels
- **AND** the screensaver guard remains in effect

#### Scenario: Erase phase timeout

- **WHEN** no `fwMapResponse` notification arrives within 30 seconds of the erase request
- **THEN** the flow enters `Failed` with `retryAvailable = true`
- **AND** the error message reads "Erase did not complete. Retry, or power-cycle the DE1."

#### Scenario: Verify-phase disconnect with retroactive success

- **WHEN** the DE1 disconnects during the verify phase
- **AND** BLE auto-reconnects within 15 seconds
- **AND** the post-reconnect firmware version matches the just-flashed version
- **THEN** the flow is reclassified as `Succeeded` without offering a retry

#### Scenario: Verify-phase disconnect with true failure

- **WHEN** the DE1 disconnects during the verify phase
- **AND** BLE does not reconnect within 15 seconds, or the post-reconnect version does not match
- **THEN** the flow enters `Failed` with `retryAvailable = true`

#### Scenario: Retry restarts from erase

- **GIVEN** a prior update attempt failed at any phase
- **WHEN** the user taps Retry
- **THEN** the system writes a fresh erase request before any new chunks are sent
- **AND** the chunk-upload index restarts from 0
- **AND** a fresh verify request is issued at the end

### Requirement: User control over availability

The system SHALL respect user dismissal of availability banners on a per-version basis. The system SHALL NOT display a dismissed banner again until a strictly newer remote firmware version is detected.

#### Scenario: Dismissal persists within a version

- **GIVEN** the user dismissed the banner for remote version V
- **WHEN** a subsequent check confirms the remote version is still V
- **THEN** the banner is not shown

#### Scenario: Dismissal does not persist across versions

- **GIVEN** the user dismissed the banner for remote version V
- **WHEN** a subsequent check finds remote version V+1 (where V+1 > V)
- **THEN** the banner reappears
- **AND** `firmware/dismissedVersion` no longer suppresses display

### Requirement: Simulator exclusion

The system SHALL disable firmware-update offerings when the DE1 simulator is the active device. The update UI SHALL NOT be shown and availability checks SHALL report `updateAvailable = false` regardless of remote state.

#### Scenario: Simulator mode active

- **GIVEN** `DE1Device::isSimulator() == true`
- **WHEN** the app evaluates firmware availability
- **THEN** `firmwareUpdater.updateAvailable` is `false`
- **AND** the home-screen banner does not appear
- **AND** the "Update now" button in `SettingsFirmwareTab` is hidden
