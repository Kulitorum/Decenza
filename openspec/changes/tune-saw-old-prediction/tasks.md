# Tasks: Tune Stop-At-Weight Old Prediction Model

## Phase 0 — Production validation (COMPLETE; see analysis.md)

Phase 0 has already been executed. Results are recorded in `analysis.md`. Summary:

- [x] σ=1.5 vs σ=0.25 measured through real production code via `tools/saw_parity/`. Result: σ=0.25 wins (overall MAE 0.348 vs 0.370, −6%; high-flow 0.592 vs 0.686, −14%; shot 887 +0.57 g vs +0.90 g, −37%).
- [x] Smart-pool bootstrap measured (env-var-gated branch in `Settings::getExpectedDripFor`). Result: gate failed. Smart-pool redistributes error (helps high-flow, hurts low-flow including the headline shot). Reverted.
- [x] 2×2 grid {σ=1.5, σ=0.25} × {scalar, smart-pool} captured in analysis.md.
- [x] Decision recorded: ship σ change only (Decision B from the original Phase 4 framework). Smart-pool dropped from this proposal.
- [x] All Phase 0 source edits reverted. Working tree clean for `src/`.

## Phase 1 — Production code change

- [ ] Update three call sites for the σ change:
  - `src/core/settings.cpp:857` (`getExpectedDrip`): change `/ 4.5` → `/ 0.125`, update inline comment from `sigma=1.5` to `sigma=0.25`.
  - `src/core/settings.cpp:1223` (`getExpectedDripFor` per-pair branch): same change, same comment update.
  - `src/machine/weightprocessor.cpp:442` (`WeightProcessor::getExpectedDrip`): same change, same comment update.
- [ ] Add new tests to `tests/tst_saw_settings.cpp` that *do* exercise σ (the existing 52 SAW tests train and query at the same flow, so σ is invisible to them — this means today σ has zero test coverage, which is why σ=1.5 → σ=0.25 didn't break anything but also why we couldn't catch a future regression):
  - Predict at flow=2.5 with training data only at flow=1.5; assert the prediction is closer to the lag-fallback (because the Gaussian dropped to near-zero) than to the training drip. Defends σ behavior against "set σ way too wide" regressions.
  - Predict at flow=1.5 with training data at flow=1.5; assert the prediction equals the training drip within tolerance. Locks in the no-flow-shift case.
  - Predict at flow=1.0 and flow=3.0 with mixed training data spanning flows 1.0-3.0; assert predictions differ from each other (would be equal under a flat-average model). Defends against σ being set so narrow it underflows everywhere.
- [ ] Build + run the full ctest suite. All green is a hard gate before any push.
- [ ] Run `tools/saw_parity/` against the real Settings code (no env-var) and confirm overall MAE ≈ 0.348 g (matching the Phase 0 measurement) on the 63-shot corpus.

## Phase 2 — Shadow logging (DROPPED 2026-04-26)

Originally planned to add `oldSigmaDrip` and `predictionSource` to the `[SAW] accuracy:` log line so post-deploy MAE could be A/B'd against σ=1.5. Dropped because:

- Detailed per-shot SAW data only reaches us when a user submits a system log, and users only submit logs when they hit a problem. That biases any "data we'd see" toward σ=0.25 *failures* rather than a clean A/B sample.
- Without telemetry to pull `oldSigmaDrip` from a representative cohort, the field is effectively dead bytes on most users' devices.
- The cost of adding it (extra qExp per shot end, 2 log fields, a new `SawPredictionDetail` API, ongoing maintenance until removed) outweighs its post-deploy value.

If a richer post-deploy signal becomes desirable later (e.g., if shotmap/visualizer telemetry is extended to include SAW prediction fields), this can be re-added as a small, focused change.

## Phase 3 — Decision (after deployment)

Decision criteria are now qualitative, not metric-based:

- [ ] **Decision A — Keep the σ change.** Default. Pick this iff:
  1. No SAW-related issue reports (overshoot/undershoot complaints) in the GitHub Issues tracker in the 2 weeks following the deploy.
  2. Jeff's own daily-driver pulls are not regressing visibly (subjective; based on his shot-by-shot observation).
- [ ] **Decision B — Roll back the σ change.** Pick this iff:
  1. ≥1 user reports a SAW regression that didn't exist pre-deploy.
  2. Jeff observes a clear regression on his own machine that the Phase 0 corpus didn't predict.
  Rollback procedure: `git revert <Phase 1 commit SHA>`. No shadow data was captured, so the rollback decision is based on reports + observation, not metrics.
- [ ] Record the decision in `analysis.md` and update `proposal.md` with a final status line.

## Phase 4 — Archive

- [ ] After deployment is stable for ≥ 2 weeks (regardless of decision):
  - [ ] Run `openspec validate tune-saw-old-prediction --strict --no-interactive` to confirm consistency.
  - [ ] If Decision A: update `specs/stop-at-weight-learning/spec.md` so the steady-state requirements reflect what's actually shipping.
  - [ ] If Decision B: revert the spec deltas; document the rollback in the archive entry.
  - [ ] Move `changes/tune-saw-old-prediction/` to `changes/archive/<YYYY-MM-DD>-tune-saw-old-prediction/`.
  - [ ] Run `openspec validate --strict --no-interactive` again to confirm the archived state passes.
