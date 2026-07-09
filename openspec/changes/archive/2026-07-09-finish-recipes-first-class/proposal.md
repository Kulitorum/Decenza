## Why

Recipes just landed as the fourth rung of the optionality ladder (`add-recipes`), but two pieces were left unfinished. First, recipes are **silently lost on device-to-device transfer and `.dcbackup` restore**: the `recipes` table physically travels inside `shots.db`, yet the import path merges bags and equipment and never touches recipes — so recipes vanish and every migrated shot's `recipe_id` provenance dangles. Second, a recipe cannot yet describe an **Americano** (espresso + added hot water), even though the machine already dispenses hot water and the steam block gives us a proven "snapshot a preset by value" pattern to mirror. Auditing the transfer path while we were in it also surfaced adjacent gaps (SAW learning, LAN extra-settings) worth closing in the same pass.

## What Changes

- **Recipes survive transfer & restore.** Add `RecipeStorage::importRecipesStatic` (mirroring `importBagsStatic`) and call it from `ShotHistoryStorage::importDatabaseStatic` after bags, remapping each recipe's `equipment_id` through the package id-map and resolving/merging bean links. Remap `shots.recipe_id` in the shot-insert loop the same way `bag_id` is remapped today, so provenance no longer dangles. Fixes **both** the LAN `DataMigrationClient` path and the full-archive `.dcbackup` restore.
- **Hot-water block in recipes → Americano.** Add a `hot_water_json` column (new DB migration) storing `{hasWater, waterWeightG or volumeMl, vesselName, temperatureC, flow, mode}`, snapshotting a **water vessel by value** exactly as the steam block snapshots a pitcher. Apply it on recipe activation (send hot-water temp/volume/flow via existing `applyHotWaterSettings()`), stamp edits through, snapshot it on shot save for promote-from-shot round-trip, and expose it on every recipe surface (composer QML, MCP, web).
- **Migration UI advertises recipes/bags/equipment.** The device-migration dialog folds bags, equipment, and recipes silently into "Shots". Surface manifest counts so users see what transfers.
- **Export⇄import parity.** Make backup symmetric so nothing is exported-but-un-importable or persisted-but-unexported. Recipes are already exported (they ride in `shots.db` and the `.dcbackup` archive) — that side needs no change, only the import merge above. The remaining asymmetries:
  - **SAW learning history** (`saw/learningHistory`) is exported by **no** path (not in `SettingsSerializer`, not in the full archive) — add it to settings serialization so it is both exported and imported.
  - **`extra_settings.json`** (shotMap location, accessibility, language) is bundled in the full archive but has **no standalone LAN endpoint and is never fetched by the LAN client** — add the serving endpoint (export) and the LAN fetch/apply (import).
  - An audit task confirms every durable store appears on both the export and import sides after this change.
  - Flow calibration remains intentionally excluded (machine-specific) — documented, not changed.

## Capabilities

### New Capabilities
- `data-transfer-coverage`: Completes device-transfer/backup coverage for data types that currently drop on migrate/restore — recipes (merge + `shots.recipe_id` remap), SAW learning history, and LAN extra-settings — plus export⇄import parity and surfacing bags/equipment/recipes counts in the migration UI.

### Modified Capabilities
<!-- Hot water is additive to the recipe capabilities promoted by archiving add-recipes (done on this branch). Expressed as deltas: -->
- `recipe-model`: adds an optional hot-water block (water-vessel snapshot by value); shots also snapshot the hot-water spec for promote round-trip.
- `recipe-activation`: applies the hot-water block on activation (no heater hold; milk-less hot-water never warms the steam heater); hot-water edits write through to the active recipe.
- `recipe-composer`: composer, promote-from-shot, and clone all cover the hot-water block (Americano is composable).
- `mcp-server`: recipe tools accept/return the hot-water block under the house field conventions.
- `shotserver-recipes`: web recipe API + `/recipes` page round-trip the hot-water block.

## Impact

- **Storage**: `src/history/recipestorage.{h,cpp}` (new `importRecipesStatic`, `hot_water_json` column + `kCols` row), `src/history/shothistorystorage.cpp` (call recipe import in `importDatabaseStatic`; remap `shots.recipe_id`; new schema migration for `hot_water_json`).
- **Transfer**: `src/core/datamigrationclient.{cpp,h}` (fetch/import extra-settings over LAN), `src/core/settingsserializer.cpp` (include SAW learning history), `qml/pages/settings/DeviceMigrationDialog.qml` + manifest handler in `src/network/shotserver_backup.cpp` (recipe/bag/equipment counts).
- **Activation & snapshot**: `src/controllers/maincontroller.cpp` (parse/apply hot-water block on activation, stamp-through on edits, snapshot on shot save via a new `currentHotWaterSpecJson()`).
- **Surfaces**: `src/mcp/mcptools_recipes.cpp`, `src/network/shotserver_recipes.cpp`, `qml/pages/RecipeComposerPage.qml` (hot-water field mirroring the steam block; water-vessel picker).
- **Docs/tests**: `docs/CLAUDE_MD/RECIPES.md` + `DATA_MIGRATION.md` updates; recipe transfer round-trip and hot-water round-trip tests; MCP register-stub gotcha applies (`tst_mcpserver_session/protocol`).
- **No BREAKING changes**: additive column + additive import path; older backups without recipes/hot-water import unchanged.
