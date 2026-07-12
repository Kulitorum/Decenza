## Why

A recipe's yield and temperature are part of the recipe's *design* — they are the baseline that recipe is meant to reproduce, not "overrides" of the profile. Today Brew Settings computes "overridden" (highlight, Clear target, Update-Recipe enablement) relative to the **profile default even when a recipe is active**, so the recipe's own designed values are painted as amber "overrides" the user never created, `Clear` wipes the recipe's design back to the profile, and there is no working way to reset to — or edit back toward — the recipe's baseline ([#1485](https://github.com/Kulitorum/Decenza/issues/1485)).

## What Changes

- When a recipe is active, the **baseline** for the Temp Delta and Stop-at (yield) fields becomes the active recipe's `tempOverrideC` / `yieldG`, not the profile default. With no recipe active, the baseline stays the profile default (unchanged).
- **Override highlight** follows the active baseline: the recipe's own yield/temp render as plain values (no highlight); only a per-brew deviation *from the recipe* is highlighted. This ends the "phantom overrides" the reporter saw.
- The **Temp Delta control anchors on the recipe temperature** in recipe mode, so it reads `0°` when the dial sits at the recipe's design temp (and `+N°/−N°` for a deviation from it).
- **Clear/reset** returns each field to the active baseline: the recipe's yield/temp when a recipe is active (stripping only per-brew deviations), the profile default when none is.
- **"Update Recipe" enablement** gates on the value differing from the *recipe's* stored value, so the baseline can be moved anywhere — including back to the profile default (fixes the button greying out exactly when the user resets to default).
- No storage/schema change: `yieldG` / `tempOverrideC` remain the recipe's stored fields (the `Override` suffix on `tempOverrideC` is a naming fossil, left as-is). The per-brew override layer in `Settings.brew` is unchanged.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `recipe-aware-brew-settings`: the override-highlight baseline, the Clear baseline, and the Update-Recipe enablement become **recipe-relative when a recipe is active** (currently all profile-relative in both modes).
- `brew-overrides`: the "Clear all overrides" behavior and the Temp Delta anchor become recipe-relative when a recipe is active (currently always profile default).

## Impact

- `qml/components/BrewDialog.qml` — the `overridden` predicates and stepper accent (Temp Delta ~688–690, Yield ~999), the Temp Delta anchor/delta, the `Clear` handler (~358–373), and the two "Update Recipe" `enabled` conditions (~722, ~1034).
- Baseline is read from `MainController.activeRecipe.tempOverrideC` / `.yieldG`, falling back to the profile default when no recipe is active or the recipe carries no override for that field.
- No C++ activation-path change: activation still applies the recipe's stored yield/temp as the opening overrides (`maincontroller.cpp:1317-1324`), which is what makes them the baseline.
- Specs: `openspec/specs/recipe-aware-brew-settings`, `openspec/specs/brew-overrides`. Docs: `docs/CLAUDE_MD/RECIPES.md` (Brew Settings baseline note).
