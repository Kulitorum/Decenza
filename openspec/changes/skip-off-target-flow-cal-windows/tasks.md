## 1. Skip instead of re-route

- [x] 1.1 In `MainController::computeAutoFlowCalibration()` (`src/controllers/maincontroller.cpp`, the block at ~`:3357` beginning "Achieved-flow deviation check"), change the positive-check action from setting `reclassifiedAsPressureCapped = true` to returning early — the shot contributes no ideal.
- [x] 1.2 Delete `reclassifiedAsPressureCapped` and the `formulaModeLabel` arm that reports "pressure (reclassified, missed flow target)". `useFlowFormula` collapses back to `isFlowProfile`; keep the single combined `if/else` for the ratio guard and formula selection, and keep the comment explaining why they must stay in one branch.
- [x] 1.3 Leave `autoFlowCalWindowTargetCheck()` and `kAutoFlowCalDeviationThreshold` (`src/controllers/autoflowcalclassifier.{h,cpp}`) untouched — same check, same 10%, same undershoot-only asymmetry. Only the caller's response changes.
- [x] 1.4 Update the comment block above the check: it currently explains why a capped window is routed to the achieved-flow formula. Replace with why it is skipped — a capped window measures the pump model at an operating point the profile never pours at, and w/mf varies up to 48% across the flow range on one machine at fixed C (cite `design.md`).
- [x] 1.5 Rewrite the decision log line to report a skip, keeping measured flow, target flow, deviation and threshold. `maincontroller.cpp` is in `MARKER_ONLY_GLOBS` in `scripts/check_log_markers.py`, so rules 2 and 5 apply (no hand-typed registered marker, no unregistered bracketed token) while rule 1's bare-`qDebug` ban does not. A plain `qDebug()` matching the surrounding "Auto flow cal: …" style is therefore correct — no bracketed prefix, no helper.
- [x] 1.6 Verify by inspection that the pressure branch is reached only by genuinely pressure-classified windows after this change, and that no other caller depends on `reclassifiedAsPressureCapped`.

## 2. Migration

- [x] 2.1 Add the v5 block inline in the `Settings` constructor (`src/core/settings.cpp`), immediately after the `calibration/v4AchievedFlowFormulaReset` block at ~`:288`, gated on a new key `calibration/v5SkipOffTargetReset`.
- [x] 2.2 Clear pending batches only, via `SettingsCalibration::clearAllFlowCalPendingIdeals()`. Do NOT call `resetAllProfileFlowCalibrations()` and do NOT touch `flowCalibrationMultiplier` — see `design.md` for why this defect is per-window rather than systemic.
- [x] 2.3 Commit the flag through `commitFlowCalMigrationFlag()` and log in the style of the adjacent v2/v3/v4 lines.

## 3. Tests

- [x] 3.1 Existing coverage surveyed first. `tests/tst_autoflowcal.cpp:360-430` covers `autoFlowCalWindowTargetCheck()` as a PREDICATE — undershoot sets `missedTarget`, overshoot never does, threshold boundary, non-positive target, plus `issue1823Batch_splitsTargetMetFromTargetMissed()` replaying the reporter's real 8-window batch. The predicate is unchanged by this change, so every one of those stays green and stays correct.
- [x] 3.2 Updated those slots' comments and two slot names (`..._notReclassified` → `..._notSkipped`, `..._neverReclassified` → `..._neverSkipped`) so they describe the skip rather than the re-route. A comment asserting the old contract is worse than no comment.
- [x] 3.3 Recorded the real coverage gap in the test file itself rather than papering over it: what the CALLER does with a positive result lives in `MainController::computeAutoFlowCalibration()`, which no test constructs. There is no `MainController` harness, and building one to reach this branch is the fault-injection shape `CLAUDE.md` treats as a stop sign. That gap is why the re-route could be swapped for a skip with the whole suite green, and it is named in the test file so the next reader does not mistake green for covered.
- [ ] 3.4 Migration coverage: `clearAllFlowCalPendingIdeals_clearsBatchesButNotMultipliers()` already covers the primitive v5 calls. The migration's own one-time gating has no test for v2/v3/v4 either, so v5 adds no new gap — decide whether to close it for all four at once or leave as-is, and say which in the PR.
- [x] 3.5 Behavioural evidence stands in `evidence/` rather than in the suite: an offline replay of the real algorithm over 75 shots from three machines, validated by reproducing this repo's own logged batch update (`median 0.8407, C 0.9183 → 0.8795`). Re-run it after the build (task 5.3) and confirm the shipped code agrees with the simulated candidate.

## 4. Documentation

- [x] 4.1 `docs/CLAUDE_MD/AUTO_FLOW_CALIBRATION.md` — rewrite the "Achieved-flow deviation check (v4)" paragraph in "Window-level Classification": the check now skips the window. Keep the undershoot-only rationale, which is unchanged.
- [x] 4.2 Same file — add a short "v5 Migration" note beside the v2/v3/v4 write-ups, stating the pending-batch-only clear and why applied multipliers are kept.
- [x] 4.3 Same file — record the measured flow-rate dependence (w/mf 0.76 at 1.9 ml/s → 1.13 at 0.72 ml/s at fixed C) as the reason a single multiplier must be measured at one operating point. This is the fact that makes the rule non-obvious, and without it a future reader will "fix" the skip back into a re-route.
- [x] 4.4 No wiki manual change — the user-facing contract is unchanged. Note the check in the change rather than leaving it implicit.

## 5. Verification

- [x] 5.1 Build via `mcp__qtcreator__build` — succeeded.
- [x] 5.2 Full suite via `mcp__qtcreator__run_tests` scope `all` — 116 passed, 0 failed, 0 warnings. The first run surfaced 5 stale final-schema-version expectations (`tst_coffeebags` ×3, `tst_dbmigration` ×2) from migration 39 in the sibling change; fixed and re-run green.
- [x] 5.3 Re-ran the replay with the SHIPPED rule (window selection unchanged, skip instead of re-route). Results now in `design.md`: this repo's DE1 14/23 contributing, flow 1.26-1.90, median 0.937 → 0.886; well-dialled third user 27/30 with the median unchanged at 0.968; his lever shots identical; the reporter 1/2 at 1.048. These differ from the flatness-variant figures the proposal first carried — corrected there rather than left standing.
- [x] 5.4 Confirmed `scripts/check_log_markers.py` (and `check_test_source_duplication.py`) pass locally — `text-invariants.yml` gates `src/**` per-PR and a red run blocks nothing on its own.

## 7. One formula for both control modes

- [x] 7.1 Replace the flow branch's `meanWeightFlow / (profileTargetFlow * kWaterDensity93C)` with the pump-model expression; both branches now call the same helper.
- [x] 7.2 Extract `autoFlowCalSensorIdeal()` into `autoflowcalclassifier.{h,cpp}` beside `autoFlowCalWindowTargetCheck()` — a pure expression next to the other pure predicate, and the only way this formula gets test coverage given `computeAutoFlowCalibration()` has no harness.
- [x] 7.3 Keep both ratio guards branch-specific and unchanged; only the formula is unified. Note that the flow guard's bounds now constrain the ideal's ratio to the current multiplier rather than its absolute value — `kCalibrationMin`/`kCalibrationMax` still bound the absolute.
- [x] 7.4 Rename `formulaModeLabel` → `windowModeLabel`. With one formula the old name is a false statement in two log lines.
- [x] 7.5 v6 migration (`calibration/v6UnifiedIdealFormula`), pending batches only, same reasoning as v4/v5.
- [x] 7.6 Correct v3's premise where it is stated as fact: the comment block in `settings.cpp` and the "v3 Migration" section of `AUTO_FLOW_CALIBRATION.md`. Left as annotated history rather than deleted — the premise reads as settled and would otherwise be re-derived from the comment.
- [x] 7.7 Correct the two stale claims in `autoflowcalclassifier.h`'s doc comments that named the target-anchored formula as current.
- [x] 7.8 Tests in `tst_autoflowcal.cpp`: fixed point, invariance under the current multiplier (with the superseded expression asserted NOT invariant in the same slot), and the sqrt fixed point demonstrated by iterating the old rule to convergence.
- [x] 7.9 Correct the proposal's "well-dialled users see no change" line — that user has auto-cal OFF and C = 1.000, so he is a control, not a live user.
- [x] 7.11 Correct the terminology: the DE1 has no flowmeter — Decent removed it for an open-loop pump-stroke model — so `autoFlowCalSensorIdeal` became `autoFlowCalIdeal` and "flow sensor"/"sensor ratio" became pump-model language across code, tests, docs and this change. Two pre-existing uses survive in files this change does not touch (`machinestate.cpp:1076`, `tst_weightprocessor.cpp:805`).
- [x] 7.12 Fold the seven-machine `e` table into `design.md` — machine model dominates, mains voltage does not, and the DE1+ row is n=1 so model and individual machine are not separable.
- [ ] 7.10 **Falsification before merge**: two shots on this repo's DE1, same beans back to back, at multipliers ~1.0 and ~1.4. Compute `k = C * w / (mf * rho)` from each. Model A predicts the same `k`; model B predicts `k` tracking `C`. If model B, drop this section's commit and merge the skip alone.

## 6. Follow-ups (NOT this change)

- [ ] 6.1 Flatness-based window selection (prefer the flattest window, require both lines flat). Motivated but weaker-evidenced; see `design.md` for the measured reasons it is separated.
- [ ] 6.2 Reply on [#1872](https://github.com/Kulitorum/Decenza/issues/1872) explaining the two operating points, that his 1.35 is his capped tail rather than his machine's pump-model error, and what this change does for him.
- [ ] 6.3 The window ratio guard's `[0.75, 1.35]` bounds clip the low side of the ideal population this repo's own machine produces (target-met ideals of 0.74-0.80 are rejected), biasing C upward. Separate defect, separate change.
