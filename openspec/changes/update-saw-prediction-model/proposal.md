# Change: Update Stop-At-Weight Prediction Model

## Why

The current SAW drip prediction is `drip = lag × flow` — a single-parameter model fit per (profile, scale) pair, plus a Gaussian-flow-similarity weighted average over the global pool as a fallback. Field data (shots 882–887, 80's Espresso + Decent Scale) shows the model systematically over-predicts at low flow:

| Shot | flow at stop | actual drip | predicted drip | error |
|------|--------------|-------------|----------------|-------|
| 882 | 2.29 ml/s | 1.05 g | 1.10 g | +0.05 |
| 884 | 1.39 ml/s | 0.70 g | 0.67 g | −0.03 |
| 886 | 1.52 ml/s | 0.72 g | 0.73 g | +0.01 |
| **887** | **1.22 ml/s** | **0.20 g** | **0.58 g** | **+0.38** |

Shot 887 stopped 0.9 g under target. The lag-only model can't capture the "low-flow tail drips less" behavior — a single parameter has no degree of freedom to set both slope and intercept.

Per-pair graduation (currently `kSawMinMediansForGraduation = 2` medians = 10 shots) takes ~2 weeks of normal use before a user sees pair-specific predictions. Even after graduation the model is still lag-only, so the low-flow miss persists.

## What Changes

Phase 0 — simulator validation (gates everything else):
- Build a standalone `tools/saw_replay/` that links the SAW math without BLE/QML/UI dependencies.
- Curate a starter corpus from the persistent debug log (≥ 30 historical SAW-triggering shots).
- Replay the corpus through both the OLD and NEW prediction models. Compare MAE and worst-case error.
- If NEW is worse than OLD overall or in the low-flow bucket, the change does not ship. The simulator output becomes the pre-deploy baseline `analysis.md` references.

Phase 1 — implement the smaller change:
- Replace the prediction formula with linear regression: `drip = a · flow + b` fitted by least squares.
- Refit on every read from the existing `(drip, flow)` pool entries; no new persistence keys, no schema migration.
- Apply at all three callsites: `Settings::getExpectedDrip`, `Settings::getExpectedDripFor`, `WeightProcessor::getExpectedDrip`.
- Smarter bootstrap: pre-graduation, fit one regression across pooled raw entries (the existing `saw/learningHistory` global pool plus the pair's pending-batch entries) — gives new pairs a real two-feature prediction the moment ≥ 2 entries exist.
- Pending-batch warm-up: if a pair has no committed medians but ≥ 2 pending-batch entries, fit on those before falling back to the global pool. Pair-specific predictions begin after the second shot in a brand-new batch.

Phase 2 — shadow logging (ships in the same PR as Phase 1):
- Keep the OLD weighted-average prediction function as `oldModelPredictForShadow(...)`. NOT used to drive SAW; only invoked at the post-shot logging point.
- Extend the existing `[SAW] accuracy:` log line with both predictions per shot (`oldModelDrip`, `newModelDrip`), the fitted `(a, b)`, and a `modelSource` tag.
- Add a per-pair entry-state log line so post-deploy analysis can join model state to outcome without guessing.
- This makes Phase 3 trivially aligned: every shot has both models' predictions side-by-side.
- No UI surface; pure observation.

Phase 3 — analysis after a target number of post-deploy shots accumulates:
- Quantify mean absolute error and worst-case error before vs after the change, broken down by flow regime (low / mid / high).
- Inspect cases where the regression's `a` or `b` lands at clamp boundaries (signal that the model is being suppressed by guards rather than fitting freely).
- Look for systematic drift: did per-pair predictions improve as data accumulated, or stay flat?

Phase 4 — decision point. Three pre-committed paths, evaluated against numeric criteria in tasks.md:
- **Decision A** — keep the trimmed change as-is. Headline gate: low-flow MAE improved by ≥ 50% AND no flow bucket regressed AND per-call cost < 5 ms median on the Decent tablet.
- **Decision B** — promote to the larger schema change (persist (a, b) per median, eliminate per-read fitting). Picked iff Decision A's accuracy criteria are met but per-call cost > 5 ms median or p95 > 15 ms. Triggers a follow-up OpenSpec change.
- **Decision C** — roll back. Picked iff low-flow MAE got worse, overall MAE got worse, clamp hits exceed 15%, any pair diverges, or BLE timing budget is breached. Rollback is `git revert <Phase 1+2 squash SHA>`.

The decision point is pre-committed so the analysis can't drift after the data lands.

## Impact

**Affected specs**: `stop-at-weight-learning` (new capability spec — establishing the contract for SAW prediction).

**Affected code**:
- `src/core/settings.cpp` and `.h` — regression fitting helper, three prediction functions, pending-batch warm-up read path. No new persistence keys.
- `src/machine/weightprocessor.cpp` and `.h` — replace `getExpectedDrip` body with regression evaluation; signature unchanged (still receives `learningDrips` and `learningFlows` arrays).
- `src/main.cpp` and `src/controllers/maincontroller.cpp` — no change to SAW wiring; instrumentation logging added at the existing `[SAW] accuracy:` site.
- Tests: `tests/tst_saw_settings.cpp` for regression math + boundary conditions; new synthetic-shot tests covering low-flow over-prediction.

**Migration**: zero. Existing pool entries are read as-is; the new prediction fits on whatever's already stored.

**User-visible behavior**:
- First post-deploy shot: prediction comes from regression on the existing 12-entry global pool — already better than the old weighted average for low-flow shots.
- Second shot in a new pair's batch: pair-specific predictions kick in via pending-batch warm-up.
- Tenth shot per pair: graduation to per-pair regression on committed medians (existing behavior, unchanged).

**Risk**: low. Algorithm-only change with clamps on (a, b); falls back to lag-only behavior for single-point inputs; no data destroyed; rollback is a one-commit revert.
