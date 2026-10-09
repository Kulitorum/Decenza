## ADDED Requirements

### Requirement: Unrelated notices wait while the machine is running
While the machine is running an operation that an operation page shows (espresso, steam including its warm-up, hot water, flush, descale, clean, transport), the app SHALL NOT open the update prompt, charging-mismatch warning, scale notices, BLE errors, local-network notice or decent-machine choice. It SHALL queue them as it does behind the screensaver, and SHALL show them when the operation ends and no other dialog is open. Prompts the operation itself raises (refill, standby switch, the no-scale shot abort) are not held.

#### Scenario: Waking from the group head into a shot
- **WHEN** the screensaver is up with a saved scale not connected, and the user starts a shot from the group head
- **THEN** the scale notice does not open over the Espresso page, and opens after the shot ends

#### Scenario: A notice already open when an operation starts
- **WHEN** a held notice is open and an operation starts
- **THEN** it is closed and queued, and shown again when the operation ends

#### Scenario: Another dialog is open when the operation ends
- **WHEN** an operation ends while a refill, standby or no-scale abort dialog is open
- **THEN** queued notices wait until that dialog closes
