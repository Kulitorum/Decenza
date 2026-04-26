# Design: Update Stop-At-Weight Prediction Model

## Why this needs a design doc

Three reasons:
1. The math change crosses three callsites with subtly different fallback behavior; getting them out of sync would mean the live-shot predictor (`WeightProcessor`) sees one model and the post-shot outlier check (`Settings`) sees another.
2. The "smart bootstrap" path pools data across pairs, which is a behavior change with privacy/correctness implications worth pinning down explicitly.
3. The decision-point criteria need to be written before the data lands, otherwise we'll rationalize after the fact.

## The math

Per-batch fit at evaluation time, recency-weighted least squares for `drip = a · flow + b`:

```
Given points {(d_i, f_i)} with recency weights w_i (linear from recencyMax at i=0 to recencyMin at i=N-1):

  S_w   = Σ w_i
  S_wf  = Σ w_i · f_i
  S_wd  = Σ w_i · d_i
  S_wff = Σ w_i · f_i²
  S_wfd = Σ w_i · f_i · d_i

  denom = S_w · S_wff − S_wf²
  a     = (S_w · S_wfd − S_wf · S_wd) / denom
  b     = (S_wd − a · S_wf) / S_w

If denom < ε (collinear flows, all the same value), fit fails → caller falls back.
Clamp: a ∈ [0, 5], b ∈ [-2, 2]. Output: clamp(a · flow + b, 0.5, 20).
```

Recency weights match the existing model: `recencyMax = 10`, `recencyMin = 3` (converged) or `1` (unconverged). Same Gaussian flow-similarity weighting is **dropped** — the regression already accounts for flow-dependent prediction.

## Three callsites, identical math

| Callsite | Input source | Fallback if fit fails |
|---|---|---|
| `Settings::getExpectedDrip(flow)` | last 12 entries from `saw/learningHistory` global pool | `flow × (sensorLag + 0.1)` (existing) |
| `Settings::getExpectedDripFor(profile, scale, flow)` graduated | last 12 entries from per-pair history | bootstrap path below |
| `Settings::getExpectedDripFor` pre-graduation | (a) pending batch ≥ 2 entries; else (b) bootstrap pool | (a) → (b) → `flow × bootstrapLag` legacy → `flow × (sensorLag + 0.1)` |
| `WeightProcessor::getExpectedDrip(flow)` | snapshot of pool taken at `configure()` | `flow × (sensorLagSeconds + 0.1)` (existing) |

The bootstrap "pool" for the smart-bootstrap path is built per-call: walk every pair in `loadPerProfileSawHistoryMap()`, take the last committed median for the matching scale type, plus every entry in pending batches for the matching scale. Pool is anonymous (no profile or scale label retained in the regression input); it just provides shape data.

## Pending-batch warm-up

Today, a pair with no committed medians falls straight to the global bootstrap. With the warm-up:

```
if pair.pendingBatch.size() >= 2:
    return regression(pair.pendingBatch, flow)
elif pair.committedMedians.size() >= kSawMinMediansForGraduation:
    return regression(pair.committedMedians, flow)
else:
    return smartBootstrap(scaleType, flow)
```

This means the user starts seeing pair-specific predictions after the second SAW shot in a brand-new batch, not after the tenth.

## Why no schema change in Phase 1

- The pool already stores everything regression needs: `(drip, flow)` pairs.
- Refitting on read is O(N) where N ≤ 12 entries; on a Samsung SM-X210 (Decent tablet) this is sub-millisecond. Profiler traces will confirm.
- Avoiding new keys means rollback is `git revert <sha>` with no data loss.

The schema change is reserved for Decision B — only worth doing if profiler shows fit cost is meaningful, or if we're about to add more features that benefit from explicit coefficient storage.

## Why no Gaussian flow-similarity weighting

Old model: weighted average of `drip_i`, with weights `recency_i × exp(-(flow_i - currentFlow)² / 4.5)`. The Gaussian was the only mechanism by which the prediction varied with flow at all.

New model: `drip = a·flow + b`. Flow dependence is intrinsic to the model. Adding Gaussian weighting on top would make the regression locally-fit (closer to a 1-NN smoother), defeating the global-shape capture that's the whole point of the change.

Recency weighting still applies — newer shots count more. Without recency weighting we'd risk locking in stale predictions from an earlier roast or grind setting.

## Boundaries on (a, b)

- `a ∈ [0, 5]`: drip can't shrink as flow grows; lag of 5 s/(ml/s) ≈ "every ml/s of flow contributes 5 g of drip" is already physically ridiculous.
- `b ∈ [-2, 2]`: the intercept absorbs the "puck depleted" or "puck still saturated" effect. Larger absolute values would only fit out-of-sample noise.

If a fit lands on a boundary, Phase 3 analysis flags it. Frequent boundary hits suggest the regression is being suppressed; we'd switch to Decision C (roll back) and consider a 3-feature model.

## Implausibility gate change

Old: `if drip/flow > 4 → reject` (i.e., implied lag > 4 s).
New: `if drip > 10 → reject`. Equivalent strictness in absolute terms; doesn't depend on the lag interpretation.

Both gates only catch broken inputs (sensor glitch, BLE mis-decode). The converged-mode outlier rejection (`|drip - expectedDrip| > max(3, expectedDrip)`) does the heavy lifting for "this shot doesn't match the model" cases, and that gate stays unchanged because it works against whatever `expectedDrip` returns.

## Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Regression unstable on small samples (n=2) | Medium | Clamps + lag-only fallback path. Phase 3 boundary-hit count flags this. |
| Pool entries from very different profile shapes pollute the global bootstrap | Low | Smart bootstrap pools per scale type, not globally. Per-pair predictions take over after pending-batch warm-up (n ≥ 2). |
| Fit cost shows up in BLE-side latency | Low | N ≤ 12, so ~6 multiplies + 1 division per call. Sub-millisecond on the SM-X210. |
| Existing well-tuned profiles (D-Flow, Adaptive v2) regress because their pool already gives good lag-only predictions | Medium | Phase 3 analysis breaks MAE down by profile. If any profile worsens, that's a Decision C signal. |
| Outlier batch (e.g., bad scale reading) fits a degenerate (a, b) and persists | Low | IQR/deviation gates at commit time still apply unchanged; bad batches still get rejected before they enter pair history. |

## Resolved decisions (previously open)

1. **Recency profile**: linear 10→3 (converged) or 10→1 (unconverged), matching the existing model. Rationale: keeping recency identical means any accuracy delta is attributable to the model change alone, not to a shifted weighting curve. If Phase 3 shows the regression is stable but slow to adapt, recency tightening is a separate follow-up that doesn't need this proposal's blessing.

2. **Pending-batch warm-up threshold**: ≥ 2 entries. Rationale: a 2-point regression is defined and defensible (any well-conditioned 2-point fit has zero residual; the predictor will pin the line through both observations). The clamps on `(a, b)` keep degenerate cases (e.g., two shots at nearly identical flow) from producing a bizarre slope. Phase 3 boundary-hit count will flag if this assumption was wrong.

3. **Smart bootstrap inclusion of legacy global pool**: yes, included for backwards compat. Rationale: per-scale sub-pool combining graduated medians + pending batches + the legacy pool gives the most data for the regression. If the legacy pool pollutes (e.g., entries from very different profile shapes pull the fit), Phase 3's per-pair MAE breakdown will surface it and Decision A's "no pair worsens" gate will catch it.

## Simulator (Phase 0)

The simulator at `tools/saw_replay/` is the single largest risk-reduction lever in this plan. It runs offline against a corpus of historical shots and reports OLD vs NEW predictions side-by-side for each one.

The corpus comes from extracting `[SAW] accuracy:` lines from the persistent debug log (Jeff's existing data) into a JSON list of shots. Replay walks the list in chronological order, growing the pool incrementally as each shot's data becomes available, and reports per-shot:

```
shot_id  flow   actual_drip  old_pred  new_pred  old_err  new_err  model_source
882      2.29   1.05         1.10      1.10      +0.05    +0.05    globalRegression
884      1.39   0.70         0.67      0.66      −0.03    −0.04    globalRegression
886      1.52   0.72         0.73      0.71      +0.01    −0.01    pendingBatchRegression
887      1.22   0.20         0.58      0.34      +0.38    +0.14    pendingBatchRegression
```

Aggregated metrics: MAE per flow bucket, worst-case error, clamp boundary hit count.

The simulator is the **gate** for Phase 1: if the simulator's NEW model isn't strictly better in the low-flow bucket on the historical corpus, the production code change does not ship. We don't ship a model that's worse on data we already have.

Phase 3 reuses the simulator's metric definitions exactly so the pre-deploy baseline and post-deploy analysis are directly comparable.
