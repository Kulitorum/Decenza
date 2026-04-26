# Change: Add SAW Flow-Trajectory Feature

## Why

The `tune-saw-old-prediction` proposal establishes that σ=0.25 is roughly the ceiling for flow-only SAW prediction models on the 63-shot corpus. It moves overall MAE from 0.370 g to 0.348 g (−6%) and shot 887's specific over-prediction from +0.90 g to +0.57 g (−37%) — but **leaves the worst-case error unchanged at ≈1.7 g**. That worst-case is dominated by a small set of stall-recovery shots: the puck stalls mid-pour, flow resumes suddenly, and an unusually large drip follows.

A literature review (recorded in conversation with prior-art research agent) finds:

1. **Stall-recovery is a regime change, not noise on the existing curve.** No flow-conditioned model can predict it from `flow_at_stop` alone, no matter how clever the kernel — the discriminating information is in flow's trajectory in the seconds *before* the stop.
2. **Industrial loss-in-weight feeders** (Hardy, Trantec, nVenia) handle the structurally identical problem (refill events that disturb the static weight-to-speed model) by **detecting them via flow derivative and refusing to update the model**, not by trying to predict their disturbed behavior. Direct transfer.
3. The expected MAE improvement is ~10-20% on the corpus, concentrated entirely in the worst quartile (the stall-recovery shots).

This proposal adds flow-trajectory awareness so SAW can:
- (a) **detect** stall-recovery and similar regime changes at the moment of trigger, and
- (b) **gate them out of model learning** (don't pollute `addSawLearningPoint` with shots whose drip is determined by puck dynamics the static model can't represent).

It optionally also adds df/dt as a second kernel dimension for prediction, but the primary lever is the gate — the simulator and corpus consistently show that excluding bad shots improves the model more than trying to predict them.

## What Changes

1. **`ShotTimingController` accumulates a flow buffer.** A circular buffer of `(timestamp, flowRate)` over the trailing 1.5 s, sampled at the existing flow-update cadence (5-10 Hz from the scale or DE1). Reset at extraction start.
2. **At SAW trigger, compute flow trajectory.** Fit a least-squares slope to the buffer's `(t, flow)` points. Emit the slope alongside the existing drip/flow values via `ShotTimingController::sawLearningComplete` (signal signature grows by one parameter).
3. **`Settings::addSawLearningPoint` adds a stall-recovery gate.** New parameter: `flowSlope` (ml/s²). New rule: if `|flowSlope| > kStallRecoveryThreshold` (proposed default: 0.5 ml/s²), reject the shot from learning. Log the rejection at `qWarning()` level so it flows into the persistent debug log. The shot still triggers SAW normally (the gate is purely about whether to *learn from* this shot).
4. **Optionally extend the kernel.** If Phase 0 simulator validation shows enough signal, also use `flowSlope` as a second kernel dimension in the prediction smoother. Gated by Phase 0 evidence; see tasks.md for the decision criterion.

The threshold value is the most consequential parameter. It will be tuned via simulator sweep on the 63-shot corpus before code lands.

## Impact

**Affected specs**: `stop-at-weight-learning` (adds new requirements; does not modify existing requirements from `tune-saw-old-prediction`).

**Affected code**:
- `src/controllers/shottimingcontroller.cpp` and `.h` — flow buffer + slope computation at SAW trigger
- `src/controllers/shottimingcontroller.cpp` `sawLearningComplete` signal grows by one parameter
- `src/main.cpp:528` (the lambda connected to `sawLearningComplete`) — pass `flowSlope` through to `addSawLearningPoint`
- `src/core/settings.cpp` and `.h` — `addSawLearningPoint` signature change + stall-recovery gate; same change for `addSawPerPairEntry`
- Optionally: `Settings::getExpectedDrip` and `getExpectedDripFor` extended to take a `currentFlowSlope` parameter for the second kernel dimension (gated by Phase 0 evidence)
- New tests in `tests/tst_saw_settings.cpp` covering: rejection on positive spike (stall recovery), rejection on negative spike (puck channeling), acceptance on flat trajectory (normal pour), acceptance on gradual decline (D-Flow profile end-of-shot).

**Migration**: zero. Existing pool entries don't have `flowSlope`; new entries do. The gate only applies to new entries being added; old entries in the pool stay unchanged.

**User-visible behavior**:
- A stall-recovery shot still stops at target weight (SAW logic unchanged); the only change is whether that shot's residual is learned from. Stall-recovery shots are characteristic of bad puck prep / channeling and shouldn't influence future predictions anyway, so excluding them is arguably more correct independent of MAE.
- Worst-case MAE drops because the per-pair history no longer accumulates the outliers that drag the smoother. Specifically: the +1.6 g overshoots from shots 877 (drip 2.8 g at flow 3.0) and 870 (drip 2.5 g at flow 5.2) on the corpus would have been gated out, and post-gating predictions for nearby shots improve.
- Persistent debug log gets a new line: `[SAW] rejected (stall recovery): drip=X flow=Y slope=Z` so users can diagnose why a shot didn't update their pair history.

**Risk**: low-medium. The slope threshold is the main risk knob — too tight and normal shots get rejected (under-trains); too loose and stall-recovery shots still leak through (no benefit). Phase 0 sweep tunes it on real data before shipping.

**Dependency**: builds cleanly on top of `tune-saw-old-prediction` but doesn't strictly require it. Can ship before, after, or alongside.

**What this proposal does not address**:
- Low-flow shots with surprisingly small drip (shot 887: flow 1.22 ml/s, drip 0.20 g vs typical 0.6 g at that flow). That's a different failure mode (regime collapse on `flow` alone) — see the `add-saw-pressure-feature` proposal for that.
- Shots that channel mid-extraction without producing a measurable flow-slope signature. If they exist in the post-deploy data, a separate detection mechanism would be needed.
