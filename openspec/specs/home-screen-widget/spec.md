# home-screen-widget Specification

## Purpose
Decenza publishes a compact, off-main-thread machine-status snapshot (phase, connection state, temperature vs. target, most-recent shot) to platform-shared storage so native iOS and Android Home Screen widgets can render machine state without the app running in the foreground. Covers snapshot freshness/staleness handling, honest display when disconnected, tap-to-open behavior, and inclusion of the widget in each platform's release pipeline.

## Requirements
### Requirement: Machine-status snapshot publication

The app SHALL maintain a compact machine-status snapshot in platform-shared storage that the widget process can read while the app is not in the foreground or not running. The snapshot SHALL be assembled from existing accessors (machine phase, connection state, temperature, target temperature, steam temperature, most-recent shot) and SHALL include an ISO-8601 capture timestamp with UTC offset. Snapshot writes SHALL occur off the main thread.

#### Scenario: Snapshot written on phase change
- **WHEN** the machine phase changes (e.g. Sleep → Heating → Ready)
- **THEN** the app writes an updated snapshot to platform-shared storage containing the new phase and a fresh capture timestamp

#### Scenario: Snapshot written on connection change
- **WHEN** the DE1 BLE connection is established or lost
- **THEN** the app writes a snapshot whose `connected` field reflects the new connection state

#### Scenario: Temperature updates are coalesced
- **WHEN** shot samples arrive at ~5 Hz and only the temperature is drifting
- **THEN** the app does NOT write the shared snapshot on every sample, but updates it on a meaningful change (threshold/round-value crossing) at a bounded rate

#### Scenario: Snapshot write never blocks the main thread
- **WHEN** a snapshot is flushed to shared storage
- **THEN** the disk/shared-storage write executes off the main thread per the project's background-I/O pattern

### Requirement: Widget displays machine phase and temperature

The widget SHALL display the current machine phase as a human-readable label and the temperature rendered relative to its target, derived solely from the shared snapshot.

#### Scenario: Heating shows current and target
- **WHEN** the snapshot phase is Heating with current 84 °C and target 93 °C
- **THEN** the widget shows a heating label with both the current and target temperature (e.g. "Heating 84 → 93 °C")

#### Scenario: At temperature shows ready
- **WHEN** the snapshot phase is Ready / at target temperature
- **THEN** the widget shows a Ready indicator alongside the current temperature

#### Scenario: Steaming shows steam temperature
- **WHEN** the snapshot phase is Steam
- **THEN** the widget shows the steam temperature rather than the group temperature

#### Scenario: Human-readable phase label
- **WHEN** the snapshot phase is any supported state (Sleep, Idle, Heating, Ready, Espresso, Steam, Hot Water, Flush)
- **THEN** the widget renders a human-readable label for that phase, never a raw numeric code

### Requirement: Widget displays last-shot summary

The widget SHALL display a summary of the most recent shot from the snapshot, including yield, duration, and the shot's quality badge when available.

#### Scenario: Recent shot is shown
- **WHEN** the snapshot contains a most-recent shot with yield, duration, and a quality badge
- **THEN** the widget shows the shot's yield, duration, and badge

#### Scenario: No shot history
- **WHEN** the snapshot contains no most-recent shot
- **THEN** the widget omits the last-shot summary without showing placeholder/garbage values

### Requirement: Widget reflects connection and staleness honestly

The widget SHALL NOT present stale data as if it were live. It SHALL derive its display state from the snapshot's `connected` flag and the age of the snapshot's capture timestamp.

#### Scenario: Disconnected state
- **WHEN** the snapshot's `connected` field is false
- **THEN** the widget shows a disconnected message and a prompt to open the app, and does NOT present a temperature as if it were current

#### Scenario: Stale snapshot
- **WHEN** the snapshot's `connected` field is true but its capture timestamp is older than the freshness window
- **THEN** the widget annotates the displayed data as stale (e.g. "updated N min ago")

#### Scenario: Fresh connected state
- **WHEN** the snapshot is connected and its capture timestamp is within the freshness window
- **THEN** the widget shows phase, temperature-vs-target, and last-shot summary without a staleness annotation

#### Scenario: Missing or unreadable snapshot
- **WHEN** no snapshot exists in shared storage or it cannot be parsed
- **THEN** the widget shows the disconnected / open-app state rather than crashing or rendering empty fields

### Requirement: Widget opens the app on tap

Tapping anywhere on the widget SHALL launch the Decenza app to its normal entry point. The widget SHALL NOT perform any machine control action.

#### Scenario: Tap launches app
- **WHEN** the user taps the widget
- **THEN** the Decenza app is launched (or brought to foreground) at its normal entry point

#### Scenario: No control actions
- **WHEN** the user interacts with the widget
- **THEN** no machine command (wake, start, stop, etc.) is issued from the widget process

### Requirement: Native widget available on iOS and Android

The widget SHALL be installable from the iOS and Android Home Screen widget pickers and SHALL be backed by the platform-shared snapshot. It SHALL be built and signed as part of the existing release pipeline for each platform.

#### Scenario: iOS widget present
- **WHEN** the user adds widgets on iOS 17+
- **THEN** a Decenza machine-status widget is available and renders from the App Group snapshot

#### Scenario: Android widget present
- **WHEN** the user adds widgets on Android (API 28+)
- **THEN** a Decenza machine-status widget is available and renders from the app-shared snapshot

#### Scenario: Builds in the release pipeline
- **WHEN** the iOS App Store CI workflow and the Android APK build run
- **THEN** the widget extension/receiver is included and signed without manual post-build steps

