## MODIFIED Requirements

### Requirement: MCP — get auto-load

The MCP server SHALL expose an `auto_load` tool with `action: "get"` and `target: "profile"` returning the current profile auto-load configuration with a read access level.

#### Scenario: Configured auto-load is reported
- **WHEN** an MCP client calls `auto_load` with `action: "get"`, `target: "profile"` AND an auto-load is configured
- **THEN** the response includes `filename`, `title`, and `revertMinutes`

#### Scenario: No auto-load configured
- **WHEN** an MCP client calls `auto_load` with `action: "get"`, `target: "profile"` AND `autoLoadProfileFilename` is `""`
- **THEN** the response includes `filename: ""` AND `revertMinutes` (the current configured timeout)

### Requirement: MCP — set auto-load

The MCP server SHALL expose an `auto_load` tool with `action: "set"` and `target: "profile"`, at a settings access level, that pins a profile as the auto-load and optionally updates the revert minutes.

#### Scenario: Successful set
- **WHEN** the client calls `auto_load` with `action: "set"`, `target: "profile"` and a `filename` that exists and is in the Selected list
- **THEN** the response is `{ success: true, filename, title, revertMinutes }` AND the setting is persisted on the GUI thread

#### Scenario: Filename missing
- **WHEN** the client calls `auto_load` with `action: "set"`, `target: "profile"` and an empty or absent `filename`
- **THEN** the response is `{ error: "filename is required" }` AND no state changes

#### Scenario: Filename not found
- **WHEN** the client calls `auto_load` with `action: "set"`, `target: "profile"` and a `filename` that does not exist
- **THEN** the response is `{ error: "Profile not found: <filename>" }` AND no state changes

#### Scenario: Filename not in Selected list
- **WHEN** the client calls `auto_load` with `action: "set"`, `target: "profile"` and a `filename` that exists but is not in the Selected list
- **THEN** the response is `{ error: "Profile is not in the Selected list" }` AND no state changes

#### Scenario: Optional revert minutes updates both keys
- **WHEN** the client supplies `revertMinutes` alongside `filename`
- **THEN** both `autoLoadProfileFilename` and `autoLoadRevertMinutes` (clamped to 0..60) are updated

### Requirement: MCP — clear auto-load

The MCP server SHALL expose an `auto_load` tool with `action: "clear"` and `target: "profile"`, at a settings access level, that disables the auto-load without affecting the revert timeout.

#### Scenario: Successful clear
- **WHEN** the client calls `auto_load` with `action: "clear"` and `target: "profile"`
- **THEN** `autoLoadProfileFilename` is set to `""` AND `autoLoadRevertMinutes` is unchanged AND the response is `{ success: true }`

## ADDED Requirements

### Requirement: The auto-load tool SHALL make profile/recipe exclusivity explicit

The `auto_load` tool SHALL require a `target` of `"profile"` or `"recipe"`, and setting one SHALL clear the other, as the two auto-loads are mutually exclusive.

#### Scenario: Setting a recipe auto-load clears a profile auto-load
- **GIVEN** a profile auto-load is configured
- **WHEN** the client calls `auto_load` with `action: "set"`, `target: "recipe"` and a valid recipe
- **THEN** the recipe auto-load is configured AND `autoLoadProfileFilename` is `""`

#### Scenario: Missing target
- **WHEN** the client calls `auto_load` with no `target`
- **THEN** the response is an error naming the valid targets AND no state changes
