# Spec Delta

## MODIFIED Requirements

### Requirement: coffee_bags database table
The system SHALL store bags in a `coffee_bags` SQLite table created by migration 19 in `src/history/shothistorystorage.cpp` (current schema version is 18). The table SHALL include all CoffeeBag fields. All lifecycle fields SHALL be nullable. DB access SHALL follow the `withTempDb()` background-thread pattern.

#### Scenario: Migration from presets
- **WHEN** the app launches after upgrade and the `bean/presets` QSettings key is present
- **THEN** each preset SHALL be converted to a bag row with `inInventory = true` and lifecycle fields null, mapping `brand` → `roasterName`, `type` → `coffeeName`, and `roastDate`/`roastLevel`/`beanBaseId`/`beanBaseData` directly; legacy grind and equipment are not stored on bags
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

## ADDED Requirements

### Requirement: CoffeeBag last-used dose field
The bag SHALL carry `doseWeightG`, nullable, recording the last-used dose; equipment, grind and RPM are not bag fields.

#### Scenario: Last-used fields are optional
- **WHEN** a bag is created without dose details
- **THEN** those fields are stored as empty

### Requirement: Selecting an active bag
The system SHALL maintain a single global `activeBagId` in `SettingsDye`, replacing the `bean/selectedPreset` index. The active bag's fields SHALL drive the next shot's bean snapshot.

#### Scenario: Bag selection applies bean fields and keeps equipment and grind
- **WHEN** the user selects a bag
- **THEN** the bag supplies its bean fields and the current equipment, grind and RPM remain unchanged

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

### Requirement: Bean edits write through without storing equipment or grind
Bean edits SHALL update the active bag. Grind, RPM and equipment changes SHALL NOT write to a bag. Shot-review corrections SHALL follow `shot-review-carry-forward`.

#### Scenario: Grinder edit before a shot
- **WHEN** the user changes grind while a bag is active
- **THEN** the live grind changes without writing to the bag

#### Scenario: Grinder edit updates the active recipe without writing to the bag
- **WHEN** the user changes grind while a recipe is active
- **THEN** the recipe is updated and the bag is not

#### Scenario: Grinder/dose correction on post-shot review
- **WHEN** the user corrects grind or dose on shot review
- **THEN** the shot is updated and eligible carry-forward requires explicit acceptance

#### Scenario: Activating a recipe applies grind without writing to the bag
- **WHEN** a recipe with grind 17 is activated
- **THEN** the live grind becomes 17 without writing grind to its bag

#### Scenario: Bean-less recipe activation touches no bag
- **WHEN** a bean-less recipe is activated
- **THEN** the active bag is cleared and its grind applies without a bag write

### Requirement: Grinder edits update the active recipe
When a recipe is active, a pre-shot grinder edit SHALL also write to that recipe's own `grindPinned` and `rpmPinned` (recipe-model). No grind or RPM SHALL be written to the bag.

#### Scenario: Grinder edit updates the recipe without writing to the bag
- **WHEN** a grinder field is edited while a recipe is active
- **THEN** the recipe's pinned grind updates immediately and the bag is unchanged

### Requirement: The bag yield spec is protected by explicit update
The bag's `doseWeightG` SHALL keep its existing write-through; grind and RPM are not bag fields. The yield spec is design intent and SHALL reach the bag only via the explicit "Update Bag" action in Brew Settings (`recipe-aware-brew-settings`).

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

#### Scenario: Grind and RPM do not write to the bag
- **WHEN** the user changes grind or RPM while a bag is active
- **THEN** no grind or RPM is written to the bag

### Requirement: Bag equipment and grind are removed on upgrade
The system SHALL remove equipment, grind and RPM from existing bag records in one database migration, preserving other bag data, all existing shot records and the active setup. The migration SHALL NOT write settings or create equipment.

#### Scenario: Upgrade existing bags
- **WHEN** a user upgrades with bags and shots containing grind and equipment
- **THEN** the bag fields are removed and the shot records and other bag data remain unchanged

#### Scenario: Active setup unchanged
- **WHEN** a user upgrades with any active recipe, equipment, bag, live grind and RPM
- **THEN** those selections and the live grind and RPM are unchanged and no equipment package is created

#### Scenario: Upgrade fails
- **WHEN** any step of the removal fails
- **THEN** the whole migration rolls back, the bag fields and their data remain, the app starts normally, and the next launch retries

### Requirement: Notice for users relying on multiple bag grinds
The upgrade SHALL show a one-time startup notice only when the pre-upgrade state has no active recipe or equipment, no in-inventory equipment package, and at least two in-inventory coffee bags with saved grinds and no non-archived linked recipe, including the active bag.

#### Scenario: Affected multi-bag user
- **WHEN** all eligibility conditions hold in the database being upgraded
- **THEN** a pending notice explains that bag grind recall has been removed and describes equipment and recipes

#### Scenario: Equipment exists
- **WHEN** an equipment package exists in inventory at upgrade
- **THEN** no notice is shown

#### Scenario: Bags used by recipes
- **WHEN** fewer than two qualifying bags lack linked recipes
- **THEN** no notice is shown

#### Scenario: Dismissal and restart
- **WHEN** an eligible notice is interrupted before acknowledgment
- **THEN** it remains pending until acknowledged and is not repeated once an acknowledgment is recorded

#### Scenario: Fresh install
- **WHEN** the app creates a new database
- **THEN** no notice is pending, whatever settings survived from an earlier install

## REMOVED Requirements

### Requirement: CoffeeBag last-used grinder and dose fields
**Reason**: Superseded by the updated requirement with clear scenario names.
**Migration**: Follow CoffeeBag last-used dose field.

### Requirement: Active bag selection
**Reason**: Superseded by the updated requirement with clear scenario names.
**Migration**: Follow Selecting an active bag.

### Requirement: Bean/grinder edits write through to the active bag
**Reason**: Superseded by the updated requirement with clear scenario names.
**Migration**: Follow Bean edits write through without storing equipment or grind.

### Requirement: Grinder edits also write to the active recipe
**Reason**: Superseded by the updated requirement with clear scenario names.
**Migration**: Follow Grinder edits update the active recipe.

### Requirement: The bag's yield spec is button-protected
**Reason**: Superseded by the updated requirement with clear scenario names.
**Migration**: Follow The bag yield spec is protected by explicit update.
