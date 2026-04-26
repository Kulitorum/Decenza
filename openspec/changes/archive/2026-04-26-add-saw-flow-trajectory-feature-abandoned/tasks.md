# Tasks: Add SAW Flow-Trajectory Feature

## Phase 0 — Simulator + production validation (gates Phase 1)

The Phase 0 work for this proposal is more substantial than for `tune-saw-old-prediction` because the feature is genuinely new (df/dt isn't currently captured per shot) and the threshold value is undetermined. Both simulator validation and production-code validation are required before Phase 1 starts; the simulator-vs-production fidelity gap from prior Phase 0 work means simulator-only evidence is not sufficient.

### Phase 0a — Corpus extension

- [ ] Extend `tools/saw_replay/data/baseline_full.json` with a `flowSlope` field per shot, computed from the per-shot debug log's flow time series over the 1.5 s window before `[SAW-Worker] Stop triggered`. Use a least-squares fit on the `(timestamp, flowRate)` points captured during that window. Use the existing `mcp__de1__shots_get_debug_log` to source the time series; extract via the same general-purpose agent pattern that built the corpus.
- [ ] Spot-check the 22 shots already in `baseline.json`: shots 877 (drip 2.8 at flow 3.0, expected stall recovery → large positive slope) and 870 (drip 2.5 at flow 5.2, expected stall recovery → large positive slope) MUST land at flowSlope > +0.4 ml/s² to validate the harvest is correct. Shots 884, 886, 887 (low-flow, normal puck) MUST land near 0 ml/s². Document the spot-check results in `analysis.md`.

### Phase 0b — Simulator implementation

- [ ] Extend `tools/saw_replay/main.cpp` with `--reject-stall-threshold N` (default disabled; -1 means "disabled"). When set to a positive value: at each shot's predict time, if its observed `flowSlope` exceeds the threshold, that shot's outcome is *not appended to the pool* (the prediction for that shot is still recorded, but the pool doesn't grow with this entry).
- [ ] Optionally also add `--use-trajectory-kernel` flag that adds slope as a second kernel dimension (Gaussian over `(flowDiff² / 2σ_f²) + (slopeDiff² / 2σ_s²)`). σ_s is a new parameter; default 0.3 ml/s².
- [ ] Run the simulator with `--variant=old --mode=legacy --sigma=0.25 --reject-stall-threshold T` for T ∈ {0.2, 0.3, 0.4, 0.5, 0.7, 1.0} ml/s². Report per-bucket MAE for each.
- [ ] Identify the optimal threshold (the one that minimizes overall MAE without rejecting normal shots). Record in `analysis.md`.

### Phase 0c — Production-code validation

The simulator's port has known fidelity gaps (no IQR rejection, no converged-mode rejection). To validate the threshold's effect on real production code:

- [ ] Implement the gate in `src/core/settings.cpp` `addSawLearningPoint`, gated by env var `DECENZA_SAW_STALL_REJECT_THRESHOLD` (parsed as a float; default unset = no gate). Compute slope inside `addSawLearningPoint` from a slope value passed through the new signal (or compute from the corpus replay using a temporary helper).
- [ ] Run `tools/saw_parity/` four times: {threshold disabled, threshold at simulator-optimal} × {σ=1.5, σ=0.25}. The σ=0.25 row is the important one (since `tune-saw-old-prediction` ships first); the σ=1.5 row is a sanity check.
- [ ] **Gate**: at the simulator-optimal threshold, production-code overall MAE at σ=0.25 must improve by ≥ **0.005 g** vs production-code σ=0.25 alone (cell C from `tune-saw-old-prediction`'s analysis: 0.348 g) AND high-flow MAE must improve by ≥ **0.05 g** vs the same baseline (high-flow is where stall-recovery shots concentrate). If the gate fails, abandon this proposal.
- [ ] Threshold values that pass the gate are recorded in `analysis.md` as the candidate production setting.

### Phase 0d — Threshold sensitivity check

- [ ] At the candidate threshold ± 25%, the gate criteria above must still pass. If not, the threshold is too sensitive and needs further tuning before production. Document sensitivity in `analysis.md`.

## Phase 1 — Production code change

- [ ] Add a flow buffer to `ShotTimingController`: circular buffer of `(timestamp, flowRate)` pairs sampled at flow-update cadence, capped at 1.5 s of history. Reset at extraction start.
- [ ] Compute `flowSlope` at SAW trigger via least-squares fit on the buffer's points. Reject as 0.0 if buffer has fewer than 3 points.
- [ ] Extend `ShotTimingController::sawLearningComplete` signal signature: add `double flowSlope` as the fourth parameter. Update the lambda at `src/main.cpp:528` to receive and forward the slope.
- [ ] Extend `Settings::addSawLearningPoint` signature: add `double flowSlope` parameter. Add the stall-recovery gate at the top of the function (after the existing physical-validity check):
  ```cpp
  if (std::abs(flowSlope) > kStallRecoveryThreshold) {
      qWarning() << "[SAW] rejected (stall recovery): drip=" << drip
                 << "flow=" << flowRate << "slope=" << flowSlope
                 << "threshold=" << kStallRecoveryThreshold;
      return;
  }
  ```
- [ ] Extend `Settings::addSawPerPairEntry` similarly. The gate should fire before the entry is appended to either the pending batch or the global pool.
- [ ] Set `kStallRecoveryThreshold` to the value Phase 0 identified (most likely 0.4-0.5 ml/s²). Define as `static constexpr double` in `settings.cpp` alongside `kSawMinMediansForGraduation`.
- [ ] Add new tests to `tests/tst_saw_settings.cpp`:
  - Rejection on positive flow-slope spike (stall recovery): create an entry with `flowSlope = +1.0`, assert it's not added to the pool.
  - Rejection on negative flow-slope spike (channeling): same with `flowSlope = -1.0`.
  - Acceptance on flat trajectory (normal pour): `flowSlope = 0.0`, assert the entry IS added.
  - Acceptance on gradual decline (D-Flow end-of-shot): `flowSlope = -0.2`, assert the entry IS added.
  - Symmetric rejection: `flowSlope = +0.6` and `flowSlope = -0.6` both gated (defends symmetric threshold).
- [ ] Build + run the full ctest suite. All green is a hard gate.
- [ ] Run `tools/saw_parity/` against the production code and confirm MAE matches Phase 0c's measurement.

## Phase 2 — Shadow logging (ships in same PR as Phase 1)

- [ ] Extend the `[SAW] accuracy:` log line at `main.cpp:532` with `flowSlope=` so post-deploy data has the trajectory value of every shot (rejected or not — for rejected shots the line is the rejection log, not accuracy).
- [ ] Confirm the rejection log line `[SAW] rejected (stall recovery)` flows through the persistent debug logger.

## Phase 3 — Analysis (after deployment)

- [ ] Wait until the persistent debug log has accumulated ≥ 30 SAW-triggering shots since the deploy date, including ≥ 3 rejection events.
- [ ] Pull the persistent log and extract `[SAW] accuracy:` and `[SAW] rejected (stall recovery)` lines.
- [ ] Compute MAE on accepted shots per flow bucket and overall.
- [ ] Report rejection rate (rejected / total triggered). Compare to Phase 0c's expected rate (~5-10% of corpus).
- [ ] Manually inspect rejected shots: were they actually stall-recovery / bad-puck shots? Or false rejections of normal shots?

## Phase 4 — Decision point

- [ ] **Decision A — Keep the change as shipped.** Pick this iff:
  1. Post-deploy MAE on accepted shots ≤ Phase 3 baseline by at least 0.005 g.
  2. Worst-case error on accepted shots is meaningfully lower than the σ=0.25-only baseline (which had worst-case 2.56 g).
  3. Rejection rate is between 3% and 15% (within the expected range for a corpus with some stall-recovery shots).
  4. Manual inspection of rejected shots shows they were genuinely stall-recovery or bad-puck shots, not false positives.
- [ ] **Decision B — Adjust threshold.** Pick this iff Decision A's MAE/worst-case criteria are met but the rejection rate is wrong (too high → threshold too tight, too low → threshold too loose). Action: open a one-line follow-up to adjust `kStallRecoveryThreshold` based on the new evidence.
- [ ] **Decision C — Roll back.** Pick this iff:
  1. Post-deploy MAE on accepted shots is worse than baseline.
  2. Rejection rate exceeds 25% (catching normal shots).
  3. Manual inspection shows false-positive rejections dominate.
  Rollback procedure: `git revert <Phase 1+2 squash commit SHA>`. The shadow-logging captures will remain in the persistent log for forensic analysis.

## Phase 5 — Archive

- [ ] After deployment is stable for ≥ 2 weeks (regardless of decision):
  - [ ] Run `openspec validate add-saw-flow-trajectory-feature --strict --no-interactive` to confirm consistency.
  - [ ] If Decision A or B: update `specs/stop-at-weight-learning/spec.md` to reflect the as-shipping behavior.
  - [ ] If Decision C: revert spec deltas; document rollback in archive.
  - [ ] Move `changes/add-saw-flow-trajectory-feature/` to `changes/archive/<YYYY-MM-DD>-add-saw-flow-trajectory-feature/`.
  - [ ] Run `openspec validate --strict --no-interactive` to confirm.
