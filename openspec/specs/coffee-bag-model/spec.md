# coffee-bag-model Specification

## Purpose
The single source of truth for the `CoffeeBag` data model and its `coffee_bags` database table: identity, freeze/notes lifecycle fields, last-used grinder/dose, the bean's own yield spec, and Visualizer sync bookkeeping, plus how bags survive backup restore and device-to-device transfer, how the active bag is selected and written through from bean/grinder edits, and how bag state is stamped onto each shot snapshot.
## Requirements
### Requirement: CoffeeBag data model
The system SHALL define a `CoffeeBag` value type with the following fields:
- Identity: `id` (int, DB primary key), `roasterName`, `coffeeName`, `roastDate`, `roastLevel`, `beanBaseId` (canonical UUID, nullable), `beanBaseData` (JSON blob, nullable)
- Lifecycle: `frozenDate` (nullable), `defrostDate` (nullable), `storageHint` (nullable string enum: `counter` / `airtight` / `vacuum-sealed` / `fridge` — describes non-frozen storage only; frozen state is determined solely by `frozenDate` being set, never by `storageHint`, so the two cannot disagree), `openedDate` (nullable date — the non-frozen analogue of `defrostDate`: when the current portion started being actively used/exposed to room temperature), `notes` (nullable), `startWeightG` (double, nullable — column retained but UNSURFACED: the UI field was removed as low-value and Visualizer has no equivalent), `inInventory` (bool, default true)
- Last-used grinder/dose: `grinderBrand`, `grinderModel`, `grinderBurrs`, `grinderSetting`, `doseWeightG` (all nullable)
- Yield spec: `yieldValue` (double) + `yieldMode` (`none` | `absolute` | `ratio`) — the bean's **own** yield, a first-class anchor rather than a deviation from the active profile's target weight (`yield-anchor`). `mode = none` means the bag designs no yield and the ladder falls through to the profile. The legacy `yieldOverrideG` column is converted by migration and left dead in place.
- Visualizer sync: `visualizerBagId` (nullable UUID string), `visualizerRoasterId` (nullable UUID string), `visualizerSyncPending` (bool, default false — a bag edit failed to push and awaits retry)

The yield spec SHALL be a local-only field, same as the grinder/dose fields — it SHALL NOT be included in `touchesVisualizerFields()`, so an anchor edit never triggers a bag PATCH. `storageHint` and `openedDate` are local-only for the same reason (`bean-freshness-followup`): neither is included in `touchesVisualizerFields()` and neither is pushed to a Visualizer bag.

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

#### Scenario: A bag's yield spec is never synced to Visualizer
- **WHEN** a bag's yield anchor is changed
- **THEN** `touchesVisualizerFields()` SHALL return `false` for a fields map containing only the yield spec keys, and no network PATCH SHALL be issued

#### Scenario: A bag cannot hold both an absolute yield and a ratio
- **WHEN** a bag holding `{40.0, absolute}` is given a ratio of 1:3 from any surface (Change Beans dialog, Brew Settings, MCP `bag_update`, web bag editor)
- **THEN** it holds `{3.0, ratio}` and retains no absolute yield

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
The system SHALL maintain a single global `activeBagId` in `SettingsDye` (replacing the `bean/selectedPreset` index). The active bag's fields drive the next shot's bean snapshot.

Applying a bag's **yield spec** SHALL be gated on **no recipe being active**: the resolution ladder of `yield-anchor` (recipe → bag → profile) SHALL be enforced explicitly, never left to emerge from the order in which the bag-selection and recipe-activation signals happen to arrive. The bag's dose continues to apply unconditionally, as today.

#### Scenario: Bag selection applies all fields
- **WHEN** the user selects a bag (from inventory or Change Beans dialog)
- **THEN** all bag fields SHALL become the active state for the next shot

#### Scenario: Bag selection applies dose and yield spec to the machine
- **WHEN** a bag with a stored `doseWeightG` and a yield spec whose mode is not `none` is selected, and no recipe is active
- **THEN** the dose SHALL drive the next shot's dose (`dyeBeanWeight`)
- **AND** switching the bean SHALL first reset the brew overrides to the active profile's defaults, then re-apply the bag's yield spec to the session anchor — so the next shot's target is the bean's own, and a bag without an anchor stays at the profile default
- **AND** the bag's yield spec is NOT routed through `dyeDrinkWeight` (which remains plain DYE drink-weight metadata)

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

#### Scenario: No active bag
- **WHEN** no bag is selected (`activeBagId` is null or references a deleted bag)
- **THEN** the bean summary SHALL display "No beans selected" and prompt the user to select a bag

### Requirement: Bean/grinder edits write through to the active bag
Pre-shot edits to grinder fields (brew dialog, bag editing surfaces) SHALL write directly to the active bag, unconditionally — including while a recipe with its own owned grind (recipe-model) is active. There SHALL be no intermediate live-DYE copy of bean/grinder state that can diverge from the bag, and no modified-state computation or save prompt. When a recipe is active, the same edit SHALL also write to that recipe's own `grindPinned`/`rpmPinned` (recipe-model) — the bag and the active recipe's own grind both update immediately from the same edit, independently of each other; neither is ever deliberately withheld from the other.

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

### Requirement: Dose stamped on shot save
The system SHALL update the active bag's `doseWeightG` to the shot's actual dose whenever a shot is saved (dose may originate from SAW/profile settings rather than a manual edit).

#### Scenario: Auto-stamp after dial-in adjustment
- **WHEN** a shot is saved with a different dose than the active bag stored
- **THEN** the active bag's `doseWeightG` SHALL be updated to the shot's value with no user prompt

### Requirement: The bag's yield spec is button-protected
The bag's dial memory SHALL split along the measurement/intent line of `yield-anchor`. `grinderSetting`, `rpm`, and `doseWeightG` are dial-in — things the user physically did — and SHALL keep their existing unconditional write-through. The yield spec is design intent and SHALL reach the bag **only** via the explicit "Update Bag" action in Brew Settings (`recipe-aware-brew-settings`).

No other action SHALL write the bag's yield spec: not a shot save, not Brew Settings OK, not a dose capture, and not a bag selection.

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

### Requirement: Shot snapshot includes bag lifecycle fields
The system SHALL snapshot `frozenDate` and `defrostDate` from the active bag into the shot record at save time.

#### Scenario: Frozen bean shot snapshot
- **WHEN** a shot is saved while the active bag has `frozenDate` and `defrostDate` set
- **THEN** the shot record SHALL include both dates in its snapshot

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

### Requirement: Bags carry a kind set at creation
The `coffee_bags` table SHALL gain a `kind` TEXT column (`"coffee"` default, `"tea"`), added by migration with kCols registration. The kind SHALL be set by the creation entry point and SHALL NOT be editable afterwards (no editor toggle; a mis-created zero-shot bag is deleted and recreated). The kind SHALL ride backup restore and device-to-device transfer, and pre-migration bags SHALL default to coffee. Bag surfaces (inventory cards, unified bean search, idle pills, MCP bag tools) SHALL be able to read the kind; the recipe wizard's bean step filters by it.

#### Scenario: Existing bags stay coffee
- **WHEN** the migration runs on an existing database
- **THEN** every existing bag has kind "coffee" and no behavior changes

#### Scenario: Kind survives transfer
- **WHEN** a tea bag is imported via device transfer or backup restore
- **THEN** it arrives with kind "tea"

### Requirement: Tea bags store structured brewing data in the blob
For tea bags, the `beanBaseData` blob vocabulary SHALL include: `teaType` (black/green/oolong/white/herbal/pu-erh), `garden` (estate), `cultivar`, `flush`, `brewTempC` (number, Celsius), `leafGramsPer100Ml` (number), and `steepTime` (display string), alongside the shared descriptive keys (origin, region, tastingNotes). These are schemaless blob keys — no migration. Absent keys mean "vendor did not state it"; consumers SHALL treat them as empty, never inferring values.

#### Scenario: Brewing data seeds without guessing
- **WHEN** a tea bag has no `brewTempC`
- **THEN** the recipe wizard uses its per-tea-type default temperature and does not invent a bag value

#### Scenario: Coffee bags unaffected
- **WHEN** a coffee bag's blob is read
- **THEN** the tea keys are simply absent and no coffee surface changes

