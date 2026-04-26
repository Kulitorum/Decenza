# Design: Tune Stop-At-Weight Old Prediction Model

## Why this needs a design doc

Two reasons:
1. The σ value `0.25` is a magic number derived empirically. The choice — and why neighboring values are worse — should be pinned down so future readers don't second-guess it.
2. Phase 0 evaluated a coupled smart-pool change that was rejected. The reasoning matters because someone reading the spec delta might wonder why the bootstrap path is left alone, and the design doc is where that record lives.

## The math, restated

OLD model formula stays identical. Only the σ constant changes:

```
flowDiff = |flow_i − flow_query|
flowWeight = exp(-(flowDiff² / 2σ²))   // production code spells 2σ² as a single constant: 4.5 for σ=1.5, 0.125 for σ=0.25
recencyWeight = recencyMax − i × (recencyMax − recencyMin) / max(1, n−1)
weight_i = recencyWeight × flowWeight
prediction = Σ(drip_i × weight_i) / Σ(weight_i)
```

Clamps stay: `qBound(0.5, prediction, 20.0)`. Lag-fallback stays: `qMin(flow × (sensorLag + 0.1), 8.0)` when `Σ(weight_i) < 0.01`.

## Why σ=0.25, specifically

A 13-cell σ sweep on the 63-shot corpus (σ ∈ {0.10, 0.15, 0.20, 0.25, 0.30, 0.35, 0.40, 0.50, 0.75, 1.0, 1.5, 2.0, 3.0}) found a plateau between σ=0.20 and σ=0.30 where overall MAE is minimized. σ=0.25 sits in the middle of that plateau:

- σ=0.10: overall MAE 0.335 (smoother starts losing samples to "totalWeight < 0.01" fallback at edge queries)
- σ=0.20: overall MAE 0.319
- **σ=0.25: overall MAE 0.315 (minimum)**
- σ=0.30: overall MAE 0.316
- σ=0.35: overall MAE 0.322 (discontinuity for shot 887 — single low-flow median stops dominating)
- σ=1.50 (current default): overall MAE 0.366
- σ=3.00: overall MAE 0.407 (smoother degenerates to a flat average)

These were simulator numbers; production validation via `tools/saw_parity/` at σ=0.25 vs σ=1.5 confirmed overall MAE 0.348 vs 0.370 (−6%); high-flow MAE 0.592 vs 0.686 (−14%); shot 887 specific error +0.57 vs +0.90 g (−37%). Real production code on real Settings instance with real `addSawLearningPoint` to grow the pool.

## Why the smart-pool change was rejected

The archived `update-saw-prediction-model` proposal included a "smart bootstrap pool" that aggregated per-scale entries across all pairs and ran the same Gaussian-weighted-average smoother over them. The simulator suggested this would improve bootstrap-shot MAE by ~30%.

Phase 0 implemented the smart-pool branch in production code (env-var-gated by `DECENZA_SAW_BOOTSTRAP_MODE=smart`) and ran `saw_parity` four times — {σ=1.5, σ=0.25} × {scalar, smart-pool} — to A/B both axes through identical Settings/state. The result:

| | scalar | smart-pool |
|---|---|---|
| σ=1.5 | 0.370 (current shipping) | 0.389 (+0.019, *worse*) |
| σ=0.25 | 0.348 (proposed) | 0.344 (−0.004, **gate fail**) |

At σ=0.25, smart-pool's overall MAE delta vs scalar is −0.004 g — well below the 0.010 g threshold the proposal pre-committed to. More importantly, the per-bucket breakdown shows error redistribution rather than reduction:
- Smart-pool helps high-flow (MAE 0.592 → 0.429, −0.16 g)
- Smart-pool hurts low-flow (MAE 0.641 → 0.682, +0.04 g)
- **Shot 887 itself gets worse** (+0.57 g over → +0.94 g over, +0.37 g)

The very shot the original proposal was trying to fix is hurt by smart-pool. The mechanism: at the new σ=0.25, scalar bootstrap returns `flow × scalar_lag` ≈ 0.61 g for shot 887 — close to the actual 0.20 g. Smart-pool's local smoother weights the nearest committed median (a 0.7-1.0 g drip at flow 1.4-1.5 ml/s), pulling the prediction up to ~1.13 g. The locality that helps high-flow hurts low-flow when nearby-flow medians don't reflect the true drip behavior.

The simulator's claim of 30% improvement was inflated by ~3× because the simulator's pool included batches that production's IQR rejection drops. This is exactly the over-confidence the gate was designed to catch.

Smart-pool is dropped from this proposal. Bootstrap path (`flow × globalSawBootstrapLag(scale)`) is unchanged.

## What's intentionally not in this proposal

- **Per-pair pending-batch warmup** (≥2 entries from the pair's pending batch giving early pair-specific predictions): tested at thresholds 2, 3, 4, 5 — all are worse than legacy or wash. Phase 0 evidence in the archived `update-saw-prediction-model/analysis.md`.
- **Linear regression replacing weighted-avg**: tested in three flavors (vanilla, MAD-trimmed, LOWESS), all worse than OLD. Same archive.
- **Implausibility gate change** (`drip / flow > 4` → `drip > 10`): no evidence collected for or against; out of scope. The existing gate stays.
- **3-feature model** (puck-state proxy, time-since-pour, etc.): the σ change addresses the headline complaint but leaves the worst-case error (≈1.7 g for stall-recovery shots) untouched. A feature-based proposal is the right vehicle for that, and is deferred until research surfaces a concrete design.

## Test coverage gap (and how to fix it)

Existing SAW tests train and query at the *same* flow value. With training/query flow identical, `flowDiff = 0` and `flowWeight = exp(0) = 1.0` regardless of σ — so σ is invisible to those tests. This proposal will land σ=0.25 production code but tests that don't probe σ.

Tasks.md Phase 1 lists three new tests that *do* probe σ. They're load-bearing: without them, future regressions to σ won't be caught.

## Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| σ=0.25 is too narrow for users with very different flow regimes (e.g., turbo profiles at 8+ ml/s) | Low | Smoother gracefully falls through to lag-fallback when totalWeight collapses. Tested at flows 1.2–5.2 ml/s; behavior at 8+ ml/s is `qMin(flow × (sensorLag + 0.1), 8.0)` exactly as today. |
| Single-user 63-shot corpus may not generalize | Medium | Phase 2 shadow logging captures both σ=1.5 (synthetic from `oldSigmaDrip`) and σ=0.25 predictions on every shot for at least one release cycle, allowing real-world A/B before locking in. |
| Worst-case error stays unchanged (≈1.7 g for stall-recovery shots) | High (this proposal doesn't address it) | Out of scope. Stall-recovery is a feature problem; addressed by a future proposal that adds a new feature dimension. |

## Resolved decisions

1. **σ value is 0.25.** Plateau between 0.20 and 0.30; midpoint chosen for stability against future corpus expansion.
2. **Bootstrap path is unchanged.** Phase 0 evidence is conclusive on this corpus.
3. **No new persistence keys.** Pool is built from existing `saw/perProfileHistory` and `saw/learningHistory` keys.
4. **`globalSawBootstrapLag` stays.** It's still consulted by the unchanged bootstrap path.
