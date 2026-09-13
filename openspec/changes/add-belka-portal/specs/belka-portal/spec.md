## ADDED Requirements

### Requirement: PORTAL is an independent measurement peripheral

Decenza SHALL allow a selected PORTAL to supply measurements alongside a DE1 and scale. A PORTAL problem SHALL NOT alter the brew profile or issue a machine stop, tare, heater or firmware command.

#### Scenario: PORTAL subscription fails

- **WHEN** the BLE link connects but measurement subscription fails
- **THEN** the PORTAL UI SHALL show unavailable measurements and a connection error
- **AND** the machine and scale SHALL retain their normal operation

### Requirement: Raw protocol fields retain their meaning

The known decoder SHALL accept exactly 13 bytes with three finite little-endian binary32 values and one raw status byte. EC SHALL NOT be stored as TDS, and the status byte SHALL NOT be labeled battery percent without verified semantics.

#### Scenario: Status exceeds 100

- **WHEN** a valid packet contains status byte 200
- **THEN** the decoder SHALL preserve the raw byte without clamping or displaying 200 percent

#### Scenario: Unknown or malformed packet

- **WHEN** a notification has a different length or a non-finite float
- **THEN** the notification SHALL be excluded from numeric curves and retained as diagnostic evidence

### Requirement: Measurements share the shot timeline

Live EC and outlet-temperature samples SHALL use the established Decenza shot-time anchor. They SHALL retain their own sample times and be available as distinct optional series during a shot and in its saved history.

#### Scenario: Independent notification rates

- **WHEN** PORTAL and DE1 send notifications at different rates
- **THEN** each PORTAL sample SHALL be recorded once at its mapped receive time
- **AND** the UI SHALL NOT fabricate additional PORTAL measurements for DE1 ticks

#### Scenario: PORTAL stops supplying valid measurements

- **WHEN** the PORTAL link is lost or its measurement stream is judged stale after five seconds without a valid notification
- **THEN** the UI SHALL mark the values unavailable and the stored curve SHALL contain a gap

### Requirement: Stored shots remain compatible

PORTAL data SHALL survive local save/load, JSON export and record import. Older shots without PORTAL data SHALL continue to display normally without zero-filled PORTAL curves.

#### Scenario: Reopen an older shot

- **WHEN** a shot contains no PORTAL fields
- **THEN** its existing graphs SHALL display normally and PORTAL series SHALL be absent

#### Scenario: Reopen a new shot

- **WHEN** a recorded PORTAL shot is saved, the app restarts, and the shot is reopened
- **THEN** its EC and outlet-temperature samples and gap boundaries SHALL match the saved recording


### Requirement: Shared connection settings

PORTAL SHALL use the existing scale/sensor connection panel and discovery list while retaining an independent saved selection and connection. Manual disconnect SHALL prevent automatic reconnection until explicit reconnect or app restart.

#### Scenario: A scale is already connected

- **WHEN** the user selects PORTAL from the common discovery list
- **THEN** PORTAL SHALL connect through its own transport without replacing the scale

#### Scenario: Other discovery transports finish first

- **WHEN** PORTAL has been discovered and the parallel WiFi or USB search reports progress or finishes
- **THEN** PORTAL SHALL remain selectable, including after the BLE search finishes
- **AND** only the start of a new BLE scan SHALL clear the previous PORTAL discovery results

### Requirement: Display synchronization reports only confirmed transport state

When enabled and supported, Decenza SHALL send the known graph-on command once at extraction start and graph-off once when shot processing completes. A BLE acknowledgement SHALL NOT be presented as confirmed device-side recording or saved-session state.

#### Scenario: Synchronization is disabled

- **WHEN** an extraction begins and finishes with display synchronization disabled
- **THEN** PORTAL measurements SHALL remain available but no automatic display command SHALL be sent

#### Scenario: Display writes await acknowledgement

- **WHEN** multiple ordered display commands have been submitted
- **THEN** the UI SHALL remain pending until all submitted writes are acknowledged or the connection fails
