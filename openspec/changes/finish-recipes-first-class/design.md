## Context

Recipes shipped in `add-recipes` as a structural clone of the coffee-bag subsystem: a `recipes` table in the shot-history DB (`RecipeStorage`, `kCols` single-source column table), single-path activation in `MainController::applyActivatedRecipe`, and MCP/web/QML surfaces. Two pieces were deferred.

**Transfer.** Device-to-device migration and `.dcbackup` restore both flow through `ShotHistoryStorage::importDatabaseStatic`, which ships the entire `shots.db` file and then row-remaps it during merge. Equipment imports first (`EquipmentStorage::importEquipmentStatic`, builds a package id-map), then bags (`CoffeeBagStorage::importBagsStatic`, remaps `equipment_id`, builds a bag id-map), then shots insert while remapping `equipment_id` and `bag_id`. There is **no** `RecipeStorage::importRecipesStatic` and no recipe handling in the shot-insert loop, so the `recipes` table that physically rides inside `shots.db` is never merged and every migrated `shots.recipe_id` dangles. This affects both the LAN `DataMigrationClient` path (which pulls `/api/backup/shots` and calls `importDatabaseStatic`) and full-archive restore (which merges `shots.db` the same way).

**Hot water.** The steam block (`steam_json`, a compact JSON column snapshotting a pitcher by value) is the proven pattern for attaching a "sub-drink" to a recipe. Hot water is the direct analog: the machine already dispenses it (`DE1Device::startHotWater`, ShotSettings BLE write for temp/vol/length, `HOT_WATER_FLOW_RATE` MMR), settings already exist (`SettingsBrew::effectiveHotWaterVolume`, water temperature, flow), and water vessels are already presets on `SettingsBrew` (`water/vesselPresets`) with the same add/update/remove/select API as steam pitchers. Nothing snapshots a vessel into a recipe today.

**Audit findings.** While tracing the transfer path we found two more silent drops worth fixing in the same pass: SAW learning history (`saw/learningHistory`) is serialized by no backup path, and `extra_settings.json` (shotMap location, accessibility, language) is bundled in the full archive but never fetched by the LAN client.

## Goals / Non-Goals

**Goals:**
- Recipes merge on both migration and restore, with `equipment_id` remapped and `shots.recipe_id` provenance preserved.
- A recipe can describe added hot water (Americano) by snapshotting a water vessel by value, applied on activation through the existing hot-water settings path.
- Hot water round-trips through promote-from-shot, MCP, web, and the composer, exactly as steam does.
- Close the adjacent transfer gaps (SAW learning, LAN extra-settings) and make the migration UI honest about recipes/bags/equipment.

**Non-Goals:**
- No changes to how the machine physically dispenses hot water or to hot-water flow-rate hardware settings.
- No automatic espresso→hot-water sequencing / "auto-Americano" macro; the recipe stores intent (including the before/after `order`) and configures the machine, but it does not orchestrate the two-stage pour — the `order` is guidance the user follows, not a scripted sequence.
- No change to the intentional exclusion of machine-specific flow calibration on import.
- No promotion of the recipes/data-migration specs to `openspec/specs/` (that happens when `add-recipes` archives).

## Decisions

### 1. Recipe import mirrors `importBagsStatic`, ordered after bags
Add `RecipeStorage::importRecipesStatic(srcDb, destDb, merge, outRecipeIdMap, packageIdMap)` and call it from `importDatabaseStatic` right after `importBagsStatic`, before the shot-insert loop. It remaps each recipe's `equipment_id` through `packageIdMap` and records an old→new `recipeIdMap`. The shot-insert loop then remaps `shots.recipe_id` through `recipeIdMap` exactly where it already remaps `bag_id`.

*Why:* Recipes are a structural clone of bags; reusing the established id-map handshake keeps ordering correctness (equipment → bags → recipes → shots) and avoids inventing a second merge mechanism. *Alternative rejected:* a separate `/api/backup/recipes` endpoint + client import case — unnecessary, since recipes already travel in `shots.db`; a separate endpoint would double the transport and reintroduce the ordering problem on the client side.

### 2. Recipe dedup on identity, bean link resolved leniently
Merge dedups recipes on a stable identity (name + profile_title + bean identity), mirroring the bag dedup on roaster+coffee+roastDate. Bean links (`beanbase_id` or roaster+coffee) are carried as stored; if the destination has no matching open bag that is a display state at activation, never an import error.

*Why:* Matches the recipe model's "bean link is bean-level, resolve at activation" rule, so import stays dumb and activation stays smart. *Alternative rejected:* remapping bean links to destination bag ids at import time — recipes link at bean level, not bag level, so there is nothing to remap.

### 3. Hot water is a new `hot_water_json` column, not reuse of `steam_json`
Add a `hot_water_json` TEXT column (one `COL_STR` row in `kCols`, one line in `ensureTableStatic`'s CREATE TABLE, one new schema migration mirroring the migration that added `rpm_pinned`). Shape: `{hasWater, vesselName, mode ("weight"|"volume"), waterWeightG or volumeMl, temperatureC, flow, order ("before"|"after")}` — a by-value water-vessel snapshot plus a pour-order flag (`before` = water first = long black; `after` = water last = Americano, the default). **Hot water is opt-in and vessel-carried:** `hasWater` toggles it on and the user selects a water vessel; the vessel's own values (amount/temperature/flow/mode) ride the snapshot. There is no separate per-recipe amount field — this differs from the steam block, which carries a `milkWeightG` distinct from the pitcher. The vessel is the single source of the values, exactly as a user selecting a vessel on the brew screen gets that vessel's settings.

*Why:* Steam and hot water are independent sub-drinks (a milk Americano is legal); overloading one column would couple them. A dedicated column keeps the `kCols` pattern and lets promote-from-shot snapshot each independently. *Alternative rejected:* a single `additions_json` holding both — worse migration story and breaks the one-column-per-concern `kCols` convention.

### 4. Activation applies hot water via `applyHotWaterSettings()`, no heater hold
In `applyActivatedRecipe`, after the steam block, parse the hot-water block, re-select/recreate the vessel snapshot into live settings, and call `applyHotWaterSettings()`. Unlike steam (5–9 min heater pre-warm → `startSteamHeating` hold), hot water needs no pre-heat, so there is no `sendMachineSettings` hold to add. Add `currentHotWaterSpecJson()` (mirroring `currentSteamSpecJson()`) for shot-save snapshot and composer prefill, plus a `stampActiveRecipeHotWater()` write-through with watchers on the hot-water settings.

*Why:* Reuses the existing hot-water settings pipeline (`sendMachineSettings` already sends `effectiveHotWaterVolume()`), so the recipe only has to push values into settings. *Trade-off:* the recipe configures hot-water parameters but the user still triggers the hot-water dispense; this matches how a recipe configures espresso without auto-starting the shot.

### 5. SAW learning + extra-settings ride the existing settings paths
SAW learning history joins the settings serialization so it transfers with settings (still excluding flow calibration and mqttPassword). For LAN extra-settings, add a client fetch of the extra-settings bundle to the settings-import step so device-to-device parity matches the full archive.

*Why:* Both are QSettings-backed and already have a home in the full archive; the fix is to stop the LAN path from being the odd one out, not to invent new transport.

## Risks / Trade-offs

- **Ordering bug if recipe import runs before bags/equipment** → call it strictly after `importBagsStatic`; unit-test the round-trip that a migrated shot's `recipe_id` resolves to a recipe whose `equipment_id` resolves to a real package.
- **Dangling `recipe_id` on partial import (recipes chosen off, shots on)** → when recipes are not merged, null out or drop `recipe_id` on inserted shots rather than leave a dangling id. Since recipes always ride in `shots.db` today, default is to always merge them with shots.
- **Migration into an older app build without the `hot_water_json` column** → column is additive and nullable; a source recipe with hot water imported into an older schema simply loses the block (graceful), and `ensureTableStatic` + the new migration converge fresh/old DBs. No BREAKING change.
- **Steam and hot water both active on one recipe** → explicitly allowed; the two blocks are independent and applied in sequence at activation. Test the milk-less hot-water case does not force the steam heater on.
- **MCP register-stub drift** → adding tool schema fields touches `register*Tools`; build `--target all` so `tst_mcpserver_session/protocol` stubs and `tst_mcptools_*` externs stay in sync (known gotcha).

## Migration Plan

1. Schema migration adds `hot_water_json` to `recipes` (new version step, mirrors the `rpm_pinned` migration); `ensureTableStatic` gains the column so fresh DBs converge.
2. Ship `importRecipesStatic` + shot `recipe_id` remap; no data backfill needed (existing local recipes already have valid ids; the change only affects imports going forward).
3. No rollback data risk: all changes are additive (new column, new import path, new settings keys in the serialized blob). Reverting the build leaves existing recipes intact; only new hot-water blocks and freshly-migrated recipes would be re-dropped, i.e. back to today's behavior.

## Open Questions

- Composer layout: a separate "Hot Water" card vs. extending the existing additions area — decide during QML implementation; spec only requires the toggle + vessel picker be present.
- Should partial migration ever let the user import shots without recipes? Default is to always co-migrate recipes with shots (they share `shots.db`); a separate toggle is out of scope unless requested.

**Resolved:** Water-vessel snapshot field set — snapshot the **full** vessel (`vesselName, volume, mode, flowRate, temperature`) by value; the vessel carries the amount/temperature/flow and there is no separate per-recipe amount input (user clarification). This gives parity with the pitcher block and survives preset deletion.
