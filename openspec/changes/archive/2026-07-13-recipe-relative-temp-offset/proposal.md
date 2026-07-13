# Proposal: recipe-relative-temp-offset

## Why

A recipe's temperature is designed **relative to its profile** ("this bean likes the profile 3° cooler"), and the wizard already presents it that way ("Temp offset"). But storage is an **absolute** temperature (`temp_override_c`), with the offset recomputed against the profile's current temperature at every display. When a profile's temperature changes — e.g. Brew Settings' "Save temperature to profile" bakes a new `espresso_temperature` into the shared profile file — every recipe pinned to that profile silently grows a phantom offset the user never set. This is the reporter's "why has this got minus five? I never adjust this" in the PR #1492 video (Decenza issue #1485 follow-up), reproduced and root-caused on current `main`.

Two adjacent display defects compound the confusion, both validated live: recipe cards render the temperature frames of whatever profile is **currently loaded** (all cards flipped "90 · 88°C" → "84 · 94°C" when a different recipe was activated), and cards paint the recipe's own designed temp/yield as amber "overrides" while the post-#1492 Shot Plan correctly shows them as the un-highlighted baseline — the same value orange on one surface, white on another.

## What Changes

- **BREAKING (schema): recipe temperature becomes a stored offset.** The `recipes` table gains `temp_offset_c` (delta °C, 0 = none); `temp_override_c` is migrated (`offset = stored absolute − profile espresso_temperature`, profile resolved by title with the recipe's embedded profile JSON as fallback) and dropped from the record model. A recipe's effective brew temperature is always `profile temp + offset`, so profile temperature edits move the recipe with the profile and never manufacture phantom deltas.
- **Activation applies the offset**: `profile temp + offset` becomes the brew temperature override; the "recipe value ≈ profile default is not an override" guard (Bug A) simplifies to `offset ≈ 0 → no override`. The recipe-baseline accessors (`activeBaselineTemperatureC`) return `profile temp + offset`.
- **Wizard stores what it edits**: the Temp offset stepper reads/writes the stored offset directly — no more open-time subtraction. Promote-from-shot converts the shot's absolute override to an offset against the shot's profile at promotion time.
- **Recipe cards render their own recipe's data**: the shot-plan line on a recipe card uses the recipe's own profile frame temperatures (already available from `getProfileByFilename`) — never the currently-loaded profile's frames.
- **Cards show source + delta; everywhere else shows only the recipe's values.** A card presents where the values come from and how the recipe modifies them: the profile's temps with the stored offset as a signed tag ("84 · 94°C −3°"), and the yield as "profile → recipe" ("36.0 → 40.0g") when it differs — with the highlight tint on the delta markers only, so amber always means "this recipe modifies its profile". Outside the card (Shot Plan, Brew Settings), an active recipe's temp/yield render as its baseline with no profile reference (per #1492). The wizard's summary preview follows the card rule.
- **MCP + ShotServer recipe surfaces** expose `tempOffsetC` (delta semantics, unit-suffixed name per MCP conventions) in place of `temperatureOverrideC`; the web recipe editor and docs (`RECIPES.md`, `MCP_SERVER.md`) follow.

## Capabilities

### New Capabilities

(none — all changes modify existing capabilities)

### Modified Capabilities

- `recipe-model`: temperature is stored as an offset relative to the recipe's profile (`temp_offset_c`), with a one-time migration from the absolute `temp_override_c`; the record model, MCP/ShotServer field names, and promote-from-shot conversion change accordingly.
- `recipe-activation`: activation computes the brew temperature as `profile temp + offset`; the coincidental-default guard keys on `offset ≈ 0`; baseline accessors return the offset-derived temperature.
- `recipe-quick-switch`: recipe management cards render the recipe's own profile's frame temperatures (never the loaded profile's) with the stored offset as a highlighted signed tag, and yield as a highlighted "profile → recipe" arrow when modified; the wizard summary preview renders identically.
- `recipe-aware-brew-settings`: the recipe-card carve-out in the live-Shot-Plan baseline requirement is retained and recast in offset terms (cards deliberately show source + delta; live surfaces show recipe values only); baseline definitions, the Update-Recipe persistence/enablement rules, and the per-brew-overrides requirement reference the offset instead of the absolute `tempOverrideC`.
- `brew-overrides`: the Brew Dialog's Temp-Delta/Clear baseline is recast as the offset-derived temperature (`espressoTemperature + tempOffsetC`).
- `plan-widgets`: the override-indicator requirement carves out the recipe-card temperature item (base temps plain, highlight on the signed offset tag only); the yield arrow and all other surfaces keep the whole-item scheme.
- `mcp-server`: the recipe data-conventions field-name example follows the rename to `tempOffsetC`.

## Impact

- **Storage/migration**: `src/history/recipestorage.{h,cpp}` (column, record field, schema migration with per-recipe profile resolution off the main thread); `tests/tst_recipestorage.cpp`.
- **Activation & baselines**: `src/controllers/maincontroller.cpp` (`applyActivatedRecipe`, `activeBaselineTemperatureC`).
- **Display**: `src/controllers/profilemanager.{h,cpp}` (per-profile-steps temperature display invokable), `qml/components/ShotPlanText.qml` (accept explicit step temps), `qml/components/RecipeDrinkCard.qml`, `qml/pages/RecipesPage.qml`, `qml/pages/RecipeWizardPage.qml` (offset editing + preview), `qml/components/layout/ScreensaverEditorPopup.qml`, `qml/components/layout/items/ShotPlanItem.qml` (recipe map key).
- **Surfaces**: `src/mcp/mcptools_recipes.cpp`, `src/network/shotserver_recipes.cpp` (+ web recipe editor payload), `src/history/recipepromotion.cpp`; `tests/tst_recipepromotion.cpp`, MCP register-stub test externs if signatures move.
- **Docs**: `docs/CLAUDE_MD/RECIPES.md`, `docs/CLAUDE_MD/MCP_SERVER.md`, `docs/CLAUDE_MD/DATA_MIGRATION.md` (schema note).
- **Interaction with in-flight work**: `extract-recipe-wiring-controller` (proposed, not yet implemented) moves the activation wiring this change edits — whichever lands second rebases mechanically; behavior contracts stay as specced here.
