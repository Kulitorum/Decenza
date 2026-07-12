## Context

`recipe-aware-brew-settings` deliberately decoupled the live dial from the recipe: yield/temp are per-brew overrides in `Settings.brew`, and the recipe's `yieldG`/`tempOverrideC` change only via the explicit "Update Recipe" button. That decoupling is correct. The bug is that the dialog still measures "overridden" against the **profile default in both modes**, so a recipe's own designed yield/temp (which almost always differ from the profile — that's the point of a recipe) are painted as amber overrides, `Clear` wipes them back to the profile, and "Update Recipe" greys out exactly when the user resets to the profile default. [#1485](https://github.com/Kulitorum/Decenza/issues/1485).

Everything needed is already in memory on the QML side: `MainController.activeRecipe` exposes `yieldG` and `tempOverrideC` (a NOTIFYing cache refreshed on `recipeUpdated`), and `Settings.dye.activeRecipeId >= 0` already drives `recipeActive` in `BrewDialog.qml`. Activation already applies the recipe's stored yield/temp as the opening `Settings.brew` overrides (`maincontroller.cpp:1317-1324`) — which is precisely what makes them the baseline. So this is a **dialog-display re-anchor only**: no C++ activation change, no storage/schema change.

## Goals / Non-Goals

**Goals:**
- When a recipe is active, treat its `yieldG`/`tempOverrideC` as the baseline for the Temp Delta and Stop-at fields: highlight, Clear target, Temp Delta zero-point, and Update-Recipe enablement all measured against the recipe, not the profile.
- Make the recipe baseline movable to any value (including the profile default) via Update Recipe.
- Leave the no-recipe dialog behavior byte-for-byte unchanged.

**Non-Goals:**
- No change to the machine temperature-delta upload math (frames still shift by `temperatureValue − espressoTemperature`; the re-anchor is display-only).
- No recipe-relative baseline for Dose or Ratio — they are dial-in values with their own write-through, not the "override vs. baseline" fields the reporter is describing. Dose keeps stamping `doseG`; ratio is not recipe-stored.
- No rename of `tempOverrideC` (fossil name; churn not worth it).
- No auto-write of yield/temp to the recipe on OK — the decoupling from `recipe-aware-brew-settings` stands; Update Recipe remains the sole path.

## Decisions

**1. Introduce two baseline accessors in `BrewDialog.qml`, one per field.**
`recipeTempBaseline` = `recipeActive && activeRecipe.tempOverrideC > 0 ? activeRecipe.tempOverrideC : profileTemperature`. `recipeYieldBaseline` = `recipeActive && activeRecipe.yieldG > 0 ? activeRecipe.yieldG : profileTargetWeight`. Both are NOTIFY-reactive (they read `Settings.dye.activeRecipeId` and `MainController.activeRecipe`, which already re-evaluate live). The `> 0` guard falls back to the profile when the recipe carries no value for that field — matching the storage sentinel (`0 = unset`) and keeping recipes that never pinned a yield/temp identical to no-recipe mode.

*Alternative considered:* compute the baseline in C++ and expose one property. Rejected — the QML already holds `profileTemperature`/`profileTargetWeight` and `activeRecipe`; a C++ property adds a signal round-trip and a second source of truth for a display concern.

**2. Re-point the three consumers at the baselines.**
- `overridden` predicates: Temp Delta `Math.abs(temperatureValue − recipeTempBaseline) > 0.1`; Yield `Math.abs(targetValue − recipeYieldBaseline) > 0.1`. Stepper accent follows the same predicate (already unified per `recipe-aware-brew-settings`).
- Temp Delta zero-point: the slider's `delta` reads `temperatureValue − recipeTempBaseline`; `temperatureValue` stays absolute internally, so OK / Update-Recipe / upload math is untouched. The "Profile: …" sub-line shows the temperature(s) the offset is applied to — in recipe mode that anchor is the recipe temperature.
- Clear handler: reset `temperatureValue = recipeTempBaseline` and `targetValue = recipeYieldBaseline` (with ratio recomputed from the restored yield/dose) instead of the profile defaults. Dose/ratio/grind clear paths unchanged.

**3. Update-Recipe enablement gates on the recipe's stored value, not the profile.**
Replace the first clause (`value !== profileDefault`) with the recipe-relative test in recipe mode: `enabled: recipeActive ? Math.abs((activeRecipe.yieldG||0) − targetValue) > 0.05 : targetValue !== profileTargetWeight` (and the temperature analogue). The existing second clause was already this test — the fix removes the profile-default short-circuit that disabled the button at the profile default. This lets the user push the baseline back to the profile default and persist it.

**4. Baseline read is display-only; writes are unchanged.**
OK still calls `activateBrewWithOverrides(...)` (which sets/clears `Settings.brew` overrides by comparison to the profile — unchanged). Update Recipe still writes the absolute `temperatureValue`/`targetValue` to `tempOverrideC`/`yieldG`. Only what the dialog *shows and highlights* moves to the recipe anchor.

## Risks / Trade-offs

- **[Profile-default recipe value is indistinguishable from "no override".]** If a user sets the recipe's yield equal to the profile default, the `> 0`/comparison logic treats the field as "at baseline = profile", which is the intended visual (no highlight). The recipe still stores the value; activation's own Bug-A guard already declines to arm an override equal to the profile default, so behavior is consistent. → Acceptable; it matches the "a recipe at the profile default has no deviation to show" model.
- **[Temp Delta anchor divergence from the machine delta.]** The displayed delta (relative to recipe) differs from the upload delta (relative to `espressoTemperature`). → Mitigation: `temperatureValue` remains the single absolute source; only the *displayed* zero-point differs, and the "Profile: …"/baseline sub-line shows what is actually applied, so the user still sees the real temperatures.
- **[`activeRecipe` cache staleness after an Update Recipe.]** The enablement/highlight read `activeRecipe.yieldG` immediately after a write. → Already handled: `requestUpdateRecipe` → `recipeUpdated` → MainController refreshes `m_activeRecipe`, and the dialog's `_pendingRecipeUpdateId` guard (existing) covers the in-flight window.

## Migration Plan

Pure UI-logic change in one QML file (+ a `RECIPES.md` note). No data migration, no schema change, no persisted-state change. Rollback = revert the commit. Verify on Android (the reporter's platform) that a recipe with a below-profile temp and a non-default stop-at opens with no highlight and `0°` delta, Clear returns to those values, and Update Recipe enables when the dial leaves them (including a reset to the profile default).

## Open Questions

- None blocking. The "Profile: …" sub-indicator label wording in recipe mode (does it read "Profile:" or "Recipe:" when anchored on the recipe temp?) is a copy detail to settle during implementation; the requirement only fixes the anchor value, not the label text.
