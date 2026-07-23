# steam-stop-controls Specification

## Purpose
TBD - created by archiving change remove-steam-purge-buttons. Update Purpose after archive.
## Requirements
### Requirement: Steam page SHALL NOT present a dedicated Purge control

The Steam page SHALL NOT display a dedicated "Purge" button in either its live-steaming
view or its settings view. The firmware steam-wand purge is triggered as a side effect of
stopping steam (a Steam→Idle state transition), so it SHALL be reached through the stop
path rather than a separate on-screen control.

#### Scenario: Live steaming view offers no Purge button

- **WHEN** the machine is actively steaming and the Steam page live view is shown
- **THEN** no "Purge" button is displayed
- **AND** on a headless machine the Stop button remains available, and stopping steam
  triggers the firmware steam-wand purge on the Steam→Idle transition

#### Scenario: Settings view offers no Purge button

- **WHEN** the machine is not steaming and the Steam page settings view is shown
- **THEN** no "Purge" button is displayed

#### Scenario: GHC machine stops steam without an on-screen control

- **WHEN** the machine is non-headless (a group-head controller is installed) and is steaming
- **THEN** the Steam page shows no in-app stop or purge button
- **AND** steam is stopped — and the firmware purge fired — via the physical group-head control

### Requirement: Keyboard and screen-reader focus order SHALL remain intact after Purge removal

Removing the Purge buttons SHALL NOT leave any `KeyNavigation`, tab, or backtab reference
pointing at a control that no longer exists. Keyboard and screen-reader users SHALL be able
to traverse the Steam page's remaining controls in a coherent order with no dead focus stops.

#### Scenario: Live view focus traversal skips the removed Purge button

- **WHEN** a keyboard or screen-reader user tabs through the live steaming view
- **THEN** focus moves among the remaining live-view controls (Stop on headless machines,
  the steam preset pills, and the flow/duration sliders) with no reference to a removed
  Purge button

#### Scenario: Settings view focus traversal skips the removed Purge button

- **WHEN** a keyboard or screen-reader user tabs through the settings view
- **THEN** focus moves among the remaining settings-view controls (add-pitcher, duration
  slider, and neighboring controls) with no reference to a removed Purge button

