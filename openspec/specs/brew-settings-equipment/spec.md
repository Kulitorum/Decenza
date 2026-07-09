# brew-settings-equipment Specification

## Purpose
Defines how `BrewDialog.qml` presents grinder equipment once equipment packages exist: the editable grinder brand/model/burrs text inputs are replaced with a read-only equipment summary resolved from the active bag's `equipment_id` plus a "Switch Equipment" button, while the grind setting and (for RPM-capable grinders) an RPM field remain editable dial-in fields that write through to both the active bag and package.
## Requirements
### Requirement: Brew Settings shows a read-only Equipment summary with a Switch button
`BrewDialog.qml` SHALL replace the editable grinder brand/model/burrs text inputs with a read-only "Equipment: {grinder identity}" summary and a "Switch Equipment" button that opens the Switch Equipment dialog. The grinder identity SHALL be resolved through the active bag's `equipment_id`.

#### Scenario: Equipment summary shown
- **WHEN** Brew Settings is opened with an equipment package linked to the active bag
- **THEN** it SHALL show a read-only summary of the grinder identity and a Switch Equipment button
- **AND** it SHALL NOT show editable grinder brand/model/burrs text inputs

#### Scenario: No equipment linked
- **WHEN** the active bag has no linked equipment (`equipment_id` is null)
- **THEN** Brew Settings SHALL show "Equipment: not set" and an "Add Equipment" affordance

### Requirement: Grind setting and RPM remain editable dial-in
`BrewDialog.qml` SHALL keep the grind setting as an editable dial-in field and SHALL add an editable RPM dial-in field. The RPM field SHALL be shown only when the linked package's grinder is `rpmCapable`. Edits SHALL follow the dual write-through rule (active bag + active package).

#### Scenario: RPM shown for a capable grinder
- **WHEN** the linked package's grinder has `rpmCapable = true`
- **THEN** Brew Settings SHALL show an editable RPM field alongside the grind setting

#### Scenario: RPM hidden for a non-capable grinder
- **WHEN** the linked package's grinder has `rpmCapable = false`
- **THEN** Brew Settings SHALL NOT show the RPM field

#### Scenario: Editing the dial writes through
- **WHEN** the user edits the grind setting or RPM
- **THEN** the value SHALL be written to the active bag and the active package's last-dial fields

