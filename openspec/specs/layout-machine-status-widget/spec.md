# layout-machine-status-widget Specification

## Purpose
TBD - created by archiving change composable-status-bar. Update Purpose after archive.
## Requirements
### Requirement: Machine status and connection status are one widget

The `connectionStatus` widget SHALL be merged into `machineStatus`: they render the same `MachineStatusItem`, which shows the machine's phase and "Disconnected" when offline (subsuming the old Online/Offline display). The palette SHALL offer a single widget. Existing layouts referencing `connectionStatus` SHALL continue to work.

#### Scenario: connectionStatus renders as machine status

- **WHEN** a layout contains a `connectionStatus` item (old config) or it is selected as a palette widget
- **THEN** it SHALL render the machine-status widget (phase + offline state), not a separate Online/Offline widget

#### Scenario: Saved configs are migrated

- **WHEN** a layout config containing `connectionStatus` items is loaded
- **THEN** those items SHALL be migrated to `machineStatus` (type unified), idempotently and persisted once

#### Scenario: Single palette entry

- **WHEN** a user browses the widget palette in either editor
- **THEN** only one machine/connection status widget SHALL be offered (no separate "Connection" widget)

#### Scenario: Default layouts updated

- **WHEN** a fresh install uses the default layout
- **THEN** the status bar and center-status zones SHALL use `machineStatus`

