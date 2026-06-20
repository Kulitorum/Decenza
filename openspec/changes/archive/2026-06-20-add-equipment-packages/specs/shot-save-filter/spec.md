## ADDED Requirements

### Requirement: Shot snapshots SHALL capture equipment_id and rpm
When a shot is saved, the snapshot SHALL record the active bag's `equipment_id` (the package pointer) and the `rpm` dial-in value, in place of the dropped grinder identity columns. The grind setting SHALL continue to be snapshotted. Grinder brand/model/burrs SHALL NOT be stored on the shot row; they are resolved via `equipment_id` at read time.

#### Scenario: Shot saved with a linked package
- **WHEN** a shot is saved while the active bag has a linked equipment package
- **THEN** the shot row SHALL store that package's `equipment_id`, the grind setting, and the rpm
- **AND** the shot row SHALL NOT store grinder brand/model/burrs strings

#### Scenario: Shot saved with no equipment
- **WHEN** a shot is saved while the active bag has no linked equipment
- **THEN** the shot's `equipment_id` SHALL be null and grinder identity SHALL resolve as empty
