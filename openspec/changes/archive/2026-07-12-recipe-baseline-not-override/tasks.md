## 1. Baseline accessors (BrewDialog.qml)

- [x] 1.1 Add a NOTIFY-reactive `recipeTempBaseline` readonly property: `recipeActive && MainController.activeRecipe.tempOverrideC > 0 ? MainController.activeRecipe.tempOverrideC : profileTemperature`
- [x] 1.2 Add a NOTIFY-reactive `recipeYieldBaseline` readonly property: `recipeActive && MainController.activeRecipe.yieldG > 0 ? MainController.activeRecipe.yieldG : profileTargetWeight`

## 2. Re-anchor highlight and Temp Delta zero-point

- [x] 2.1 Temp Delta `overridden`: change to `Math.abs(temperatureValue - recipeTempBaseline) > 0.1` (was vs. `profileTemperature`)
- [x] 2.2 Temp Delta slider `delta` and zero-point: read `temperatureValue - recipeTempBaseline` so it reads `0°` at the recipe temperature; keep `temperatureValue` absolute internally (from/to/onValueModified re-anchored too)
- [x] 2.3 Yield `overridden`: change to `Math.abs(targetValue - recipeYieldBaseline) > 0.1` (was vs. `profileTargetWeight`)
- [x] 2.4 Confirm the unified stepper accent for both fields follows the re-anchored `overridden` predicate (no per-type color regression) — verified live (amber on deviation, default at baseline)
- [x] 2.5 Sub-indicator: tag = total offset from profile (truthful to what the machine runs); highlight follows deviation-from-baseline. Verified: "Profile: 84 · 94°C +1°" amber on deviation, plain at baseline

## 3. Clear returns to the active baseline

- [x] 3.1 Clear handler: when a recipe is active set `temperatureValue = recipeTempBaseline` and restore a pinned recipe `yieldG` (ratio recomputed), else the profile-derived fallback (preserves volume/timer path); dose/grind/cup-tare unchanged
- [x] 3.2 Verify Clear does NOT call `requestUpdateRecipe` — verified live: Clear returned Stop-at to 40 (recipe baseline) without touching the stored recipe

## 4. Update-Recipe enablement gates on the recipe, not the profile

- [x] 4.1 Temperature "Update Recipe": enabled ⟺ `tempInput.overridden` (deviates from baseline). NOTE: initial impl compared against raw `tempOverrideC||0`, which wrongly enabled the button at delta 0 when the override was unset (found by driving the app); fixed to use `overridden`.
- [x] 4.2 Yield "Update Recipe": enabled ⟺ `targetInput.overridden`
- [x] 4.3 Confirm resetting the dial to the profile default while the recipe holds a different value leaves "Update Recipe" enabled — verified live (Stop-at 36 amber + enabled while recipe held 40)

## 5. Verification

- [x] 5.1 Compile — qmlcachegen + link clean (BrewDialog recompiled), 0 errors/warnings
- [x] 5.2 Fix any pre-existing accessibility/issue-class violations in BrewDialog.qml touched regions (per CLAUDE.md whole-file rule)
- [x] 5.3 Manual check driven via computer-use on the macOS build (pure-QML change, no `Q_OS_*` branches, so macOS is representative): recipe value 40 shows un-highlighted while profile is 36; Clear returns to the recipe baseline; Update Recipe enables on deviation incl. reset-to-profile-default; Temp button disabled at 0° after the enablement fix. (Optional: a CI Android build for extra confidence — not required since no platform code changed.)

## 6. Docs & spec

- [ ] 6.1 Update `docs/CLAUDE_MD/RECIPES.md` Brew Settings note: the override baseline/highlight/Clear is the active recipe's yield/temp when a recipe is active, not the profile default
- [ ] 6.2 Run `/opsx:archive` as the final commit on the branch so the spec deltas promote into the PR

## 7. Open observation (decide before PR)

- [ ] 7.1 Ratio stays profile-relative by design (not recipe-stored), so with a recipe yield ≠ profile the Stop-at can read at-baseline (white) while the derived Ratio reads overridden (amber), and vice-versa. Verified as intended per the design doc, but it's a minor visual nuance — decide whether to also anchor the ratio baseline on the recipe's yield/dose, or leave as-is.
