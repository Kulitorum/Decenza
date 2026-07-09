## 1. Recipe transfer & backup import

- [x] 1.1 Add `RecipeStorage::importRecipesStatic(srcDb, destDb, merge, outRecipeIdMap, packageIdMap)` in `src/history/recipestorage.{h,cpp}`, mirroring `CoffeeBagStorage::importBagsStatic`: insert each source recipe, remap `equipment_id` through `packageIdMap`, dedup on identity (name + profile_title + bean identity) in merge mode, and record old→new ids in `outRecipeIdMap`. Preserve `archived` state.
- [x] 1.2 Call `importRecipesStatic` from `ShotHistoryStorage::importDatabaseStatic` immediately after `importBagsStatic` (order: equipment → bags → recipes → shots), passing the equipment `packageIdMap` and capturing the returned `recipeIdMap`.
- [x] 1.3 In the shot-insert loop of `importDatabaseStatic`, remap `shots.recipe_id` through `recipeIdMap` at the same site `bag_id` is remapped; if a shot's recipe was not merged, null the `recipe_id` rather than leave it dangling.
- [x] 1.4 In replace (non-merge) mode, ensure the destination `recipes` table is cleared alongside shots/samples/phases so a replace import is clean.
- [x] 1.5 Add manifest counts for recipes, coffee bags, and equipment in `handleBackupManifest` (`src/network/shotserver_backup.cpp`).
- [x] 1.6 Surface recipe/bag/equipment presence in the migration manifest summary in `qml/pages/settings/DeviceMigrationDialog.qml`.

## 2. Export⇄import parity audit & adjacent gaps

- [x] 2.0 Audit every durable store (all `shots.db` tables + all QSettings/`extra_settings` keys + filesystem media/profiles) against BOTH the export side (`handleBackupFull` `.dcbackup`, `/api/backup/*` endpoints, `handleBackupManifest`) and the import side (`datamigrationclient.cpp`, `handleBackupRestore`). Produce the table of "data type → exported? / imported?" and confirm no served-but-dropped or persisted-but-unexported type remains after this change.
- [x] 2.1 Include SAW learning history (`saw/learningHistory`) in `SettingsSerializer` so it is both exported (LAN settings + `.dcbackup` `settings.json`) and imported. Verify it lands in the full-archive export, not just the LAN path.
- [x] 2.2 Close the extra-settings LAN gap on BOTH sides: add a `/api/backup/extra-settings` serving endpoint in `shotserver_backup.cpp` (export), and a matching fetch + apply of shotMap location / accessibility / language in the settings-import step of `src/core/datamigrationclient.{cpp,h}` (import). Confirm the full-archive already covers it (it does) so the two paths reach parity.
- [x] 2.3 Confirm flow calibration and mqttPassword remain excluded on import (no regression); add a comment documenting the intentional exclusion.
- [x] 2.4 Confirm recipes are already exported (they ride in `shots.db` via `/api/backup/shots` and `.dcbackup`); no export-side change needed — the recipe work in group 1 is import-side only. Note this in the audit table so it is not mistaken for a gap.

## 3. Hot-water data model

- [x] 3.1 Add `hotWaterJson` member to the recipe struct and a `COL_STR("hot_water_json", hotWaterJson)` row in `kCols` in `src/history/recipestorage.{h,cpp}`; add the column to `ensureTableStatic`'s CREATE TABLE. Document the JSON shape `{hasWater, vesselName, mode, waterWeightG|volumeMl, temperatureC, flow, order}` — a by-value snapshot of the selected water vessel (opt-in; the vessel carries amount/temperature/flow, no separate per-recipe amount field) plus `order` ("before" = long black, "after" = americano; default "after").
- [x] 3.2 Add a new schema migration (next version after `rpm_pinned`) in `src/history/shothistorystorage.cpp` that `ALTER TABLE recipes ADD COLUMN hot_water_json TEXT`, gated on the version range, mirroring the migration-26 block.
- [x] 3.3 Ensure `importRecipesStatic` (task 1.1) carries `hot_water_json` and degrades gracefully when importing into a schema without the column.

## 4. Hot-water activation & snapshot (MainController)

- [x] 4.1 Add a `parseHotWaterBlock` / build helper near the existing `parseSteamBlock`/`compactJson` in `src/controllers/maincontroller.cpp`.
- [x] 4.2 In `applyActivatedRecipe`, after the steam block, re-select the snapshotted water vessel by name (recreating the preset from the by-value snapshot if it was deleted, mirroring the pitcher path) so its values become the live hot-water settings, then call `applyHotWaterSettings()`; do NOT add a heater hold (hot water needs no pre-warm). Verify a milk-less hot-water recipe does not trigger `startSteamHeating`.
- [x] 4.3 Add `currentHotWaterSpecJson()` mirroring `currentSteamSpecJson()`, and stamp it into shot metadata at each shot-save site alongside `steamJson`.
- [x] 4.4 Add `stampActiveRecipeHotWater()` and watchers on hot-water settings (amount/temperature/flow) so live edits write through to the active recipe, echo-guarded like steam; re-selecting the same values must not deactivate.

## 5. Hot-water recipe surfaces

- [x] 5.1 QML composer (`qml/pages/RecipeComposerPage.qml`): add hot-water field properties, `applyHotWaterJson()` prefill, `buildHotWaterJson()`, an "add hot water" toggle, a water-vessel picker (mirroring the pitcher picker) bound to `Settings.brew.waterVesselPresets` that snapshots the selected vessel by value (name/volume/mode/flowRate/temperature) — no separate amount field — and a before/after order choice (long black vs americano); include it in the save payload and promote prefill. Internationalize all new visible text.
- [x] 5.2 MCP (`src/mcp/mcptools_recipes.cpp`): add a `hotWaterBlockSchema()`, parse `hotWater`→`hotWaterJson` on create/update, serialize it into recipe JSON, and handle it in create-from-shot. Follow MCP field-naming conventions (`volumeMl`, `temperatureC`, `flowMlPerSec`).
- [x] 5.3 Web (`src/network/shotserver_recipes.cpp`): parse the `hotWater` body field, serialize the block into the recipe representation, handle create-from-shot, and add form inputs/summary to the HTML surface.

## 6. Tests & docs

- [x] 6.1 Add a recipe transfer round-trip test: import a source `shots.db` with recipes + shots into a destination, assert recipes merged, `equipment_id` remapped to a real package, and every migrated `shots.recipe_id` resolves (no dangle); include the dedup-on-repeat-import case.
- [x] 6.2 Add a hot-water round-trip test: save a recipe with a hot-water block, load it back unchanged; activate and assert hot-water settings applied and no steam heater hold when milk-less; promote-from-shot reconstructs the block.
- [x] 6.3 Update MCP register stubs and externs (`tst_mcpserver_session/protocol`, `tst_mcptools_*`) for the new schema fields; build `--target all` before pushing.
- [x] 6.4 Update `docs/CLAUDE_MD/RECIPES.md` (remove the "device transfer / backup does not yet copy recipes" caveat, document the hot-water block) and `docs/CLAUDE_MD/DATA_MIGRATION.md` (recipes/SAW/extra-settings now covered).
- [ ] 6.5 Verify on a GitHub Android test build for any platform-touching changes; then hand to Jeff to launch the app and confirm migration + Americano recipe end-to-end.
