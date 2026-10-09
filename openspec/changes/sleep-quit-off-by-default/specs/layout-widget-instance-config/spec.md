## MODIFIED Requirements

### Requirement: Configurable quit option for the sleep widget

The `sleep` widget SHALL have a per-instance `allowQuit` option controlling whether long-press-to-quit is available. It SHALL be editable by long-pressing the Sleep widget in the layout editor (in-app and web), persisted via the existing item-property mechanism. When `allowQuit` is not set, long-press SHALL NOT quit. The default SHALL be declared once (`SettingsNetwork::sleepOptionDefaults()`), and every rendering path and both editors SHALL read it.

#### Scenario: Default does not quit

- **WHEN** a `sleep` widget has no `allowQuit` set (existing layouts included)
- **THEN** tap SHALL sleep and long-press SHALL NOT quit the app

#### Scenario: Compiled centre-zone Sleep follows the same option

- **WHEN** a `sleep` widget renders in a centre zone (compiled to a custom tile)
- **THEN** its long-press SHALL quit only if that instance's `allowQuit` is true

#### Scenario: Enabling the quit option

- **WHEN** a user enables `allowQuit` on a Sleep instance in either editor
- **THEN** that Sleep instance SHALL quit on long-press
- **AND** the "long-press to quit" accessibility hint SHALL be present for that instance
- **AND** the setting SHALL persist for that instance only

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
