## ADDED Requirements

### Requirement: Set Scale Connection-Priority Mode Tool

The MCP server SHALL expose a `devices_set_scale_priority_mode` tool that sets the persistent backoff policy mode to `enforce` or `observe`. It MUST require an explicit confirmation argument before applying (matching `devices_reset_scale_priority`). The mode change MUST persist immediately, but the HIGH-forcing effect of `observe` is eventually-consistent — it applies on the next scale (re)connect and does NOT tear down the current connection. The response MUST state this explicitly. An unrecognized mode value MUST be rejected without changing state.

#### Scenario: Set observe mode
- **WHEN** the tool is called with `mode: "observe"` and `confirmed: true`
- **THEN** the persisted mode becomes `observe` immediately
- **AND** the response states the mode persisted and that HIGH-forcing applies on the next scale reconnect (the current connection is not torn down)

#### Scenario: Set back to enforce
- **WHEN** the tool is called with `mode: "enforce"` and `confirmed: true`
- **THEN** the persisted mode becomes `enforce`
- **AND** the prior persisted latch (if any) is honored again on the next reconnect

#### Scenario: Missing confirmation
- **WHEN** the tool is called without `confirmed: true`
- **THEN** no state changes and the response indicates confirmation is required

#### Scenario: Invalid mode value
- **WHEN** the tool is called with a `mode` other than `enforce` or `observe`
- **THEN** the call is rejected and the persisted mode is unchanged

### Requirement: Connection Status Reports Mode And Observe Events

The `devices_connection_status` tool SHALL report the active `backoffMode` and a bounded list of recent observe events. Each observe event MUST carry an ISO-8601 timestamp with UTC offset, the trigger kind as a human-readable string, an event kind (`wouldBackoff` or `recovered`), and the relevant duration with units in the field name (`stallSec` / `gapSec`). The list MUST be bounded (most recent first) and MUST be empty when the mode has never been `observe`.

#### Scenario: Status includes mode
- **WHEN** `devices_connection_status` is called
- **THEN** the response includes `backoffMode` as `"enforce"` or `"observe"`

#### Scenario: Observe events surfaced
- **WHEN** the mode is `observe` and one or more would-back-off and/or recovery events have occurred
- **THEN** the response includes a bounded, most-recent-first list of those events
- **AND** each event has an ISO-8601-with-offset timestamp, a human-readable trigger kind, an event kind of `wouldBackoff` or `recovered`, and a unit-suffixed duration field

#### Scenario: No observe history
- **WHEN** the mode has never been `observe` this run
- **THEN** the recent-observe-events list is empty (and `backoffMode` reports the current mode)
