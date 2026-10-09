## ADDED Requirements

### Requirement: Permission prompts are classified by kind, not by text
A BLE error that only the user can fix in system settings (a Location or Bluetooth permission, or Location services being off) SHALL reach the UI with its kind. The UI SHALL choose the dialog's title and settings button from that kind, never by searching the message, which is translated.

#### Scenario: Location permission denied in another language
- **WHEN** the app language is not English and the Location permission is denied
- **THEN** the dialog shows its Location title and the button that opens Location settings

#### Scenario: Location services turned off
- **WHEN** a scan fails because Location services are off
- **THEN** the dialog shows a translated request to turn Location on, with the Location settings button
