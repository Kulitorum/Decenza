# Design: Add SAW Pressure Feature

## Why this needs a design doc

Three reasons:
1. The feature changes the prediction-API signature across multiple files and adds a column to a persisted JSON schema. The migration story (how old entries without pressure interact with new code) needs to be pinned down.
2. The 2-D kernel introduces a second bandwidth parameter (σ_p) without prior calibration. The Phase 0 sweep methodology and the sentinel-value backwards-compat path need explicit design.
3. The proposal's premise (pressure separates regimes that flow alone collapses) rests on academic theory and a single corpus example (shot 887). The design needs to be falsifiable in Phase 0 — which spot-checks make the premise break, and what's the abandon criterion.

## The math

Current OLD model (post-`tune-saw-old-prediction`):
```
weight_i = recencyWeight_i × exp(-(flowDiff² / 2σ_f²))   // σ_f = 0.25 ml/s
prediction = Σ(drip_i × weight_i) / Σ(weight_i)
```

New 2-D OLD model:
```
flowDiff = |flow_i − queryFlow|
pressureDiff = |pressure_i − queryPressure|
weight_i = recencyWeight_i × exp(-(flowDiff² / 2σ_f²)) × exp(-(pressureDiff² / 2σ_p²))
        = recencyWeight_i × exp(-(flowDiff² / 2σ_f²) − (pressureDiff² / 2σ_p²))
prediction = Σ(drip_i × weight_i) / Σ(weight_i)
```

The kernel is product-of-Gaussians (factorizable), not Mahalanobis (correlated). Justification: flow and pressure are not independent (the puck couples them), but the noise structure on each is roughly independent at the moment of trigger — the puck dynamics that connect them happen *before* trigger. Product-of-Gaussians is computationally cheaper and doesn't require estimating a covariance matrix.

Clamps stay: `qBound(0.5, prediction, 20.0)`. Lag-fallback stays: `qMin(flow × (sensorLag + 0.1), 8.0)` when `Σ(weight_i) < 0.01`.

## Why pressure (not pressure/flow ratio)

Two reasonable choices for the second feature:
- **Raw pressure (bar)** at trigger
- **Pressure/flow ratio** (a permeability proxy: bar / (ml/s) ≈ s·bar/ml)

The proposal uses raw pressure. Reasoning:

1. **Raw pressure is what the DE1 actually measures and streams.** Pressure/flow ratio requires both signals and division by a small flow value at the moment of trigger, which is numerically unstable for low-flow shots — exactly the regime the proposal is trying to fix.
2. **The kernel's σ_p naturally captures flow-conditioned variation.** If a 1.2 ml/s query at 8 bar is best matched by 1.2 ml/s + 8 bar history entries, the 2-D kernel finds them automatically. There's no advantage to pre-dividing.
3. **Forward-compatibility**: if a future proposal wants to add pressure-derivative or peak-pressure-during-pour as features, raw pressure is the more natural foundation.

Phase 0b's sweep is over σ_p in bars, not in ratio units.

## Schema migration: backwards-compatibility for old entries

Old per-pair history entries (stored before this proposal lands) don't have a `pressure` field. The 2-D kernel must handle that gracefully.

Two options:
- **(a) Sentinel value**: read missing pressure as `-1` (or NaN). In the kernel, when an entry's pressure is the sentinel, the pressure-axis weight is forced to 1.0 (no contribution). The 2-D kernel reduces to 1-D for that entry.
- **(b) Migration sweep**: at first launch under the new code, walk `saw/perProfileHistory` and `saw/learningHistory` and add `pressure: -1` to every entry. Same end state but with an explicit migration step.

The proposal uses (a). It avoids any migration code path, has zero on-disk write cost, and the sentinel is invisible to anyone inspecting the JSON ("oh, this entry doesn't have pressure" is the right interpretation).

The kernel implementation:
```cpp
double pressureWeight = 1.0;  // default if entry has no pressure
if (entry.pressure >= 0.0 && queryPressure >= 0.0) {
    double diff = entry.pressure - queryPressure;
    pressureWeight = std::exp(-(diff * diff) / (2.0 * sigmaP * sigmaP));
}
double weight = recencyWeight * flowWeight * pressureWeight;
```

The same logic applies on the query side: if the caller passes `queryPressure = -1` (the single-arg overload), pressureWeight is 1.0 for every entry, and the kernel is purely 1-D.

This means the proposal degrades gracefully in three scenarios:
- **No history has pressure** (immediately post-deploy): kernel is 1-D, behavior identical to today.
- **Some history has pressure**: those entries contribute via 2-D, others via 1-D. Predictions blend.
- **All history has pressure** (after ~10 shots accumulate): kernel is fully 2-D, expected gains realized.

## Sentinel value choice

Use `-1.0` (not 0.0, not NaN). Reasons:
- Real pressure values are always ≥ 0 (gauge pressure can't be negative on the DE1)
- A check `if (pressure < 0)` is cleaner than NaN comparison (which has surprising semantics)
- Stored as a JSON number, `-1` round-trips cleanly; NaN doesn't (JSON has no NaN representation)

## σ_p choice

No prior art for this exact kernel. Phase 0b sweeps σ_p ∈ {0.5, 1.0, 1.5, 2.0} bar at σ_f=0.25 ml/s. Initial guess: 1.0 bar (typical pressure variation across the puck-permeability regime is roughly 2-3 bar from very tight to very loose; σ=1.0 captures meaningful variation while still producing nontrivial weights for samples ~2 bar from the query).

Phase 0b picks the value that:
- Minimizes overall MAE
- Doesn't regress any flow bucket vs σ_p=∞ (i.e., the 1-D baseline)
- Doesn't make shot 887 prediction worse than σ=0.25 1-D

If multiple σ_p values pass these criteria, pick the largest (smoother prediction, less sensitivity to per-shot pressure noise).

## Why the proposal can fail

Several ways:

1. **Spot-check fails (Phase 0a).** If shot 887's pressure isn't notably higher than other low-flow shots in the corpus, the proposal's premise (pressure separates regimes) is wrong. Abandon.
2. **Simulator gate fails (Phase 0b).** If the 2-D kernel can't beat the 1-D baseline by ≥0.005 g overall MAE on the corpus, there's no benefit to ship.
3. **Production gate fails (Phase 0c).** Even if the simulator says it works, production-code validation through `saw_parity` is the load-bearing test (the simulator's port has known IQR-rejection gaps). If the production gate fails, abandon.
4. **Backwards-compat regression (Phase 0d).** If the pressure feature degrades 1-D predictions for entries without pressure (e.g., due to a bug in the sentinel-value path), abandon and re-investigate.

## Interaction with `add-saw-flow-trajectory-feature`

The two proposals are orthogonal:
- Flow-trajectory addresses stall-recovery shots (regime change in *time*; gates them out of learning).
- Pressure addresses low-flow regime collapse (regime ambiguity in *flow space*; adds a discriminating feature).

If both ship, they stack: pressure improves the 2-D kernel for accepted shots, while flow-trajectory rejects shots whose drip is unpredictable from any kernel. Neither's gain depends on the other.

The `addSawLearningPoint` signature would gain *two* new parameters in the combined case: `flowSlope` (used by the rejection gate) and `pressureAtStop` (stored for kernel use). Both are part of the same data flow — `ShotTimingController` captures both at trigger and forwards both via `sawLearningComplete`.

## Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Single-corpus σ_p generalizes poorly | Medium | Shadow logging in Phase 2 captures both 1-D and 2-D predictions; Phase 3 A/B confirms post-deploy. |
| Schema change introduces a regression on first install | Low | Sentinel-value path tested in Phase 0d; integration tests added in Phase 1. |
| `addSawLearningPoint` signature growth makes future signature changes harder | Low | Already growing for flow-trajectory; if either lands, future feature additions follow the same pattern. |
| Pressure noise at trigger (BLE jitter, sensor calibration drift) corrupts kernel weights | Low-medium | DE1 pressure is sub-bar precision; σ_p ≥ 0.5 bar provides margin. Phase 3 monitoring catches if real-world noise is larger. |
| Premise is wrong (pressure doesn't separate regimes) | Medium-low | Phase 0a spot-check is explicitly designed to falsify; abandons before any code lands. |

## Resolved decisions

1. **Raw pressure, not pressure/flow ratio.** Numerical stability at low flow.
2. **Product-of-Gaussians kernel, not Mahalanobis.** Computationally cheaper; flow/pressure noise approximately independent at trigger.
3. **Sentinel value `-1.0` for missing pressure.** Cleaner than NaN; round-trips through JSON.
4. **σ_p TBD by Phase 0b sweep.** Initial guess 1.0 bar; Phase 0c locks in the production value.
5. **No on-disk migration step.** Sentinel-value path handles backwards-compat at read time.
