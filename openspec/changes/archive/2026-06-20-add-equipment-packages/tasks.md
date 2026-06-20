# Tasks — Equipment Packages

## 1. Data model & storage
- [x] 1.1 Define `EquipmentPackage` + `EquipmentItem` value types (`src/history/equipmentstorage.h`)
- [x] 1.2 New `EquipmentStorage` class: CRUD via `withTempDb()`, background-thread pattern modeled on `CoffeeBagStorage` (`equipmentstorage.{h,cpp}`)
- [x] 1.3 Single source-of-truth column lists for `equipment_packages` and `equipment_items` (mirror `bagColumnList()` convention) — package uses the descriptor `kCols`; items use a fixed column string + JSON `attrs` blob
- [x] 1.4 `inInventory` soft-delete + inventory query (index on `(in_inventory, last_used DESC)`)
- [x] 1.5 Register `EquipmentStorage` with `MainController` and expose to QML (`MainController.equipmentStorage`; attached to `SettingsDye::setEquipmentStorage`; `qmlRegisterUncreatableType`)

## 2. Migration 22 (additive) + Migration 23 (drop columns)
- [x] 2.1 Create `equipment_packages` + `equipment_items` tables (`EquipmentStorage::ensureTablesStatic`, called by migration 22)
- [x] 2.2 Add `equipment_id` (nullable FK) + `rpm` (nullable int) to `coffee_bags` and `shots` — **migration 22**. (DROP of `grinder_brand`/`grinder_model`/`grinder_burrs` moved to **migration 23**, after all readers resolve via `equipment_id` — see new task 4.4. Dropping now would break CoffeeBagStorage/shot reads at runtime.)
- [x] 2.3 Grind/rpm split helper `EquipmentStorage::splitGrindAndRpm`: trailing `(\d+)\s*rpm$` → `rpm` + trimmed `grinder_setting`; non-marker settings verbatim. (Unit test pending — task 10.3.)
- [x] 2.4 Package creation: default package from current settings + one per distinct `(brand, model, burrs)` across bags+shots; `rpmCapable` seeded from `GrinderAliases` (`migrateFromGrinderColumnsStatic`)
- [x] 2.5 Link every bag/shot to its matching package; null `equipment_id` for empty grinder identity
- [x] 2.6 Seed package `lastGrindSetting`/`lastRpm` (default ← current settings; per-grinder ← grinder's most-recent shot)
- [x] 2.7 Bump schema version to 22 only after the transaction commits; failure path leaves DB usable (gated on tables+cols+data)
- [x] 2.8 `importDatabaseStatic`: migrate `equipment_packages` + `equipment_items` with id-remap; remap `coffee_bags.equipment_id`, `shots.equipment_id`, and `equipment_packages.superseded_by` on import (replaces the current stopgap that nulls the transferred `equipment_id`)

## 3. SettingsDye & dial memory
- [x] 3.1 `dyeGrinderBrand/Model/Burrs` are read-only, resolved from the active package (async via EquipmentStorage `packageReady`); setters are display-cache-only (no bag write-through); `applyActiveBag` drives identity through `setActiveEquipmentId(bag.equipmentId)`. (= 4b.5)
- [x] 3.2 Add `activeEquipmentId` + `switchToEquipment` that applies the package's `lastGrindSetting`/`lastRpm` and points the active bag at it
- [x] 3.3 Add `dyeGrinderRpm`; grind setting + rpm edits fan out to BOTH active bag and active package (`writeThroughToActivePackage`)
- [x] 3.4 `rpmCapable` derivation helper (`EquipmentStorage::deriveRpmCapable`: registry → `variableRpm`; custom → true); re-derived on identity edit in `updateGrinderItemStatic`
- [x] 3.5 `SettingsSerializer`: `dye/activeEquipmentId` excluded from export (field-by-field export never emits it; comment updated)

## 4. Shot projection & history queries
- [x] 4.1 `ShotProjection` resolves grinder brand/model/burrs via `equipment_id` JOIN; add `rpm`
- [x] 4.2 Re-scope `getDistinctGrinderBrands/ModelsForBrand/BurrsForModel/SettingsForGrinder` to `equipment_items WHERE kind='grinder'`
- [x] 4.3 Shot save captures `equipment_id` + `rpm` (ShotMetadata→ShotSaveData→INSERT; ShotRecord/ShotProjection read them back). Still writes the grinder identity snapshot columns until migration 23.
- [x] 4.4 **Migration 23**: drop `grinder_brand`/`grinder_model`/`grinder_burrs` from `coffee_bags` AND `shots` (SQLite ≥3.35 `DROP COLUMN`); remove from `CoffeeBagStorage::kCols`; rework `shots_fts` to drop grinder columns — only after all readers (4.1/4.2/4b) resolve via `equipment_id`

## 4b. Copy-on-write immutability + pointer-only normalization (REVISED model — see design §2b)
- [x] 4b.1 Store `name` at package creation (persistent; defaults to "{brand} {model}")
- [x] 4b.2 `superseded_by` lineage column on `equipment_packages` (struct + `kCols` + `ensureTablesStatic`)
- [x] 4b.3 Copy-on-write (`supersedeOrEditGrinderStatic`): identity edit of a used package forks a new one, repoints referencing bags, marks old `in_inventory=0` + `superseded_by`; unused edits in place; `requestUpdatePackage` + MCP `equipment_update` use it and surface the result id. (Switch-dialog active-repoint folds into 4b.5.)
- [x] 4b.4 Merge on collision: `supersedeOrEditGrinderStatic` repoints to an existing in-inventory package with the same grinder identity instead of duplicating (`findPackageByGrinderIdentityStatic` gained an `excludeId`). Unit-tested in `tst_equipment::copyOnWriteAndMerge`.
- [x] 4b.5 `SettingsDye` `dyeGrinderBrand/Model/Burrs` resolve read-only via the active package (async cache, refreshed on bag/package change) — replaces the QSettings cache (this is 3.1 under the revised model)
- [x] 4b.6 Grinder search via pointer: shot-history search resolves the term against `equipment_items` for **all** history-referenced packages (inventory or not) → `equipment_id IN (…)`; drop grinder from `shots_fts`; no grinder shadow on shots
- [x] 4b.7 Display: render current-vs-superseded from `in_inventory` + `superseded_by` lineage (e.g. "older"/"retired" in shot history); never bake into `name`

## 5. Equipment window & idle button
- [x] 5.1 `EquipmentPage.qml` (mirror `BeanInfoPage.qml`): empty state + Add Equipment + package cards
- [x] 5.2 `EquipmentCard.qml`: grinder identity, burrs subtitle, last-dial + RPM-adjustable captions, edit + remove actions
- [x] 5.3 `EquipmentItem.qml` idle widget + 4-place registration (`CMakeLists.txt`, `LayoutItemDelegate.qml` switch+compile, `CustomItem.qml` navigate map, `shotserver_layout.cpp` list+labels)
- [x] 5.4 Add `equipment` to `defaultLayoutJson()` next to `beans`
- [x] 5.5 Idempotent run-once layout-injection migration in `getLayoutObject()` (insert after `beans`, fallback append to `bottomRight`)
- [x] 5.6 Register `EquipmentPage` page-title map; nav from idle button + CustomItem `navigate:equipment` (pushed by URL)

## 6. Switch Equipment dialog
- [x] 6.1 `SwitchEquipmentDialog.qml`: create + edit; tapping a card / creating a package switches the active bag via `Settings.dye.switchToEquipment`
- [x] 6.2 Registry-backed grinder suggestions (brand/model/burrs) + shot-history distincts
- [x] 6.3 Create flow derives `rpmCapable` (in storage); select flow applies the package's last dial
- [x] 6.4 Edit flow (reference semantics, re-derive `rpmCapable`)

## 7. Brew Settings rework
- [x] 7.1 `BrewDialog.qml`: grinder brand/model/burrs inputs replaced by read-only Equipment summary + burrs line + Switch Equipment button (opens `SwitchEquipmentDialog` picker)
- [x] 7.2 Grind setting dial-in kept; RPM dial-in added, shown only when `Settings.dye.grinderRpmCapable(...)`
- [x] 7.3 Empty-state ("Equipment: Not set" + Add button)
- [x] 7.4 Dial edits use dual write-through (`setDyeGrinderSetting`/`setDyeGrinderRpm`)

## 8. Visualizer
- [x] 8.1 Upload resolves grinder identity through the projection JOIN (4.1) — the snapshot columns are gone (migration 23); `shotData.grinderBrand/Model/Burrs` are populated from the package's grinder item
- [x] 8.2 Upload appends rpm to `grinder_setting` (`"{setting} {rpm}rpm"`) at all payload sites via `grinderSettingWithRpm`
- [x] 8.3 Import path untouched (profile-only; no equipment linkage) — confirmed earlier

## 9. MCP
- [x] 9.1 `de1://dialing` (mcpresources) grinder block gains `rpm` + `rpmAdjustable` + `packageId`. (Shot-derived dialing/ai blocks still read the shot's grinder columns, which remain until migration 23 — no change needed there yet.)
- [x] 9.2 `equipment_list` tool (mirror `bag_list`; `isActive`, `rpmAdjustable`, last dial)
- [x] 9.3 `equipment_select` tool (mirror `bag_select`; applies the package's grinder identity + last dial)
- [x] 9.4 `equipment_update` tool (edit grinder identity/name; re-derives `rpmCapable`)

## 10. i18n, accessibility, tests
- [x] 10.1 All new UI strings use `TranslationManager.translate(key, fallback)` / `Tr` with `equipment.*`, `idle.button.equipment`, `brewDialog.equipment*`/`grindLabel`/`rpm*` keys. (English is the fallback; there is no checked-in base JSON — other languages register at runtime.)
- [x] 10.2 Accessibility on new interactive elements: `AccessibleButton`/`AccessibleMouseArea`/`AccessibleTapHandler`, `Accessible.role/name`, headings on page/dialog titles
- [x] 10.3 Unit tests: `tst_equipment` (split heuristic + edges, dedup, `rpmCapable`, CRUD, copy-on-write/merge sub-branches, name derivation); `tst_coffeebags::settingsDyeEquipmentSwitchAndDualWrite` (switch applies identity+dial, dual write-through to bag + package); `tst_dbmigration` (migration-22 idempotency + shot equipment_id/rpm round-trip).
- [x] 10.4 Migration test (`tst_equipment::migrationSplitsAndLinks`): bag/shot corpus → expected packages + links + split values
- [x] 10.5 Verify Android build — done (GitHub Android CI build). This change has no `#ifdef Q_OS_ANDROID` branches; the DB + layout migrations are platform-agnostic, so the macOS build/tests are representative.

## 11. Docs
- [~] 11.1 Equipment-package model documented in the OpenSpec proposal/design (source of truth); no separate `RECIPE_PROFILES.md` section added
- [x] 11.2 `docs/CLAUDE_MD/MCP_SERVER.md` updated with the new equipment tools + access levels + dialing fields
- [x] 11.3 Wiki Manual page for Equipment — done (Manual: Bean Bags + Equipment Packages sections + screenshots)
