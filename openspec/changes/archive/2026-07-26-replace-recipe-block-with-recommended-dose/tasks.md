## 1. Parity checker: excuse the deliberate removal

- [x] 1.1 Add a deliberately-dropped-key set beside `nonZeroDefaultKeys()` in `profile.cpp` containing `recipe`, and excuse it in the KEY-LOST branch of `collectParityErrors`
- [x] 1.2 Verify all four consumers now accept a stripped profile: `upgradeStoredEncoding` (`profilemanager.cpp:1318`), the temperature-repair persist (`:1504`), `migrateProfileFormat` (`:3288`), and `tools/profile_sync --rewrite-format`
- [x] 1.3 Confirm any key other than `recipe` is still reported as lost

## 2. Serializer: stop writing the block

- [x] 2.1 Remove the `obj["recipe"] = recipeJson()` emit from `Profile::toJsonObject()`
- [x] 2.2 Extend the `kKnownProfileKeys` comment with the inverse rule — a key we have STOPPED modelling must stay listed, because the list is the passthrough's exclusion list and delisting would preserve stale blocks forever
- [x] 2.3 Remove `Profile::recipeJson()` if it has no remaining caller

## 3. Reader: strip on sight, promote a set dose

- [x] 3.1 Replace the `RecipeParams::fromJson` read in `Profile::fromJson()` with a dose-only promotion: a `recipe.dose` differing from the default sets `recommended_dose` + `has_recommended_dose`, never over an existing explicit recommendation
- [x] 3.2 Set a "block was stripped" flag when a block was present, mirroring `m_espressoTemperatureHealed`
- [x] 3.3 Persist the strip once in `ProfileManager::loadProfile`, alongside the existing temperature-repair write-back; a profile that cannot be written still loads, with the failure reported not raised
- [x] 3.4 Confirm `m_hasRecipeParams` is no longer set from JSON, and that the only remaining reader of `hasRecipeParams()` is `tests/tst_tclimport.cpp`

## 4. Dead code the flag change strands

- [x] 4.1 Remove the `regenerateFromRecipe()` safety net in `parseVisualizerProfile` (`visualizerimporter.cpp:654`) and the now-false comment above it, leaving the `isValid()` / `steps().isEmpty()` rejection in both callers

## 5. No-op-save check

- [x] 5.1 In `uploadRecipeProfile`, source `oldRecipe` from `RecipeAnalyzer::extractRecipeParams(m_currentProfile)` **only** for `dflow`/`aflow`; advanced profiles share that branch and MUST keep comparing against `m_currentProfile.recipeParams()`, or their target-weight edits silently stop applying
- [x] 5.2 Update the `frameAffectingFieldsEqual` tripwire comment in `recipeparams.h` to describe the new comparison basis and the advanced-profile carve-out

## 6. Remove the dead dose field

- [x] 6.1 Remove `RecipeParams::dose` and its toJson/fromJson/toVariantMap/fromVariantMap/clamp/validate handling
- [x] 6.2 Add a real `dose` handler to `profiles_edit_params`, running BEFORE the `currentParams` membership loop so it does not land in `ignoredKeys`; write `setRecommendedDose` + `setHasRecommendedDose`, clamping to the `[0, 100]` bound the removed `RecipeParams::clamp()` provided
- [x] 6.3 Report `recommendedDoseG` **with** `hasRecommendedDose` from `profiles_get_params`, so the 18 g default cannot read as a recommendation
- [x] 6.4 Note that `recommended_dose` / `has_recommended_dose` remain separately settable on the advanced branch, and make the two paths consistent or document why they differ

## 7. One-time upgrade

- [x] 7.1 Replace `ProfileManager::migrateRecipeFrames()` with a pass that strips `recipe` and promotes a set dose across the user, downloaded and SAF stores, skipping `_current.json`
- [x] 7.2 Move it BEFORE `migrateProfileFormat` in the constructor, so the legacy-format parity gate is not defeated by an ungated rewrite of the same files
- [x] 7.3 Gate it on a new settings flag, retiring `recipe_frames_migrated`, so an install that never ran the old pass is not skipped
- [x] 7.4 Record in the code that retiring the old pass is a deliberate behaviour change: an install that never ran it keeps its frames rather than having them regenerated from an untrustworthy block
- [x] 7.5 Log per-profile outcomes by name — migrated, skipped, failed — and leave the original file intact on a write failure

## 8. Shipped built-ins

- [x] 8.1 Regenerate the 8 built-ins through `profile_sync --rewrite-format` rather than editing them by hand — they mirror de1app sources, so the tool is the only sanctioned writer. Confirmed each already has `recommended_dose: 18.0`, so nothing is lost
- [x] 8.2 Confirm no other shipped profile carries a block, and that the 4 legacy keys (`fillPressure`, `fillFlow`, `fillTimeout`, `infuseEnabled`) leave with it

## 9. Tests

- [x] 9.1 Assert no serialized profile emits `recipe`, and that loading a file with one and saving drops it
- [x] 9.2 Assert a profile carrying a block is stripped AND written back on load, and that a second load performs no write
- [x] 9.3 Assert the parity checker excuses `recipe` and still reports every other lost key
- [x] 9.4 Assert BLE header and frames are byte-identical with and without a stored block, for a D-Flow and an A-Flow profile
- [x] 9.5 Assert a `settings_2a`/`2b` profile loses its block with every scalar and its generated frames unchanged
- [x] 9.6 Cover dose promotion: non-default promoted, default not promoted, explicit recommendation wins
- [x] 9.7 Cover the no-op-save guard three ways — an unedited D-Flow save leaves frames untouched (including an off-formula `exit_pressure_over`), an edited save regenerates, and an advanced profile's target-weight edit still applies
- [x] 9.8 Cover the upgrade: blocks removed, every other key unchanged, runs once, a legacy profile converted only through the audited path, write failure leaves the file intact
- [x] 9.9 Cover the MCP surface: `dose` accepted and not reported ignored, `recommendedDoseG` accompanied by `hasRecommendedDose`
- [x] 9.10 Reconcile `tests/tst_builtinprofileformat.cpp:226-236`, which already asserts unconditional dose promotion, with the three-way rule
- [x] 9.11 Update the remaining test files that pin block presence or content

## 10. Documentation

- [x] 10.1 Update `docs/CLAUDE_MD/RECIPE_PROFILES.md` — the JSON format section documenting the block, the Decenza-extensions list, and the `profile_sync --rewrite-format` audit note
- [x] 10.2 Update `docs/CLAUDE_MD/MCP_SERVER.md` for the `dose` / `recommendedDoseG` / `hasRecommendedDose` surface
- [x] 10.3 Record the sunset in the code: which pieces are transitional and deletable next release, and why the `kKnownProfileKeys` entry is not among them

## 11. Verify

- [x] 11.1 Run the full suite via `mcp__qtcreator__run_tests` (scope `all`) and clear every warning
- [x] 11.2 Re-run the D-Flow/A-Flow edit matrix and confirm it is still at zero divergences
- [ ] 11.3 Confirm no profile in any store carries a block after a run — built-ins, user, downloaded, SAF
- [ ] 11.4 Archive the change with `openspec archive` as the last commit on the branch
