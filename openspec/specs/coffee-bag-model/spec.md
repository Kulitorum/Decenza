# coffee-bag-model Specification

## Purpose
The single source of truth for the `CoffeeBag` data model and its `coffee_bags` database table: identity, freeze/notes lifecycle fields, last-used grinder/dose, the bean's own yield spec, and Visualizer sync bookkeeping, plus how bags survive backup restore and device-to-device transfer, how the active bag is selected and written through from bean/grinder edits, and how bag state is stamped onto each shot snapshot.
## Requirements
### Requirement: CoffeeBag data model
The system SHALL define a `CoffeeBag` value type with identity fields `id` (int, DB primary key), `roasterName`, `coffeeName`, `roastDate`, `roastLevel`, `beanBaseId` (canonical UUID, nullable) and `beanBaseData` (JSON blob, nullable).

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

#### Scenario: A frozen bag retains its out-of-freezer storage plan
- **WHEN** a bag has `frozenDate` set and `storageHint = "vacuum-sealed"`
- **THEN** both values SHALL be stored and returned unchanged
- **AND** no read or write path SHALL clear `storageHint` on account of `frozenDate` being set

#### Scenario: A bag's yield spec is never synced to Visualizer
- **WHEN** a bag's yield anchor is changed
- **THEN** `touchesVisualizerFields()` SHALL return `false` for a fields map containing only the yield spec keys, and no network PATCH SHALL be issued

#### Scenario: A bag cannot hold both an absolute yield and a ratio
- **WHEN** a bag holding `{40.0, absolute}` is given a ratio of 1:3 from any surface (Change Beans dialog, Brew Settings, MCP `bag_update`, web bag editor)
- **THEN** it holds `{3.0, ratio}` and retains no absolute yield

### Requirement: CoffeeBag freeze fields
The bag SHALL carry `frozenDate` and `defrostDate` (nullable), `storageHint` (nullable string enum `counter` / `airtight` / `vacuum-sealed` / `fridge`, the out-of-freezer storage plan) and `openedDate` (nullable, the non-frozen analogue of `defrostDate`). The enum SHALL have no `"frozen"` value; frozen state SHALL be determined solely by `frozenDate` being set. `storageHint` SHALL NOT be cleared, hidden or suppressed on account of `frozenDate`.

#### Scenario: Storage plan is kept on a thawed bag
- **WHEN** a bag with a storage hint is thawed
- **THEN** the storage hint is retained

### Requirement: CoffeeBag notes, start weight and inventory flag
The bag SHALL carry `notes` (nullable), `startWeightG` (double, nullable, retained but NOT surfaced in the UI) and `inInventory` (bool, default true).

#### Scenario: Start weight is retained but hidden
- **WHEN** a bag with a stored start weight is shown in the UI
- **THEN** the start weight is not displayed

### Requirement: CoffeeBag last-used grinder and dose fields
The bag SHALL carry `grinderBrand`, `grinderModel`, `grinderBurrs`, `grinderSetting` and `doseWeightG`, all nullable, recording the last-used grinder and dose.

#### Scenario: Last-used fields are optional
- **WHEN** a bag is created without grinder or dose details
- **THEN** those fields are stored as empty

### Requirement: CoffeeBag yield spec
The bag SHALL carry `yieldValue` (double) and `yieldMode` (`none`, `absolute` or `ratio`). The yield spec is the bean's own yield, a first-class anchor rather than a deviation from the profile's target weight (`yield-anchor`). `mode = none` means the bag designs no yield, so the ladder falls through to the profile. The legacy `yieldOverrideG` column SHALL be converted by migration and left dead in place.

#### Scenario: No-yield bag falls through to the profile
- **WHEN** a bag has `yieldMode = none`
- **THEN** the yield resolves from the profile

### Requirement: CoffeeBag Visualizer sync fields
The bag SHALL carry `visualizerBagId` and `visualizerRoasterId` (nullable UUID strings) and `visualizerSyncPending` (bool, default false), which is true while a bag edit that failed to push awaits retry.

#### Scenario: Failed push is marked pending
- **WHEN** a bag edit fails to push to Visualizer
- **THEN** `visualizerSyncPending` is true until the retry succeeds

### Requirement: Yield spec and freeze fields are local-only
The yield spec, `storageHint` and `openedDate` SHALL be local-only. None SHALL be included in `touchesVisualizerFields()`, so an anchor edit never triggers a bag PATCH, and none SHALL be pushed to a Visualizer bag.

#### Scenario: Local-only edit sends nothing to Visualizer
- **WHEN** only the yield spec, `storageHint` or `openedDate` changes
- **THEN** no bag PATCH is sent

### Requirement: beanBaseData is valid without a canonical id
The `beanBaseData` blob SHALL be valid without a canonical `id`. A manual bag MAY carry user-entered detail keys (`origin`, `region`, `farm`, `producer`, `variety`, `elevation`, `process`, `harvest`, `qualityScore`, `placeOfPurchase`, `tastingNotes`, `link`, `degree`) while remaining unlinked. `isLinked` SHALL remain defined solely by a non-empty `id`.

#### Scenario: Manual bag details without a link
- **WHEN** a manual bag carries detail keys and no canonical id
- **THEN** the bag is unlinked and its details are retained

### Requirement: A linked blob carries a pristine canonical snapshot
A linked `beanBaseData` blob SHALL additionally carry a `canonical` sub-object, the pristine entry snapshot used for revert. Consumers of the flat working keys SHALL ignore it, and shot snapshots SHALL carry it unchanged.

#### Scenario: Revert restores the canonical snapshot
- **WHEN** the user reverts a linked bag
- **THEN** the working keys are restored from the `canonical` sub-object

### Requirement: coffee_bags database table
The system SHALL store bags in a `coffee_bags` SQLite table created by migration 19 in `src/history/shothistorystorage.cpp` (current schema version is 18). The table SHALL include all CoffeeBag fields. All lifecycle and grinder fields SHALL be nullable. DB access SHALL follow the `withTempDb()` background-thread pattern.

#### Scenario: Migration from presets
- **WHEN** the app launches after upgrade and the `bean/presets` QSettings key is present
- **THEN** each preset SHALL be converted to a bag row with `inInventory = true` and lifecycle fields null, mapping `brand` → `roasterName`, `type` → `coffeeName`, and `roastDate`/`roastLevel`/`beanBaseId`/`beanBaseData`/grinder fields directly
- **AND** the preset's `name` SHALL be stored in the bag's `notes` when it differs from "{brand} {type}"; `barista` and `showOnIdle` are intentionally dropped (barista is per-shot and already snapshot on every shot; idle visibility is now "in inventory")
- **AND** `bean/selectedPreset` (index) SHALL map to `activeBagId` (the DB id of the corresponding converted row)
- **AND** the `bean/presets` and `bean/selectedPreset` QSettings keys SHALL be removed only after the DB transaction commits successfully

#### Scenario: Migration failure
- **WHEN** the DB transaction fails during preset migration
- **THEN** the app SHALL log the error and leave QSettings intact
- **AND** the app SHALL start with an empty bag inventory rather than crashing

#### Scenario: Presets reappear after migration (device transfer / backup restore)
- **WHEN** the `bean/presets` QSettings key is present AND the `coffee_bags` table is non-empty
- **THEN** presets that do not match an existing bag (case-insensitive `roasterName` + `coffeeName` + `roastDate`) SHALL be imported as new bags; matching presets SHALL be skipped
- **AND** the merge outcome SHALL be logged and the QSettings keys cleared after commit
- **AND** the import SHALL never be skipped wholesale merely because bags already exist

### Requirement: shots table gains beanbase_id column for history search
Migration 19 SHALL add a nullable `beanbase_id` TEXT column to the `shots` table (the canonical UUID currently lives only inside the `beanbase_json` blob), backfilled via `json_extract(beanbase_json, '$.id')`, with an index on `beanbase_id` (an index on `(bean_brand, bean_type)` already exists as `idx_shots_bean`). New shot saves SHALL populate the column directly.

#### Scenario: Migration backfills and new saves populate the column
- **WHEN** migration 19 runs on a database whose shots carry a canonical id inside `beanbase_json`
- **THEN** a nullable `beanbase_id` column and its index SHALL be added, and each existing shot's `beanbase_id` SHALL be backfilled from `json_extract(beanbase_json, '$.id')` (NULL when absent)
- **AND** every subsequent shot save SHALL write `beanbase_id` directly

### Requirement: Bags survive backup restore and device-to-device transfer
The DB import path (`ShotHistoryStorage::importDatabaseStatic`) SHALL migrate `coffee_bags` rows and remap `shots.bag_id` to the new bag row ids (shot ids are remapped on import; bag ids must follow). The settings import path (`SettingsSerializer`) SHALL translate a legacy `beans.presets` JSON section into bag rows; `dye/activeBagId` SHALL be excluded from settings export/import.

#### Scenario: Restoring a backup with bags
- **WHEN** a backup containing a `coffee_bags` table is restored
- **THEN** all bags SHALL be imported with new row ids
- **AND** imported shots' `bag_id` values SHALL point at the corresponding imported bags

#### Scenario: Importing settings from an old-version device
- **WHEN** a settings export containing a legacy `beans.presets` section is imported on a new-version device
- **THEN** the presets SHALL be converted to bags via the same merge-import rules as migration (import non-duplicates, skip matches, log)

#### Scenario: Backfill on migration
- **WHEN** migration 19 runs on a database with existing shots carrying `beanbase_json`
- **THEN** each such shot's `beanbase_id` column SHALL contain the canonical UUID extracted from the blob
- **AND** shots without a blob SHALL have a null `beanbase_id`

### Requirement: Active bag selection
The system SHALL maintain a single global `activeBagId` in `SettingsDye`, replacing the `bean/selectedPreset` index. The active bag's fields SHALL drive the next shot's bean snapshot.

#### Scenario: Bag selection applies all fields
- **WHEN** the user selects a bag (from inventory or Change Beans dialog)
- **THEN** all bag fields SHALL become the active state for the next shot

#### Scenario: Bag selection applies dose and yield spec to the machine
- **WHEN** a bag with a stored `doseWeightG` and a yield spec whose mode is not `none` is selected, and no recipe is active
- **THEN** the dose SHALL drive the next shot's dose (`dyeBeanWeight`)
- **AND** switching the bean SHALL first reset the brew overrides to the active profile's defaults, then re-apply the bag's yield spec to the session anchor — so the next shot's target is the bean's own, and a bag without an anchor stays at the profile default
- **AND** the bag's yield spec is NOT routed through `dyeDrinkWeight` (which remains plain DYE drink-weight metadata)

#### Scenario: Recipe-driven bag selection does not overwrite the recipe's dose
- **WHEN** a bag with a stored `doseWeightG` is selected while a recipe supplying a dose is active
- **THEN** the next shot's dose SHALL remain the recipe's
- **AND** the bag's dose SHALL NOT be written to `dyeBeanWeight`

#### Scenario: A bag's own anchor is a baseline, not an override
- **WHEN** a bag holding `{42.0, absolute}` is active, no recipe is active, and the profile's `target_weight` is 36 g
- **THEN** every surface SHALL render 42 g as the BASELINE — un-highlighted, with no `36.0 → 42.0g` arrow on the Shot Plan — because the bean's yield is its design, not a deviation from the profile (the `yield-anchor` ladder resolves the baseline; a bag's anchor is button-protected and therefore always deliberate)
- **AND** only a per-brew deviation FROM 42 g SHALL highlight, arrowing against the bean's 42 g rather than the profile's 36 g
- **AND** pressing "Update Bag" on a deviation SHALL make the shown value the bean's stored spec, clearing the highlight on every surface

#### Scenario: Recipe-driven bag selection does not overwrite the recipe's anchor
- **WHEN** a recipe holding `{2.0, ratio}` is activated and activation selects the recipe's own linked bag, which holds `{40.0, absolute}`
- **THEN** the session anchor is `{2.0, ratio}`
- **AND** the bag's yield spec is not applied

#### Scenario: A manual bean switch still hands the brew to the bag
- **WHEN** a recipe is active and the user manually changes the active bean
- **THEN** the recipe deactivates (`recipe-activation`), so no recipe is active and the newly selected bag's yield spec applies normally

#### Scenario: New bag with no dose or yield spec yet
- **WHEN** a bag with a null/0 `doseWeightG` and a yield mode of `none` is selected
- **THEN** the current global dose SHALL remain in effect and the brew yield SHALL follow the profile default
- **AND** the bag SHALL adopt the dose on the first edit or shot save, and its yield spec only when the user presses "Update Bag" in brew settings

#### Scenario: A bag's dose is not known until its row arrives
- **WHEN** a bag is selected and a profile carrying a recommended dose is loaded before that bag's row has been read
- **THEN** the profile's dose SHALL NOT be applied
- **AND** the bag's stored `doseWeightG` SHALL be unchanged

The row load is asynchronous, so between the selection and its arrival the bag is indistinguishable
from one holding no dose. Reading it as empty would let the profile's dose through — and because
that write travels back to the bag, it would replace the dose the bean actually remembered
(`dose-source-precedence`).

#### Scenario: No active bag
- **WHEN** no bag is selected (`activeBagId` is null or references a deleted bag)
- **THEN** the bean summary SHALL display "No beans selected" and prompt the user to select a bag

### Requirement: Bag yield spec and dose follow the recipe ladder
Applying a bag's **yield spec** SHALL be gated on no recipe being active, and the `yield-anchor` ladder (recipe, then bag, then profile) SHALL be enforced explicitly, never left to the arrival order of signals. Applying a bag's dose SHALL be gated the same way, per `dose-source-precedence`.

#### Scenario: Bag selection does not replace a recipe's dose
- **WHEN** a bag is selected while a recipe is active
- **THEN** the recipe's dose and yield are kept

### Requirement: Bean/grinder edits write through to the active bag
Pre-shot edits to grinder fields (brew dialog, bag editing surfaces) SHALL write directly to the active bag, unconditionally, including while a recipe with its own owned grind (recipe-model) is active. No intermediate live-DYE copy of bean or grinder state SHALL exist, and there SHALL be no modified-state computation or save prompt.

#### Scenario: Grinder edit before a shot
- **WHEN** the user changes the grinder setting in the brew dialog while a bag is active
- **THEN** the active bag's `grinderSetting` SHALL be updated immediately (background DB write)
- **AND** no save prompt or modified indicator SHALL appear

#### Scenario: Grinder edit with an active recipe updates both the bag and the recipe
- **WHEN** the user changes the grinder setting while a recipe is active
- **THEN** the active bag's `grinderSetting` SHALL be updated immediately, exactly as when no recipe is active
- **AND** the active recipe's own `grindPinned` SHALL also be updated immediately
- **AND** neither write waits on or is gated by the other

#### Scenario: Grinder/dose correction on post-shot review
- **WHEN** the user corrects the grinder setting or dose on the post-shot review page (e.g. the recorded value was wrong)
- **THEN** both the just-saved shot's record AND the active bag SHALL be updated (preserving today's dual-write behaviour)

#### Scenario: Activating a recipe updates its linked bag's grind
- **WHEN** a recipe whose own grind is "17" is activated and its linked bag's stored grind was "18"
- **THEN** the live grind becomes "17" and the bag's stored `grinderSetting` updates to "17" — the bag mirrors the most recently dialed grind, and activating a recipe that selects this bag counts as dialing

#### Scenario: Bean-less recipe activation touches no bag
- **WHEN** a bean-less recipe with its own grind is activated
- **THEN** the active bag is cleared as part of activation (recipe-activation), so the recipe's grind — and any subsequent grind edits — write through to no bag at all (the write-through is a no-op with no active bag)

### Requirement: Grinder edits also write to the active recipe
When a recipe is active, a pre-shot grinder edit SHALL also write to that recipe's own `grindPinned` and `rpmPinned` (recipe-model). The bag and the recipe's grind SHALL each update immediately from the same edit, independently of each other.

#### Scenario: Grinder edit updates bag and recipe together
- **WHEN** a grinder field is edited while a recipe is active
- **THEN** both the active bag and the recipe's pinned grind update immediately

### Requirement: Dose stamped on shot save
The system SHALL update the active bag's `doseWeightG` to the shot's actual dose whenever a shot is saved (dose may originate from SAW/profile settings rather than a manual edit).

#### Scenario: Auto-stamp after dial-in adjustment
- **WHEN** a shot is saved with a different dose than the active bag stored
- **THEN** the active bag's `doseWeightG` SHALL be updated to the shot's value with no user prompt

### Requirement: The bag's yield spec is button-protected
The bag's dial memory SHALL split along the measurement/intent line of `yield-anchor`. `grinderSetting`, `rpm` and `doseWeightG` are dial-in and SHALL keep their unconditional write-through. The yield spec is design intent and SHALL reach the bag only via the explicit "Update Bag" action in Brew Settings (`recipe-aware-brew-settings`).

#### Scenario: Yield is not stamped on shot save
- **WHEN** a shot is saved at a target that differs from the active bag's stored yield spec
- **THEN** the active bag's yield spec SHALL be unchanged

#### Scenario: Yield is not stamped on Brew Settings OK
- **WHEN** the user dials a yield or ratio in Brew Settings and taps OK without pressing "Update Bag"
- **THEN** the value applies to the session anchor only
- **AND** the active bag's yield spec SHALL be unchanged

#### Scenario: Yield reaches the bag only via Update Bag
- **WHEN** no recipe is active, the user dials a ratio of 1:3 in Brew Settings and taps "Update Bag"
- **THEN** the active bag holds `{3.0, ratio}`

#### Scenario: A dose capture cannot drift the bag's stored pair
- **WHEN** a bag holds `{36.0, absolute}` with a `doseWeightG` of 18 and a dose capture reads 17.5 g
- **THEN** the bag's `doseWeightG` becomes 17.5 and its yield spec stays `{36.0, absolute}`
- **AND** no implicit ratio is derived from or written to the pair

#### Scenario: Grind and rpm write-through are untouched
- **WHEN** the user changes the grinder setting or RPM while a bag is active
- **THEN** the active bag's `grinderSetting`/`rpm` SHALL be updated immediately, exactly as before this change

### Requirement: No other action writes the bag's yield spec
No other action SHALL write the bag's yield spec: not a shot save, not Brew Settings OK, not a dose capture, and not a bag selection.

#### Scenario: Shot save leaves the yield spec alone
- **WHEN** a shot is saved
- **THEN** the bag's stored yield spec is unchanged

### Requirement: Shot snapshot includes bag lifecycle fields
The system SHALL snapshot `frozenDate`, `defrostDate`, `storageHint` and `openedDate` from the active bag into the shot record at save time, in the `shots` table's own `frozen_date`, `defrost_date`, `storage_hint` and `opened_date` columns.

#### Scenario: Frozen bean shot snapshot
- **WHEN** a shot is saved while the active bag has `frozenDate` and `defrostDate` set
- **THEN** the shot record SHALL include both dates in its snapshot

#### Scenario: Non-frozen bean shot snapshot
- **WHEN** a shot is saved while the active bag has `storageHint = "airtight"` and `openedDate` set, with no `frozenDate`/`defrostDate`
- **THEN** the shot record SHALL include `storageHint` and `openedDate` in its snapshot
- **AND** `frozenDate`/`defrostDate` SHALL remain absent from that shot's snapshot

### Requirement: Shot snapshots are stored on the shot, not referenced
The snapshot SHALL use the shot's own columns, the same pattern `frozen_date` and `defrost_date` already use (`shothistorystorage.cpp`), and SHALL NOT be a foreign-key-only reference to the bag row. A later bag edit therefore never changes what a saved shot recorded.

#### Scenario: Later bag edit leaves a saved shot unchanged
- **WHEN** a bag's freeze fields are edited after a shot was saved
- **THEN** the saved shot's snapshot is unchanged

### Requirement: canonical_roaster_id stored in beanBaseData blob
The system SHALL include `canonical_roaster_id` in the beanBaseData blob when populated via `parseCanonicalPayload`.

#### Scenario: Canonical fetch stores roaster id
- **WHEN** `fetchCanonicalDetails` resolves a roaster UUID for a bean
- **THEN** `beanBaseData` SHALL include a `canonicalRoasterId` key with that UUID

### Requirement: visualizer_sync_pending column
A schema migration SHALL add a `visualizer_sync_pending INTEGER NOT NULL DEFAULT 0` column to `coffee_bags`, set when an edit-time Visualizer push fails retryably and cleared on successful push (see `visualizer-coffee-management`). The column SHALL survive backup restore and device transfer like every other bag column.

#### Scenario: Migration adds the column
- **WHEN** the schema migration runs on an existing database
- **THEN** every existing bag SHALL have `visualizer_sync_pending = 0`

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
The same schema migration (or a paired one) SHALL add nullable `storage_hint` (TEXT) and `opened_date` (TEXT, ISO date) columns to the `shots` table, mirroring `frozen_date` and `defrost_date`.

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

### Requirement: Every freeze-column path carries the new columns
Every code path that reads or writes `frozen_date` or `defrost_date` on `shots` SHALL be extended to the two new columns: the shot-save `INSERT` and its bound parameters, the shot-read `SELECT` and its `ShotRecord` mapping, and the device-transfer and backup-restore `INSERT`. The last SHALL resolve source-column presence for older source databases that predate the columns.

#### Scenario: Restore from an older database
- **WHEN** a backup from a database predating the columns is restored
- **THEN** the new columns are resolved as absent and restored as empty

### Requirement: Bags carry a kind set at creation
The `coffee_bags` table SHALL gain a `kind` TEXT column (`"coffee"` default, `"tea"`), added by migration with kCols registration. The kind SHALL be set by the creation entry point and SHALL NOT be editable afterwards. The kind SHALL ride backup restore and device-to-device transfer, and pre-migration bags SHALL default to coffee.

#### Scenario: Existing bags stay coffee
- **WHEN** the migration runs on an existing database
- **THEN** every existing bag has kind "coffee" and no behavior changes

#### Scenario: Kind survives transfer
- **WHEN** a tea bag is imported via device transfer or backup restore
- **THEN** it arrives with kind "tea"

### Requirement: Bag surfaces read the kind
Bag surfaces (inventory cards, unified bean search, idle pills, MCP bag tools) SHALL be able to read the kind, and the recipe wizard's bean step SHALL filter by it.

#### Scenario: Wizard bean step filters by kind
- **WHEN** the recipe wizard's bean step is shown for a tea recipe
- **THEN** only tea bags are offered

### Requirement: Tea bags store structured brewing data in the blob
For tea bags, the `beanBaseData` blob vocabulary SHALL include `teaType` (black, green, oolong, white, herbal or pu-erh), `garden` (estate), `cultivar`, `flush`, `brewTempC` (number, Celsius), `leafGramsPer100Ml` (number) and `steepTime` (display string), alongside the shared descriptive keys. These schemaless keys need no migration. Absent keys mean the vendor did not state it, and consumers SHALL treat them as empty, never inferring values.

#### Scenario: Brewing data seeds without guessing
- **WHEN** a tea bag has no `brewTempC`
- **THEN** the recipe wizard uses its per-tea-type default temperature and does not invent a bag value

#### Scenario: Coffee bags unaffected
- **WHEN** a coffee bag's blob is read
- **THEN** the tea keys are simply absent and no coffee surface changes

### Requirement: shots table gains bean_repair_pending column

A schema migration SHALL add a `bean_repair_pending INTEGER NOT NULL DEFAULT 0` column to `shots`, set when a bag's borrowed canonical link is dropped and cleared once visualizer.coffee has confirmed that shot needs nothing further (see `visualizer-coffee-management`). The column SHALL survive backup restore and device transfer like every other shot column.

#### Scenario: Migration adds the column

- **WHEN** the schema migration runs on an existing database
- **THEN** every existing shot SHALL have `bean_repair_pending = 0`

#### Scenario: The column is the only durable record of the queue

- **WHEN** the app is closed mid-repair, or a repair pass is abandoned
- **THEN** the still-flagged shots SHALL remain flagged and be retried on a later launch

### Requirement: Stored bags carrying a borrowed canonical record are unlinked by migration
A schema migration SHALL apply the identity check to every stored bag and unlink those whose own roaster or coffee name names a different coffee than the record they point at. Detection SHALL be offline and conservative: it can only prove a conflict while the stored snapshot still carries the record's own names, and an empty name on either side SHALL NOT count as disagreement.

#### Scenario: A stored borrowed link is dropped

- **WHEN** the migration runs on a database holding a bag whose roaster names a different coffee than its canonical record
- **THEN** the bag's canonical id SHALL be cleared and its snapshot stripped of the link keys
- **AND** every descriptive field and the product URL SHALL be kept

#### Scenario: A correctly linked bag is untouched

- **WHEN** the migration runs on a database holding a bag whose names still match its record
- **THEN** the bag SHALL keep its canonical id

#### Scenario: The unlink reaches the shots' own snapshots

- **WHEN** a bag is unlinked by the migration
- **THEN** the shots of that bag SHALL have the link keys removed from their own stored snapshots

#### Scenario: Only uploaded shots are queued for repair

- **WHEN** a bag with both uploaded and never-uploaded shots is unlinked
- **THEN** only the uploaded shots SHALL be flagged `bean_repair_pending`
- **AND** the never-uploaded shots SHALL NOT be flagged, since the server cannot have renamed a shot it does not have

### Requirement: The unlink propagates to each bag's shots
The unlink SHALL propagate to the shots of every bag it fixes, because each shot carries its own snapshot and upload reads that copy. Uploaded shots of those bags SHALL additionally be flagged for repair.

#### Scenario: Uploaded shots are queued for repair
- **WHEN** an unlinked bag has shots that were already uploaded
- **THEN** those shots are flagged for repair

### Requirement: The unlink does not depend on the repair queue
The unlink SHALL be applied even if the repair-queue column could not be added, and only the cloud repair is lost in that case. Where the migration cannot determine whether that column exists, it SHALL defer to a later launch rather than unlink without queueing.

#### Scenario: Unknown column state defers the migration
- **WHEN** the migration cannot determine whether the repair-queue column exists
- **THEN** it defers to a later launch and does not unlink

