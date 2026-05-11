# profile-auto-load Specification

## Purpose
Let the user pin a single "default" profile that Decenza auto-loads on app startup, on every DE1 wake from sleep, and after a configurable period of inactivity on the Idle page. The pinned profile is selected from the Selected list, surfaced with a pin marker and an optional status strip on `ProfileSelectorPage`, persisted via `Settings.app`, round-tripped through settings backup, and controllable from MCP clients via three new tools.

## Requirements

### Requirement: Auto-load profile setting

The system SHALL persist a single, optional auto-load profile filename and a configurable revert-timeout in minutes.

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

### Requirement: Auto-load entry point

`ProfileManager` SHALL expose a single entry point that decides whether to load the configured auto-load profile and handles stale-target cleanup.

#### Scenario: No-op when no auto-load configured
- **WHEN** `loadAutoLoadProfileIfNeeded` is called AND `autoLoadProfileFilename` is `""`
- **THEN** the system does nothing and the active profile is unchanged

#### Scenario: No-op when the auto-load is already active
- **WHEN** `loadAutoLoadProfileIfNeeded` is called AND `autoLoadProfileFilename` equals the active profile filename
- **THEN** the system does not call `loadProfile` and the active profile is unchanged

#### Scenario: Stale target is cleared
- **WHEN** `loadAutoLoadProfileIfNeeded` is called AND `autoLoadProfileFilename` does not resolve to a profile currently in the Selected list
- **THEN** the system clears `autoLoadProfileFilename` AND emits `autoLoadStaleCleared` so the UI can show a toast

#### Scenario: Successful auto-load
- **WHEN** `loadAutoLoadProfileIfNeeded` is called AND `autoLoadProfileFilename` resolves to a Selected-list profile AND that profile is not the active profile
- **THEN** the system calls `ProfileManager.loadProfile(autoLoadProfileFilename)`

### Requirement: Trigger — app startup

The system SHALL invoke the auto-load entry point once during startup, after `ProfileManager` is initialised.

#### Scenario: Auto-load fires on cold launch
- **WHEN** the app starts AND an auto-load is configured AND the active profile differs from the configured filename
- **THEN** the configured profile is loaded as the active profile before the user interacts with the app

### Requirement: Trigger — DE1 wake from sleep

The system SHALL invoke the auto-load entry point on every `DE1Device` state transition from `Sleep` to `Idle` that represents a genuine user-initiated wake, while ignoring transitions replayed by a BLE reconnect.

#### Scenario: Wake from sleep reloads the auto-load
- **WHEN** `DE1Device.state` transitions from `Sleep` to `Idle` while the device has remained connected AND an auto-load is configured AND it differs from the active profile
- **THEN** the configured profile is loaded

#### Scenario: BLE reconnect does not false-fire
- **WHEN** `DE1Device.connected` toggles (reconnect) AND the first state notification after reconnect shows `Idle`
- **THEN** the auto-load entry point is NOT invoked from the state-change path (the reconnect resets the previous-state tracker so the first notification is treated as initial state, not a transition)

#### Scenario: Other state transitions do not trigger
- **WHEN** `DE1Device.state` transitions between any pair of states other than `Sleep → Idle`
- **THEN** the auto-load entry point is not invoked from the state-change path

### Requirement: Trigger — inactivity on the Idle page

The system SHALL invoke the auto-load entry point after `autoLoadRevertMinutes` of continuous inactivity while the Idle page is the currently displayed page. The inactivity countdown SHALL be independent of the auto-sleep countdown.

#### Scenario: Idle timeout reloads the auto-load
- **WHEN** the current page is `idlePage` AND no user activity has been registered for `autoLoadRevertMinutes` minutes AND no machine operation is active AND an auto-load is configured
- **THEN** the configured profile is loaded AND the inactivity countdown resets

#### Scenario: User activity resets the countdown
- **WHEN** the user provides input (touch, mouse, phase change) while on the Idle page
- **THEN** the inactivity countdown resets to `autoLoadRevertMinutes`

#### Scenario: Leaving the Idle page suspends the countdown
- **WHEN** the user navigates away from `idlePage`
- **THEN** the inactivity countdown does not decrement until the user returns to `idlePage`

#### Scenario: Zero disables the inactivity trigger
- **WHEN** `autoLoadRevertMinutes` is `0`
- **THEN** the inactivity trigger is disabled AND the app-startup and wake-from-sleep triggers continue to function

#### Scenario: Auto-sleep disabled does not disable the inactivity trigger
- **WHEN** the user has set `Settings.app.autoSleepMinutes` to `0` ("Never") AND `autoLoadRevertMinutes` is non-zero
- **THEN** the auto-load inactivity countdown continues to run and fire normally (the countdown lives in a dedicated timer, independent of the sleep countdown)

#### Scenario: Active machine operation suspends the countdown
- **WHEN** the machine is in an active phase (Pouring, Steaming, Preinfusion, Ending, EspressoPreheating, HotWater, Flushing, Descaling, Cleaning) OR firmware is being flashed
- **THEN** the inactivity countdown does not decrement

### Requirement: ProfileSelector overflow action

The overflow action surface on `ProfileSelectorPage` rows SHALL expose a contextual auto-load action delivered through an accessible modal Dialog (not a popup Menu).

#### Scenario: Action available for Selected-list profiles
- **WHEN** the user opens the overflow dialog for a row whose profile is in the Selected list
- **THEN** the dialog shows a Set / Disable Auto-Load button

#### Scenario: Label and action reflect current state
- **WHEN** the row's profile is the current auto-load
- **THEN** the button is labelled "Disable Auto-Load" AND activating it clears `autoLoadProfileFilename`

#### Scenario: Setting on a different row replaces the prior auto-load
- **WHEN** the user activates "Set Auto-Load" on a row whose profile is not the current auto-load
- **THEN** `autoLoadProfileFilename` is set to that row's filename AND any prior auto-load is no longer marked

#### Scenario: Action hidden for non-Selected profiles
- **WHEN** the user opens the overflow dialog for a row whose profile is not in the Selected list (e.g. browsing Built-In or User views without selection)
- **THEN** the dialog does not show an auto-load button

### Requirement: Auto-load row marker

The selector row representing the current auto-load profile SHALL show a visible marker so the user can identify it at a glance.

#### Scenario: Pin icon visible on the auto-load row
- **WHEN** the row's profile filename equals `autoLoadProfileFilename`
- **THEN** the `pin.svg` icon is visible beside the profile title, colored `Theme.primaryColor`

#### Scenario: Pin icon has an accessible name
- **WHEN** an accessibility screen reader focuses the pin icon
- **THEN** the reader announces "Auto-load profile"

#### Scenario: Marker hidden for non-auto-load rows
- **WHEN** the row's filename does not equal `autoLoadProfileFilename`
- **THEN** the pin icon is not rendered

### Requirement: ProfileSelector status strip

A strip at the top of `ProfileSelectorPage` SHALL surface the configured auto-load and allow tuning the revert minutes, visible only when an auto-load is configured and resolves to a Selected-list profile. The strip's text SHALL be sized via `Theme.captionFont` so it respects the user's `customFontSizes.captionSize` accessibility override.

#### Scenario: Strip visible when configured
- **WHEN** `autoLoadProfileFilename` is non-empty AND resolves to a Selected-list profile
- **THEN** the strip appears above the view-filter row showing: pin icon, "Auto-load:" label, profile title, "revert after" label, a numeric input for minutes, and a clear button

#### Scenario: Strip hidden when no auto-load is set
- **WHEN** `autoLoadProfileFilename` is `""`
- **THEN** the strip is not rendered AND the page layout matches the pre-feature appearance

#### Scenario: Editing revert minutes from the strip
- **WHEN** the user changes the value in the strip's numeric input
- **THEN** `Settings.app.autoLoadRevertMinutes` is updated live AND any in-progress inactivity countdown is reset to the new value

#### Scenario: "off" rendering at zero
- **WHEN** `autoLoadRevertMinutes` is `0`
- **THEN** the strip's minute input displays "off" instead of "0 min" (the startup and wake-from-sleep triggers still fire)

#### Scenario: Clear button disables auto-load
- **WHEN** the user activates the strip's clear button
- **THEN** `autoLoadProfileFilename` is cleared AND a toast confirms "Auto-load disabled" AND the strip disappears

### Requirement: Eligibility limited to Selected-list profiles

The system SHALL enforce that only profiles in the Selected list can be assigned as auto-load, and SHALL gracefully recover when a previously-pinned profile leaves the Selected list.

#### Scenario: Auto-load is cleared when its profile is hidden
- **WHEN** `SettingsApp::addHiddenProfile` is called with the current auto-load filename
- **THEN** `autoLoadProfileFilename` is cleared so the strip disappears immediately

#### Scenario: Auto-load is cleared when its built-in profile is de-selected
- **WHEN** `SettingsApp::removeSelectedBuiltInProfile` is called with the current auto-load filename
- **THEN** `autoLoadProfileFilename` is cleared

#### Scenario: Auto-load is cleared when its profile is deleted
- **WHEN** `ProfileManager::deleteProfile` is called with the current auto-load filename
- **THEN** `autoLoadProfileFilename` is cleared

#### Scenario: Toast on stale clear at trigger time
- **WHEN** a trigger fires AND the auto-load filename no longer resolves to a Selected-list profile
- **THEN** the user sees a toast "Auto-load profile is no longer available" AND the setting is cleared

### Requirement: MCP — get auto-load

The MCP server SHALL expose a `profiles_get_auto_load` tool returning the current auto-load configuration with a read access level.

#### Scenario: Configured auto-load is reported
- **WHEN** an MCP client calls `profiles_get_auto_load` AND an auto-load is configured
- **THEN** the response includes `filename`, `title`, and `revertMinutes`

#### Scenario: No auto-load configured
- **WHEN** an MCP client calls `profiles_get_auto_load` AND `autoLoadProfileFilename` is `""`
- **THEN** the response includes `filename: ""` AND `revertMinutes` (the current configured timeout)

### Requirement: MCP — set auto-load

The MCP server SHALL expose a `profiles_set_auto_load` tool with a settings access level that pins a profile as the auto-load and optionally updates the revert minutes.

#### Scenario: Successful set
- **WHEN** the client calls `profiles_set_auto_load` with a `filename` that exists and is in the Selected list
- **THEN** the response is `{ success: true, filename, title, revertMinutes }` AND the setting is persisted on the GUI thread

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

### Requirement: MCP — clear auto-load

The MCP server SHALL expose a `profiles_clear_auto_load` tool with a settings access level that disables the auto-load without affecting the revert timeout.

#### Scenario: Successful clear
- **WHEN** the client calls `profiles_clear_auto_load`
- **THEN** `autoLoadProfileFilename` is set to `""` AND `autoLoadRevertMinutes` is unchanged AND the response is `{ success: true }`
