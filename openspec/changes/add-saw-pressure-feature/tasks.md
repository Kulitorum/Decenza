# Tasks: Add SAW Pressure Feature

## Phase 0 — Simulator + production validation (gates Phase 1)

The pressure feature has more parameters and surface area than the previous SAW changes. Phase 0 must front-load both simulator validation AND production-code validation; simulator-only evidence is not sufficient (the prior `update-saw-prediction-model` Phase 0 demonstrated that the simulator inflates gains by ~2-3× because it lacks production's IQR-based and converged-mode outlier rejection).

### Phase 0a — Corpus extension

- [ ] Extend `tools/saw_replay/data/baseline_full.json` with a `pressureAtStop` field per shot, harvested from per-shot debug logs. The DE1 streams pressure as `[ExtractionTrack] pressure actual=` lines and direct samples; capture the most recent pressure value before the `[SAW-Worker] Stop triggered` line for each shot. Use a general-purpose agent following the same pattern that built the corpus in the prior phase.
- [ ] Spot-check: shot 887 (flow=1.22, drip=0.20, expected high pressure → tight puck) MUST land at pressure > 7.5 bar. Shots 884, 886 (flow ~1.5, drip ~0.7 g, expected lower pressure or wider puck) MUST land at lower pressures than shot 887. Document spot-check results in `analysis.md`. If shot 887's pressure ISN'T notably higher than the other low-flow shots, the entire premise of this proposal (pressure separates low-flow regimes) is wrong; abandon and re-investigate.
- [ ] If the spot-check passes, also harvest `pressureAtStop` for the other ~50 shots in the corpus. Validate distribution: pressure should range roughly 4-9 bar (typical espresso range).

### Phase 0b — Simulator implementation

- [ ] Extend `tools/saw_replay/main.cpp`:
  - Read `pressureAtStop` from the corpus JSON
  - Add `--sigma-pressure σ_p` flag (default 1.0 bar; explored values: 0.5, 1.0, 1.5, 2.0)
  - Implement 2-D kernel: `weight = recency × exp(-(flowDiff² / 2σ_f²) - (pressureDiff² / 2σ_p²))`
  - Add `--use-pressure-feature` flag to enable the 2-D kernel; without it, run as today (1-D)
- [ ] Sweep σ_p ∈ {0.5, 1.0, 1.5, 2.0} bar at σ_f=0.25 ml/s. Report per-bucket MAE and shot 887 specific error for each.
- [ ] Identify optimal σ_p (minimum overall MAE without low-flow regression). Record in `analysis.md`.
- [ ] **Simulator gate**: at the optimal σ_p, simulator overall MAE must improve by ≥ **0.005 g** vs simulator at σ=0.25 1-D, AND shot 887 specific error must improve by ≥ **0.10 g** (from +0.51 g toward 0). If the simulator gate fails, abandon this proposal — the pressure feature isn't separating regimes the way the literature predicts.

### Phase 0c — Production-code validation

- [ ] Implement the 2-D kernel in `src/core/settings.cpp`'s `getExpectedDripFor`, gated by env var `DECENZA_SAW_USE_PRESSURE=true`. Schema fallback: when stored entries don't have `pressure`, treat them as `pressure = queryPressure` (zero contribution to the kernel weight, matching the 1-D fallback).
- [ ] For Phase 0 only: add a temporary `pressureAtStop` field to the env-var-driven flow into `addSawLearningPoint`. Allows `saw_parity` to grow the production-side pool with pressure-bearing entries during corpus replay.
- [ ] Run `tools/saw_parity/` four times: {pressure feature off, pressure feature on at simulator-optimal σ_p} × {σ=1.5, σ=0.25}. The σ=0.25 row is the important one.
- [ ] **Production gate** (the proposal-shipping criterion):
  1. Overall MAE at (σ=0.25, pressure-on) must improve by ≥ **0.005 g** vs (σ=0.25, pressure-off). Threshold chosen as ~50% of the simulator's claimed gain to absorb the simulator-vs-production fidelity gap.
  2. Low-flow MAE must improve by ≥ **0.02 g** vs (σ=0.25, pressure-off). Pressure's main job is the low-flow regime; this is the headline metric.
  3. Shot 887 specific error must improve by ≥ **0.10 g**. If pressure can't fix the literal example shot the proposal cites, the premise is wrong.
  4. NO bucket may regress by more than 0.02 g vs (σ=0.25, pressure-off). Catches the case where pressure helps low-flow but hurts mid-/high-flow disproportionately.
- [ ] If the production gate fails, abandon this proposal. The σ-tuning + flow-trajectory combination ships without pressure.
- [ ] If the production gate passes, the σ_p value is locked in for Phase 1.

### Phase 0d — Backwards-compatibility check

- [ ] Run `saw_parity` with the env-var enabled but using a fresh empty pool (no entries have pressure). Confirm overall MAE matches σ=0.25 1-D (no degradation from the missing-pressure fallback path).

## Phase 1 — Production code change

- [ ] In `ShotTimingController`: subscribe to `DE1Device::pressureChanged` (or equivalent) and store the most recent pressure as `m_lastPressure`. At SAW trigger, capture this as `pressureAtStop`.
- [ ] Extend `ShotTimingController::sawLearningComplete` signal: add `double pressureAtStop` parameter.
- [ ] Update lambda at `src/main.cpp:528` to receive and forward `pressureAtStop`.
- [ ] Extend `Settings::addSawLearningPoint` and `addSawPerPairEntry` to accept `double pressure`. Store in the per-pair JSON entry as `"pressure"`.
- [ ] Extend `Settings::getExpectedDrip(flow)` to overload `getExpectedDrip(flow, pressure)`. The single-arg version calls the new version with `pressure = -1` (sentinel for "no pressure data") which makes the 2-D kernel degrade to 1-D.
- [ ] Same for `Settings::getExpectedDripFor`.
- [ ] Extend `WeightProcessor::configure` to take `pressureAtStop` snapshot; extend `WeightProcessor::getExpectedDrip` to accept query pressure.
- [ ] Set `kSawPressureSigma` to the value Phase 0c locked in. Define as `static constexpr double` in `settings.cpp` alongside `kSawMinMediansForGraduation`.
- [ ] Add new tests to `tests/tst_saw_settings.cpp`:
  - 2-D kernel: same flow, different pressures → predictions differ
  - 2-D kernel: same pressure, different flows → predictions differ (preserves σ-tuning behavior)
  - Backwards-compat: entry stored without pressure → kernel falls back gracefully, prediction is finite and within clamps
  - Sentinel: `getExpectedDrip(flow)` (no pressure) returns the same value as `getExpectedDrip(flow, -1)`
- [ ] Build + run the full ctest suite. All green is a hard gate.
- [ ] Run `tools/saw_parity/` against production code (no env-var) and confirm MAE matches Phase 0c's measurement at (σ=0.25, pressure-on).

## Phase 2 — Shadow logging (ships in same PR as Phase 1)

- [ ] Extend `[SAW] accuracy:` log line at `main.cpp:532` with:
  - `pressureAtStop=` — value captured at trigger
  - `pressurePredictedDrip=` — what the 2-D kernel predicted
  - `flowOnlyPredictedDrip=` — what the 1-D kernel would have predicted (compute via the 1-D fallback path)
- [ ] Confirm both predictions flow through the persistent debug logger.

## Phase 3 — Analysis (after deployment)

- [ ] Wait until ≥ 30 SAW-triggering shots accumulate post-deploy with `pressureAtStop` data, across ≥ 3 (profile, scale) pairs.
- [ ] Pull the persistent log; compute MAE for `pressurePredictedDrip` vs `flowOnlyPredictedDrip` against `actualDrip`.
- [ ] Per-bucket and per-pair breakdown.
- [ ] Manually inspect any low-flow shots where pressurePredictedDrip is a worse predictor than flowOnlyPredictedDrip — those are the cases where the 2-D kernel failed to help.

## Phase 4 — Decision point

- [ ] **Decision A — Keep the change.** Pick this iff:
  1. Post-deploy overall MAE for the 2-D kernel ≤ 1-D kernel by ≥ 0.005 g.
  2. Post-deploy low-flow MAE has improved by ≥ 0.02 g.
  3. No (profile, scale) pair shows divergence: 3+ consecutive low-flow over-predictions > 1 g in same direction.
- [ ] **Decision B — Roll back.** Pick this iff:
  1. Post-deploy overall MAE for 2-D kernel is worse than 1-D.
  2. Low-flow MAE worsens.
  3. Any (profile, scale) pair diverges per criterion above.
  Rollback procedure: `git revert <Phase 1+2 squash commit SHA>`. Shadow-logging captures remain in persistent log for forensic analysis.
- [ ] Record the decision and supporting numbers in `analysis.md`.

## Phase 5 — Archive

- [ ] After deployment is stable for ≥ 2 weeks (regardless of decision):
  - [ ] Run `openspec validate add-saw-pressure-feature --strict --no-interactive` to confirm consistency.
  - [ ] If Decision A: update spec deltas to reflect as-shipping behavior.
  - [ ] If Decision B: revert spec deltas; document rollback in archive.
  - [ ] Move `changes/add-saw-pressure-feature/` to `changes/archive/<YYYY-MM-DD>-add-saw-pressure-feature/`.
  - [ ] Run `openspec validate --strict --no-interactive` to confirm.
