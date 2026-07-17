## MODIFIED Requirements

### Requirement: CoffeeBag data model
The system SHALL define a `CoffeeBag` value type with the following fields:
- Identity: `id` (int, DB primary key), `roasterName`, `coffeeName`, `roastDate`, `roastLevel`, `beanBaseId` (canonical UUID, nullable), `beanBaseData` (JSON blob, nullable)
- Lifecycle: `frozenDate` (nullable), `defrostDate` (nullable), `storageHint` (nullable string enum: `counter` / `airtight` / `vacuum-sealed` / `fridge` — describes non-frozen storage only; frozen state is determined solely by `frozenDate` being set, never by `storageHint`, so the two cannot disagree), `openedDate` (nullable date — the non-frozen analogue of `defrostDate`: when the current portion started being actively used/exposed to room temperature), `notes` (nullable), `startWeightG` (double, nullable — column retained but UNSURFACED: the UI field was removed as low-value and Visualizer has no equivalent), `inInventory` (bool, default true)
- Last-used grinder/dose: `grinderBrand`, `grinderModel`, `grinderBurrs`, `grinderSetting`, `doseWeightG` (all nullable)
- Yield spec: `yieldValue` (double) + `yieldMode` (`none` | `absolute` | `ratio`) — the bean's **own** yield, a first-class anchor rather than a deviation from the active profile's target weight (`yield-anchor`). `mode = none` means the bag designs no yield and the ladder falls through to the profile. The legacy `yieldOverrideG` column is converted by migration and left dead in place.
- Visualizer sync: `visualizerBagId` (nullable UUID string), `visualizerRoasterId` (nullable UUID string), `visualizerSyncPending` (bool, default false — a bag edit failed to push and awaits retry)

`storageHint` and `openedDate` are local-only fields, same as `frozenDate`/`defrostDate` — neither is included in `touchesVisualizerFields()` and neither is pushed to or read from Visualizer. The yield spec is local-only for the same reason.

> **Merge note (mirror of `add-yield-ratio-anchor`'s):** this requirement is rewritten wholesale by BOTH changes, so whichever archives second silently deletes the other's fields. The yield spec above is folded in deliberately — `add-yield-ratio-anchor` archived FIRST (2026-07-17) and retired `yieldOverrideG`, so this delta would otherwise revert the bag to a field the code no longer has. Do not "restore" `yieldOverrideG` here: it is a dead column, converted by migration 34. If any third change rewrites this requirement, it must carry BOTH this change's lifecycle fields and the yield spec.

The `beanBaseData` blob SHALL be valid **without** a canonical `id`: a manual bag may carry user-entered detail keys (`origin`, `region`, `farm`, `producer`, `variety`, `elevation`, `process`, `harvest`, `qualityScore`, `placeOfPurchase`, `tastingNotes`, `link`, `degree`) while remaining unlinked (`isLinked` stays defined solely by a non-empty `id`). A linked blob additionally carries a `canonical` sub-object — the pristine entry snapshot for revert — which consumers of the flat working keys ignore and shot snapshots carry along unchanged.

#### Scenario: Bag creation with full canonical data
- **WHEN** a user creates a bag from a Bean Base canonical result
- **THEN** the bag SHALL store `beanBaseId`, `beanBaseData` (origin, variety, process, harvest, tasting, producer, elevation, canonical_roaster_id), `roasterName`, `coffeeName`, and the user-entered `roastDate`

#### Scenario: Bag creation without canonical data
- **WHEN** a user creates a bag via manual entry
- **THEN** the bag SHALL store only user-entered fields; `beanBaseId` SHALL be null
- **AND** `beanBaseData` SHALL be null unless the user entered bean details, in which case it SHALL carry those keys with no `id`

#### Scenario: Unlinked blob does not read as linked
- **WHEN** a bag's `beanBaseData` carries detail keys but no `id`
- **THEN** `isLinked` SHALL be false and no canonical id SHALL be sent on shot PATCH for it

#### Scenario: storageHint and openedDate are never synced to Visualizer
- **WHEN** a bag with `storageHint = "airtight"` and `openedDate` set is edited
- **THEN** `touchesVisualizerFields()` SHALL return `false` for a fields map containing only those two keys
- **AND** no Visualizer PATCH SHALL be triggered by that edit alone

### Requirement: Shot snapshot includes bag lifecycle fields
The system SHALL snapshot `frozenDate`, `defrostDate`, `storageHint`, and `openedDate` from the active bag into the shot record at save time, in the `shots` table's own `frozen_date`, `defrost_date`, `storage_hint`, and `opened_date` columns — the same columns-on-`shots` pattern `frozen_date`/`defrost_date` already use (`shothistorystorage.cpp`), not a foreign-key-only reference to the bag row (a later bag edit must not retroactively change what an already-saved shot recorded).

#### Scenario: Frozen bean shot snapshot
- **WHEN** a shot is saved while the active bag has `frozenDate` and `defrostDate` set
- **THEN** the shot record SHALL include both dates in its snapshot

#### Scenario: Non-frozen bean shot snapshot
- **WHEN** a shot is saved while the active bag has `storageHint = "airtight"` and `openedDate` set, with no `frozenDate`/`defrostDate`
- **THEN** the shot record SHALL include `storageHint` and `openedDate` in its snapshot
- **AND** `frozenDate`/`defrostDate` SHALL remain absent from that shot's snapshot

## ADDED Requirements

### Requirement: coffee_bags table gains storage_hint and opened_date columns
A schema migration SHALL add nullable `storage_hint` (TEXT) and `opened_date` (TEXT, ISO date) columns to `coffee_bags`. Existing bags SHALL have both columns unset (NULL) after migration — no backfill. The columns SHALL survive backup restore and device-to-device transfer via the same generic column-copy path as every other `CoffeeBag` field.

#### Scenario: Migration adds the columns
- **WHEN** the schema migration runs on an existing database
- **THEN** every existing bag SHALL have `storage_hint = NULL` and `opened_date = NULL`
- **AND** no existing bag's other fields SHALL change

#### Scenario: Columns survive device transfer
- **WHEN** a bag with `storageHint = "vacuum-sealed"` and `openedDate` set is exported and imported via device-to-device transfer or backup restore
- **THEN** the imported bag SHALL carry the same `storageHint` and `openedDate` values

### Requirement: shots table gains storage_hint and opened_date columns

The same schema migration (or a paired one) SHALL add nullable `storage_hint` (TEXT) and `opened_date` (TEXT, ISO date) columns to the `shots` table, mirroring the existing `frozen_date`/`defrost_date` columns added by an earlier migration. Every code path that reads or writes `frozen_date`/`defrost_date` on `shots` SHALL be extended to the same two new columns: the shot-save `INSERT` and its bound parameters, the shot-read `SELECT` and its `ShotRecord` field mapping, and the device-transfer/backup-restore `INSERT` (including the source-column-presence index resolution used for older source databases that predate the columns).

#### Scenario: Migration adds the shots columns
- **WHEN** the schema migration runs on an existing database
- **THEN** the `shots` table SHALL have nullable `storage_hint` and `opened_date` columns
- **AND** every existing shot row SHALL have both columns NULL

#### Scenario: New shot save populates the columns
- **WHEN** a shot is saved while the active bag has `storageHint`/`openedDate` set
- **THEN** the inserted `shots` row SHALL carry those values in its own `storage_hint`/`opened_date` columns

#### Scenario: Device transfer carries the columns forward, including from older sources
- **WHEN** a shot is exported via device-to-device transfer or backup restore
- **THEN** the imported shot row SHALL carry the source shot's `storage_hint`/`opened_date` values
- **AND** a source database predating this migration (columns absent) SHALL import with both columns NULL rather than failing or logging an "unknown field" warning per row
