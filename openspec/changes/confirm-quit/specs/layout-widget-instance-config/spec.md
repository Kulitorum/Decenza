## MODIFIED Requirements

### Requirement: Configurable quit option for the sleep widget

The `sleep` widget SHALL gain a per-instance `allowQuit` option controlling whether long-press-to-quit is available. It SHALL be editable by long-pressing the Sleep widget in the layout editor (in-app and web), persisted via the existing item-property mechanism. The default SHALL preserve current behaviour (quit enabled). With `allowQuit` on, a long-press SHALL request a quit through the app's quit confirmation (`quit-confirmation`) rather than quit at once.

#### Scenario: Default keeps quit available

- **WHEN** a `sleep` widget has no `allowQuit` set (existing layouts)
- **THEN** long-press SHALL offer to quit, and the app SHALL quit once the user confirms

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
