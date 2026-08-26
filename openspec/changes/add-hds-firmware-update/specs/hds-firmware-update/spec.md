## Purpose

Lets Decenza identify and present signed, compatible Half Decent Scale firmware releases and hand the existing WiFi update flow to the selected scale without adding noise to normal Connections use.

## ADDED Requirements

### Requirement: HDS release availability is lifecycle-driven

Decenza SHALL retrieve the OpenScale release manifest used by HDS at application launch and after each genuine return from suspension. It SHALL retain the latest successfully parsed manifest for the app session and SHALL NOT run a periodic HDS-update polling timer. When a connected Bluetooth or USB HDS is the selected scale, Decenza SHALL compare its known installed firmware version with releases eligible for that scale.

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

### Requirement: Existing HDS OTA is handed off safely

When the user confirms an available HDS update, Decenza SHALL send the existing HDS WiFi-update command over the selected Bluetooth or USB scale transport. The scale SHALL remain responsible for retrieving and verifying its signed manifest and update assets. Decenza SHALL describe this as an update handoff and SHALL NOT report successful installation merely because the command was sent.

#### Scenario: User starts an update on current firmware

- **WHEN** the user confirms the available update for a connected HDS that supports the existing WiFi-update command
- **THEN** Decenza sends that command over the active scale transport
- **AND** instructs the user to continue version selection and confirmation on the HDS display

#### Scenario: HDS rejects or cannot complete its update

- **WHEN** the HDS cannot connect to WiFi, cannot verify its manifest, or rejects the selected release
- **THEN** the HDS retains responsibility for reporting the failure and preserving its installed firmware
- **AND** Decenza SHALL NOT represent the earlier command dispatch as a completed update

### Requirement: GUI-driven HDS OTA remains non-blocking future work

Decenza SHALL ship this availability and handoff feature without waiting for a future HDS remote-OTA protocol. A future protocol MAY allow Decenza to select a release, confirm installation, and display scale-reported progress, but it MUST preserve the HDS as the authority that independently selects compatible releases and verifies signed update assets.

#### Scenario: Current HDS supports only device-side selection

- **WHEN** a connected HDS exposes only the existing WiFi-update command
- **THEN** Decenza SHALL still offer the available-update handoff
- **AND** it SHALL require the user to complete selection and confirmation on the HDS display
