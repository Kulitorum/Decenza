# steam-stop-controls Specification

## Purpose

Governs the Steam page's stop and Back controls and their keyboard/screen-reader focus order. Post-steam and idle-page Purge actions are specified in `steam-purge`.

## Requirements

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

### Requirement: Back on the Steam page stops steam
The Steam page SHALL show its bottom bar with Back while steam runs, including steam heating, a soft stop and puffing. Back SHALL then act as the Stop control does, as Back stops a shot on the espresso page. With no steam running, Back SHALL leave the page as before.

#### Scenario: Back while steaming
- **WHEN** the machine is steaming and the user taps Back
- **THEN** the app sends the same stop request as the Stop control

#### Scenario: Back with no steam running
- **WHEN** the Steam page shows its settings and the user taps Back
- **THEN** the page closes without a stop request
