# Tasks

## 1. Detector struct + Arm 2 signal

- [ ] 1.1 Add `bool verifiedClean = false;` to `ShotAnalysis::GrindCheck` and a matching `bool grindVerifiedClean` to `DetectorResults`.
- [ ] 1.2 In `analyzeFlowVsGoal`, after the existing `if (flowSamples >= 5 && pressurizedDuration >= CHOKED_DURATION_MIN_SEC) { ... }` block: when neither `flowChoked` nor `yieldShortfall` fires, set `result.hasData = true; result.verifiedClean = true; result.sampleCount = flowSamples;`.
- [ ] 1.3 Confirm the projection in `src/history/shotbadgeprojection.h` does NOT change — the badge boolean still requires `chokedPuck || yieldOvershoot || |delta| > threshold`. Add a unit test that a verified-clean result projects to `grindIssueDetected = false`.

## 2. Coverage enum

- [ ] 2.1 Add a `grindCoverage` field on `DetectorResults` taking values `"verified"`, `"notAnalyzable"`, `"skipped"`, or absent (when not espresso). Populate inside `analyzeShot` from `GrindCheck.{hasData, verifiedClean, skipped}`.
- [ ] 2.2 Emit `grindCoverage` in `ShotHistoryStorage::convertShotRecord`'s structured `detectorResults` output.
- [ ] 2.3 Document the field in `docs/CLAUDE_MD/MCP_SERVER.md` "Shot Detector Outputs".

## 3. Summary lines + verdict prose

- [ ] 3.1 In `analyzeShot`, when the grind block does NOT fire any caution/warning AND `grind.verifiedClean == true`, append a `[good]` line with text "Grind tracked goal during pour" and severity `good`.
- [ ] 3.2 When the grind block does NOT fire AND `grind.hasData == false && grind.skipped == false`, append an `[observation]` line "Could not analyze grind on this profile shape — check flow trend, channeling, and taste instead."
- [ ] 3.3 In the verdict cascade, when the only deviation from a fully-clean read is `grindCoverage == "notAnalyzable"` and no warnings/cautions fire, replace `"Verdict: Clean shot. Puck held well."` with `"Verdict: Clean shot, but grind could not be evaluated for this profile shape."`. Otherwise verdict unchanged.

## 4. Tests

- [ ] 4.1 `tst_shotanalysis::grindCoverage_verifiedClean_passesPositive` — synthesize a healthy 30 s shot with steady pressure ≥ 4 bar, mean flow 2 mL/s, target_weight=36 g, final=36 g; assert `verifiedClean=true`, `hasData=true`, `chokedPuck=false`, line list contains `[good] Grind tracked goal during pour`.
- [ ] 4.2 `tst_shotanalysis::grindCoverage_notAnalyzable_doesNotLieClean` — synthesize a 25 s pour with no pressure-mode phase markers and pressure that never sustains 4 bar; assert `hasData=false`, `verifiedClean=false`, line list contains `[observation] Could not analyze grind on this profile shape...`, verdict reads "...grind could not be evaluated for this profile shape."
- [ ] 4.3 `tst_shotanalysis::grindCoverage_choked_unchanged` — re-run an existing choked-puck fixture; verify `chokedPuck=true`, `verifiedClean=false`, badge fires identically to before.
- [ ] 4.4 `tst_shotanalysis::grindCoverage_pourTruncatedSuppresses` — verify the cascade: when `pourTruncated` fires, the grind block is skipped entirely (no verified-clean line, no notAnalyzable line, no grindCoverage emission).
- [ ] 4.5 `tst_shotanalysis::grindCoverage_yieldOvershoot_unchanged` — verify yield-overshoot still fires the same way; `verifiedClean=false`.
- [ ] 4.6 `tst_shotanalysis::badgeProjection_verifiedClean_doesNotFireGrindBadge` — `DetectorResults` with `grindHasData=true && grindVerifiedClean=true` must project `grindIssueDetected=false`.
- [ ] 4.7 Update `tests/data/shots/manifest.json` for any fixture whose summary text changes from `"Clean shot. Puck held well."` to the new "verified" or "not analyzable" wording.

## 5. Docs

- [ ] 5.1 Update `docs/SHOT_REVIEW.md` §2.2 (grind detector internals) to describe the verified-clean branch on Arm 2 and the `grindCoverage` field.
- [ ] 5.2 Update `docs/SHOT_REVIEW.md` §3 (Shot Summary dialog) to list the two new line types in the "Observations emitted" enumeration.
- [ ] 5.3 Update `docs/SHOT_REVIEW.md` §4 (Persistence semantics) — note that `grindCoverage` is recomputed on every load just like the other detector outputs; no DB column.

## 6. Validation

- [ ] 6.1 `openspec validate add-grind-detector-coverage-signal --strict --no-interactive` passes.
- [ ] 6.2 Build (`mcp__qtcreator__build`) clean.
- [ ] 6.3 `tst_shotanalysis` and `shot_corpus_regression` ctest targets pass.
- [ ] 6.4 Manual smoke: open a Malabar shot in Shot Detail, confirm the new `[good]` line appears; open an A-Flow short-pressurized shot, confirm the `[observation]` line + adjusted verdict appear.
