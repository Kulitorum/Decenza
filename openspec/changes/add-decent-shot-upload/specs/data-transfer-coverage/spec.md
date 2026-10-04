## ADDED Requirements

### Requirement: Decent account credentials never transfer

The linked Decent account's email and encrypted password SHALL be treated as sensitive settings. They SHALL be excluded from `.dcbackup` archives, from the LAN settings backup endpoint, and from device-to-device migration, and SHALL be ignored if present in an imported settings file. The user re-links the account on the destination device, as they already re-enter Visualizer credentials.

#### Scenario: Migrate to a new tablet
- **WHEN** a user migrates from a device with a linked Decent account to a new device
- **THEN** the new device has no linked Decent account
- **AND** the Decent and Visualizer switches and the shared Upload settings do transfer

#### Scenario: Restore a backup
- **WHEN** a `.dcbackup` archive is restored
- **THEN** no Decent account credentials are restored, even if the archive was hand-edited to include them

### Requirement: Decent upload state travels with the shots

The per-shot Decent upload state (upload time, server shot id, uploaded-under serial, pending replacement, rejection) SHALL be carried with each shot when shot-history databases are merged during device migration and `.dcbackup` restore. A shot that was uploaded on the source device SHALL be recognised as uploaded on the destination and not uploaded again.

#### Scenario: Migrated history is not re-uploaded
- **WHEN** a history containing 500 uploaded shots is migrated and the user links the same Decent account on the new device
- **THEN** the backlog drain does not re-upload any of those 500 shots
