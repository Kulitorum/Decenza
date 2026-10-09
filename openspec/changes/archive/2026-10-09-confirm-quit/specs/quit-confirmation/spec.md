## ADDED Requirements

### Requirement: Sleep's long-press can ask before quitting
When a Sleep widget has both `allowQuit` and `confirmQuit` on, its long-press SHALL raise `AppShell.quitRequested()` rather than quit directly. The shell SHALL respond with a confirmation whose default focus is Cancel, and SHALL quit only when the user confirms. During a firmware flash it SHALL instead show the firmware-flash exit warning. Every other quit path SHALL be unaffected.

#### Scenario: Accidental hold on Sleep
- **WHEN** the user holds a Sleep widget with long-press-to-quit and "Ask before quitting" on
- **THEN** a "Quit Decenza?" confirmation opens, and the app keeps running unless the user confirms

#### Scenario: Default is unchanged
- **WHEN** the user holds a Sleep widget whose `confirmQuit` is unset
- **THEN** the app quits at once, as before

#### Scenario: Quit during a firmware flash
- **WHEN** a confirmed quit is requested while firmware is flashing
- **THEN** the firmware-flash exit warning opens instead of the plain confirmation
