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

