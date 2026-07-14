## 1. `ShotPlanText.qml` — fold the offset into the shift, drop the tag

- [x] 1.1 In `_tempStr`'s step-based branch, change the `temperatureDisplayForSteps` call's `baselineShiftC` argument from `0` to `recipeTempOffsetC`, so the card's own profile frames render already shifted by the recipe's stored offset.
- [x] 1.2 Delete `_tempTagStr` and its concatenation in `_build()` (the `if (_tempTagStr !== "") temp += " " + fmt(_tempTagStr, true, true)` block) — `temp` becomes just `fmt(_tempStr, true, _tempOverride)`.
- [x] 1.3 Update the code comments around `profileStepTemps`/`recipeTempOffsetC` (lines ~62-88) to describe them as resolving the card's own (possibly not-loaded) profile and folding its offset into the displayed value — not as a "source + delta" presentation — and explicitly note why `profileStepTemps` can't be replaced by `recipeBaselineTemp` (that path reads the currently-loaded profile).

## 2. `RecipeDrinkCard.qml` — drop the yield arrow

- [x] 2.1 Change `yieldOverridden` on the card's `ShotPlanText` from the profile-relative diff comparison (`card.recipe.yieldG > 0 && card.profileYieldG > 0 && Math.abs(...) > 0.1`) to `false`.
- [x] 2.2 Update the surrounding comment block (lines ~41-48, ~224-236) to describe the temperature/yield bindings as baseline resolution against the card's own profile, not "source + delta" (recipe-relative-temp-offset).
- [x] 2.3 Confirm `RecipeWizardPage.qml`'s summary/WYSIWYG hero step (which reuses `RecipeDrinkCard`) needs no separate change and picks up the fix automatically. Confirmed: `RecipeWizardPage.qml:2506` instantiates `RecipeDrinkCard` directly, passing only `recipe`/`active`/`profileTempC`/`profileYieldG`/`profileStepTemps`/`imageKey` — no `ShotPlanText` overrides of its own.

## 3. Docs

- [x] 3.1 Update `docs/CLAUDE_MD/RECIPES.md`'s paragraph describing "Recipe cards deliberately show the opposite decomposition — source + delta" to describe the new baseline behavior, rereading the full paragraph (it's dense and covers several related mechanisms) so no sentence is left contradicting the new code.
- [x] 3.2 Checked the GitHub wiki manual (`Kulitorum/Decenza.wiki.git`, cloned to scratch and removed after). `Manual.md`'s "Shot Plan / Recipe Card Widget" section (~line 497) only describes generic shared override-tinting behavior ("when a deliberate override is active, only that item tints amber") and never documents the old recipe-card-specific "source + delta" tag format (no "84 · 94°C −3°" example) — no edit needed.
- [x] 3.3 (found during implementation) `openspec/specs/recipe-aware-brew-settings/spec.md`'s "The live Shot Plan treats an active recipe's yield/temp as the baseline" requirement also had a stale cross-reference sentence claiming recipe cards "deliberately show the opposite decomposition" — added a delta spec for this capability too (`specs/recipe-aware-brew-settings/spec.md` in this change), correcting the sentence without changing the requirement's own substance.

## 4. Verification

- [x] 4.1 Build via the Qt Creator MCP (list_projects → match this worktree → build) to confirm the QML/C++ changes compile clean. Build succeeded: 0 errors, 0 warnings, 36s.
- [x] 4.2 Manually verify in the running app (ask the user to launch it — do not launch it locally per project convention): a recipe with a non-zero `tempOffsetC` and a yield differing from its profile's target shows plain resulting values with no tag/arrow/highlight on its management-page card and the wizard summary hero, matching what the live idle Shot Plan widget shows once that recipe is activated. Also re-check the "cards are immune to the loaded profile" case — a card's temperature must still reflect its own recipe's profile even while a different profile is currently loaded on the machine. Confirmed by user: "looks good."
