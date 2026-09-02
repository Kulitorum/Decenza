## Why

A recipe's steam and hot-water blocks are rewritten by any live tweak while that recipe is active. Selecting a different water vessel on the Hot Water page — a one-off "this cup needs 140 ml, not 70" — permanently redefines the recipe, and the next drink on that recipe pours the wrong amount. [Issue #1895](https://github.com/Kulitorum/Decenza/issues/1895) reports exactly this: an americano made for someone else silently doubles the water in every later americano, and re-selecting the recipe does not restore it because the stored row itself was changed.

The dial-in values (grind, RPM, dose) are things the user physically did and should keep following the dial. Which vessel or pitcher is on the bench for one drink is not — it is a per-brew choice, and the design already has a place for that shape: yield and temperature are per-brew overrides that only reach the recipe through an explicit edit.

## What Changes

- **BREAKING (behavioral)**: steam-block and hot-water-block changes made while a recipe is active no longer write through to that recipe. Selecting a different water vessel or steam pitcher, editing a vessel/pitcher preset, or changing the steamed-milk weight applies to the current brew only.
- Grind, RPM, and dose write-through are untouched — grind stays on the recipe (and mirrors to the bag), dose stays welded to the dose-source-precedence ladder.
- The steam and hot-water blocks change only through the recipe editor (wizard summary), MCP `recipe_update`, and the ShotServer recipe form — the same routes that already own yield and `tempOffsetC`.
- Re-activating the recipe restores its stored vessel/pitcher, so the live setting is recoverable without any new UI. No "Update Recipe" button is added for these blocks; no revert-on-dispense machinery is added.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `recipe-activation`: the "Tweaks write through; ingredient swaps deactivate" requirement moves steam values, milk weight, and the hot-water vessel selection from the write-through set to the per-brew-override set, alongside yield and temperature. Dose and grind/RPM stay where they are.

## Impact

- `src/controllers/maincontroller.cpp`: five write-through connections removed (`selectedSteamPitcherChanged`, `steamPitcherPresetsChanged`, `lastSteamMilkGChanged`, `selectedWaterVesselChanged`, `waterVesselPresetsChanged`). `stampActiveRecipeSteam()` / `stampActiveRecipeHotWater()` and their `currentSteamSpecJson()` / `currentHotWaterSpecJson()` helpers lose their only auto callers — the JSON builders stay, since shot-metadata and editor paths still assemble the blocks.
- `src/controllers/maincontroller.h`: the two stamp declarations.
- Activation is unchanged: `applyActivatedRecipe`'s steam and hot-water stages already re-push the recipe's stored block, which is what makes the live value recoverable.
- Docs: `docs/CLAUDE_MD/RECIPES.md` ("Tweaks write through" paragraph) and the wiki manual page covering recipes.
- No schema change, no migration, no MCP or web surface change.
