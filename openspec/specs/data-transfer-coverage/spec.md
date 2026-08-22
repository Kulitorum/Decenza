# data-transfer-coverage Specification

## Purpose
TBD - created by archiving change finish-recipes-first-class. Update Purpose after archive.

## Requirements

### Requirement: Recipes transfer on device migration and backup restore

The system SHALL include the `recipes` table when merging one shot-history database into another during device-to-device migration and `.dcbackup` restore. Recipes SHALL be merged (not dropped) with the same lifecycle guarantees as coffee bags, and every merged recipe's foreign keys SHALL be remapped to the destination database's ids.

#### Scenario: Recipes are merged, not dropped

- **WHEN** a source `shots.db` containing recipes is imported into a destination database via `importDatabaseStatic` (merge mode)
- **THEN** each source recipe is inserted into the destination `recipes` table
- **AND** the recipe's `equipment_id` is remapped through the equipment package id-map produced by the equipment import
- **AND** the destination retains any recipes it already had

#### Scenario: Shot recipe provenance is preserved across transfer

- **WHEN** shots that carry a `recipe_id` are migrated alongside their recipes
- **THEN** each migrated shot's `recipe_id` is remapped to the newly inserted recipe's destination id
- **AND** no migrated shot references a recipe id that does not exist in the destination database

#### Scenario: Duplicate recipes are not doubled on repeat import

- **WHEN** the same source recipe is imported into a destination that already contains an equivalent recipe
- **THEN** the merge dedups on the recipe's identity rather than creating a second copy
- **AND** shots pointing at either copy resolve to a single destination recipe

#### Scenario: Full-archive restore carries recipes

- **WHEN** a `.dcbackup` archive is restored on a device
- **THEN** recipes contained in the archived `shots.db` are present after restore
- **AND** archived shots' `recipe_id` links resolve to those restored recipes

#### Scenario: Archived recipes keep their state

- **WHEN** a source recipe is flagged `archived`
- **THEN** it is imported with its `archived` state intact so shot provenance never dangles

### Requirement: Migration UI advertises recipe, bag, and equipment counts

The device-migration manifest and the migration dialog SHALL advertise counts for recipes, coffee bags, and equipment so users can see that these data types transfer, rather than folding them silently into the "Shots" total.

#### Scenario: Manifest reports recipe/bag/equipment counts

- **WHEN** a client fetches `/api/backup/manifest` from a server that has recipes, bags, and equipment
- **THEN** the manifest includes a count for each of recipes, coffee bags, and equipment

#### Scenario: Dialog surfaces what will transfer

- **WHEN** the migration dialog displays the source device's manifest summary
- **THEN** the presence of recipes, bags, and equipment is visible to the user before import

### Requirement: Every durable data type is both exported and imported

Backup and transfer SHALL be symmetric: every durable data type the app persists SHALL appear in an export path (the `.dcbackup` full archive and, where user-selectable, a LAN endpoint) AND have a matching import/restore path. No data type SHALL be exportable but silently un-importable, or persisted but absent from backup.

#### Scenario: Full-archive self-backup is complete

- **WHEN** a user creates a `.dcbackup` full-archive export
- **THEN** the archive contains every durable data type — settings, profiles, shots (with samples/phases), media, AI conversations, coffee bags, equipment, recipes, SAW learning history, and extra settings (shot-map location, accessibility, language)
- **AND** restoring that archive on a fresh device reproduces each of those data types

#### Scenario: LAN export and import stay in parity

- **WHEN** a data type is offered for device-to-device migration
- **THEN** the source device exposes it via an export endpoint (or within `shots.db`/settings)
- **AND** the destination client has a matching import path, so nothing is served-but-dropped or fetched-but-unserved

### Requirement: SAW learning history transfers

The system SHALL include stop-at-weight (SAW) learning history in the settings that are exported and imported during device migration and backup, so learned per-(profile, scale) offsets are not lost on device change.

#### Scenario: SAW learning survives migration

- **WHEN** settings are exported and imported during device-to-device migration
- **THEN** the SAW learning history is included in the transferred settings
- **AND** the destination device retains the learned offsets after import

#### Scenario: SAW learning is present in the full archive

- **WHEN** a `.dcbackup` full archive is created and later restored
- **THEN** the SAW learning history is contained in the archive and restored with it

### Requirement: Extra settings transfer over LAN migration

The LAN device-migration client SHALL fetch and import the extra-settings bundle (shot-map location, accessibility preferences, language) that the full-archive backup already includes, so device-to-device migration does not silently drop these settings.

#### Scenario: LAN migration carries extra settings

- **WHEN** a user runs device-to-device migration and imports settings
- **THEN** the client fetches the extra-settings bundle from the source device
- **AND** the shot-map location, accessibility preferences, and language are applied on the destination device

#### Scenario: Flow calibration remains excluded

- **WHEN** settings are imported during migration
- **THEN** machine-specific flow calibration is not overwritten on the destination device

### Requirement: An import SHALL NOT renumber a shot whose id is free

A shot's id is the handle every reference to it uses, including references the system does not own. Renumbering a shot invalidates all of them, so the system SHALL preserve each imported shot's own id wherever that id is not already taken in the destination.

In replace mode the destination is cleared first, so every source id is free: a restored database SHALL hold the ids it was backed up with, and its id sequence SHALL be realigned so subsequent shots continue the restored history. In merge mode, shots already in the destination SHALL keep their ids, and an incoming shot SHALL keep its own id unless that id is occupied.

An incoming shot whose id is occupied SHALL be assigned an id above every id in use in either database, so relocating one shot can never consume an id that another incoming shot is entitled to keep.

#### Scenario: Restoring a backup returns the original ids

- **GIVEN** a backup whose shots carry ids 1 to N
- **AND** a destination whose id sequence has advanced beyond N
- **WHEN** the backup is restored in replace mode
- **THEN** the restored shots SHALL carry ids 1 to N
- **AND** the id sequence SHALL be realigned to N

#### Scenario: A merge leaves existing shots and free incoming ids alone

- **GIVEN** a destination holding shots at ids 1 and 2
- **AND** a source holding shots at ids 1, 2 and 3
- **WHEN** a merge-mode import runs
- **THEN** the destination's own shots SHALL remain at ids 1 and 2
- **AND** the incoming shot whose id is 3 SHALL keep id 3
- **AND** the two incoming shots whose ids are occupied SHALL be assigned ids above every id in use

### Requirement: Shot ids are remapped for every reference that survives an import

An import that cannot preserve a shot's id assigns it a new one. The system SHALL produce a mapping from each source shot id to the destination id it received — an identity mapping where the id was preserved — and SHALL apply that mapping to every reference to a shot id that is carried across by the same import, whether that reference lives inside `shots.db` or outside it.

A reference whose source shot id is absent from the mapping — because the shot was skipped as a duplicate, failed to import, or was never in the source — SHALL be cleared rather than left holding the source id. An uncleared stale id is not inert: shot ids are assigned in increasing order, so a stale id eventually becomes a valid id belonging to an unrelated shot, at which point a write intended for one shot lands on another.

This extends the existing remap guarantee, which today covers equipment packages, coffee bags and recipes, to shot ids themselves and to references held in settings.

#### Scenario: Settings-resident shot references follow the renumbering

- **GIVEN** a backup whose shots carry ids that are occupied in the destination
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
