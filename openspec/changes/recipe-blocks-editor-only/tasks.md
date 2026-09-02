## 1. Remove the write-through

- [x] 1.1 Delete the five stamp connections in `MainController`'s constructor (`selectedSteamPitcherChanged`, `steamPitcherPresetsChanged`, `lastSteamMilkGChanged`, `selectedWaterVesselChanged`, `waterVesselPresetsChanged`, `src/controllers/maincontroller.cpp:1608-1627`) and rewrite the two comment blocks above them to state the new rule; verify the file compiles.
- [x] 1.2 Delete `stampActiveRecipeSteam()` / `stampActiveRecipeHotWater()` from `maincontroller.cpp` and `maincontroller.h` now that nothing calls them; verify with `grep -rn "stampActiveRecipe\(Steam\|HotWater\)" src` returning nothing.
- [x] 1.3 Confirm `currentSteamSpecJson()` / `currentHotWaterSpecJson()` still have live callers and keep them; verify by listing each remaining call site (`grep -rn "current\(Steam\|HotWater\)SpecJson" src`) and naming what each one builds the block for. If either is left with no caller, delete it too.
- [x] 1.4 Re-read the stale comment at `maincontroller.cpp:3833` ("emits selectedSteamPitcherChanged, which is wired to stampActiveRecipeSteam()") and any other comment asserting the removed behavior; verify with `grep -rn "stampActiveRecipe" src` that no surviving comment describes a stamp that no longer exists.

## 2. Prove the rule holds

- [x] 2.1 Add a test FUNCTION (not a new file — a new `tst_*.cpp` costs ~1.4 s of build forever) to `tests/tst_recipeeditorparity.cpp` that reads `src/controllers/maincontroller.cpp` and asserts none of the five signals is connected to a recipe stamp; verify it goes RED against the pre-change file before keeping it.
- [x] 2.2 In the same function, assert the grind/RPM/dose stamps are still wired — the test must fail if a later change removes them too; verify by deleting one stamp locally and watching the test go red.
- [x] 2.3 Run the full suite through `mcp__qtcreator__run_tests` (scope `all`) and verify zero failures and zero new warnings. **The qtcreator MCP was not reachable when this change was written (`ENDPOINT_NOT_FOUND` at `http://127.0.0.1:3001`) — it must be back before this task can run; do not fall back to a CLI build.**

## 3. Manual verification on the machine

- [ ] 3.1 With an americano recipe active, select a different water vessel on the Hot Water page, then reopen the Recipes page and verify the recipe card still shows the original vessel/amount.
- [ ] 3.2 Re-activate that recipe from the Recipes page and verify the live hot-water amount, temperature, and flow return to the recipe's own values.
- [ ] 3.3 Repeat 3.1-3.2 for a latte recipe and the steam pitcher, and confirm changing the steamed-milk weight leaves the recipe's steam block unchanged.
- [ ] 3.4 Verify a grind change while a recipe is active still stamps the recipe (and mirrors to the bag) — the one write-through that survives.

## 4. Documentation

- [x] 4.1 Rewrite the "Tweaks write through; ingredient swaps deactivate" paragraph in `docs/CLAUDE_MD/RECIPES.md` so dose and grind/RPM are the only write-throughs and the blocks are editor-only; verify no other line in that file still claims a block stamp (`grep -n "stampActiveRecipe" docs/CLAUDE_MD/RECIPES.md`).
- [x] 4.2 Check the wiki manual's recipe page (clone `Kulitorum/Decenza.wiki.git` if not already local) for any statement that a live tweak is remembered by the recipe. If present, correct it in one or two sentences — what sticks (grind, dose) and where a water/milk change is kept (the recipe editor). If the manual never made the claim, add nothing and record that here. Hold the push per the usual wiki-timing convention.
- [ ] 4.3 Post a short comment on [issue #1895](https://github.com/Kulitorum/Decenza/issues/1895) stating what changed and that re-activating the recipe restores the live setting; verify the comment is visible on the issue.

## 5. Land it

- [ ] 5.1 Open a PR from a feature branch (never push to `main`) with the issue linked; verify the `text-invariants.yml` run on the PR is GREEN before merging — it gates `src/**` and nothing blocks a merge on it.
- [ ] 5.2 Run the automated `/pr-review-toolkit:review-pr` on the PR and address the findings.
- [ ] 5.3 Archive this change with the spec sync as the FINAL commit on the same PR (never a separate archive-only PR), then squash-merge and delete the branch.
