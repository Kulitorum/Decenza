## ADDED Requirements

### Requirement: Scale Connection-Priority State Read and Reset

The MCP server SHALL expose the **in-memory, app-run-scoped** scale connection-priority (dual-HIGH backoff) state for inspection and SHALL provide a way to reset it. Since there is intentionally no UI, this is the field-diagnostic surface (confirming whether and when a device backed off this run, including a probe-provoked backoff) and the in-session escape hatch to re-enter detection after a false-positive or for re-testing — without forcing an app restart. The state is scoped to the current app run only; an app restart already clears it (the latch lives on the in-memory `BLEManager` singleton — persistence is deliberately not in scope). Responses SHALL follow the project MCP data conventions (human-readable strings, units/scale in field names, ISO 8601 timestamps with timezone offset, no raw Unix timestamps).

#### Scenario: Read the current state when latched
- **WHEN** an MCP client requests the scale connection-priority state AND the in-memory skip-HIGH latch is set this app run
- **THEN** the server SHALL report that the scale link is operating at BALANCED (latched) this run, the trigger kind (`"de1-fault-cluster"` or `"scale-feed-stall"`), the ISO 8601 timestamp (with offset) it was set, and a human-readable elapsed-since-app-start value (e.g. minutes after app start when it latched)

#### Scenario: Read the current state when not latched
- **WHEN** an MCP client requests the scale connection-priority state AND the latch is not set
- **THEN** the server SHALL report that the scale link operates at HIGH and detection is active (not latched), with no spurious trigger metadata

#### Scenario: Reset clears the in-memory latch
- **WHEN** an MCP client invokes the scale connection-priority reset
- **THEN** the server SHALL clear the in-memory `BLEManager` skip-HIGH latch
- **AND** SHALL report the reset as accepted/queued and applying on the next scale (re)connect, NOT assert a verified-complete state change (the clear is marshalled to the BLEManager and the server cannot confirm execution synchronously)
- **AND** the next scale (re)connect SHALL request HIGH and re-enter detection (including the startup probe) as if the device were seen for the first time this run

#### Scenario: Reset is eventually-consistent with a live connection
- **WHEN** a reset is issued while a scale is currently connected at BALANCED
- **THEN** the cleared state SHALL take effect on the next scale (re)connection's detection pass
- **AND** the server SHALL NOT be required to forcibly tear down the active connection to apply the reset

#### Scenario: State is app-run-scoped, not persisted
- **WHEN** the application is restarted after a latch was set
- **THEN** the MCP read SHALL report not-latched on the new run (the in-memory latch did not survive restart)
- **AND** no persisted connection-priority record SHALL be read or written by this surface
