## ADDED Requirements

### Requirement: In-app quits ask once
Every quit the app's own UI offers (the Quit widget, the Sleep widget's long-press, and the `command:quit` layout action) SHALL raise `AppShell.quitRequested()` rather than quit directly. The shell SHALL respond with a confirmation whose default focus is Cancel, and SHALL quit only when the user confirms. During a firmware flash it SHALL instead show the firmware-flash exit warning.

#### Scenario: Accidental hold on Sleep
- **WHEN** the user holds the Sleep widget with long-press-to-quit enabled
- **THEN** a "Quit Decenza?" confirmation opens, and the app keeps running unless the user confirms

#### Scenario: Quit during a firmware flash
- **WHEN** a quit is requested while firmware is flashing
- **THEN** the firmware-flash exit warning opens instead of the plain confirmation
