# Spec Delta

## MODIFIED Requirements

### Requirement: Brew Settings shows a read-only Equipment summary with a Switch button
Brew Settings SHALL show the active equipment's grinder identity and a Switch Equipment button without editable grinder identity inputs.

#### Scenario: Equipment summary shown
- **WHEN** Brew Settings opens with an active equipment package
- **THEN** its grinder identity and a Switch Equipment button are shown

#### Scenario: No equipment linked
- **WHEN** no equipment package is active
- **THEN** Brew Settings shows equipment is not set and an Add Equipment affordance

### Requirement: Grind setting and RPM remain editable dial-in
Brew Settings SHALL provide editable grind and, when the active grinder supports it, RPM. Edits SHALL update the active equipment's last dial and any active recipe under its existing rules, without writing to the bag.

#### Scenario: RPM shown for a capable grinder
- **WHEN** the active grinder supports RPM
- **THEN** an editable RPM field appears

#### Scenario: RPM hidden for a non-capable grinder
- **WHEN** the active grinder does not support RPM
- **THEN** the RPM field is hidden

#### Scenario: Editing the dial writes through
- **WHEN** grind or RPM is edited with equipment active
- **THEN** the active equipment's last dial updates and the bag does not
