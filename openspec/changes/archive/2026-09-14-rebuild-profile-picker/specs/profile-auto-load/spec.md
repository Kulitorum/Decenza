## MODIFIED Requirements

### Requirement: ProfileSelector overflow action

The overflow action surface on profile picker cards — on `ProfileSelectorPage` and on the recipe wizard's profile step — SHALL expose a contextual auto-load action delivered through an accessible modal Dialog (not a popup Menu).

#### Scenario: Action available for Selected-list profiles
- **WHEN** the user opens the overflow dialog for a card whose profile is a favorite
- **THEN** the dialog shows a Set / Disable Auto-Load button

#### Scenario: Label and action reflect current state
- **WHEN** the card's profile is the current auto-load
- **THEN** the button is labelled "Disable Auto-Load" AND activating it clears `autoLoadProfileFilename`

#### Scenario: Setting on a different row replaces the prior auto-load
- **WHEN** the user activates "Set Auto-Load" on a card whose profile is not the current auto-load
- **THEN** `autoLoadProfileFilename` is set to that card's filename AND any prior auto-load is no longer marked

#### Scenario: Action hidden for non-Selected profiles
- **WHEN** the user opens the overflow dialog for a card whose profile is not a favorite
- **THEN** the dialog does not show an auto-load button

### Requirement: Auto-load row marker

The picker card representing the current auto-load profile SHALL show a visible marker so the user can identify it at a glance, in both hosts.

#### Scenario: Pin icon visible on the auto-load row
- **WHEN** the card's profile filename equals `autoLoadProfileFilename`
- **THEN** the `pin.svg` icon is visible beside the profile title, colored `Theme.primaryColor`

#### Scenario: Pin icon has an accessible name
- **WHEN** an accessibility screen reader focuses the pin icon
- **THEN** the reader announces "Auto-load profile"

#### Scenario: Marker hidden for non-auto-load rows
- **WHEN** the card's filename does not equal `autoLoadProfileFilename`
- **THEN** the pin icon is not rendered

### Requirement: ProfileSelector status strip

A strip at the top of `ProfileSelectorPage`, above the picker, SHALL surface the configured auto-load and allow tuning the revert minutes, visible only when an auto-load is configured and resolves to a favorite. The strip SHALL NOT appear in the recipe wizard host. The strip's text SHALL be sized via `Theme.captionFont` so it respects the user's `customFontSizes.captionSize` accessibility override.

#### Scenario: Strip visible when configured
- **WHEN** `autoLoadProfileFilename` is non-empty AND resolves to a favorite
- **THEN** the strip appears above the picker's search and chip rows showing: pin icon, "Auto-load:" label, profile title, "revert after" label, a numeric input for minutes, and a clear button

#### Scenario: Strip hidden when no auto-load is set
- **WHEN** `autoLoadProfileFilename` is `""`
- **THEN** the strip is not rendered

#### Scenario: Editing revert minutes from the strip
- **WHEN** the user changes the value in the strip's numeric input
- **THEN** `Settings.app.autoLoadRevertMinutes` is updated live AND any in-progress inactivity countdown is reset to the new value

#### Scenario: "off" rendering at zero
- **WHEN** `autoLoadRevertMinutes` is `0`
- **THEN** the strip's minute input displays "off" instead of "0 min" (the startup and wake-from-sleep triggers still fire)

#### Scenario: Clear button disables auto-load
- **WHEN** the user activates the strip's clear button
- **THEN** `autoLoadProfileFilename` is cleared AND a toast confirms "Auto-load disabled" AND the strip disappears

#### Scenario: Strip absent in the wizard
- **WHEN** the recipe wizard's profile step opens while an auto-load is configured
- **THEN** no auto-load strip is shown

### Requirement: Eligibility limited to Selected-list profiles

The system SHALL enforce that only favorites can be assigned as auto-load, and SHALL gracefully recover when a previously-pinned profile is no longer a favorite. (The Selected list this requirement's name refers to is removed — rebuild-profile-picker folded it into favorites; eligibility is "is a favorite".)

#### Scenario: Auto-load is cleared when its profile is hidden
- **WHEN** `SettingsApp::removeFavoriteProfile` un-favorites the current auto-load's downloaded or user-created profile
- **THEN** `autoLoadProfileFilename` is cleared so the strip disappears immediately

#### Scenario: Auto-load is cleared when its built-in profile is de-selected
- **WHEN** `SettingsApp::removeFavoriteProfile` un-favorites the current auto-load's built-in profile
- **THEN** `autoLoadProfileFilename` is cleared

#### Scenario: Auto-load is cleared when its profile is deleted
- **WHEN** `ProfileManager::deleteProfile` is called with the current auto-load filename
- **THEN** `autoLoadProfileFilename` is cleared

#### Scenario: Toast on stale clear at trigger time
- **WHEN** a trigger fires AND the auto-load filename no longer resolves to a favorite
- **THEN** the user sees a toast "Auto-load profile is no longer available" AND the setting is cleared

### Requirement: MCP — set auto-load

The MCP server SHALL expose an `auto_load` tool with `action: "set"` and `target: "profile"`, at a settings access level, that pins a profile as the auto-load and optionally updates the revert minutes.

#### Scenario: Successful set
- **WHEN** the client calls `auto_load` with `action: "set"`, `target: "profile"` and a `filename` that exists and is a favorite
- **THEN** the response is `{ success: true, filename, title, revertMinutes }` AND the setting is persisted on the GUI thread

#### Scenario: Filename missing
- **WHEN** the client calls `auto_load` with `action: "set"`, `target: "profile"` and an empty or absent `filename`
- **THEN** the response is `{ error: "filename is required" }` AND no state changes

#### Scenario: Filename not found
- **WHEN** the client calls `auto_load` with `action: "set"`, `target: "profile"` and a `filename` that does not exist
- **THEN** the response is `{ error: "Profile not found: <filename>" }` AND no state changes

#### Scenario: Filename not in Selected list
- **WHEN** the client calls `auto_load` with `action: "set"`, `target: "profile"` and a `filename` that exists but is not a favorite
- **THEN** the response is `{ error: "Profile is not a favorite" }` AND no state changes

#### Scenario: Optional revert minutes updates both keys
- **WHEN** the client supplies `revertMinutes` alongside `filename`
- **THEN** both `autoLoadProfileFilename` and `autoLoadRevertMinutes` (clamped to 0..60) are updated
