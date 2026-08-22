## ADDED Requirements

### Requirement: Shot ids are remapped for every reference that survives an import

Importing a shot history assigns each imported shot a new id in the destination database. The system SHALL produce a mapping from each source shot id to the destination id it received, and SHALL apply that mapping to every reference to a shot id that is carried across by the same import — whether that reference lives inside `shots.db` or outside it.

A reference whose source shot id is absent from the mapping — because the shot was skipped as a duplicate, failed to import, or was never in the source — SHALL be cleared rather than left holding the source id. An uncleared stale id is not inert: shot ids are assigned in increasing order, so a stale id eventually becomes a valid id belonging to an unrelated shot, at which point a write intended for one shot lands on another.

This extends the existing remap guarantee, which today covers equipment packages, coffee bags and recipes, to shot ids themselves and to references held in settings.

#### Scenario: Settings-resident shot references follow the renumbering

- **GIVEN** a backup whose shots carry ids that the destination will not reuse
- **AND** settings data in the same backup that names those source shot ids
- **WHEN** the backup is restored
- **THEN** each such reference SHALL name the destination id of the same shot
- **AND** no reference SHALL name an id from the source database

#### Scenario: A reference to a shot that was not imported is cleared

- **GIVEN** a stored reference naming a source shot id
- **AND** that shot is skipped as a duplicate or fails to import
- **WHEN** the import completes
- **THEN** the reference SHALL be cleared
- **AND** SHALL NOT retain the source id

#### Scenario: Remapping applies to every import surface

- **WHEN** a shot history is imported through backup restore, through device-to-device migration, or through the backup endpoint
- **THEN** the same remapping SHALL be applied in each case
- **AND** no surface SHALL carry references across without remapping them

#### Scenario: Repeat restore of the same backup does not accumulate stale references

- **GIVEN** a backup that has already been restored once
- **WHEN** the same backup is restored again
- **THEN** references SHALL resolve to the destination's current shot ids
- **AND** SHALL NOT accumulate references to ids from either prior import

### Requirement: A write to a shot id that does not exist SHALL be reported as a failure

When the system resolves a shot id from stored state and writes to that shot, a write naming an id with no matching shot SHALL be reported to the caller as a failure rather than only recorded in a log. A caller that acted on a user's input SHALL be able to tell that the input was not saved.

The system SHALL NOT silently discard user-supplied data because the id it was addressed to no longer resolves.

#### Scenario: Metadata write to a missing shot fails visibly

- **GIVEN** stored state naming a shot id with no matching shot
- **WHEN** the system attempts to write metadata to that shot
- **THEN** the attempt SHALL report failure to its caller
- **AND** the failure SHALL identify the unresolvable id

#### Scenario: User input captured against a stale reference is not lost silently

- **GIVEN** a user supplies information that the system attributes to a shot
- **AND** the stored reference to that shot does not resolve
- **WHEN** the system attempts to persist the information
- **THEN** the failure SHALL be surfaced rather than absorbed
- **AND** the user SHALL NOT be left believing the information was recorded
