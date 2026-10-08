## ADDED Requirements

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

## MODIFIED Requirements

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

## REMOVED Requirements

### Requirement: Availability checks are scheduled and bandwidth-light
**Reason**: Availability now comes from firmware bundled with the app, so there is no CDN request to make light.
**Migration**: The startup and 168-hour cadence moves into "Firmware availability detection".
