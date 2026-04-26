# Change: Add SAW Pressure Feature

## Why

The σ-tuning proposal (`tune-saw-old-prediction`) and the flow-trajectory proposal (`add-saw-flow-trajectory-feature`) together address two of the three known SAW failure modes:
- **σ-tuning**: high-flow over-prediction caused by mid-flow medians bleeding into high-flow predictions (resolved at σ=0.25).
- **Flow trajectory**: stall-recovery shots that pollute the model with non-representative drip values (resolved by the rejection gate).

The remaining failure mode is **low-flow shots with surprisingly small drip** — exemplified by shot 887 in the corpus: flow=1.22 ml/s, drip=0.20 g, while a typical shot at that flow has drip=0.6-0.7 g. Even after σ=0.25 ships, shot 887's prediction is still +0.57 g over actual.

A literature review (recorded in conversation with prior-art research agent) identified this as a regime-collapse failure: at low flow, the puck's state can vary widely without being visible in the flow signal alone. Cameron, Hendon, and Wadsworth's 2019 *Matter* paper [Systematically Improving Espresso](https://www.cell.com/matter/fulltext/S2590-2385(19)30410-2) and the 2026 [Royal Society Open Science permeability paper](https://royalsocietypublishing.org/rsos/article/13/4/252031/) frame espresso flow as Darcy flow through a porous puck, where:

```
flow = (permeability × pressure_gradient) / viscosity
```

Permeability is set by the puck (grind, dose, prep). At a given flow rate, the puck's permeability separates two regimes:
- **Tight puck, high pressure**: water is barely moving through; residual drainage after stop is small. (Shot 887 — small drip)
- **Wide puck, low pressure**: water is freely moving; residual drainage is larger. (Typical low-flow shot — larger drip)

Flow alone collapses these two regimes. **Pressure (or pressure-divided-by-flow as a permeability proxy) separates them.**

Expected MAE improvement, per the prior-art review: 5-10% overall, larger on low-flow shots specifically. Smaller than the flow-trajectory gain (10-20% on worst quartile) but addresses a different failure mode and stacks cleanly with it.

## What Changes

1. **Capture pressure-at-trigger** in `ShotTimingController` at the moment SAW fires. The DE1 already streams pressure at high frequency (≥10 Hz typically); subscribe to `DE1Device::pressureChanged` and store the most recent value. At trigger, that becomes the `pressureAtStop` value carried alongside `flow` and `drip` into the learning signal.
2. **Extend the learning entry schema** so the per-pair history (`saw/perProfileHistory`) and the global pool (`saw/learningHistory`) store `pressure` per entry, in addition to existing `drip`, `flow`, `overshoot`, `scale`, `profile`, `ts`. Backwards-compatible: old entries without `pressure` continue to work in a fallback path that uses the 1-D kernel.
3. **Extend the prediction kernel from 1-D to 2-D**. The Gaussian smoother weights an entry by both flow-similarity and pressure-similarity:
   ```
   flowDiff = |flow_i − queryFlow|
   pressureDiff = |pressure_i − queryPressure|
   weight = recency × exp(-(flowDiff² / 2σ_f²) − (pressureDiff² / 2σ_p²))
   ```
   `σ_f = 0.25 ml/s` (per the σ-tuning proposal). `σ_p` is a new parameter; Phase 0 sweeps to find the optimum.
4. **Pressure parameter added to the prediction APIs**. `Settings::getExpectedDrip(flow, pressure)`, `Settings::getExpectedDripFor(profile, scale, flow, pressure)`, and `WeightProcessor::getExpectedDrip(flow, pressure)` all gain a pressure parameter. Backwards-compatible overloads (one-arg versions returning the 1-D fallback) remain so external callers — like the QML diagnostic surface — don't all need updates simultaneously.

The proposal does NOT change the bootstrap path or the substate-stratification design — those are independent and can stack on top.

## Impact

**Affected specs**: `stop-at-weight-learning` (adds new requirements; does not modify existing requirements from `tune-saw-old-prediction` or `add-saw-flow-trajectory-feature`).

**Affected code**:
- `src/controllers/shottimingcontroller.cpp` and `.h` — capture `pressureAtStop` at trigger; extend `sawLearningComplete` signal signature
- `src/main.cpp:528` lambda — receive and forward `pressureAtStop`
- `src/core/settings.cpp` and `.h` — extend `addSawLearningPoint` / `addSawPerPairEntry` to store pressure; extend `getExpectedDrip` / `getExpectedDripFor` to take and use pressure; the per-pair JSON schema gains a `"pressure"` field
- `src/machine/weightprocessor.cpp` and `.h` — extend `configure` to take pressure snapshot; extend `getExpectedDrip` to take pressure; subscribe to pressure updates if not already
- New tests covering: 2-D kernel correctness (predictions vary with pressure when flow is held constant), backwards-compatibility (old entries without pressure still produce predictions via the fallback path), σ_p sweep results (locks in the chosen σ_p value).

**Migration**:
- Existing per-pair entries without `pressure` are read with `pressure = -1` (or some sentinel). The 2-D kernel falls back to the 1-D form (no pressure-axis weighting) for those entries. As new entries accumulate with pressure, the kernel gradually transitions to full 2-D behavior on a per-pair basis.
- No version bump; no on-disk migration step.

**User-visible behavior**:
- Shot 887-class shots (low-flow, tight puck, high pressure) get a smaller predicted drip than they would under the σ=0.25-only model — closer to actual.
- Shots where the kernel doesn't have nearby pressure data (e.g., D-Flow / Q profile users with only Adaptive v2 history) fall back to flow-only predictions, identical to today.
- No latency impact: pressure read is one additional `qExp` call per entry in the smoother, sub-millisecond on the SM-X210.

**Risk**: medium. Larger surface area than σ-tuning (4 files vs 3, schema change vs constant change). Phase 0 production validation must confirm the 2-D kernel actually wins on real production code before shipping; the simulator's known port gaps (no IQR rejection) make simulator-only evidence insufficient.

**Dependency**: requires `tune-saw-old-prediction` to land first (the 2-D kernel formula uses σ_f=0.25 from that proposal). Does NOT depend on `add-saw-flow-trajectory-feature` — can ship before, after, or alongside.

**What this proposal does not address**:
- Stall-recovery shots — that's `add-saw-flow-trajectory-feature`.
- Substate effects (preinfusion vs pour vs decline) — deferred to a future proposal once we see what residuals are left after pressure + trajectory ship.
- Worst-case error (≈1.7 g) — flow-trajectory addresses that, not pressure.
