# hds-firmware-update Specification

## Purpose
Lets Decenza identify and present signed, compatible Half Decent Scale firmware releases and install a chosen release on the selected scale over Bluetooth, USB, or WiFi, without the owner touching the scale and without adding noise to normal Connections use.

## Requirements

### Requirement: HDS release availability is lifecycle-driven

Decenza SHALL retrieve the OpenScale release manifest used by HDS at application launch and after each genuine return from suspension, on every platform. On platforms other than iOS, and only while the user's auto-check-for-updates setting is enabled, Decenza SHALL also refresh it on the same shared periodic timer the app's own update checker uses (a 30-second post-startup check and hourly thereafter) rather than running a second, separate polling timer. A manual check-for-updates action SHALL refresh it unconditionally, on every platform and regardless of that setting. Decenza SHALL retain the latest successfully parsed manifest for the app session. When a connected HDS is the selected scale on any supported transport, Decenza SHALL compare its known installed firmware version with releases eligible for that scale.

#### Scenario: Selected HDS has a newer eligible release

- **WHEN** a current-session manifest contains an eligible release newer than the selected connected HDS firmware
- **THEN** Decenza marks that release as available for the selected scale
- **AND** it makes the release version and release-notes reference available to the Connections UI

#### Scenario: HDS has no newer eligible release

- **WHEN** the selected connected HDS already runs the newest eligible release
- **THEN** Decenza SHALL expose no update action or availability notice

#### Scenario: Selected HDS runs a preview or release-candidate build numerically equal to the newest eligible stable release

- **WHEN** the selected connected HDS reports an installed version with a `-preview.<n>` or `-rc.<n>` suffix whose numeric prefix equals a manifest release's version, and no release is numerically newer
- **THEN** Decenza marks that release as available for the selected scale even though it is not numerically newer
- **AND** Decenza SHALL NOT apply this exception when the installed version carries no such suffix, a different suffix, or is numerically ahead of every manifest release

#### Scenario: Manifest check cannot complete

- **WHEN** a manifest request fails and no prior valid session manifest is available
- **THEN** Decenza SHALL log the failure without showing an update error or changing the Connections page

#### Scenario: Resume check ignores focus flickers

- **WHEN** the application becomes active without first entering the suspended state
- **THEN** Decenza SHALL NOT issue another HDS manifest request

### Requirement: An HDS update installs without interaction at the scale

When the user confirms an available HDS update, Decenza SHALL start the update by naming the target release version in the start command, so the scale installs that release without presenting its on-device release picker and without requiring a hold-to-confirm gesture. Decenza SHALL support this on every transport an HDS can be selected over: Bluetooth, USB, and WiFi.

Decenza SHALL contribute only a version number. The scale SHALL remain responsible for retrieving and verifying its signed manifest and update assets, and for resolving the requested version against its own eligibility rules.

#### Scenario: User confirms an available update

- **WHEN** the user confirms the available update for a connected HDS
- **THEN** Decenza sends the start command carrying the available release version over the active scale transport
- **AND** the user is not asked to select or confirm anything on the scale

#### Scenario: HDS is connected over WiFi

- **WHEN** the selected connected HDS is reached over WiFi rather than Bluetooth or USB
- **THEN** Decenza SHALL offer and start the update over that transport
- **AND** it SHALL NOT require the user to reconnect the scale over another transport to update it

#### Scenario: Scale firmware predates targeted-update support

- **WHEN** the connected HDS runs firmware that does not understand a named version
- **THEN** the scale SHALL fall back to its own on-device release picker
- **AND** Decenza SHALL NOT gate, alter, or suppress the command based on the reported firmware version

### Requirement: A started update is never reported as an installed update

Decenza SHALL treat a scale's acceptance of a start request as *queued*, not as installed or installable. It SHALL NOT represent a dispatched command as a completed update, and SHALL infer a completed update only from the scale reconnecting on the target version.

#### Scenario: Start request is accepted

- **WHEN** the scale accepts the start request
- **THEN** Decenza SHALL report that the update has started
- **AND** it SHALL NOT report that the new version is installed

#### Scenario: HDS refuses or cannot complete its update

- **WHEN** the HDS cannot connect to WiFi, cannot verify its manifest, or refuses the requested release because its own rules would not offer it, after already accepting the start request
- **THEN** the HDS retains responsibility for reporting the failure and preserving its installed firmware
- **AND** Decenza SHALL NOT represent the earlier command dispatch as a completed update

#### Scenario: A second start request arrives while one is running

- **WHEN** the scale reports that an update is already queued or running
- **THEN** Decenza SHALL surface that refusal rather than treating the request as accepted

#### Scenario: HDS synchronously refuses the request over WiFi

- **WHEN** the connected HDS is reached over WiFi and its reply to the start command is an explicit, immediate refusal — including, but not limited to, the "already running" case above
- **THEN** Decenza SHALL surface that refusal to the user and SHALL NOT continue representing the request as pending
- **AND** on Bluetooth or USB, which carry no such reply, Decenza SHALL continue to rely on the scale reconnecting on the target version, or on the scale's own display, for the outcome
