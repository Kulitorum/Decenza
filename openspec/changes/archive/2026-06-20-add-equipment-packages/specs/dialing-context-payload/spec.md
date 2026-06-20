## ADDED Requirements

### Requirement: Dialing grinder context SHALL resolve via the equipment package
The grinder identity in dialing surfaces (`grinderContext`, `currentBean` setup, the grinder-calibration inputs) SHALL be resolved through the resolved shot's `equipment_id` rather than from grinder identity columns on the shot row. Inputs to the grinder-calibration block (grinder model + burrs) SHALL come from the resolved package's grinder item.

#### Scenario: grinderContext sourced from the package
- **WHEN** `dialing_get_context` builds `grinderContext` for the resolved shot
- **THEN** the grinder brand/model/burrs SHALL be resolved by following `equipment_id` to the package's grinder item

#### Scenario: Shot with no linked equipment
- **WHEN** the resolved shot has a null `equipment_id`
- **THEN** the grinder context SHALL be omitted or empty rather than fabricated

### Requirement: Dialing context SHALL expose the rpm dial-in
The dialing context SHALL include the shot's `rpm` dial-in value (when present) alongside the grind setting, and SHALL indicate whether the grinder is `rpmCapable`.

#### Scenario: rpm present on a capable grinder
- **WHEN** the resolved shot used an rpm-capable grinder and recorded an rpm
- **THEN** the dialing context SHALL include the `rpm` value

#### Scenario: rpm absent
- **WHEN** the resolved shot recorded no rpm
- **THEN** the `rpm` field SHALL be omitted
