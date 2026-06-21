## ADDED Requirements

### Requirement: Per-instance display mode for readout widgets

The `machineStatus`, `temperature`, `steamTemperature`, and `scaleWeight` widgets SHALL each gain a per-instance `displayMode` property with values `text` (default) and `icon`. In `icon` mode the widget SHALL render a tinted icon ahead of its value, using its existing icon asset; in `text` mode it SHALL render exactly as it does today. The mode SHALL be read from the item's stored properties (`modelData`), persist per instance, and apply in any zone the widget is placed in.

#### Scenario: Default is text mode

- **WHEN** one of these widgets has no `displayMode` set (existing layouts)
- **THEN** it SHALL render value-only, identical to today

#### Scenario: Icon mode renders an icon

- **WHEN** a widget's `displayMode` is `icon`
- **THEN** it SHALL render its icon ahead of the value, tinted to the surrounding text/contrast color

#### Scenario: Works in any zone

- **WHEN** an icon-mode widget is placed in any zone (status bar, a bottom bar, the lower-mid bar, etc.)
- **THEN** it SHALL render its icon form there — the mode is not gated by a zone style

#### Scenario: Two instances differ

- **WHEN** the same widget type is placed twice with different `displayMode` values
- **THEN** each instance SHALL render according to its own mode

#### Scenario: Edited via long-press in the editor

- **WHEN** a user long-presses one of these widgets in the in-app or web layout editor
- **THEN** an editor SHALL expose the `displayMode` choice for that instance (and for `scaleWeight`, its existing `dataMode` as well)
- **AND** the chosen mode SHALL persist for that instance only

#### Scenario: Disconnected state

- **WHEN** the widget's device is disconnected
- **THEN** it SHALL show its existing placeholder ("—"/"--") in either mode, without errors

### Requirement: Configurable quit option for the sleep widget

The `sleep` widget SHALL gain a per-instance `allowQuit` option controlling whether long-press-to-quit is available. It SHALL be editable by long-pressing the Sleep widget in the layout editor (in-app and web), persisted via the existing item-property mechanism. The default SHALL preserve current behaviour (quit enabled).

#### Scenario: Default keeps quit available

- **WHEN** a `sleep` widget has no `allowQuit` set (existing layouts)
- **THEN** long-press-to-quit SHALL behave exactly as it does today

#### Scenario: Removing the quit option

- **WHEN** a user disables `allowQuit` on a Sleep instance in either editor
- **THEN** that Sleep instance SHALL sleep on tap but SHALL NOT quit on long-press
- **AND** the "long-press to quit" accessibility hint SHALL be dropped for that instance
- **AND** the setting SHALL persist for that instance only

#### Scenario: Long-press opens the sleep editor in-app

- **WHEN** a user long-presses a `sleep` widget in the in-app layout editor
- **THEN** an editor SHALL open exposing the quit-option toggle for that instance

#### Scenario: Toggling the sleep icon

- **WHEN** a user toggles the Sleep widget's `showIcon` option (default on) in either editor
- **THEN** that Sleep instance SHALL show or hide its icon accordingly (off = label only)
- **AND** the setting SHALL persist for that instance only
