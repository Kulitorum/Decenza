# belka-portal Specification

## Purpose

Defines the optional PORTAL (Belka) measurement peripheral, which supplies raw
EC and outlet temperature alongside the DE1 and scale. Covers its independence
from machine control, shot-timeline storage, display synchronization, discovery
and connection settings, backup restore, and status-bar reconnect.

## Requirements

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

Background discovery MAY observe a PORTAL the user has never selected. Such a
discovery SHALL NOT change the Connections screen or the shared discovered-
devices list until that screen has started a scan.

#### Scenario: A PORTAL is nearby but the user never scans

- **WHEN** an unpaired PORTAL advertises while the user has a scale and refractometer connected
- **THEN** the discovered-devices list SHALL remain in the state it would have without PORTAL

#### Scenario: The user scans

- **WHEN** the user starts a scan from the Connections screen and a PORTAL is discovered
- **THEN** it SHALL be listed for selection alongside other discovered devices

#### Scenario: A scale disconnects routinely

- **WHEN** a scale's transport is disconnected by a timeout or retry
- **THEN** its reconnect target SHALL be retained so it can reconnect after a Bluetooth power-cycle

### Requirement: PORTAL ownership has one definition

Whether the user owns a PORTAL SHALL have one definition, `BelkaPortal.owned`,
that every PORTAL-specific QML control reads. The web theme editor SHALL derive
the same fact from the saved pairing.

#### Scenario: PORTAL controls read one ownership fact

- **WHEN** a PORTAL-specific control decides whether to show its PORTAL state
- **THEN** it reads `BelkaPortal.owned`, and the web theme editor reaches the same answer from the saved pairing

### Requirement: Forgetting a PORTAL stops its reconnects

Forgetting a PORTAL SHALL stop the transport from reconnecting to it after a
Bluetooth power-cycle. No other device's reconnect target SHALL be affected by
PORTAL.

#### Scenario: A forgotten PORTAL is not reconnected

- **WHEN** the user forgets the PORTAL and the Bluetooth stack is power-cycled
- **THEN** no reconnect to that PORTAL is attempted, and a scale's reconnect target is unchanged

### Requirement: Restoring a partial backup preserves omitted PORTAL fields

Settings import SHALL apply only the PORTAL fields present in the backup. Omitting the address or name SHALL leave the saved pairing unchanged and SHALL NOT disconnect a live PORTAL.

#### Scenario: Backup carries only the display preference

- **WHEN** a backup whose `portal` object contains only `syncDisplay` is restored
- **THEN** the saved PORTAL address and name SHALL be unchanged
- **AND** a connected PORTAL SHALL stay connected

### Requirement: Mass analysis preserves observed coverage

PORTAL mass analysis SHALL use physical-scale evidence and all recorded EC changes.
It SHALL preserve scale plateaus and SHALL NOT bridge a recorded interruption.
Tail metrics SHALL be unavailable when the last five grams contain missing EC data.
Same-timestamp scale updates SHALL resolve to the latest monotonic value; reversed
time and resets SHALL remain invalid. Recorded-curve failures SHALL be distinguished
from missing physical-scale evidence.

#### Scenario: A scale interval spans a PORTAL gap

- **WHEN** weight packets bracket an interruption without a weight packet inside it
- **THEN** the interrupted mass SHALL remain uncovered rather than integrating between its endpoints

#### Scenario: Only the tail is incomplete

- **WHEN** whole-shot coverage is sufficient but the final five grams contain a gap
- **THEN** the analyzer SHALL report insufficient coverage rather than a low-concentration tail

#### Scenario: Ordered scale updates share a clock tick

- **WHEN** multiple recorded scale snapshots share a timestamp
- **THEN** analysis SHALL use the latest monotonic snapshot at that timestamp without double-counting mass
- **AND** reversed timestamps and scale resets SHALL remain invalid
- **AND** an invalid recorded curve SHALL be distinguished from a missing physical scale in Shot Review

### Requirement: Refractometer estimates use independent comparable measurements

A shot SHALL be excluded from its own calibration. Calibration SHALL use the
same bag or bean and mounting mode, excluding measurements with uncertainty
warnings. Fewer than five independent references SHALL be marked preliminary.

#### Scenario: No matching reference exists

- **WHEN** only the target shot or a different bean has a paired refractometer reading
- **THEN** the target SHALL retain relative EC metrics without an estimated TDS/EY

### Requirement: Advice does not invent taste or unmeasured solids

Ratio advice SHALL use explicit feedback on the current cup and SHALL describe
experiments rather than a proven optimum. Rated references SHALL match coffee,
equipment, profile and a close dose. EC SHALL NOT be reported as remaining coffee
solids. The app SHALL NOT append warnings to the user's tasting notes when saving or reading a shot.

#### Scenario: Another cup was marked sour

- **WHEN** the current cup has no taste feedback
- **THEN** another cup's feedback SHALL NOT mark the current cup under-extracted

#### Scenario: Empirical calibration is preliminary

- **WHEN** the same reliable measurement has zero or fewer than five refractometer references
- **THEN** its measurement-based ratio advice SHALL remain unchanged
- **AND** any available TDS/EY estimate SHALL still report calibration uncertainty

#### Scenario: Metadata changes in Shot Review

- **WHEN** taste or refractometer metadata is saved
- **THEN** the shared outcome response SHALL refresh PORTAL analysis without replacing unsaved editing fields

### Requirement: Settings exposes the current empirical calibration

The PORTAL settings panel SHALL show the current coffee and mounting mode's
proportional k, active model, included/excluded reference shots, individual k,
measured ratio range and leave-one-out error when at least two references exist.
It SHALL distinguish measurement eligibility from prediction uncertainty, and
SHALL NOT present extrapolations outside the measured EC/ratio envelope as reliable.
The fit SHALL be derived from stored measurements rather than a hardcoded factor.

#### Scenario: A saved TDS reading is corrected

- **WHEN** a user saves or corrects refractometer TDS in shot metadata
- **THEN** the Settings model SHALL refresh after the write succeeds
- **AND** references with measurement warnings SHALL remain excluded
- **AND** the database read SHALL run off the main thread

#### Scenario: The selected coffee changes while a calibration read is pending

- **WHEN** a prior coffee's read completes after another coffee is selected
- **THEN** its result SHALL NOT replace the current calibration
- **AND** calibration SHALL use the same shared service in native and Wasm clients

### Requirement: Calibration uses saved measurement provenance

Calibration references SHALL record the actual serving weight source, mounting mode and PORTAL presence from samples. Unknown or virtual sources SHALL be ineligible. Reads SHALL NOT backfill the database or parse diagnostic logs; schema migration SHALL NOT inspect historical sample blobs. Shots saved before this change carry no weight provenance and SHALL remain ineligible.

#### Scenario: FlowScale serves a shot with a saved physical-scale SAW key

- **WHEN** FlowScale supplies any weight samples despite a physical-scale key in the log
- **THEN** the shot SHALL remain ineligible as a calibration reference

#### Scenario: Dose correction crosses the thermal gate

- **WHEN** a dose correction makes a reference thermally unreliable
- **THEN** it SHALL be excluded from subsequent calibration immediately after the write
- **AND** an open Shot Review SHALL refresh its saved outcome after a Visualizer metadata pull without replacing the editing form

#### Scenario: History contains no PORTAL data

- **WHEN** a non-PORTAL history row has a refractometer reading
- **THEN** its existing visible and spoken presentation SHALL remain unchanged
- **AND** no PORTAL calibration query SHALL be needed for that row

#### Scenario: An older coffee has valid references

- **WHEN** more than 200 newer measurements exist for other coffees
- **THEN** the older coffee SHALL retain its matching references

#### Scenario: Upgrading from main

- **WHEN** history migrates from main's schema
- **THEN** the PORTAL migration SHALL only add empty columns
- **AND** no shot rows or sample blobs SHALL be read

#### Scenario: Upgrading a contributor preview

- **WHEN** history migrates from a preview schema that already stored PORTAL summaries
- **THEN** shots with a stored PORTAL summary SHALL be marked present in the migration's transaction
- **AND** shots without weight provenance SHALL remain ineligible for calibration

### Requirement: Empirical models demonstrate predictive value

Fitted coefficients SHALL remain inspectable independently of prediction support.
Missing cross-validation SHALL NOT be reported as zero error. TDS/EY predictions
SHALL require complete leave-one-out evaluation, positive EC/TDS association and
lower RMSE than the leave-one-out reference-mean baseline. Predictions SHALL be
withheld on measurement warnings or outside the measured EC/ratio envelope.

#### Scenario: Only one independent reference exists

- **WHEN** a coefficient can be fitted but independent prediction error is unavailable
- **THEN** review, history and structured payloads SHALL withhold estimated TDS/EY
- **AND** measured refractometer data and relative EC metrics SHALL remain available

#### Scenario: More EC accompanies less measured TDS

- **WHEN** paired measurements fail the positive-association or held-out baseline comparison
- **THEN** the calibration SHALL report inconsistent prediction support rather than a reliable estimate
- **AND** measurement eligibility and measurement-based ratio advice SHALL remain independent of that result

### Requirement: Preserve uninterpreted temperature evidence

The auxiliary BLE float SHALL be retained optionally as `auxiliaryTemperatureRaw`
through capture, compressed storage, import/export and shared shot projections.
Its meaning and units SHALL NOT be assumed or used for a correction. Temperature
diagnostics SHALL use the same covered positive mass increments as EC integration,
without changing the legacy sample-temperature mean or its persisted column.

#### Scenario: Loading a recording without the auxiliary field

- **WHEN** an old sample has no auxiliary value
- **THEN** its EC and primary temperature SHALL remain usable
- **AND** the auxiliary value SHALL remain absent rather than becoming zero

#### Scenario: Temperature changes while no coffee mass arrives

- **WHEN** the scale remains on a plateau before, during or after the pour
- **THEN** that interval SHALL contribute no weight to the mass-temperature diagnostics
