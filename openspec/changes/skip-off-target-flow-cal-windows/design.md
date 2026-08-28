## Context

`MainController::computeAutoFlowCalibration()` (`src/controllers/maincontroller.cpp:3030`) picks the
longest window where pressure is stable, classifies it flow- or pressure-controlled from the shot's
phase markers, then computes a calibration ideal. Since 2.0.4 a flow-classified window that
undershoots its frame's target flow by more than 10% (`autoFlowCalWindowTargetCheck()`,
`autoflowcalclassifier.cpp`) is re-routed through the pressure-branch formula
`currentMultiplier * weightFlow / (machineFlow * density)`.

That re-routing was introduced by `2026-08-18-fix-flow-cal-pressure-capped-windows` to fix
[#1823](https://github.com/Kulitorum/Decenza/issues/1823). It shipped, and
[#1872](https://github.com/Kulitorum/Decenza/issues/1872) is the same reporter saying the problem
persists.

## Evidence

The scripts in `evidence/` replay the real algorithm offline over shot curves.
`flowcal_sim.py` is a port of `computeAutoFlowCalibration()`'s sample gates, window search, formula
selection and batch/median/EMA update; `db_extract.py` pulls curves out of a `shots.db`;
`sweep.py` reports contribution rate and spread per rule.

**The port is validated against the app, not assumed.** On every shot whose recorded build matches
today's code it reproduces the logged window and ideal to three decimals, and on this repo's own
machine it reproduces a full batch update: `median 0.8407, C 0.9183 -> 0.8795` against the app's
logged `updated "d_flow_q" from 0.918313 to 0.87949 (batch median: 0.840668 from 5 ideals)`.

Three datasets, 75 shots. Measured with the SHIPPED rule — window selection unchanged, only the off-target action changed:

| dataset | before (re-route) | after (skip) |
|---|---|---|
| this repo's DE1, 23 shots | 20 contribute, median 0.937, flow 0.72-1.90 ml/s | 14 contribute, median 0.886, flow 1.26-1.90 |
| third user, 30 D-Flow shots (holds target; auto-cal OFF, C = 1.000) | 30 contribute, median 0.968, flow 1.22-1.71 | 27 contribute, median 0.968, flow 1.59-1.71 |
| third user, 20 lever/pressure shots | 19 contribute, median 1.026 | identical — the pressure branch is untouched |
| #1872 reporter, 2 shots | 2 contribute, median 1.200 (1.048 and 1.351) | 1 contributes, 1.048 |

The third user's median does not move at all, which is the result that matters for shipping. He is
a CONTROL rather than a live user — auto flow calibration is off on his machine and his multiplier
is exactly 1.000, so these are ideals the algorithm would have produced, not ones it applied. That
does not weaken the comparison (the replay never applies anything either), but it does mean this
row is not a live well-dialled user reporting no disruption. This repo's own machine moves ~5% (0.937 → 0.886), above the 3% update deadband, so its
stored multiplier will drift down over the next few batches — that is the correction working, not a
side effect: the 0.937 was held up by ideals of 0.95-1.08 taken at 0.72-1.10 ml/s on a profile that
pours at 1.8.

The flow-rate dependence that makes a mixed pool wrong is measurable on one machine at a FIXED
multiplier — weight-flow / machine-flow ratio by operating point on this repo's DE1 at C ≈ 0.88-0.92:

| machine flow | w/mf |
|---|---|
| 0.72 ml/s | 1.134 |
| 1.10 | 1.034 |
| 1.79 | 0.868 |
| 1.90 | 0.763 |

## Goals / Non-Goals

**Goals:**
- Stop feeding an operating point the profile never reaches into a constant that governs the one it
  does.
- Leave every well-dialled user's numbers materially unchanged.
- Correct the ideal FORMULA for flow-controlled windows, which converged on the square root of the
  pump-model error rather than the error itself.
- Record the multiplier each shot poured under, so a stored flow curve stays interpretable.

**Non-Goals:**
- Not changing window SELECTION (see below).
- Not touching the pressure branch, the ratio guard, batching, or the sanity bounds.
- Not resetting anyone's stored multiplier.
- Not attempting a flow-rate-dependent calibration curve. A per-operating-point model is a much
  larger design question; this change makes the existing scalar honest about what it measures.

## Decisions

### Skip, don't re-route

A window that missed target measures the pump model at a flow the profile does not pour at. Both
formulas give a defensible number for that window and both are answers to a question nobody asked:
the constant is applied across the whole shot, and on the evidence above the pump model's error at
0.7-1.1 ml/s differs from its error at 1.8 ml/s by up to 48%.

Re-routing also failed on its own terms. It did not remove the error, it reversed its sign: the
reporter's capped Aug 17 window produced 0.902 pre-2.0.4 (flow branch, pulling the multiplier down)
and 1.351 post-2.0.4 (achieved-flow branch, pulling it up). A user whose pucks cap intermittently
now gets a value that oscillates with how many shots capped that week.

**Alternative considered — keep re-routing but weight capped windows lower in the median.** Rejected:
the median already has no weighting concept, adding one is new machinery, and the premise is still
wrong. A measurement at the wrong operating point does not become right by counting less.

**Alternative considered — calibrate per operating point** (store a small curve rather than a
scalar). This is where the evidence actually points, and it is explicitly out of scope: it changes
the storage format, the MMR write, and the meaning of every stored multiplier. Recorded here so the
next person sees it was considered rather than missed.

### One formula for both control modes

The flow branch's `ideal = weightFlow / (targetFlow * density)` is replaced by the expression the
pressure branch already used, `ideal = currentFactor * weightFlow / (machineFlow * density)`.

Write `e` for that second expression's value — the flow pump model's error, and the number the
stored multiplier is meant to equal. Two behaviours are possible and they give opposite answers:

- **Model B** — the multiplier scales REPORTING only. Then `weightFlow` does not respond to it, `e`
  is proportional to the current multiplier, and iterating on `e` runs away. This is what v3
  asserted, and it is the reason the flow branch was given a target-anchored formula instead.
- **Model A** — the DE1 servos its CALIBRATED flow, so a higher multiplier delivers less water.
  Then `weightFlow` falls in proportion, `e` is invariant, and every batch's ideal is the target
  value itself.

**The logs say model A**, on every machine with enough history to test it. Across three unrelated
machines the stored multiplier moved 15-38% while `e` moved only 4-15% (cablecj74 +36% C / +5% k;
GCDE-VER1 +38% / +15%; the #1872 reporter +15% / +4%). `evidence/logscan.py` recovers this from
submitted debug logs.

Under model A the two formulas are not independent: on a window holding target
(`machineFlow ~= targetFlow`, which the skip above now enforces) the old expression equals `k / C`.

The multiplier is not set to the ideal directly — it is blended in at the batch EMA's
`alpha = 0.5`. So the old rule's update is `C := (C + k/C) / 2`, which is **Babylonian
square-root iteration**: `alpha = 0.5` is precisely the Babylonian coefficient. It settles on
`sqrt(e)`, not `e`. (The damping is load-bearing. Applied undamped, `C := k/C` is a period-2 map
that oscillates forever and converges to nothing — so the square root is a property of the update
as a whole, not of the formula alone.) That is exactly where flow-branch machines sit, while
pressure-branch machines sit on `e`:

| machine | branch | k | sqrt(e) | converged C |
|---|---|---|---|---|
| this repo's DE1 | flow | 0.737 | 0.858 | 0.8795 (+2.4% vs sqrt(e)) |
| #1872 reporter | flow | 1.44 | 1.200 | 1.17 (-2.5%) |
| mcastaldelli | pressure | 1.30 | — | 1.30 |
| cablecj74 | pressure | 1.35 | — | 1.3555 |

The sign of the error against `e` flips either side of `k = 1`, which is the signature of a square
root and not of a constant bias.

**What v2's runaway actually was.** Bad scale data reaching a formula with no ratio guards — the
per-sample and window ratio guards added alongside v2/v3 are what stopped it, and they remain. v3
changed the formula on top of that fix, so the formula change was never the thing under test.

**This is retrospective evidence and it has one clean falsification.** Two shots on one machine,
same beans back to back, at deliberately different multipliers (e.g. 1.0 and 1.4). Under model A
`e` comes out the same both times; under model B it moves with the multiplier. Run it before merge.

**Independent of the skip.** `e` still varies with flow RATE (the 48% table above), so a window must
still be measured where the profile actually pours. The two changes compose; neither substitutes for
the other.

### What `e` is, measured across seven machines

`e` is the DE1's pump-model error, not a sensor error: Decent removed the flowmeter and estimates
flow open-loop from pump strokes
([blog](https://decentespresso.com/blog/perfectly_calibrating_decent_flow_measurements)). Recovered
from submitted debug logs (`evidence/logscan.py`). This table medians over EVERY window regardless of operating point, while the on-target table above restricts to windows that held their frame's target — same quantity, two populations, which is why a machine's two numbers differ:

| user | volts | model | e median (ALL windows) | e range | n |
|---|---|---|---|---|---|
| this repo's DE1 | 120 | DE1+ | 0.83 | 0.64-1.08 | 22 |
| GCDE-VER1 | (bad read) | — (pcb 1.0) | 1.03 | 0.96-1.19 | 15 |
| ItsGoodCoffee | 120 | DE1XL | 1.29 | — | 1 |
| mcastaldelli | 220 | DE1PRO | 1.30 | 1.25-1.35 | 8 |
| nachtrieb | 120 | DE1XL | 1.32 | 0.90-1.40 | 11 |
| cablecj74 | 120 | DE1PRO | 1.37 | 1.07-1.57 | 18 |
| #1872 reporter | 120 | DE1PRO | 1.39 | 1.04-1.51 | 16 |

**Machine model dominates; mains voltage does not.** Two DE1PROs at 120 V and 220 V differ by 5%;
two 120 V machines of different models differ by 65%. Decent's compare page gives DE1PRO and DE1XL
identical pump specs and the data cannot tell them apart either. Note n=1 for DE1+, so "DE1+ machines
read low" and "this particular machine reads low" are not separable here.

Two consequences beyond this change, both worth their own issue: the multiplier defaults to 1.0 when
a PRO/XL belongs near 1.3, and `e` varies enough with operating point to question whether one scalar
per profile is the right shape at all.

### The flatness-based selection rule is NOT in this change

The same replay tested a candidate that additionally requires the window to be flat in both machine
flow and weight flow, and prefers the flattest window over the longest. It is well motivated — the
current pressure-stability gate preferentially selects the capped stretch, because a pressure
pinned at its limiter is the most stable thing in the shot, and Decent's own guidance ("It is best
to adjust flow rate data for where the pressure curve is flat",
`de1app/de1plus/plugins/Graphical_Flow_Calibrator/plugin.tcl:109`) predates flow frames with
limiters.

But the measured case for it is weaker than for the skip:

- It does NOT reduce ideal spread. On the third user's D-Flow shots spread rose 15% → 20%, because
  shorter flat windows average fewer samples. An earlier read of a smaller subset suggested it
  halved the spread; that did not survive the larger sample and is corrected here.
- It is threshold-sensitive in a way the skip is not. At 5 s / 3% flatness only 2 of 23 shots on
  this repo's machine still contribute; at 1.5 s / 10% it admits 3-second windows whose ideals swing
  25%. The workable band tested was ~3 s / 7%.

Both halves are independent — the skip needs no flatness test, and the flatness test would not have
fixed #1872 on its own (a capped window can contain a genuinely flat low-flow stretch). Shipping the
better-evidenced half alone keeps the diff a near-revert.

### Migration: clear pending batches only

Identical reasoning to the v4 migration this supersedes. The defect is per-window, not systemic: a
stored multiplier reached under the re-routing rule is a median over a mix of good and capped
windows, bounded rather than runaway, and auto-calibration will walk it back within a few batches
now that the capped windows stop contributing. Resetting every user's per-profile value to 1.0 would
force well-dialled users — the majority, on this evidence — back through several batches for a
defect their data does not exhibit.

What must not happen is a median that mixes ideals from both rules, so the pending accumulator is
cleared under a new key, `calibration/v5SkipOffTargetReset`.

## Risks / Trade-offs

- **Slower convergence for users whose pucks cap.** The reporter loses roughly 60% of his shots as
  calibration input; a batch closes in ~13 shots instead of 5. Accepted: a slow correct value beats
  a fast oscillating one, and a user capping that often has a dial-in problem the calibration cannot
  fix.
- **A profile that ALWAYS caps will never calibrate.** Its multiplier stays at whatever it is
  (global fallback, or the last value learned). That is the honest outcome — there is no on-target
  data to learn from — but it is a behaviour change worth stating: silence rather than a wrong
  number. The skip is logged per shot, so it is diagnosable.
- **This reverts a user-visible fix from 2.0.4.** #1823 will effectively return to "capped windows
  contribute nothing", which is what the pre-2.0.4 ratio guard did by accident (rejecting them as
  `flow profile ratio outside bounds`). The difference is that it is now deliberate, logged, and
  explained.

## Migration Plan

Inline in the `Settings` constructor (`src/core/settings.cpp`), following the v4 block immediately
above it:

**Two settings migrations, one schema migration.**

Settings, both through the shared `clearPendingBatchesOnce()` lambda introduced here so a future v7
cannot quietly drop the fresh-install guard or the flag commit:

1. `calibration/v5SkipOffTargetReset` — off-target windows now produce no ideal.
2. `calibration/v6UnifiedIdealFormula` — both control modes now compute the same ideal.

Each clears the pending accumulator only; stored multipliers are not reset.

Schema: migration 39 adds `shots.flow_calibration`. The version bump is gated on the column being
present — it is a schema fact, and every reader names it, so an absent column fails every shot load
and save rather than merely losing the new field. The probe distinguishes "the PRAGMA failed" from
"the column is absent" and changes nothing in the first case.

No schema change, no shot-history migration, nothing to roll back.
