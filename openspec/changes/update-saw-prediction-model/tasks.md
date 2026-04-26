# Tasks: Update Stop-At-Weight Prediction Model

## Phase 0 — Simulator + dry-run validation (gates Phase 1)

- [ ] Build a standalone simulator at `tools/saw_replay/main.cpp` that links the SAW math from `Settings` (or a header-only extracted helper) without pulling in BLE/QML/UI. Input: a list of `(drip, flow, scale, profile, ts, overshoot)` tuples plus a "current shot" `(flow, target)`. Output: predicted drip from the OLD model and predicted drip from the NEW model, side-by-side.
- [ ] Add `--corpus <path>` mode that reads a JSON file with a list of historical shots and replays them in chronological order, growing the pool with each shot, emitting `(shot_id, flow, actual_drip, old_pred, new_pred, old_err, new_err, model_source)` per row.
- [ ] Curate a starter corpus from the persistent debug log: extract `[SAW] accuracy:` lines for the most recent ≥ 30 SAW-triggering shots into `tools/saw_replay/data/baseline.json`. Include shot id, profile filename, scale type, flow at stop, actual drip, predicted drip (from the live system), overshoot, timestamp.
- [ ] Run the simulator on the baseline corpus. Record results in `analysis.md`. Compute MAE and worst-case error for OLD vs NEW, broken down by flow bucket (low < 1.5, mid 1.5–3, high > 3 ml/s).
- [ ] **Gate**: if the simulator shows NEW is worse than OLD overall OR worse in the low-flow bucket, STOP. Document the failure in `analysis.md`. Reconsider feature set (3-feature regression, alternate model) before any code lands in production paths.
- [ ] Capture the simulator's pre-deploy MAE numbers in `analysis.md` under "Pre-deploy baseline" so Phase 3 can compare against frozen reference values.

## Phase 1 — Regression model implementation (ships with Phase 2)

- [ ] Add a `fitDripRegression(...)` helper to `Settings` (private static or anonymous-namespace in settings.cpp). Recency-weighted least squares; clamp `a ∈ [0, 5]`, `b ∈ [-2, 2]`. Returns success/failure so callers can fall back.
- [ ] Replace `Settings::getExpectedDrip(double currentFlowRate)` body with: collect last 12 entries from the pool (existing logic), call `fitDripRegression`, return `clamp(a × flow + b, 0.5, 20)` on success; on failure use the existing `flow × (sensorLag + 0.1)` fallback.
- [ ] Replace `Settings::getExpectedDripFor(profile, scale, flow)` graduated branch with the same regression.
- [ ] Add pending-batch warm-up: in `getExpectedDripFor`, before falling back to bootstrap, read `loadPerProfileSawBatchMap()[key]`; if it has ≥ 2 entries, fit regression on those and return.
- [ ] Smart bootstrap path: replace the `globalSawBootstrapLag(scaleType) × flow` formula with regression. Pool the latest committed median per pair (matching scale type) plus all pending-batch entries for that scale plus the existing legacy global pool entries. Fit and return.
- [ ] Update `WeightProcessor::getExpectedDrip(currentFlowRate)` body with the same regression evaluation. Snapshot fields stay `m_learningDrips` and `m_learningFlows`; the fit happens at evaluation time.
- [ ] Replace the implausibility gate at `addSawLearningPoint` (`drip / flow > 4`) with `drip > 10`.
- [ ] Touch existing tests in `tests/tst_saw_settings.cpp`: remove or update any test that exercised the Gaussian-flow-similarity weighting (the new model has flow dependence intrinsically; Gaussian-on-top is gone). Add new tests covering: regression fit math correctness, clamp boundaries, n<2 fallback, collinear-flows fallback, pending-batch warm-up path.
- [ ] Add at least one synthetic-shot regression test that locks in the behavior the simulator validated: feed Phase 0's baseline shots through `Settings::getExpectedDripFor` and assert the predictions match the simulator's NEW output to ±0.05 g.
- [ ] Build + run the full ctest suite (33 targets, including `shot_corpus_regression`). All green is a hard gate before any push.

## Phase 2 — Shadow logging (ships with Phase 1, single PR)

- [ ] Keep the OLD weighted-average prediction function in the codebase, gated by a static `oldModelPredictForShadow(...)` helper. NOT used to drive SAW; only invoked at the post-shot logging point.
- [ ] Extend the `[SAW] accuracy:` log line at `main.cpp:532` to emit BOTH predictions per shot:
  - `oldModelDrip=` — what the legacy weighted-avg formula would have predicted at this shot's flow
  - `newModelDrip=` — what the regression actually returned (already captured as `predictedDrip`)
  - `modelA=`, `modelB=`, `modelSource=` (`perPairRegression` | `pendingBatchRegression` | `globalRegression` | `lagFallback` | `sensorLagDefault`)
- [ ] Add a per-pair entry-state log at `addSawPerPairEntry` start (before any state mutation): `[SAW] entry_state: pair=<key> batchSize=<n> historyN=<m> graduated=<bool>`.
- [ ] Confirm both log lines flow through the persistent debug logger so they survive past per-shot capture.

## Phase 3 — Analysis (after deployment)

- [ ] Wait until the persistent debug log has accumulated ≥ 30 SAW-triggering shots since the deploy date, across at least 3 distinct (profile, scale) pairs.
- [ ] Pull the persistent log via `mcp__de1__debug_get_log` and extract `[SAW] accuracy:` + `[SAW] entry_state:` lines.
- [ ] Compute these metrics for both `oldModelDrip` and `newModelDrip` against `actualDrip`:
  - Mean absolute error (MAE), overall and per (profile, scale) pair
  - MAE within flow buckets: low (< 1.5 ml/s), mid (1.5–3 ml/s), high (> 3 ml/s)
  - Worst-case error per bucket
  - Count of shots where `modelA` or `modelB` lands at a clamp boundary (signal of over-constraint)
- [ ] Profile the new code path on the Decent tablet (Samsung SM-X210) under live shot conditions. Measure median + p95 wall-clock cost per `Settings::getExpectedDripFor` call. Compare against a 50 ms budget (BLE round-trip latency budget for SAW trigger).
- [ ] Append all metrics to `analysis.md` alongside the Phase 0 pre-deploy baseline, so the comparison is in one place.

## Phase 4 — Decision point

Criteria below are pre-committed; the analyst (or future Claude session) MUST evaluate against these and not relax them after seeing the data:

- [ ] **Decision A — Stay with refit-on-read.** Pick this iff:
  1. **Low-flow MAE** (< 1.5 ml/s bucket) has improved by ≥ 50% vs the pre-deploy baseline. (Headline complaint; this is the threshold gate.)
  2. Overall MAE has improved or held even (no regression in mid/high flow).
  3. Per-call cost on the SM-X210 stays at ≤ 5 ms median (well under the 50 ms BLE budget).
  4. Clamp boundary hits stay below 10% of fits.
  5. No (profile, scale) pair shows worse MAE than baseline.
- [ ] **Decision B — Promote to a schema change.** Pick this iff Decision A's criteria are met **but** per-call cost exceeds 5 ms median, or per-call p95 exceeds 15 ms. Action: open a follow-up OpenSpec change (`persist-saw-regression-coefficients`) to store `(a, b)` per median, recompute global bootstrap (a, b) at commit time, eliminate per-read fitting cost.
- [ ] **Decision C — Roll back.** Pick this iff ANY of:
  1. Low-flow MAE is worse than baseline.
  2. Overall MAE is worse than baseline.
  3. Clamp boundary hits exceed 15% of fits (regression being suppressed, not fitting freely).
  4. Any (profile, scale) pair shows divergence: 3+ consecutive overshoots > 2 g in same direction after deploy.
  5. Per-call p95 exceeds 50 ms (BLE budget breach).

  Rollback procedure: `git revert <Phase 1+2 squash commit SHA>`. The shadow-logging captures will remain in the persistent log for forensic analysis. Document the failure mode in `analysis.md`. Consider a 3-feature model (puck-state proxy, time-since-pour-start) for a future attempt.

- [ ] Record the decision and supporting numbers in `analysis.md` and update `proposal.md` with a final status line.

## Phase 5 — Archive

- [ ] After deployment is stable for ≥ 2 weeks (regardless of which decision was made):
  - [ ] Run `openspec validate update-saw-prediction-model --strict --no-interactive` to confirm consistency.
  - [ ] If Decision A or B was chosen: update `specs/stop-at-weight-learning/spec.md` so the steady-state requirements reflect what's actually shipping.
  - [ ] If Decision C was chosen: revert the spec deltas; document the rollback in the archive entry; the next attempt would be a new OpenSpec change.
  - [ ] Move `changes/update-saw-prediction-model/` to `changes/archive/<YYYY-MM-DD>-update-saw-prediction-model/`.
  - [ ] Run `openspec validate --strict --no-interactive` again to confirm the archived state passes.
