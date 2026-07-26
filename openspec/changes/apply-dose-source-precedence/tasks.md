## 1. The resolver

- [x] 1.1 Add `DoseOwner` (Recipe / Bag / Profile) and `doseOwner()` to `SettingsDye`, registered so QML can read it
- [x] 1.2 Cache `m_activeBagDoseG` in `SettingsDye::applyActiveBag` (it already reads `doseWeightG` locally); clear it when no bag is active, refresh it on the keep-fields path
- [x] 1.3 Push `m_activeRecipeDoseG` from `MainController` alongside `setActiveRecipeId`; cleared by `setActiveRecipeId(-1)`, which `deactivateRecipe` calls
- [x] 1.4 Keep both caches in step with a DIALED dose — the bag's via `setDyeBeanWeight`'s write-through, the recipe's via the `dyeBeanWeightChanged` stamp — so dialing a dose onto a source that had none makes it the owner
- [x] 1.5 Unit-test the resolver against the real async bag path in `tst_coffeebags`: each rung, a doseless recipe falling through to the bag, a doseless bag falling through to the profile, a dialed dose arming a rung, and both caches clearing on deactivation

## 2. Gate the writers

- [x] 2.1 Gate `ProfileManager::loadProfile`'s recommended-dose write on `doseOwner() == Profile`, extracted into `applyRecommendedDoseIfProfileOwnsIt()`
- [x] 2.2 Skip that write entirely on the startup load — the ladder cannot be answered while the bag and recipe rows are still loading, and the live dose is already persisted
- [x] 2.3 Gate `SettingsDye::applyActiveBag`'s dose write on no recipe supplying one
- [x] 2.4 **Reversed from the plan:** KEEP the queued dispatch on the recipe-activation dose write. `applyActivatedRecipe` calls `loadProfile` at its top, when `activeRecipeId` is still the previous recipe's, so the profile's deferred write is already armed and a synchronous recipe write would be clobbered. Comment at the site says which collision each mechanism covers
- [x] 2.5 Correct the shot / auto-favorite replay's ordering comment — it is outside the ladder and keeps its queued write for the same reason

## 3. Editing

- [x] 3.1 **Dropped, confirmed with the user:** no profile write target from Brew Settings. `setCurrentProfileRecommendedDose` marks the profile modified and `activateBrewWithOverrides` runs on every OK, so a dose nudge would dirty the loaded profile. Reason recorded at the site and in `design.md`
- [x] 3.2 Spec amended to match — the "edit persists on the profile" scenario becomes "does not dirty the profile"
- [x] 3.3 Verified the recipe-switch re-seed in `BrewDialog.qml` writes only local `root.*` values — `seedFromCurrentState()` touches no `Settings`, and the dialog's only `Settings.dye` writes are grind/RPM in the OK handler

## 4. MCP

- [x] 4.1 Remove `recommended_dose` / `has_recommended_dose` from the schema AND strip them explicitly — nothing validates against the schema, so the advanced branch's map loop would otherwise still apply them
- [x] 4.2 Delete the conflict rule and `conflictedFields`; report the retired names as `retiredFields` with a note naming `dose` as the replacement
- [x] 4.3 Report `recommendedDoseG` / `hasRecommendedDose` on every editor type — the asymmetry existed only because the advanced branch accepted the raw names
- [x] 4.4 Tests: `dose` on an advanced profile (owed from the previous change's review), a retired spelling reported not applied, `dose` still applying alongside one, and the pair reported on advanced

## 5. Ladder tests

- [x] 5.1 Loading a profile with a recipe active leaves the dose untouched
- [x] 5.2 Loading a profile with only a bag active leaves the bag's dose in effect
- [x] 5.3 Loading a profile with neither active applies the profile's recommended dose
- [x] 5.4 A profile whose recommendation is not enabled changes nothing
- [x] 5.5 The startup load applies no dose at all
- [x] 5.6 Selecting a bag with a recipe active does not overwrite the recipe's dose (in `tst_coffeebags`, driving the real `bagReady` path)

## 6. Verify and land

- [x] 6.1 Full suite green through `mcp__qtcreator__run_tests` (scope `all`): 101 passed, 0 warnings
- [x] 6.2 Falsified every gate individually — each one removed on its own fails at least one new test:
  - `loadProfile` doseOwner gate → `loadingAProfileDoesNotOverwriteAnActiveRecipesDose` (21 vs 19), `loadingAProfileDoesNotOverwriteAnActiveBagsDose` (21 vs 20)
  - `loadProfile` startup skip → `theStartupLoadDoesNotApplyTheProfilesDose` (21 vs 19.5)
  - `applyActiveBag` recipe gate → `settingsDyeDoseLadder` (20 vs 19)
  - `setDyeBeanWeight` bag-cache follow → `settingsDyeDoseLadder` (owner reads Profile, not Bag)
- [ ] 6.2b NOT covered by tests: `setActiveRecipeDose` in `applyActivatedRecipe` and the recipe-cache follow in the `dyeBeanWeightChanged` stamp listener. Both live in `MainController`, which no fixture wires up. Covered by the live MCP check below instead
- [ ] 6.3 Exercise the ladder against the running app over MCP: activate a recipe, switch profiles, read the recipe's stored dose back
- [ ] 6.4 Visually confirm the two repaired Dose sliders (Recipe Editor, Simple Profile Editor) move and persist — still unconfirmed from the previous change
- [ ] 6.5 Update the wiki manual where it describes which dose a shot uses
- [ ] 6.6 `openspec archive apply-dose-source-precedence` as the last commit on the branch
