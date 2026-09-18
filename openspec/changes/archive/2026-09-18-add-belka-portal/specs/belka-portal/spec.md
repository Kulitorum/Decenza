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
- **THEN** the UI SHALL remain pending until all submitted writes are acknowledged or a submitted operation or connection fails

#### Scenario: Display command times out

- **WHEN** the shared GATT queue abandons a display write
- **THEN** PORTAL SHALL report the command as failed and allow another manual request while idle
- **AND** valid measurements SHALL remain available

### Requirement: Existing users retain their UI and behavior

PORTAL-specific controls SHALL remain hidden until the user explicitly selects a discovered PORTAL or a previous pairing has been restored. First-selection progress and errors SHALL remain visible before pairing succeeds. The shared connection heading and scan-button availability SHALL remain unchanged.

#### Scenario: No PORTAL is configured

- **WHEN** a user without a saved PORTAL opens Connections or the extraction screen
- **THEN** the existing section heading, shared scan action and extraction layout SHALL be retained

### Requirement: Live settings restore updates the peripheral

Settings backup restoration SHALL update the active PORTAL selection and display preference without restarting Decenza. A previously selected device SHALL NOT overwrite the restored selection.

#### Scenario: Replace a connected PORTAL from backup

- **WHEN** a backup selects a different PORTAL while the previous peripheral is connected
- **THEN** the previous link SHALL be disconnected and the new selection SHALL be retained
- **AND** reconnect SHALL respect synchronous Qt teardown or pending native cancellation and reuse existing discovery

### Requirement: Live rendering and logs remain bounded per notification

Live notifications SHALL append points without rebuilding complete variant or segment lists. Recurring packet/state logs SHALL be collapsed within a shot or connection episode, with real link faults always reported.

#### Scenario: Long extraction with intermittent measurements

- **WHEN** many measurements arrive with repeated stale/recovery transitions
- **THEN** existing live renderers SHALL receive incremental appends and gaps SHALL begin new segments
- **AND** repeated state logs SHALL be summarized at episode end rather than emitted per cycle

### Requirement: Optional display errors do not discard measurements

Display-command failure SHALL leave an established measurement stream available. Subscription failure or absent initial notifications SHALL terminate setup with a visible failure. Only notification dispatch starts the initial-data deadline.

#### Scenario: Graph-on fails at extraction start

- **WHEN** the transport reports both the failed display operation and its error
- **THEN** valid measurement notifications SHALL continue to be captured
- **AND** finalization SHALL still attempt graph-off if the link supports it

#### Scenario: Notification setup produces no data

- **WHEN** notification enable fails or no valid notification arrives within the freshness window after dispatch
- **THEN** setup SHALL end with a visible error and permit an idle retry

### Requirement: History inspection includes PORTAL curves

The inspect readout SHALL show raw EC and outlet temperature in the selected temperature unit for enabled PORTAL curves. Values SHALL follow the drawn segment between recorded samples without extrapolation or interpolation across a recorded gap.

#### Scenario: Inspect a gap or an older shot

- **WHEN** the cursor is outside a recorded PORTAL segment, within an interruption, or on a shot without PORTAL samples
- **THEN** the readout SHALL omit unavailable PORTAL values while retaining available machine values

#### Scenario: Toggle a curve while inspecting

- **WHEN** a PORTAL curve is hidden or shown with the cursor active
- **THEN** its inspect value SHALL disappear or reappear without another tap

### Requirement: Saved PORTAL reconnect is available from the status bar

A saved PORTAL SHALL show a droplet icon with an EC curve and connection state immediately after the status-bar scale slot. Users without a saved PORTAL SHALL retain their existing bar and saved layout. Reconnect SHALL use the existing peripheral reconnect path and SHALL be blocked during machine operations and pending connections.

#### Scenario: Tap an offline PORTAL while idle

- **WHEN** the user taps the offline PORTAL status with the machine idle
- **THEN** Decenza SHALL retry the saved PORTAL connection without navigating to Settings

#### Scenario: Tap during a shot

- **WHEN** the machine is busy or PORTAL is already connected or connecting
- **THEN** the status SHALL NOT disconnect or restart the peripheral

### Requirement: Refill does not prevent PORTAL recovery

A DE1 in Refill SHALL allow PORTAL discovery, selection and reconnection. Extraction and other active operation guards SHALL remain effective, including the extraction-finalization guard.

#### Scenario: Startup connection drops while waiting for tank refill

- **WHEN** PORTAL connects at startup, the DE1 enters Refill, and the PORTAL link is lost
- **THEN** the user SHALL be able to reconnect from the status bar or Connections without restarting Decenza
- **AND** the reconnect SHALL work with a cached device or after fresh shared discovery

### Requirement: Status bar shows current raw EC

The PORTAL status slot SHALL show live EC to three decimal places when a valid, fresh notification is available. Its accessible description and tooltip SHALL identify the value as raw EC.

#### Scenario: Measurements change or become unavailable

- **WHEN** a fresh EC value arrives, including zero
- **THEN** the status-bar value SHALL update without navigation or a manual refresh
- **WHEN** the stream is stale, disconnected or still connecting
- **THEN** the status SHALL show the appropriate unavailable/progress indication instead of the last value

### Requirement: PORTAL curves are advanced-mode series

Both PORTAL curves SHALL be advanced-mode series on every graph. Their legend entries, plotted curves, axis labels, history cursor values and inspect-bar values SHALL appear only while the graph is in advanced mode, in addition to the existing requirement that the user has a saved PORTAL or a shot with recorded PORTAL samples. The graph's plot area and margins SHALL be identical to a graph without PORTAL data whenever advanced mode is off.

#### Scenario: Advanced mode is off

- **WHEN** a user with a saved PORTAL views the live, review, history or last-shot graph in basic mode
- **THEN** no PORTAL legend entry, curve, axis label or cursor value SHALL be shown
- **AND** the graph layout SHALL be unchanged

#### Scenario: Advanced mode is turned on

- **WHEN** advanced mode is enabled and a PORTAL curve's visibility setting is on
- **THEN** its legend entry, curve and axis SHALL appear, and its value SHALL appear in the inspect readout

### Requirement: A nearby unpaired PORTAL does not change the connections screen

Background discovery MAY observe a PORTAL that the user has never selected. Such a discovery SHALL NOT change the Connections screen or the shared discovered-devices list until that screen has started a scan. Whether the user owns a PORTAL SHALL have one definition, exposed as `BelkaPortal.owned`, that every PORTAL-specific QML control reads; the web theme editor derives the same fact from the saved pairing. Forgetting a PORTAL SHALL also stop the transport from reconnecting to it after a Bluetooth power-cycle, and no other device's reconnect target SHALL be affected by PORTAL.

#### Scenario: A PORTAL is nearby but the user never scans

- **WHEN** an unpaired PORTAL advertises while the user has a scale and refractometer connected
- **THEN** the discovered-devices list SHALL remain in the state it would have without PORTAL

#### Scenario: The user scans

- **WHEN** the user starts a scan from the Connections screen and a PORTAL is discovered
- **THEN** it SHALL be listed for selection alongside other discovered devices

#### Scenario: A scale disconnects routinely

- **WHEN** a scale's transport is disconnected by a timeout or retry
- **THEN** its reconnect target SHALL be retained so it can reconnect after a Bluetooth power-cycle

### Requirement: Restoring a partial backup preserves omitted PORTAL fields

Settings import SHALL apply only the PORTAL fields present in the backup. Omitting the address or name SHALL leave the saved pairing unchanged and SHALL NOT disconnect a live PORTAL.

#### Scenario: Backup carries only the display preference

- **WHEN** a backup whose `portal` object contains only `syncDisplay` is restored
- **THEN** the saved PORTAL address and name SHALL be unchanged
- **AND** a connected PORTAL SHALL stay connected
