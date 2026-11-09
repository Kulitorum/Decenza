# Spec Delta

## ADDED Requirements

### Requirement: Machine status is the single status widget

The `connectionStatus` widget SHALL be merged into `machineStatus`: they render the same `MachineStatusItem`, which shows the machine's phase and "Disconnected" when offline (subsuming the old Online/Offline display). The palette SHALL offer a single widget. Existing layouts referencing `connectionStatus` SHALL continue to work.

#### Scenario: connectionStatus renders as machine status

- **WHEN** a layout contains a `connectionStatus` item (old config) or it is selected as a palette widget
- **THEN** it SHALL render the machine-status widget (phase + offline state), not a separate Online/Offline widget

#### Scenario: Single palette entry

- **WHEN** a user browses the widget palette in either editor
- **THEN** only one machine/connection status widget SHALL be offered (no separate "Connection" widget)

#### Scenario: Default layouts updated

- **WHEN** a fresh install uses the default layout
- **THEN** the status bar and center-status zones SHALL use `machineStatus`

## REMOVED Requirements

### Requirement: Machine status and connection status are one widget
**Reason**: Its "Saved configs are migrated" scenario describes a one-time rewrite that shipped in 1.8.1 and has already run on every installation at or above the upgrade floor (2.0.7). A layout that still names `connectionStatus` keeps rendering through the alias.
**Migration**: None. Replaced by "Machine status is the single status widget".
