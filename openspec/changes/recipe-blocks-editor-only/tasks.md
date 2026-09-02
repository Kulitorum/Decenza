## 1. Remove the write-through

- [x] 1.1 Delete the five stamp connections in `MainController::setupRecipeConnections()` (`selectedSteamPitcherChanged`, `steamPitcherPresetsChanged`, `lastSteamMilkGChanged`, `selectedWaterVesselChanged`, `waterVesselPresetsChanged`) and rewrite the comment blocks above them to state the new rule; verify the file compiles.
- [x] 1.2 Delete `stampActiveRecipeSteam()` / `stampActiveRecipeHotWater()` from `maincontroller.cpp` and `maincontroller.h` now that nothing calls them; verify `grep -rn "stampActiveRecipe\(Steam\|HotWater\)" src` returns nothing.
- [x] 1.3 Confirm `currentSteamSpecJson()` / `currentHotWaterSpecJson()` still have live callers and keep them; verify by naming each remaining call site. If either is left with no caller, delete it too.
- [x] 1.4 Sweep for comments asserting the removed behavior — `maincontroller.cpp`'s descale-snapshot note, `recipestorage.{h,cpp}`, and `steamheaterpolicy.h`'s stamp-site count; verify no surviving comment describes a stamp that no longer exists.

## 2. Deactivate instead — a pitcher or vessel is an ingredient

- [x] 2.1 Add `Recipe::ownsSteamPitcherChoice` / `steamPitcherDiverged` / `ownsWaterVesselChoice` / `waterVesselDiverged` to `src/history/recipestorage.{h,cpp}`, beside `ownsProfileChoice`/`profileDiverged`; verify each gates on ownership first.
- [x] 2.2 Add the two deactivation watchers to `MainController::setupRecipeConnections()` on the SELECTION signals only, beside the bean/equipment/profile ones; verify the preset-change and milk-weight signals are not wired to anything.
- [x] 2.3 Clear the parked standing pitcher in the steam watcher before `deactivateRecipe()`; verify against `SettingsBrew::resolveRecipePitcherOverride` that the unwind then re-selects nothing.
- [x] 2.4 Cover the four rules in `tests/tst_recipestorage.cpp` — ownership, case-insensitive match, the Heater-off marker both ways, and the incomplete/dormant hot-water block.
- [x] 2.5 Invert each new assertion once and watch it go red, so none of them is vacuous.

## 3. Prove the stamps stay gone

- [x] 3.1 Add a test FUNCTION (not a new file — a new `tst_*.cpp` costs ~1.4 s of build forever) to `tests/tst_recipestorage.cpp` (NOT `tst_recipeeditorparity` — that is the profile Recipe Editor, a different subsystem) asserting which fields still stamp; verify it goes RED against the pre-change source.
- [x] 3.2 Write it as an ALLOWLIST of stamped fields, not a denylist of signals: a denylist is satisfied by a member-slot connect, a renamed helper, or a signal nobody listed. Verify grind/RPM/dose are still asserted present.
- [x] 3.3 Run the full suite through `mcp__qtcreator__run_tests` (scope `all`) and verify zero failures and zero new warnings. (Ran green at 117/117 before the deactivation rework; must be re-run after it.)

## 4. Review fallout

- [x] 4a.1 Water vessel preset removal clamped instead of shifting (pre-existing; steam was fixed, water never was), so deleting a vessel below the selection silently retargeted it AND emitted no signal — invisible to the new watcher. Both `removeWaterVesselPreset` and `moveWaterVesselPreset` now use the shared `shiftedForRemoval`/`shiftedForMove`; verify with three tests in `tst_settings`, the removal one proven red against the old clamping.
- [x] 4a.2 The steam watcher cleared the parked standing pitcher on a path where the user picked nothing: deleting the preset the recipe names lands the selection on the Heater off sentinel, so the user's standing pitcher was destroyed and the boiler left cold. Gate the clear on `recipeSteamPitcherStillExists`.
- [x] 4a.3 Extend the `recipeReady` reconcile — the only place that catches the startup restore and an external edit — to the pitcher and vessel; without it an MCP/web/wizard edit leaves the recipe active against the old selection, permanently now that the write-through is gone.
- [x] 4a.4 Harden the allowlist test: count over RAW text (the old count shared the stripped copy, so a `//`-hidden call vanished from both and stayed consistent), assert `requestUpdateRecipe` has exactly one call site, and assert the three must-not-be-wired signals are absent from `setupRecipeConnections`.
- [x] 4a.5 Cover the statics' edge cases: whitespace-only names (the `trimmed()` was load-bearing and untested), malformed JSON, `heaterOff` with `pitcherName` alongside it, a dormant block, and an empty live preset.

## 5. Manual verification on the machine

- [x] 5.1 With an americano recipe active, select a different water vessel on the Hot Water page; verify the recipe DEACTIVATES (pill deselects, Active badge clears) and its card still shows the original vessel and amount.
- [ ] 5.2 Tap that recipe's idle pill ONCE and verify it re-activates (rather than starting a shot), with the live hot-water amount, temperature and flow back at the recipe's own values.
- [x] 5.3 Repeat for a latte recipe and the steam pitcher; verify the pitcher the user picked stays selected after the deactivation (not the parked standing one).
- [x] 5.4 Verify what must NOT deactivate: finishing a steam session (the measured milk weight), editing a vessel or pitcher preset, and re-selecting the recipe's own vessel/pitcher.
- [ ] 5.5 Verify a grind change while a recipe is active still stamps the recipe and mirrors to the bag — the write-through that survives.
- [ ] 5.6 With *Let the recipe decide* on and a latte recipe active, pick the "Heater off" pitcher and start a shot; verify the boiler stays cold (the recipe deactivated, so the shot-start pre-heat no longer fires).

## 6. Documentation

- [x] 6.1 Rewrite the "Tweaks write through; ingredient swaps deactivate" paragraph in `docs/CLAUDE_MD/RECIPES.md` for the deactivation model; verify no line in that file still claims a block stamp.
- [x] 6.2 Wiki manual updated and pushed (`e4bf677`): the pitcher and vessel joined the ingredient list that deactivates, and the write-through line shrank to dose and grind. The earlier `1984bc3` described the per-brew-override model this change replaced.
- [ ] 6.3 AFTER the merge, post a short comment on [issue #1895](https://github.com/Kulitorum/Decenza/issues/1895) saying what changed. Held deliberately: the reporter should not be told it is fixed while it is a branch.

## 7. Land it

- [x] 7.1 Open a PR from a feature branch (never push to `main`) with the issue linked.
- [x] 7.2 Update the PR description for the deactivation model — the current text describes the per-brew-override design.
- [x] 7.3 Verify the `text-invariants.yml` run on the PR is GREEN before merging — it gates `src/**` and nothing blocks a merge on it.
- [x] 7.4 Re-run the automated `/pr-review-toolkit:review-pr` on the reworked change and address the findings.
- [ ] 7.5 Archive this change with the spec sync as the FINAL commit on the same PR (never a separate archive-only PR), then squash-merge and delete the branch.
