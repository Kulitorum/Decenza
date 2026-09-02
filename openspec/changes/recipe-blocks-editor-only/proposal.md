## Why

A recipe's steam and hot-water blocks are rewritten by any live tweak while that recipe is active. Selecting a different water vessel on the Hot Water page — a one-off "this cup needs 140 ml, not 70" — permanently redefines the recipe, and the next drink on that recipe pours the wrong amount. [Issue #1895](https://github.com/Kulitorum/Decenza/issues/1895) reports exactly this: an americano made for someone else silently doubles the water in every later americano, and re-selecting the recipe does not restore it because the stored row itself was changed.

The dial-in values (grind, RPM, dose) are things the user physically did and should keep following the dial. Which vessel or pitcher is on the bench for one drink is not — it is a per-brew choice, and the design already has a place for that shape: yield and temperature are per-brew overrides that only reach the recipe through an explicit edit.

## What Changes

- **BREAKING (behavioral)**: the steam pitcher and the water vessel become **ingredients**. Selecting a different one while a recipe is active deactivates the recipe — the same rule the bean, equipment package and profile already follow — instead of rewriting it.
- The recipe's stored block is left untouched, so one tap on its pill re-activates and restores the pitcher and vessel. Deactivation is what makes that one tap work: a deselected pill activates on the first tap, where the selected pill would have started a shot.
- Grind, RPM, and dose write-through are untouched — grind stays on the recipe (and mirrors to the bag), dose stays welded to the dose-source-precedence ladder.
- Two things deliberately do NOT deactivate: editing a pitcher/vessel preset (not an ingredient swap — the block is a by-value snapshot), and the steamed-milk weight (captured automatically at the end of steaming, so deactivating on it would cost every milk drink's shot its recipe attribution).
- The blocks change only through the wizard, MCP `recipe_update`, and the ShotServer recipe form. No "Update Recipe" button is added for them, and no revert-on-dispense machinery.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `recipe-activation`: the "Tweaks write through; ingredient swaps deactivate" requirement moves the steam-pitcher and water-vessel selections from the write-through set to the ingredient-swap set, alongside bean, equipment and profile. Preset edits and the measured milk weight leave the recipe alone entirely. Dose and grind/RPM stay where they are.

## Impact

- `src/controllers/maincontroller.cpp`: five write-through connections removed (`selectedSteamPitcherChanged`, `steamPitcherPresetsChanged`, `lastSteamMilkGChanged`, `selectedWaterVesselChanged`, `waterVesselPresetsChanged`); two deactivation watchers added beside the bean/equipment/profile ones, on the two SELECTION signals only. `stampActiveRecipeSteam()` / `stampActiveRecipeHotWater()` go with them; the `currentSteamSpecJson()` / `currentHotWaterSpecJson()` builders stay, since shot-metadata and promote-from-shot still assemble the blocks.
- `src/history/recipestorage.h`: the ownership + divergence rules as `Recipe::` statics beside `profileDiverged`, so both watchers and their tests ask one definition.
- `MainController::deactivateRecipe()` / the steam watcher: a user-driven pitcher pick becomes the standing selection before deactivating, so the override unwind cannot re-select the parked pitcher over the user's own choice.
- Activation is unchanged: `applyActivatedRecipe`'s steam and hot-water stages already re-push the recipe's stored block.
- Docs: `docs/CLAUDE_MD/RECIPES.md` and the wiki manual page covering recipes.
- No schema change, no migration, no MCP or web surface change.
