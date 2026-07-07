# Recipes

A **recipe** is the whole drink: profile (required) + bean link + equipment package + dose/yield/temperature + grind routing + steam block + a name. It is the fourth rung of the optionality ladder — profile alone always pulls a shot; beans, equipment, and recipes are strictly additive. No flow ever requires a recipe, and a recipe works with whatever rungs the user tracks (bean-less and equipment-less recipes are valid).

OpenSpec change: `add-recipes`. Spec files under `openspec/changes/add-recipes/specs/` are the behavioral source of truth.

## Data model (`src/history/recipestorage.{h,cpp}`)

- `recipes` table in the shot-history DB (migration 25), managed by `RecipeStorage` — a structural clone of `CoffeeBagStorage` (kCols single-source column table, async request/ready signals, `SerialDbWorker`).
- **Bean link is bean-level, not bag-level**: canonical Bean Base id when available, else roaster+coffee identity. `resolveOpenBagStatic()` resolves it to the current open bag at activation; "no open bag of this bean" is a display state, never an error.
- **Grind: inherit by default, pin by exception.** `grind_pinned` empty = the recipe follows the linked bean's bag grind (re-dials update all sibling recipes). Non-empty = the recipe's private dial. Bean-less recipes keep grind in the pin by construction. Grind text is opaque (free-form strings — no deltas).
- **Steam block** (`steam_json`, compact JSON): `{hasMilk, milkWeightG, pitcherName, durationSec, flow, temperatureC}`. The pitcher is snapshotted **by value** (snapshot-not-reference, like Bean Base); activation re-selects the preset by name and *recreates it from the snapshot if deleted*.
- **Lifecycle mirrors bags**: 0 shots = hard-deletable; any shots = archive only (`archived` flag), so `shots.recipe_id` provenance never dangles.
- Shots record `recipe_id` + a `steam_json` snapshot at save time (all three metadata build sites in MainController), so promote-from-shot round-trips the whole drink.

## Activation (single path)

`MainController::activateRecipe(id)` → `RecipeStorage::requestRecipeForActivation` (one background pass: recipe + resolved bag) → `applyActivatedRecipe`:
profile by title with stored-JSON fallback → bag via `setActiveBagKeepFields` + explicit field applies from the bundle's bag map (deterministic; no async `applyActiveBag` race) → equipment → grind routing (suspension flag set **before** the setter when pinned) → queued dose write (beats loadProfile's deferred recommended dose) → yield/temp overrides + re-upload → steam block + heater intent (`hasMilk` → `startSteamHeating("recipe-activated")`; never fights `keepSteamHeaterOn`) → `activeRecipeId` last.

QML pill taps, MCP `recipe_activate`, and the web `/activate` route all call this one path. Terminal status: `recipeActivated(id, success)` (queued, lands after the dose write).

- **Tweaks write through; ingredient swaps deactivate.** Dose/yield/temp/steam/milk edits stamp the active recipe (`stampActiveRecipe`, echo-guarded like the bag pattern). Grind edits go to the bag when inherited (`SettingsDye` write-through) and to the pin when pinned (`setGrindBagWriteThroughSuspended` blocks the bag write). Changing profile/bag/equipment deactivates — the watchers compare against the recipe's **own** ingredients, so re-selecting the same thing (or startup auto-load) never deactivates.
- Active id persists in `SettingsDye` (`dye/activeRecipeId`); startup restores the cache + pin routing without re-applying values.

## Surfaces

- **QML**: `RecipesItem` idle widget (BeansItem clone — MRU pills of the last 5, double/long-press → `RecipesPage`), `RecipeComposerPage` (one window: blank / promote-from-shot via `promoteShotId` / clone via `prefill`), promote buttons on ShotHistoryPage (beside Load), ShotDetailPage header, AutoFavoritesPage rows. Navigate target is `recipeList` — bare `recipes` is the pre-existing profile Recipe Editor.
- **MCP**: `mcptools_recipes.cpp` — see `MCP_SERVER.md` for the tool table. Register-stub gotcha applies (`tst_mcpserver_session/protocol`).
- **Web**: `shotserver_recipes.cpp` (+ `shotserver_bags.cpp`, `shotserver_equipment.cpp`) — see `SHOTSERVER.md`.

## Gotchas

- `ShotProjection` gained `recipeId`/`steamJson`; the composer prefill reads them from `shotReady`.
- Adding a recipe column = a kCols row in recipestorage.cpp **plus** the CREATE TABLE + a migration step (same rule as bags).
- Steam-heater warm-up time on real hardware is unmeasured — heater derivation deliberately only *turns on* (milk drinks); milk-less recipes change nothing. Revisit aggressive-off only with a measured warm-up.
- Device transfer / backup import does not yet copy the `recipes` table (follow-up; bags/equipment have importers to mirror).
