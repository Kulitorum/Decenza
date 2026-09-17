## Purpose

Lets Decenza identify and present signed, compatible Half Decent Scale firmware releases and install a chosen release on the selected scale over Bluetooth, USB, or WiFi, without the owner touching the scale and without adding noise to normal Connections use.

## ADDED Requirements

### Requirement: HDS release availability is lifecycle-driven

Decenza SHALL retrieve the OpenScale release manifest used by HDS at application launch and after each genuine return from suspension. It SHALL retain the latest successfully parsed manifest for the app session and SHALL NOT run a periodic HDS-update polling timer. When a connected HDS is the selected scale on any supported transport, Decenza SHALL compare its known installed firmware version with releases eligible for that scale.

#### Scenario: Selected HDS has a newer eligible release

- **WHEN** a current-session manifest contains an eligible release newer than the selected connected HDS firmware
- **THEN** Decenza marks that release as available for the selected scale
- **AND** it makes the release version and release-notes reference available to the Connections UI

#### Scenario: HDS has no newer eligible release

- **WHEN** the selected connected HDS already runs the newest eligible release
- **THEN** Decenza SHALL expose no update action or availability notice

#### Scenario: Manifest check cannot complete

- **WHEN** the launch or resume manifest request fails and no prior valid session manifest is available
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

- **WHEN** the HDS cannot connect to WiFi, cannot verify its manifest, or refuses the requested release because its own rules would not offer it
- **THEN** the HDS retains responsibility for reporting the failure and preserving its installed firmware
- **AND** Decenza SHALL NOT represent the earlier command dispatch as a completed update

#### Scenario: A second start request arrives while one is running

- **WHEN** the scale reports that an update is already queued or running
- **THEN** Decenza SHALL surface that refusal rather than treating the request as accepted
