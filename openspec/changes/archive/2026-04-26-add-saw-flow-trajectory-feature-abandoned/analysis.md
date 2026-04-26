# Analysis: Phase 0a Falsification

**Status: ABANDONED 2026-04-26.** The proposal's central hypothesis — that the worst-case SAW overshoots in the corpus are stall-recovery events detectable via a positive flow-slope spike in the trailing 1.5 s before SAW trigger — does not survive the data. This document preserves the falsification evidence so the same investigation does not get re-run later.

## What Phase 0a was supposed to validate

`tasks.md` Phase 0a required spot-checking the harvest:

> Shots 877 (drip 2.8 at flow 3.0, expected stall recovery → large positive slope) and 870 (drip 2.5 at flow 5.2, expected stall recovery → large positive slope) MUST land at flowSlope > +0.4 ml/s² to validate the harvest is correct. Shots 884, 886, 887 (low-flow, normal puck) MUST land near 0 ml/s².

The "MUST" criterion is the hypothesis under test: if these specific shots aren't trajectory outliers, the proposal's classifier has nothing to classify.

## What the data shows

Least-squares slope on the trailing 1.5 s before SAW trigger, computed from the shot detail's `weightFlowRate` and DE1 machine `flow` time-series (both available via `mcp__de1__shots_get_detail`):

| shot | drip (g) | overshoot | trigger flow | wfr slope | machine-flow slope |
|---|---|---|---|---|---|
| 877 | 2.8 | +1.6 | 3.04 | **−0.01** | **−0.03** |
| 870 | 2.5 | +1.5 | 5.20 | **+0.05** | **+0.06** |

Both shots are essentially flat at trigger time. Required: > +0.4 ml/s². Actual: 0.05 ml/s². **Falsified by ~8×.**

Inspection of the full flow trajectories confirms there is no stall-and-release event in either shot. They are normal preinfusion → rise-and-hold → decline pours that happened to hit target weight while flow was high (5.2 / 3.0 ml/s) because the puck never built sufficient resistance.

## Why the hypothesis was wrong

A literature transfer from industrial loss-in-weight feeders motivated the design: refill events disturb the static weight-to-speed model, are detected via flow derivative, and excluded from learning. The design assumed the SAW corpus's worst-case errors had the same structure.

But the corpus's worst-case errors are not regime-change events. They share one feature: **short pour duration + high trigger flow**.

| shot | pour duration | trigger flow | overshoot |
|---|---|---|---|
| 868 | ~7 s | 8.46 | huge (stop@33.7g vs 36g target) |
| 870 | ~12 s | 5.20 | +1.5 |
| 874 | ~13 s | 4.73 | +1.3 |
| 877 | ~20 s | 3.04 | +1.6 |
| 879 | ~14 s | 3.94 | n/a (recent) |

The puck never built full resistance, target was reached during high-flow extraction, and the residual drip at high flow is physically larger than the SAW model predicts because the model has very few high-flow training points. This is **regime-collapse on flow alone**, not a trajectory phenomenon. Flow-trajectory has no leverage here because the trajectory looks normal — the *level* is the issue, not the *change*.

## Alternative signals also explored (per Jeff's "sample 2 and 3" prompt)

To rule out a methodology error, two alternative signals were checked across 7 corpus shots (3 high-overshoot, 4 small/negative-overshoot):

**Window variation (Option 2)** — sliding the slope window from 1.5 s up to 5 s, and shifting it earlier into the preinfusion-to-pressurization transition. The transition slope (8–11 s on shot 870, ~−0.35 ml/s²) is present on essentially every shot at similar magnitude — it's the shape of the profile, not an anomaly. No window isolates a stall-release on the high-overshoot shots because the event isn't there.

**Machine-vs-weight gap (Option 3)** — the auto-flow-cal ratio (mean machine flow / mean weight flow over the steady-state window) was extracted from each shot's debug log. Results:

| shot | overshoot | flow cal ratio |
|---|---|---|
| 870 | +1.5 | 1.139 |
| 874 | +1.3 | 1.149 |
| 879 | recent | 1.166 |
| 877 | +1.6 | 1.071 |
| 878 | −0.9 | 1.076 |
| 882 | −0.05 | 1.018 |
| 884 | −0.3 | 0.972 |
| 886 | −0.5 | 1.013 |
| **887** | **−0.9** | **1.115** |

The ratio mostly correlates with **flow level** (high-flow shots ratio 1.07-1.17; low-flow shots ratio ~1.0) rather than overshoot. Shot 887 — the smallest drip in the corpus — breaks the pattern with ratio 1.115, showing the gap conflates flow regime with whatever it was supposed to detect.

## What this implies for the worst-case-MAE budget

The +1.5 g overshoots are not addressable by a flow-trajectory gate. They are addressable by a model that conditions on something other than flow alone — pressure being the obvious candidate, since pressure-vs-flow distinguishes "high flow because puck never built resistance" from "high flow despite resistance". That work belongs in the `add-saw-pressure-feature` proposal.

If a real channeling shot (sharp negative flow slope mid-pour) shows up in post-deploy data, a separate detection mechanism could be revisited — but the current corpus does not appear to contain one.
