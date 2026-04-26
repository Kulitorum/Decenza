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

## Phase 2 — Shadow logging (ships in same PR as Phase 1)

- [ ] Extend the existing `[SAW] accuracy:` log line at `main.cpp:532` with two additional fields:
  - `oldSigmaDrip=` — what σ=1.5 would have predicted at this shot's flow given the same pool. Compute via a private helper that runs the OLD math with the OLD constant. Allows post-deploy A/B comparison against the σ=0.25 prediction.
  - `predictionSource=` — `perPair` | `globalBootstrap` | `scaleDefault`. Lets Phase 3 analysis split metrics by which path fired.
- [ ] Confirm both new fields flow through the persistent debug logger so they survive past per-shot capture.

## Phase 3 — Analysis (after deployment)

- [ ] Wait until the persistent debug log has accumulated ≥ 30 SAW-triggering shots since the deploy date, across at least 3 distinct (profile, scale) pairs.
- [ ] Pull the persistent log via `mcp__de1__debug_get_log` and extract `[SAW] accuracy:` lines.
- [ ] Compute MAE for both `oldSigmaDrip` (σ=1.5) and `predictedDrip` (σ=0.25) against `actualDrip`:
  - Overall and per (profile, scale) pair
  - Per flow bucket (low / mid / high)
  - Per `predictionSource` (perPair / globalBootstrap / scaleDefault)
- [ ] Append all metrics to `analysis.md` alongside the Phase 0 pre-deploy baseline.

## Phase 4 — Decision point

Pre-committed criteria:

- [ ] **Decision A — Keep the σ change.** Pick this iff:
  1. Overall post-deploy MAE for σ=0.25 ≤ Phase 0 baseline (0.348 g, within noise).
  2. Low-flow post-deploy MAE has improved or held even vs the captured `oldSigmaDrip` field.
  3. No (profile, scale) pair shows divergence: 3+ consecutive overshoots > 2 g in the same direction post-deploy.
- [ ] **Decision B — Roll back the σ change.** Pick this iff:
  1. Overall post-deploy MAE worsens vs the captured `oldSigmaDrip` field.
  2. Low-flow worsens.
  3. Any (profile, scale) pair diverges per the threshold above.
  Rollback procedure: `git revert <Phase 1+2 squash commit SHA>`. Shadow-logging captures will remain in the persistent log for forensic analysis.
- [ ] Record the decision and supporting numbers in `analysis.md` and update `proposal.md` with a final status line.

## Phase 5 — Archive

- [ ] After deployment is stable for ≥ 2 weeks (regardless of decision):
  - [ ] Run `openspec validate tune-saw-old-prediction --strict --no-interactive` to confirm consistency.
  - [ ] If Decision A: update `specs/stop-at-weight-learning/spec.md` so the steady-state requirements reflect what's actually shipping.
  - [ ] If Decision B: revert the spec deltas; document the rollback in the archive entry.
  - [ ] Move `changes/tune-saw-old-prediction/` to `changes/archive/<YYYY-MM-DD>-tune-saw-old-prediction/`.
  - [ ] Run `openspec validate --strict --no-interactive` again to confirm the archived state passes.
