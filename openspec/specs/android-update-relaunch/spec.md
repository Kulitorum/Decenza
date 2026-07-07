# android-update-relaunch Specification

## Purpose
TBD - created by archiving change add-android-auto-relaunch. Update Purpose after archive.
## Requirements
### Requirement: Optional auto-relaunch after Android self-update

The Android build SHALL provide an opt-in mechanism for the app to relaunch itself automatically after a successful self-update, without requiring the user to find and tap the launcher icon. The mechanism SHALL be gated on the user granting the `SYSTEM_ALERT_WINDOW` permission. When the permission is not granted, the system SHALL behave as it does today (the app exits after install and the user re-opens it manually).

#### Scenario: User has not granted SYSTEM_ALERT_WINDOW

- **WHEN** an Android self-update completes and the user has not granted `SYSTEM_ALERT_WINDOW`
- **THEN** the app SHALL NOT auto-relaunch
- **AND** the user experience SHALL match today's behavior (user finds the launcher icon and taps it)
- **AND** no crash, error dialog, or visible failure SHALL be produced

#### Scenario: User has granted SYSTEM_ALERT_WINDOW and the OS allows the launch

- **WHEN** an Android self-update completes and the user has granted `SYSTEM_ALERT_WINDOW`
- **AND** the OS permits the background activity launch
- **THEN** the app's main activity SHALL be started automatically within a few seconds of the install completing
- **AND** the launched activity SHALL be tagged in a way that the app can detect "this launch was the result of auto-relaunch"

#### Scenario: User has granted SYSTEM_ALERT_WINDOW but the OS blocks the launch

- **WHEN** an Android self-update completes and the user has granted `SYSTEM_ALERT_WINDOW`
- **AND** the OS blocks the background activity launch (e.g., a future Android version closes the exemption, or an OEM ROM restricts it)
- **THEN** the failure SHALL be logged but SHALL NOT crash the app
- **AND** the user experience SHALL match today's behavior

### Requirement: Permission prompt is opt-in and explanatory

The app SHALL provide a user-discoverable surface (e.g., in Settings) that explains why `SYSTEM_ALERT_WINDOW` is needed and offers to open the system permission page. The app MUST NOT auto-trigger the permission request without explicit user action. The permission state SHALL be re-checkable on demand.

#### Scenario: User opens the permission prompt and grants the permission

- **WHEN** the user opens the permission prompt and chooses to enable it
- **THEN** the app SHALL navigate the user to `Settings.ACTION_MANAGE_OVERLAY_PERMISSION` scoped to the Decenza package
- **AND** when the user returns to the app, the app SHALL re-check `Settings.canDrawOverlays()`
- **AND** the new state SHALL be reflected in the app's UI (e.g., the toggle now reads "Enabled")

#### Scenario: User declines or dismisses the permission prompt

- **WHEN** the user dismisses the permission prompt without granting the permission
- **THEN** the app SHALL NOT re-prompt automatically
- **AND** the prompt SHALL remain discoverable in Settings so the user can change their mind later

#### Scenario: Permission is revoked externally

- **WHEN** the user (or Android's auto-revoke / hibernation) revokes `SYSTEM_ALERT_WINDOW` after it was previously granted
- **THEN** the app SHALL detect the change on the next launch
- **AND** the in-app UI SHALL reflect the revoked state (e.g., toggle now reads "Disabled")
- **AND** no auto-relaunch attempt SHALL be reported as successful for updates that occurred while the permission was revoked

### Requirement: Diagnostic signal for relaunch outcome

The system SHALL record whether each app launch was the result of an auto-relaunch attempt and SHALL expose this information so it can be retrieved post-hoc for diagnosis. This is the primary deliverable of the experiment: without it, silent failure is indistinguishable from success.

#### Scenario: Launch is the result of auto-relaunch

- **WHEN** the receiver's `startActivity()` call results in the activity being shown
- **THEN** the activity SHALL detect the tagging intent extra on creation
- **AND** persistent state SHALL be updated to record "the most recent launch was auto-relaunched, at timestamp T"
- **AND** a log entry tagged `DecenzaAutoRelaunch` SHALL be written

#### Scenario: Launch is a normal user-initiated launch

- **WHEN** the activity is launched by user action (launcher icon, recents, etc.) and not via the receiver
- **THEN** persistent state SHALL be updated to record "the most recent launch was manual"
- **AND** no `DecenzaAutoRelaunch` "auto-relaunch fired" log entry SHALL be written

#### Scenario: Diagnostic state is human-readable from the app

- **WHEN** the user (or a support diagnosis flow) inspects the diagnostic surface inside the app
- **THEN** the surface SHALL display whether the most recent launch was auto-relaunched
- **AND** SHALL display the timestamp of the most recent auto-relaunch attempt (if any)

### Requirement: Scope limited to Android

This capability SHALL NOT apply to iOS, Windows, macOS, or Linux. No new build artifacts, settings, or runtime behavior SHALL be added to non-Android platforms as part of this change.

#### Scenario: Non-Android build is compiled

- **WHEN** the app is compiled for iOS, Windows, macOS, or Linux
- **THEN** none of the Android receiver, manifest entries, or runtime permission code SHALL be included
- **AND** the diagnostic surface SHALL either be hidden or display "not applicable on this platform"

#### Scenario: Android self-update flow is unchanged for users who do not opt in

- **WHEN** an Android user has not granted `SYSTEM_ALERT_WINDOW` and triggers a self-update
- **THEN** the install dialog, the `PackageInstaller` session flow, and the existing `UpdateChecker` state machine SHALL behave identically to before this change

