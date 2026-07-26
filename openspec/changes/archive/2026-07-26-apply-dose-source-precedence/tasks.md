## 1. The resolver

- [x] 1.1 Add `DoseOwner` (Recipe / Bag / Profile) and `doseOwner()` to `SettingsDye`, registered so QML can read it
- [x] 1.2 Cache `m_activeBagDoseG` in `SettingsDye::applyActiveBag` (it already reads `doseWeightG` locally); clear it when no bag is active, refresh it on the keep-fields path
- [x] 1.3 Push the recipe's dose from `MainController` **in the same call that sets the id** — `setActiveRecipe(id, doseG)`, not a separate `setActiveRecipeDose`. Two setters let the id advance while the cache lagged, which is exactly the failure the ladder exists to prevent; cleared by `setActiveRecipeId(-1)`, which `deactivateRecipe` calls
- [x] 1.3b Claim the rung from **every** path that learns the row, not only activation: the `recipeReady` handler serves the startup restore and the external-edit re-read, and without it the rung read empty for the whole session after a launch — a profile load would then take the dose *and* stamp its own value over the recipe's stored `doseG`
- [x] 1.3c Track per-rung RESOLUTION (`doseLadderResolved()`): an id is set synchronously, its row arrives from a worker, and in between an unresolved rung is indistinguishable from a source that designs no dose. Set false on selecting a different bag/recipe, true when the row lands or the source is cleared
- [x] 1.4 Keep both caches in step with a DIALED dose — the bag's via `setDyeBeanWeight`'s write-through, the recipe's via the `dyeBeanWeightChanged` stamp — so dialing a dose onto a source that had none makes it the owner
- [x] 1.4b Match each cache write to the guards of the persist it rides on: the bag's includes `m_bagStorage` (as `writeThroughToBag` does), the recipe's includes `m_recipeStorage` and the profile-less exclusion (as `stampActiveRecipe` and activation do). A rung standing on a value no row holds suppresses the profile's dose in favour of a dose nothing remembers, and a hot-water tea must not climb onto a shot-dose rung
- [x] 1.5 Unit-test the resolver against the real async bag path in `tst_coffeebags`: each rung, a doseless recipe falling through to the bag, a doseless bag falling through to the profile, a dialed dose arming a rung, and both caches clearing on deactivation

## 2. Gate the writers

- [x] 2.1 Gate `ProfileManager::loadProfile`'s recommended-dose write on `doseOwner() == Profile`, extracted into `applyRecommendedDoseIfProfileOwnsIt()`
- [x] 2.1b Resolve the ladder **inside the queued write, not where it is armed**, and require `doseLadderResolved()` there too. An arm-time check reads the window between a bag/recipe selection and its row arriving as "nobody else supplies a dose" — two quick taps (select bean, then load a profile) then land the profile's dose on the new bean and `writeThroughToBag` rewrites its stored dose. Deciding late also lets the title-mismatch watcher deactivate first, so switching profiles with a recipe active now correctly applies the new profile's dose instead of stranding the departed recipe's
- [x] 2.1c Log every declined write, naming the winning rung and the MCP tool that edits it — the suppression is deliberate and counterintuitive, and a field AI reading `debug_get_log` otherwise sees no evidence the ladder ran at all
- [x] 2.2 Also skip on the startup load — the live dose is already persisted. Kept alongside 2.1b rather than replaced by it: `m_startupLoadDone` flips at the end of the `ProfileManager` constructor, but `loadAutoLoadProfileIfNeeded()` fires later from QML, so the flag alone never covered the launch-time load a configured user actually gets
- [x] 2.3 Gate `SettingsDye::applyActiveBag`'s dose write on no recipe supplying one
- [x] 2.4 **Reversed from the plan:** KEEP the queued dispatch on the recipe-activation dose write — it must land after the id is set and `m_applyingRecipe` is cleared, or it stamps the dose onto the recipe being left. (The original justification, that it had to out-race the profile's armed write, no longer holds: 2.1b makes that write re-check on arrival rather than fire blind.)
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
- [x] 4.5 A call of nothing but retired spellings returns `success: false` and changes nothing. It used to report success with "Profile updated… call profiles_save", and the upload on the way out marked the profile modified — so a fully rejected edit dirtied the profile and invited the caller to save a change it never made
- [x] 4.6 Fold the retired-name caveat into `message` as `ignoredFields` already does; a client reading only `success` + `message` was being told a clean "Profile updated" for a call that dropped an argument
- [x] 4.7 Validate `dose` instead of coercing it: it is the one key read straight as a number, and a failed read yields 0, which CLEARS the recommendation. (A stringified number is safe — `normalizeArguments` coerces it off the schema type; the exposure is a value no parse can rescue.) Report a clamp in `adjustedFields` rather than echoing the caller's number back
- [x] 4.8 Drop the snake_case pair from the advanced read spread — with the camelCase pair now unconditional, `profiles_get_params` was showing four keys for two fields and inviting a reader to send back the spelling that no longer writes

## 5. Ladder tests

- [x] 5.1 Loading a profile with a recipe active leaves the dose untouched
- [x] 5.2 Loading a profile with only a bag active leaves the bag's dose in effect
- [x] 5.3 Loading a profile with neither active applies the profile's recommended dose
- [x] 5.4 A profile whose recommendation is not enabled changes nothing
- [x] 5.5 The startup load applies no dose at all
- [x] 5.6 Selecting a bag with a recipe active does not overwrite the recipe's dose (in `tst_coffeebags`, driving the real `bagReady` path)
- [x] 5.7 The bag's **stored row** survives a profile load, not just the session value. The first version of this test ran with no bag storage attached, so `writeThroughToBag` early-returned and there was no row to corrupt — it would have passed just as happily with the whole gate removed. Now uses a real migrated DB and reads `dose_weight_g` back
- [x] 5.8 A rung goes back to unresolved when a DIFFERENT source is selected. The first selection cannot prove this — the rung starts unresolved, so a missing reset is invisible until a resolved rung has to go back
- [x] 5.9 `setActiveBagKeepFields` (the shot-replay path) still resolves the rung without applying the bag's dose — previously exercised by no test in the suite at all
- [x] 5.10 Brew Settings leaves the profile clean: `activateBrewWithOverridesSetsSettings` now asserts the recommended dose is unchanged and the profile is not modified, so "completing the ladder" with a third write target fails a test instead of quietly dirtying the profile on every dial-in

## 6. Verify and land

- [x] 6.1 Full suite green through `mcp__qtcreator__run_tests` (scope `all`): 101 passed, 0 failures, 0 warnings
- [x] 6.2 Falsified every gate individually — each one removed on its own fails at least one new test:
  - `loadProfile` doseOwner gate → `loadingAProfileDoesNotOverwriteAnActiveRecipesDose` (21 vs 19), `loadingAProfileDoesNotOverwriteAnActiveBagsStoredDose` (21 vs 20)
  - `loadProfile` startup skip → `theStartupLoadDoesNotApplyTheProfilesDose` (21 vs 19.5)
  - `applyActiveBag` recipe gate → `settingsDyeDoseLadder` (20 vs 19)
  - `setDyeBeanWeight` bag-cache follow → `settingsDyeDoseLadder` (owner reads Profile, not Bag)
  - bag-rung un-resolve on switch → `settingsDyeDoseLadderIsUnresolvedUntilTheRowLands`. Verified by re-running with the line deleted: 1 failed. The FIRST version of that test did not catch it (the rung starts unresolved, so the mutation was masked) — the two-bag switch was added specifically because the mutation survived
- [x] 6.2b `MainController` still has no test fixture, so the `recipeReady` seeding and the stamp listener's rung follow are not covered directly. Narrowed rather than accepted: the id/dose pairing now lives in `setActiveRecipe`, which IS covered, so the untested part is which value gets passed rather than whether the pair can drift. Live MCP check below still owed
- [x] 6.2c Docs: `docs/CLAUDE_MD/MCP_SERVER.md` still documented `recommended_dose` / `has_recommended_dose` as "directly settable on the advanced branch" — the exact behaviour this change removes. Rewritten with the retirement, the validation rules and the reporting contract
- [x] 6.2d Fixed the isolation bug in `tst_coffeebags`' dye tests (7 sites, 2 of them added by this change): a bare `QSettings` cleared a store `SettingsDye` never reads while writing to the developer's real preferences. Now goes through `Settings::testQSettingsPath()`, so the tests actually start clean. Added a `cleanup()` to `tst_profilemanager` for the same reason — the dose-ladder tests restored the active ids on their last line, which a failed assertion skips
- [x] 6.3 Exercised against the running macOS app over MCP, with a restart in the middle. All against real rows, state restored afterwards:
  - **Bag rung + the persistent damage.** Bag (18 g) active, loaded a profile recommending 21 g → live dose stayed 18 **and** the bag's stored `doseWeightG` stayed 18. The log carried the advisory naming the bag and `bag_update` as the remedy
  - **Recipe rung across a restart** — the path with no unit coverage. Recipe active (18 g), app quit and relaunched, then an external `bag_update` set the bag to 25 g: live dose stayed 18 and the recipe's stored `doseG` stayed 18, while the bag row took the 25. Pre-fix the restored rung was empty, so the bag's 25 would have applied and the stamp would have rewritten the recipe's `doseG`
  - **Deciding late, observed.** The first attempt loaded a profile with a DIFFERENT title, which trips the title-mismatch watcher: the recipe deactivated mid-load and the profile's 21 g correctly applied, with the recipe's stored `doseG` untouched. Under the arm-time check this is the case that stranded the departed recipe's dose
  - **MCP surface**: retired spellings → `success: false`, both named, replacement named, `modified: false` after; `dose: 150` → `adjustedNote` naming the clamp; `profiles_get_params` on an advanced profile reports `recommendedDoseG` / `hasRecommendedDose` with no snake_case duplicates in the spread
- [ ] 6.4 Visually confirm the two repaired Dose sliders (Recipe Editor, Simple Profile Editor) move and persist — still unconfirmed from the previous change
- [x] 6.5 Wiki manual updated and PUSHED (`Kulitorum/Decenza.wiki` @ 9a0bd99): Manual's "Recommended Dose" and the matching FAQ entry both said the profile's dose applies whenever the profile is selected — now they carry the ladder, why it exists, and where to actually change a dose; plus a new FAQ entry for the symptom users hit first ("I loaded a profile with a recommended dose and my dose didn't change")
- [ ] 6.6 `openspec archive apply-dose-source-precedence` as the last commit on the branch
