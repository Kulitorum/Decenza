## 1. Signature plumbing

- [x] 1.1 Add `bool profileKbResolved = true` parameter to `ShotAnalysis::analyzeFlowVsGoal` in `src/ai/shotanalysis.h` and `src/ai/shotanalysis.cpp` (default at the declaration; ordered after `finalWeightG` to keep positional callers stable).
- [x] 1.2 Add `bool profileKbResolved = true` parameter to `ShotAnalysis::analyzeShot` in `src/ai/shotanalysis.h` and `src/ai/shotanalysis.cpp` (default at the declaration; ordered after `expectedFrameCount` and before `expertBand` to fit the existing optional-trailing pattern, OR appended after `expertBand` if the optional-last invariant is load-bearing — pick whichever keeps every existing call site compiling without reorder).
- [x] 1.3 Thread `profileKbResolved` from `analyzeShot` into its single call of `analyzeFlowVsGoal` (around shotanalysis.cpp:874).

## 2. Arm 1 gating

- [x] 2.1 In `analyzeFlowVsGoal`, gate the flow-mode-range builder block (the `if (!phases.isEmpty())` block at shotanalysis.cpp:368-405) on `profileKbResolved`. When `false`, leave `flowModeRanges` empty.
- [x] 2.2 Confirm the existing averaging block (shotanalysis.cpp:409-459) already early-exits when `flowModeRanges.isEmpty()`. No additional guard needed there; this is a verify-and-comment step, not a code change.
- [x] 2.3 Confirm the existing `skipped = true` early-return path (shotanalysis.cpp:333-336) is NOT touched — that path is reserved for non-espresso beverages and the `grind_check_skip` flag, which project to `grindCoverage = "skipped"` not `"notAnalyzable"`. The new gate must leave `skipped = false` so Arm 2 still runs and the projection falls into `notAnalyzable` (when Arm 2 has no data) or `verified` (when it does).
- [x] 2.4 Add a brief comment on the new gate at the call site explaining why `profileKbResolved = false` widens the population that hits the existing `notAnalyzable` coverage instead of inventing a new state.

## 3. Call-site propagation

- [x] 3.1 `src/ai/shotsummarizer.cpp:145` (`ShotSummarizer::summarize(ShotDataModel*)` live path): derive `const bool profileKbResolved = !summary.profileKbId.isEmpty();` and pass to `analyzeShot`.
- [x] 3.2 `src/ai/shotsummarizer.cpp:457` (`summarizeFromHistory`): same derivation from `summary.profileKbId`, pass to `analyzeShot`.
- [x] 3.3 `src/history/shothistorystorage.cpp:1122` (`saveShot`): derive from `data.profileKbId`, pass to `analyzeShot`.
- [x] 3.4 `src/history/shothistorystorage.cpp:1837` (`loadShotRecordStatic`): derive from `record.profileKbId`, pass to `analyzeShot`.
- [x] 3.5 `src/history/shothistorystorage_serialize.cpp:121` (`convertShotRecord`): derive from `record.profileKbId`, pass to `analyzeShot`.
- [x] 3.6 Re-grep for any additional `ShotAnalysis::analyzeShot(` and `ShotAnalysis::analyzeFlowVsGoal(` call sites outside tests; pass the derived flag (or leave defaulting to `true` only when the call is in a context that genuinely has no `profileKbId` and intends "act as if resolved" — comment why).

## 4. Tests

- [x] 4.1 Add `tst_shotanalysis.cpp` scenario `grindArm1_skippedWhenKbUnresolved_armTwoSilentProjectsNotAnalyzable`: build a flow-mode-only shot whose Arm 1 would otherwise fire (mean delta well past 0.4), call `analyzeShot` with `profileKbResolved = false`, assert `GrindCheck.sampleCount == 0`, `GrindCheck.delta == 0`, `GrindCheck.hasData == false`, `DetectorResults.grindCoverage == "notAnalyzable"`, `grindIssueDetected == false`, and the summary line "Could not analyze grind on this profile shape" is present.
- [x] 4.2 Add scenario `grindArm1_runsWhenKbResolved_preservesPreChangeBehaviour`: same shot as 4.1, but `profileKbResolved = true`. Assert Arm 1 runs and produces the same `delta` and badge outcome it would have on the pre-change build (capture the values from a parallel pre-change run during development; assert exact numbers).
- [x] 4.3 Add scenario `grindArm1_skipped_armTwoYieldShortfallStillFires`: shot with `flowSamples >= 5`, `targetWeightG = 36`, `finalWeightG = 22`, `profileKbResolved = false`. Assert `chokedPuck == true`, `grindCoverage == "verified"`, `grindIssueDetected == true`, and the yield-shortfall warning line fires.
- [x] 4.4 Add scenario `grindArm1_defaultsTrueForDirectCallers`: invoke `analyzeShot` without the new parameter, assert Arm 1 runs (sample count > 0 on a shot designed to produce qualifying samples).
- [x] 4.5 Run the full `tst_shotanalysis` suite via Qt Creator MCP (`mcp__qtcreator__run_tests` scoped to that test class); confirm pre-existing scenarios still pass without modification (the `=true` default preserves their behaviour).

## 5. Regression-corpus diff

- [x] 5.1 Build the `shot_eval` target via Qt Creator MCP (`mcp__qtcreator__build`).
- [x] 5.2 Run `shot_eval --json tests/data/shots/*.json > /tmp/before.json` on the current build (record the pre-change baseline).
- [x] 5.3 Apply the code changes from sections 1-3.
- [x] 5.4 Rebuild `shot_eval`, run `shot_eval --json tests/data/shots/*.json > /tmp/after.json`.
- [x] 5.5 Diff `before.json` vs `after.json`. Expected: zero shifts on corpus shots whose profile titles resolve via KB (every Adaptive, Cremina, Damian, Londinium, 80s, Malabar, Rao, Classic Italian, E61, Extractamundo entry). Any shift on a non-resolved corpus shot is a deliberate audit point — record the verdict change in the PR body with a one-line justification.
- [x] 5.6 If any KB-resolved corpus shot shifts, that's a regression — investigate and fix before merge.

## 6. Docs

- [x] 6.1 Update `docs/SHOT_REVIEW.md` §2.2 "Grind issue": document the new `profileKbResolved` precondition for Arm 1, and the resulting `notAnalyzable` projection on KB-unresolved profiles. Cross-reference openspec change `skip-grind-arm1-when-kb-unresolved`.
- [x] 6.2 Confirm no update needed to `docs/CLAUDE_MD/` reference docs (the change is internal to the shot-analysis pipeline; no contributor-facing convention changes).

## 7. Land

- [ ] 7.1 Run full test suite via Qt Creator MCP (`mcp__qtcreator__run_tests`); expect 0 failures, 0 new warnings.
- [ ] 7.2 Run `tools/validate_kb.py` (no KB content change, but cheap sanity).
- [ ] 7.3 Open PR on a feature branch (e.g. `feat/skip-grind-arm1-when-kb-unresolved`) with proposal/design/spec excerpts in the body, the corpus-diff result from section 5, and a closing-keyword link to the openspec change.
- [ ] 7.4 Run `/pr-review-toolkit:review-pr` on the PR; address any issues it surfaces above the noise floor.
- [ ] 7.5 After approval/sign-off, `/merge-pr` to squash-merge and delete the branch.
- [ ] 7.6 Run `/openspec-archive-change skip-grind-arm1-when-kb-unresolved` (or the openspec CLI equivalent) once the PR has landed on `main`.
