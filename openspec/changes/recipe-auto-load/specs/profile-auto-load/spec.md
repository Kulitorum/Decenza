## MODIFIED Requirements

### Requirement: Auto-load profile setting

The system SHALL persist a single, optional auto-load profile filename and a configurable revert-timeout in minutes. Setting a non-empty auto-load profile filename SHALL clear any configured recipe auto-load (`Settings.dye.autoLoadRecipeId`), since exactly one of {a profile, a recipe} can be auto-loadable at a time.

#### Scenario: Default state
- **WHEN** the user has never configured auto-load
- **THEN** `Settings.app.autoLoadProfileFilename` is `""` and `Settings.app.autoLoadRevertMinutes` is `5`

#### Scenario: Setting a profile replaces any prior auto-load
- **WHEN** an auto-load is already configured for profile A and the user sets the auto-load to profile B
- **THEN** `Settings.app.autoLoadProfileFilename` equals B's filename and the previous selection on A is no longer in effect

#### Scenario: Clearing the auto-load preserves the timeout
- **WHEN** the user clears the auto-load profile
- **THEN** `Settings.app.autoLoadProfileFilename` is `""` AND `Settings.app.autoLoadRevertMinutes` retains its prior value

#### Scenario: Revert minutes is clamped
- **WHEN** the system receives a value below 0 or above 60 for `autoLoadRevertMinutes`
- **THEN** the value is clamped into 0..60 and the clamped value is persisted

#### Scenario: Both keys round-trip through settings backup
- **WHEN** the user exports a settings bundle while an auto-load is configured and imports it on another device
- **THEN** the imported device's `autoLoadProfileFilename` and `autoLoadRevertMinutes` match the exported values

#### Scenario: Setting a profile auto-load clears a recipe auto-load
- **WHEN** `Settings.dye.autoLoadRecipeId` is not `-1` AND the user sets `Settings.app.autoLoadProfileFilename` to a non-empty filename
- **THEN** `Settings.app.autoLoadProfileFilename` equals the new filename AND `Settings.dye.autoLoadRecipeId` becomes `-1`

#### Scenario: Clearing the profile auto-load does not touch an unrelated recipe auto-load
- **WHEN** `Settings.dye.autoLoadRecipeId` is `-1` AND the user clears `Settings.app.autoLoadProfileFilename`
- **THEN** `Settings.dye.autoLoadRecipeId` remains `-1` (no spurious write)

### Requirement: MCP — set auto-load

The MCP server SHALL expose a `profiles_set_auto_load` tool with a settings access level that pins a profile as the auto-load and optionally updates the revert minutes. Successfully setting a profile auto-load SHALL clear any configured recipe auto-load.

#### Scenario: Successful set
- **WHEN** the client calls `profiles_set_auto_load` with a `filename` that exists and is in the Selected list
- **THEN** the response is `{ success: true, filename, title, revertMinutes }` AND the setting is persisted on the GUI thread AND `Settings.dye.autoLoadRecipeId` is cleared to `-1`

#### Scenario: Filename missing
- **WHEN** the client calls `profiles_set_auto_load` with an empty or absent `filename`
- **THEN** the response is `{ error: "filename is required" }` AND no state changes

#### Scenario: Filename not found
- **WHEN** the client calls `profiles_set_auto_load` with a `filename` that does not exist
- **THEN** the response is `{ error: "Profile not found: <filename>" }` AND no state changes

#### Scenario: Filename not in Selected list
- **WHEN** the client calls `profiles_set_auto_load` with a `filename` that exists but is not in the Selected list
- **THEN** the response is `{ error: "Profile is not in the Selected list" }` AND no state changes

#### Scenario: Optional revert minutes updates both keys
- **WHEN** the client supplies `revertMinutes` alongside `filename`
- **THEN** both `autoLoadProfileFilename` and `autoLoadRevertMinutes` (clamped to 0..60) are updated

#### Scenario: Tool description documents the cross-clear
- **WHEN** an MCP client inspects the `profiles_set_auto_load` tool description
- **THEN** the description states that setting a profile auto-load clears any existing recipe auto-load

### Requirement: MCP — clear auto-load

The MCP server SHALL expose a `profiles_clear_auto_load` tool with a settings access level that disables the profile auto-load without affecting the revert timeout. This tool only clears `autoLoadProfileFilename` and SHALL NOT modify `Settings.dye.autoLoadRecipeId`.

#### Scenario: Successful clear
- **WHEN** the client calls `profiles_clear_auto_load`
- **THEN** `autoLoadProfileFilename` is set to `""` AND `autoLoadRevertMinutes` is unchanged AND the response is `{ success: true }`

#### Scenario: Clearing the profile auto-load does not affect an unrelated recipe auto-load
- **WHEN** `Settings.dye.autoLoadRecipeId` is not `-1` (independent of the profile setting) AND the client calls `profiles_clear_auto_load`
- **THEN** `Settings.dye.autoLoadRecipeId` is unchanged
