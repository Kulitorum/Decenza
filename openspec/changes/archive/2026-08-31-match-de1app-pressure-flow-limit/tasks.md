# Tasks

## 1. Frame naming
- [x] 1.1 Add `ProfileFrame::kForcedRiseName` (`"forced rise"`) alongside the existing
      `kForcedRiseWithoutLimitName`, plus `ProfileFrame::isForcedRiseName()` accepting both.
- [x] 1.2 Route `Profile::countPreinfuseFramesWithForcedRise()` through the predicate.

## 2. The default limit
- [x] 2.1 Add `Profile::kDefaultPressureFlowLimit` (8.0 mL/s) and `Profile::kMaxSettableFlow`
      (20.0 mL/s), and `Profile::applyDefaultPressureFlowLimit()` returning whether it changed
      anything.
- [x] 2.2 Expose both constants to QML as CONSTANT properties on `ProfileManager` so no QML file
      writes the numbers itself.

## 3. Generation
- [x] 3.1 `Profile::generatePressureProfileFrames()`: name the rise frames `"forced rise"` and
      attach the limiter when `maximumFlow > 0`, matching de1app's `pressure_to_advanced_list`.
- [x] 3.2 Same for `RecipeGenerator`'s pressure path.

## 4. Application points
- [x] 4.1 Call `applyDefaultPressureFlowLimit()` in `ProfileManager::loadProfile()`, after every
      on-disk write in that function so the normalization can never be persisted by it.
- [x] 4.2 Call it in `ProfileManager::loadProfileFromJson()`.
- [x] 4.3 Do NOT call it after an editor save. de1app caps at load only, so its plugins write
      uncapped frames on save; `tst_recipeeditorapppath`'s plugin goldens caught the attempt and
      are the oracle. A saved profile is capped the next time it is loaded, in both apps.

## 5. Editors
- [x] 5.1 `ProfileEditorPage`: limiter control floors at 0.1 and drops "off" for a pressure step;
      flow goal and flow limit ceilings to 20.
- [x] 5.2 `ProfileEditorPage`: switching a step to `pressure` snaps a 0 limiter to the default.
- [x] 5.3 `SimpleProfileEditorPage`: same floor/ceiling/"off" treatment for the Pressure editor's
      flow-limit slider.
- [x] 5.4 `RecipeEditorPage`: pour-flow ceiling to 20.
- [x] 5.5 `RecipeParams::clamp()`: flows and `limiterValue` to 20.

## 6. Tests
- [x] 6.1 `tst_profile`: an unlimited pressure step is capped; an explicit limit and a flow step
      are left alone; a `settings_2a` profile's `maximum_flow` defaults and reaches the generated
      frames.
- [x] 6.2 `tst_profile`: forced-rise frames are counted under both names.
- [x] 6.3 Run the full suite through Qt Creator MCP.

## 6b. Oracles regenerated (de1app bump)
- [x] 6b.1 `tools/gen_de1app_pack_corpus.py` against the updated mirror — 4 goldens moved, all
      settings_2a profiles gaining the rise frame's extension frame.
- [x] 6b.2 `tools/de1app_edit_oracle.tcl` now sources `profile.tcl` and calls the real
      `::profile::apply_default_flow_limit_to_pressure_steps`, so the edit-matrix oracle models
      de1app's load path instead of skipping it; `tools/gen_edit_matrix.py` passes it the de1plus
      dir. 107 goldens regenerated, limiter-only change.
- [x] 6b.3 `profile_sync --sync` — 4 built-in JSONs pick up the rise frame's limiter and its new
      name. No unrelated drift.

## 7. Docs
- [x] 7.1 Wiki manual: pressure steps always have a flow limit (short — 3-5 sentences).
- [x] 7.2 `docs/CLAUDE_MD/RECIPE_PROFILES.md`: record the default and where it is applied.
- [x] 7.3 `openspec archive match-de1app-pressure-flow-limit` as the last commit on the branch.
