## ADDED Requirements

### Requirement: Connection Status Reports Detection Epoch And Diagnostic Build Code

The `devices_connection_status` tool SHALL report the current detection epoch and, when the scale link is latched, the build code that last set the classification as a diagnostic field. The build code MUST be presented as diagnostic ("last classified by build N"), not as the gating mechanism. Field names MUST follow the project MCP data conventions (human-readable, no Unix timestamps; the existing ISO-8601-with-offset latch timestamp is unchanged).

#### Scenario: Epoch always reported
- **WHEN** `devices_connection_status` is called
- **THEN** the scale connection-priority block includes the current detection epoch

#### Scenario: Diagnostic build code on a latched link
- **WHEN** the scale link is latched (skip-HIGH)
- **THEN** the response includes the build code that last set the classification, labeled as diagnostic
- **AND** the response does not present the build code as the rehydration gate

#### Scenario: Reset tool unchanged
- **WHEN** `devices_reset_scale_priority` is called
- **THEN** it clears the latch as before (the in-session escape hatch is unchanged by epoch scoping)
