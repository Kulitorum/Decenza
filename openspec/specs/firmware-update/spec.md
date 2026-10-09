# firmware-update Specification

## Purpose
Manage DE1 firmware using Stable and opt-in Early access images bundled with Decenza, validating their manifest, digest and header before BLE flashing. Covers availability and dismissal, reflash/downgrade, erase/upload/verify, retry and reconnect handling, and simulator exclusion.

## Requirements

### Requirement: Firmware availability detection

The system SHALL determine DE1 firmware availability from firmware images bundled with the installed application and compare the selected bundled version with the connected DE1's installed version. Availability checks SHALL NOT require network access. The check SHALL run at app startup (30 s after the main window is shown) and once per 168 hours while the app is running, so cadence, banners and dismissal behave as before.

#### Scenario: Newer firmware available

- **WHEN** the scheduled check runs and the selected bundled firmware version is strictly greater than the installed version
- **THEN** the system shows a dismissible home-screen banner indicating an update is available
- **AND** `firmwareUpdater.updateAvailable` evaluates to `true`

#### Scenario: Same version on remote

- **WHEN** the selected bundled firmware version equals the installed version
- **THEN** no update banner is shown
- **AND** the Firmware tab still allows the user to intentionally reflash that same bundled version

#### Scenario: Older firmware on remote (downgrade offered)

- **WHEN** the selected bundled firmware version is strictly less than the installed version
- **THEN** `firmwareUpdater.updateAvailable` evaluates to `true`
- **AND** `firmwareUpdater.isDowngrade` evaluates to `true`
- **AND** the UI labels the action as a downgrade and displays both the installed and the selected bundled versions so the user understands what flashing will do

#### Scenario: Bundled firmware metadata invalid

- **WHEN** the selected bundled firmware entry is missing, cannot be loaded, has a mismatched digest, has an unexpected byte length, or has header metadata that does not match the image
- **THEN** the failure is logged with the firmware log tag
- **AND** no BLE write is issued
- **AND** the user is shown a non-retryable firmware-file-validity error

#### Scenario: Network unavailable during check

- **WHEN** the availability check runs while the device has no internet connectivity
- **THEN** the selected bundled firmware entry is evaluated normally
- **AND** no network error is shown or logged for firmware availability

#### Scenario: Weekly cadence honoured

- **GIVEN** the last automatic firmware check was less than 168 hours before now
- **WHEN** the app starts
- **THEN** no automatic firmware availability evaluation is performed at startup
- **AND** the next automatic evaluation is scheduled for the 168-hour mark

#### Scenario: User dismisses banner for current version

- **WHEN** the user taps the dismiss control on the availability banner
- **THEN** the currently selected bundled firmware version is recorded as dismissed
- **AND** the banner does not reappear until the selected bundled firmware version changes

#### Scenario: Channel switch invalidates cache

- **GIVEN** the user has selected one bundled firmware channel
- **WHEN** the user toggles between Stable and Early access firmware
- **THEN** the previous channel's loaded firmware state and dismissal state do not suppress availability for the newly selected channel
- **AND** the next availability check evaluates the newly selected bundled firmware entry

#### Scenario: Early access wording

- **WHEN** the user views the firmware channel toggle
- **THEN** the opt-in channel is labelled as Early access
- **AND** no user-facing text describes the opt-in channel as nightly firmware

#### Scenario: Existing nightly selection is reset

- **GIVEN** an existing installation has `firmware/nightlyChannel` set to `true`
- **WHEN** the app runs the one-time firmware-channel upgrade
- **THEN** it selects Stable firmware
- **AND** it persists `firmware/EA` as `false`
- **AND** it removes the historical preference
- **AND** a later app launch does not overwrite a user's explicit `firmware/EA` selection

#### Scenario: Release notes shown for selected bundled firmware

- **WHEN** the user views the Firmware tab after a bundled firmware entry has been selected
- **THEN** the app shows the selected entry's release notes from the bundled manifest
- **AND** the release notes are associated with the displayed selected firmware version and channel

### Requirement: Firmware download and validation

The system SHALL load the selected bundled firmware file only when the user initiates an update and SHALL validate its 64-byte header before any BLE write to the DE1. Validation SHALL parse the seven little-endian u32 header fields, confirm BoardMarker at offset 4 equals 0xDE100001, confirm the file size matches the bundled entry's expected length, is at least ByteCount + 64 and within the size ceiling, and confirm the digest matches the bundled catalog.

#### Scenario: Successful download and validation

- **WHEN** the user taps the update action and the selected bundled firmware file is present
- **THEN** the system parses the 64-byte header
- **AND** confirms `BoardMarker == 0xDE100001`
- **AND** confirms the file size and digest match the bundled catalog entry
- **AND** confirms the on-disk file size is at least `ByteCount + 64`
- **AND** enters the ready-to-flash state

#### Scenario: Download resume

- **WHEN** the user initiates a firmware update
- **THEN** the system loads the selected bundled firmware image from the installed application
- **AND** does not attempt to resume a prior network download or append to a cached partial file
- **AND** does not contact Decent's update CDN to fetch firmware bytes

#### Scenario: Invalid firmware file — bad board marker

- **WHEN** the selected bundled firmware file's `BoardMarker` header field does not equal `0xDE100001`
- **THEN** the flow enters a failed state with retry unavailable
- **AND** the user sees "The firmware file is not valid. Please report this."
- **AND** no BLE write is issued

#### Scenario: Invalid firmware file — truncated payload

- **WHEN** the selected bundled firmware file is smaller than the selected catalog entry's expected length or smaller than `ByteCount + 64`
- **THEN** the flow enters a failed state with retry unavailable
- **AND** the user sees "The firmware file is not valid. Please report this."
- **AND** no BLE write is issued

### Requirement: The DE1's verify response is the correctness check
The DE1's own verify-phase response (FirstError == {0xFF, 0xFF, 0xFD}) SHALL be the authoritative correctness check for the written firmware. Client-side checksum validation over the encrypted payload is deferred pending a protocol question to Decent.

#### Scenario: Verify response decides correctness
- **WHEN** the DE1's verify-phase response reports FirstError {0xFF, 0xFF, 0xFD}
- **THEN** the written firmware is accepted as correct

### Requirement: Three-phase firmware flash procedure

The system SHALL execute a three-phase erase, upload and verify procedure against the DE1 over BLE. Phase 1 SHALL write an FWMapRequest with FWToErase=1 and FWToMap=1 and wait for the DE1 to notify FWToErase=0, plus an OS-dependent post-erase delay. Phase 2 SHALL stream the payload in 16-byte chunks paced by a 1 ms timer. Phase 3 SHALL write an FWMapRequest with FWToErase=0, and SHALL treat a firstError of {0xFF, 0xFF, 0xFD} as success.

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

#### Scenario: Wire details of each phase
- **WHEN** phase 1 runs, the request is written to characteristic `0000A009-…`, with a post-erase delay of 10 s on Android and 1 s on other platforms
- **AND** phase 2 writes to characteristic `0000A006-…` with opcode `0x10` and a 24-bit little-endian address field

### Requirement: Failure recovery

The system SHALL treat any interruption during flash as a non-destructive failure and SHALL offer a one-tap retry that restarts the full erase-upload-verify sequence from scratch. Screensaver suppression and navigation guards SHALL remain in effect across failure and retry until a successful update completes or the user cancels.

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

### Requirement: A verify-phase disconnect is distinguished from a verify failure
The system SHALL distinguish a BLE disconnect during the verify phase from a genuine verify failure by inspecting the firmware version the DE1 reports after its auto-reconnect.

#### Scenario: Disconnect during verify
- **WHEN** the link drops during the verify phase and the DE1 reconnects
- **THEN** the firmware version it reports decides between a disconnect and a verify failure

### Requirement: User control over availability

The system SHALL respect user dismissal of availability banners on a per-version basis. The system SHALL NOT display a dismissed banner again until the remote firmware version changes in either direction (e.g. a strictly newer upgrade target, or a channel swap that produces a downgrade offer for a previously-unseen version).

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

### Requirement: Two bundled firmware channels
The system SHALL bundle two channels: Stable (the default, DE1 build 1352 from `decentespresso/decaid` `assets/firmware/de1/de1-1352.bin`) and Early access (opt-in, build 1358 from decentespresso/decaid#594 `assets/firmware/de1/de1-1358.bin`). The selected image SHALL expose its version, channel label, release notes, expected header fields, byte length, digest and provenance.

#### Scenario: Stable is the default channel
- **WHEN** the user has never opted into Early access
- **THEN** availability is computed against the bundled Stable build 1352

### Requirement: The Early access opt-in is a fresh preference
The Early access opt-in SHALL be persisted as `firmware/EA`, and the selected channel SHALL be Stable when that key is absent or false. A one-time upgrade SHALL remove the historical `firmware/nightlyChannel` preference, set `firmware/EA` to false, and record completion, so a prior nightly selection does not opt the user into Early access.

#### Scenario: The upgrade runs once
- **WHEN** the one-time upgrade has recorded completion and the user later enables Early access
- **THEN** a later launch does not reset `firmware/EA`
