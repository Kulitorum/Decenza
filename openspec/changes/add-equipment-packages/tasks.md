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
- [ ] 2.8 `importDatabaseStatic`: migrate equipment tables + remap `coffee_bags.equipment_id` / `shots.equipment_id`

## 3. SettingsDye & dial memory
- [ ] 3.1 `dyeGrinderBrand/Model/Burrs` → read-only, resolved via active bag's `equipment_id` — **deferred** (transitional: still QSettings-backed + bag write-through; identity applied from the package on switch). Required before migration 23 (4.4).
- [x] 3.2 Add `activeEquipmentId` + `switchToEquipment` that applies the package's `lastGrindSetting`/`lastRpm` and points the active bag at it
- [x] 3.3 Add `dyeGrinderRpm`; grind setting + rpm edits fan out to BOTH active bag and active package (`writeThroughToActivePackage`)
- [x] 3.4 `rpmCapable` derivation helper (`EquipmentStorage::deriveRpmCapable`: registry → `variableRpm`; custom → true); re-derived on identity edit in `updateGrinderItemStatic`
- [x] 3.5 `SettingsSerializer`: `dye/activeEquipmentId` excluded from export (field-by-field export never emits it; comment updated)

## 4. Shot projection & history queries
- [ ] 4.1 `ShotProjection` resolves grinder brand/model/burrs via `equipment_id` JOIN; add `rpm`
- [ ] 4.2 Re-scope `getDistinctGrinderBrands/ModelsForBrand/BurrsForModel/SettingsForGrinder` to `equipment_items WHERE kind='grinder'`
- [ ] 4.3 Shot save captures `equipment_id` + `rpm` (drop grinder identity write)
- [ ] 4.4 **Migration 23**: drop `grinder_brand`/`grinder_model`/`grinder_burrs` from `coffee_bags` and `shots` (SQLite ≥3.35 `DROP COLUMN`) — only after 3.1/4.1/4.2 switch every reader to `equipment_id`; remove those columns from `CoffeeBagStorage::kCols`

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
- [ ] 7.1 `BrewDialog.qml`: replace grinder brand/model/burrs inputs with read-only Equipment summary + Switch Equipment button
- [ ] 7.2 Keep grind setting dial-in; add RPM dial-in shown only when `rpmCapable`
- [ ] 7.3 Empty-state ("Equipment: not set → Add Equipment")
- [ ] 7.4 Dial edits use dual write-through

## 8. Visualizer
- [ ] 8.1 Upload: resolve `equipment_id` → "brand model"; omit when null
- [ ] 8.2 Upload: append rpm to `grinder_setting` (`"{setting} {rpm}rpm"`) when rpm present
- [ ] 8.3 Confirm import path untouched (regression check)

## 9. MCP
- [ ] 9.1 Resolve grinder reads via package in `mcpresources.cpp`, `mcptools_dialing.cpp`, `mcptools_ai.cpp`; add `rpm` + `rpmAdjustable` + package identity
- [ ] 9.2 `equipment_list` tool (mirror `bag_list`)
- [ ] 9.3 `equipment_select` tool (mirror `bag_select`; applies last-dial)
- [ ] 9.4 `equipment_update` tool (mirror `bag_update`; create + edit, re-derive `rpmCapable`)

## 10. i18n, accessibility, tests
- [ ] 10.1 Translation keys for Equipment page, dialog, Brew Settings labels, idle button
- [ ] 10.2 Accessibility on all new interactive elements (roles, names, focus order) per `ACCESSIBILITY.md`
- [ ] 10.3 Unit tests: grind/rpm split heuristic, package dedup, `rpmCapable` derivation, dual write-through
- [ ] 10.4 Migration test: representative bag/shot corpus → expected packages + links + split values
- [ ] 10.5 Verify Android build for the DB-migration + layout-migration paths

## 11. Docs
- [ ] 11.1 Update `docs/CLAUDE_MD/RECIPE_PROFILES.md` / relevant domain doc for the equipment-package model
- [ ] 11.2 Update `docs/CLAUDE_MD/MCP_SERVER.md` with the new equipment tools
- [ ] 11.3 Note the wiki Manual page for Equipment (user-visible feature)
