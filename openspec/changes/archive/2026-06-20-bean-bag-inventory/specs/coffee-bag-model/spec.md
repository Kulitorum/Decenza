## ADDED Requirements

### Requirement: CoffeeBag data model
The system SHALL define a `CoffeeBag` value type with the following fields:
- Identity: `id` (int, DB primary key), `roasterName`, `coffeeName`, `roastDate`, `roastLevel`, `beanBaseId` (canonical UUID, nullable), `beanBaseData` (JSON blob, nullable)
- Lifecycle: `frozenDate` (nullable), `defrostDate` (nullable), `notes` (nullable), `startWeightG` (double, nullable — column retained but UNSURFACED: the UI field was removed as low-value and Visualizer has no equivalent), `inInventory` (bool, default true)
- Last-used grinder/dose: `grinderBrand`, `grinderModel`, `grinderBurrs`, `grinderSetting`, `doseWeightG` (all nullable)
- Yield override: `yieldOverrideG` (double, 0 = none) — the bean's override of the active profile's target weight, NOT a standalone target. 0 means the bag follows the profile default.
- Visualizer sync: `visualizerBagId` (nullable UUID string), `visualizerRoasterId` (nullable UUID string)

#### Scenario: Bag creation with full canonical data
- **WHEN** a user creates a bag from a Bean Base canonical result
- **THEN** the bag SHALL store `beanBaseId`, `beanBaseData` (origin, variety, process, harvest, tasting, producer, elevation, canonical_roaster_id), `roasterName`, `coffeeName`, and the user-entered `roastDate`

#### Scenario: Bag creation without canonical data
- **WHEN** a user creates a bag via manual entry
- **THEN** the bag SHALL store only user-entered fields; `beanBaseId` and `beanBaseData` SHALL be null

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

#### Scenario: Bag selection applies all fields
- **WHEN** the user selects a bag (from inventory or Change Beans dialog)
- **THEN** all bag fields SHALL become the active state for the next shot

#### Scenario: Bag selection applies dose and yield override to the machine
- **WHEN** a bag with stored `doseWeightG`/`yieldOverrideG` is selected
- **THEN** the dose SHALL drive the next shot's dose (`dyeBeanWeight`)
- **AND** switching the bean SHALL first reset the brew overrides to the active profile's defaults, then re-apply the bag's `yieldOverrideG` (> 0) to `Settings.brew`'s `brewYieldOverride` — so a bag with an override turns the idle brew-settings widget yellow and a bag without one stays at the profile default
- **AND** `yieldOverrideG` is NOT routed through `dyeDrinkWeight` (which remains plain DYE drink-weight metadata)

#### Scenario: New bag with no dose or override yet
- **WHEN** a bag with null/0 `doseWeightG`/`yieldOverrideG` is selected
- **THEN** the current global dose SHALL remain in effect and the brew yield SHALL follow the profile default
- **AND** the bag SHALL adopt the dose on the first edit or shot save, and the yield override when the user commits one in brew settings

#### Scenario: No active bag
- **WHEN** no bag is selected (`activeBagId` is null or references a deleted bag)
- **THEN** the bean summary SHALL display "No beans selected" and prompt the user to select a bag

### Requirement: Bean/grinder edits write through to the active bag
Pre-shot edits to grinder fields (brew dialog, bag editing surfaces) SHALL write directly to the active bag. There SHALL be no intermediate live-DYE copy of bean/grinder state that can diverge from the bag, and no modified-state computation or save prompt.

#### Scenario: Grinder edit before a shot
- **WHEN** the user changes the grinder setting in the brew dialog while a bag is active
- **THEN** the active bag's `grinderSetting` SHALL be updated immediately (background DB write)
- **AND** no save prompt or modified indicator SHALL appear

#### Scenario: Grinder/dose correction on post-shot review
- **WHEN** the user corrects the grinder setting or dose on the post-shot review page (e.g. the recorded value was wrong)
- **THEN** both the just-saved shot's record AND the active bag SHALL be updated (preserving today's dual-write behaviour)

### Requirement: Dose/yield-override stamped on shot save
The system SHALL update the active bag's `doseWeightG` to the shot's actual dose whenever a shot is saved (dose may originate from SAW/profile settings rather than a manual edit). The active bag's `yieldOverrideG` SHALL be set to the shot's target weight only when that target differs from the active profile's default weight; a shot pulled at the profile default SHALL store 0 (no override) so the bag is not pinned to the profile's own number.

#### Scenario: Auto-stamp after dial-in adjustment
- **WHEN** a shot is saved with a different dose than the active bag stored
- **THEN** the active bag's `doseWeightG` SHALL be updated to the shot's value with no user prompt

#### Scenario: Yield override committed in brew settings
- **WHEN** the user commits a yield in brew settings that differs from the profile's target weight
- **THEN** the active bag's `yieldOverrideG` SHALL be set to that yield (single commit point: `ProfileManager::activateBrewWithOverrides`)
- **AND WHEN** the committed yield equals the profile default (e.g. after Clear), the active bag's `yieldOverrideG` SHALL be reset to 0

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
