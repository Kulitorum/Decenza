# Design: Add SAW Flow-Trajectory Feature

## Why this needs a design doc

Three reasons:
1. The feature is genuinely new (df/dt isn't currently captured per shot), so the data flow from `ShotTimingController` through to `Settings::addSawLearningPoint` needs to be designed before coded.
2. The threshold value `kStallRecoveryThreshold` is the most consequential parameter and there's no prior art to lift from. Phase 0 will tune it on real data — but the *methodology* of that tuning (corpus extension + simulator sweep + production validation) belongs here so reviewers can sanity-check it before any implementation.
3. The proposal makes a deliberate choice to use df/dt as a **classifier** (gate bad shots out) rather than a **kernel feature** (predict their drip). The reasoning matters because it's the opposite of what you'd intuit from "more features = better predictions."

## Why classify, not predict

A flow-trajectory feature could be used two ways:
- **(a) As a classifier**: detect stall-recovery and similar regime changes, exclude those shots from learning. The model continues to predict using the existing flow-only kernel; bad shots simply don't pollute future predictions.
- **(b) As a kernel dimension**: weight history entries by both flow-similarity AND flow-slope-similarity, making the smoother condition on both. Stall-recovery predictions come from other stall-recovery history entries.

Use (a) is preferred because:

1. **The number of stall-recovery shots is small** (≈5-10% of the corpus). Use (b) requires that small set to be self-consistent enough to predict each other accurately. A 2-shot stall-recovery history is barely a model.
2. **The user value of a precise stall-recovery prediction is questionable.** A stall-recovery shot is itself a prep failure (channeling, bad puck distribution). The right user response is to fix the puck, not get a precisely-late stop. Excluding them from learning is arguably more correct than predicting them better.
3. **Industrial precedent** (loss-in-weight feeders) does (a), not (b). Refill events are detected by flow-rate disturbance and the model is held constant during them. Same problem class.
4. **Use (b) is computationally larger**: 2-D kernel, new σ_s parameter to tune, schema change for per-pair history (entries grow a column). Use (a) is a single threshold compare.

That said, the proposal leaves the door open: tasks.md Phase 0b includes an *optional* `--use-trajectory-kernel` flag. If the simulator shows the kernel-dimension approach gives substantially more MAE reduction than the classifier approach (a stretch given the reasoning above), the proposal can pivot. The base case is the classifier.

## Data flow

```
DE1Device.flowChanged →
  ShotTimingController.flowChanged (existing)
    ┌─ updates the flow buffer (NEW, capped at 1.5 s)
    └─ existing logic runs unchanged

WeightProcessor.stopAtWeightTriggered →
  ShotTimingController.startSawSettling (existing)
    ↓ (after settling completes)
  ShotTimingController.sawLearningComplete(drip, flow, overshoot, flowSlope)  ← new param
    ↓
  main.cpp:528 lambda
    ↓
  Settings::addSawLearningPoint(drip, flow, scale, overshoot, profile, flowSlope)  ← new param
    ┌─ NEW: stall-recovery gate (reject if |flowSlope| > kStallRecoveryThreshold)
    └─ existing logic runs unchanged
```

The flow buffer lives in `ShotTimingController` because that's already the SAW-coordinator object and already has the flow signal wired up. Putting the buffer in `WeightProcessor` would also work but `WeightProcessor` runs on a worker thread; `ShotTimingController` is on the main thread next to `MainController`, simpler signal plumbing.

## Slope computation

Least-squares fit on the buffer's `(timestamp, flowRate)` points. Standard one-pass formula:

```
n = buffer.size()
sum_t = Σ t_i
sum_f = Σ flow_i
sum_tf = Σ t_i × flow_i
sum_tt = Σ t_i²
slope = (n × sum_tf − sum_t × sum_f) / (n × sum_tt − sum_t²)
```

Returns 0.0 if `n < 3` or denominator is 0 (collinear timestamps — shouldn't happen in practice). Units: ml/s² (flow in ml/s, time in s).

The buffer is capped at 1.5 s of history specifically. Justification:
- Too short (< 0.5 s) and the slope is dominated by per-sample noise.
- Too long (> 3 s) and the slope is dominated by the entire pour shape, not the trajectory at trigger time. We want the *recent* trajectory.
- 1.5 s aligns with the existing `[SAW-Worker] flowShort` window structure.

## Threshold choice

Symmetric: a slope > +T or < -T both reject the shot.

- Positive slope (flow accelerating into the trigger) — stall-recovery signature. Puck stalled, then released; flow jumps; drip will be unusually large.
- Negative slope (flow decelerating into the trigger) — could be normal end-of-shot for a declining-flow profile (D-Flow / Q's tail), but a *sharp* negative slope is channeling: flow is collapsing because water is finding an easier path. Drip after channeling is unpredictable.

Phase 0c's threshold sweep on real data is the only way to set the constant. Initial guess based on the literature analogue (loss-in-weight feeders use ~0.4-0.5 in their domain): `0.5 ml/s²` as a starting point. Phase 0b sweeps {0.2, 0.3, 0.4, 0.5, 0.7, 1.0} and picks the optimum.

## Why the existing physical-validity check (`drip > 10`) is insufficient

The existing `addSawLearningPoint` already has a gate that rejects entries with `drip > 10` g (sensor-glitch detection). That doesn't catch stall-recovery shots: shot 870 has drip 2.5 g, shot 877 has drip 2.8 g — both well below the 10 g cap. The flow-trajectory gate fires on the *signature* of the regime change, not on the resulting drip.

Conversely, the existing converged-mode outlier rejection (`if |drip - expectedDrip| > max(3, expectedDrip)`) DOES catch stall-recovery shots once the model has converged. But:
- It only fires when `isSawConverged()` returns true (≥3 entries with avg |overshoot| < 1.5 g).
- It doesn't catch the first stall-recovery shot for a new pair (model hasn't converged yet, no prediction to deviate from).
- It catches the symptom (large deviation) not the cause (regime change), so it can also reject genuinely-novel-but-correct shots.

The flow-trajectory gate is independent: fires on the input signature, before the per-pair model is consulted, regardless of convergence state.

## Test coverage strategy

The new tests in tasks.md Phase 1 are categorized:

- **Threshold unit tests** (deterministic): inject specific flowSlope values, assert acceptance/rejection. Defends the threshold constant against accidental regression.
- **Symmetric tests**: positive and negative spikes both reject. Defends against asymmetric regression (e.g., "if (slope > T)" instead of "if (|slope| > T)").
- **Boundary tests**: exactly at threshold, just above, just below. Defends the comparison operator.

The simulator validation in Phase 0 is the *quantitative* evidence — these unit tests are the *correctness* evidence (the gate works as documented).

## Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Threshold tuned on this corpus is wrong for production users on different scales/profiles | Medium | Phase 0d sensitivity check (±25% range still passes) gives margin. Phase 3 manual inspection of post-deploy rejections catches drift. |
| Buffer accumulates wrong samples (e.g., during preinfusion when flow is low and unstable) | Low | Buffer reset at extraction start; samples only matter at SAW trigger which is by definition during pouring substate. |
| Slope computation is noisy at low scale-update rates (Bookoo at 2 Hz) | Medium | Buffer's 1.5 s window captures 3+ samples even at 2 Hz. Slope rejected as 0.0 if buffer < 3 points. Phase 3 should bucket rejection rate by scale type to catch any scale-specific issue. |
| Genuine stall-recovery shots produce slopes below the threshold (false negatives) | Medium | Phase 3 manual inspection. If false negatives are common, propose a secondary signal (e.g., flow variance) as a follow-up. |
| The 5-10% rejection rate eats too much learning data, slowing pair graduation | Low | Pair graduation requires 2 medians = 10 shots. A 10% rejection rate adds 1 shot to the path. Acceptable. |

## Resolved decisions

1. **Use df/dt as a classifier, not a kernel feature.** Rationale above; door left open in tasks.md Phase 0b.
2. **Buffer in `ShotTimingController`, not `WeightProcessor`.** Threading + signal plumbing simpler.
3. **1.5 s window**, least-squares slope, symmetric threshold. All tunable in Phase 0 if simulator suggests otherwise.
4. **Threshold value TBD** by Phase 0c production validation. Initial guess 0.5 ml/s²; final value lands in `settings.cpp` as a `static constexpr`.
5. **Existing converged-mode outlier rejection stays** — it's the second line of defense for shots that pass the trajectory gate but turn out to be outliers in drip space.
