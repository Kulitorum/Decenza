## ADDED Requirements

### Requirement: Visualizer upload SHALL resolve grinder identity via the equipment package
The Visualizer upload payload builders in `src/network/visualizeruploader.cpp` SHALL resolve grinder brand/model by following the shot's `equipment_id` to the package's grinder item, then combine them into the single `grinder_model` string Visualizer expects (`"brand model"`). The payload shape SHALL be otherwise unchanged: `grinder_model`, `grinder_setting`, `grinder_dose_weight` (and their `meta.grinder.*` / `settings.*` mirrors). Burrs SHALL remain unsent (Visualizer has no field for it).

#### Scenario: Pointer resolved on upload
- **WHEN** a shot with a linked equipment package is uploaded
- **THEN** `grinder_model` SHALL contain the resolved "brand model" string from the package's grinder item

#### Scenario: Shot with no linked equipment
- **WHEN** a shot has a null `equipment_id`
- **THEN** the grinder fields SHALL be omitted from the payload rather than sent empty

### Requirement: Visualizer upload SHALL append rpm to the grind setting
Because Visualizer has no rpm field, when a shot has an rpm dial-in value the uploader SHALL append it to the `grinder_setting` string using the community convention (`"{setting} {rpm}rpm"`, e.g. `"2.4 1400rpm"`). When rpm is absent the `grinder_setting` SHALL be sent unmodified.

#### Scenario: rpm appended
- **WHEN** a shot has `grinder_setting = "2.4"` and `rpm = 1400`
- **THEN** the uploaded `grinder_setting` SHALL be `"2.4 1400rpm"`

#### Scenario: no rpm
- **WHEN** a shot has `grinder_setting = "2.4"` and no rpm
- **THEN** the uploaded `grinder_setting` SHALL be `"2.4"`

### Requirement: Visualizer profile import SHALL remain unchanged
Visualizer import (`src/network/visualizerimporter.cpp`) imports profiles only; the `grinder_model`/`grinder_setting` it reads are display-only metadata never persisted to a local shot or bag. This change SHALL NOT add equipment-package creation or `equipment_id` linkage on the import path.

#### Scenario: Import does not create packages
- **WHEN** a user imports a shared shot's profile from Visualizer
- **THEN** no equipment package SHALL be created and no local shot/bag SHALL be linked
