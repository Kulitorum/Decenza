## Why

The 2026-08-18 change `fix-flow-cal-pressure-capped-windows` (shipped in 2.0.4) routes a
flow-controlled window that undershoots its frame's target flow by more than 10% through the
achieved-flow (pressure-branch) formula, on the reasoning that a pressure-capped window still
measures something usable. Replaying the algorithm against real shots from three machines shows it
does not: a capped window measures the pump model at a completely different operating point from the
one the profile pours at, and folding it into the same per-profile constant is what makes that
constant move.

[Kulitorum/Decenza#1872](https://github.com/Kulitorum/Decenza/issues/1872) is the visible symptom —
the same reporter as #1823, back to say the fix did not resolve it. His own two shots show the
mechanism exactly (his `D-Flow / 20g-2.50-91`, pour frame flow 1.70 ml/s with an 8.50 bar limiter):

| his shot | regime | ideal produced |
|---|---|---|
| Jan 8 | reaches target, 1.70 ml/s | **1.048** (flow branch) |
| Aug 17 | capped, 1.15 ml/s, pressure pinned at 8.4-8.6 bar | **1.351** (reclassified) |

Both are honest measurements 29% apart, averaged into one median. Worse, the reclassification
reversed the direction of the error rather than removing it: pre-2.0.4 that same capped window
produced 0.902 through the flow branch (pulling his calibration DOWN — the original #1823
complaint), and post-2.0.4 it produces 1.351 (pulling it UP). His value now oscillates between two
attractors depending on how many of that week's shots capped.

The underlying fact, measured on this repo's own DE1 at a fixed multiplier: the ratio of scale
weight flow to reported machine flow rises monotonically as flow falls — 0.76 at 1.9 ml/s, 1.03 at
1.1 ml/s, 1.13 at 0.72 ml/s, a 48% swing with no change in calibration. A single scalar multiplier
cannot describe both ends, so the only correct thing to do with a window that missed its target is
to not measure from it at all.

## What Changes

- A flow-controlled window whose measured mean machine flow undershoots the frame's target flow by
  more than the existing 10% threshold SHALL be **skipped** — the shot contributes no ideal — rather
  than re-routed through the achieved-flow formula.
- `autoFlowCalWindowTargetCheck()` and its threshold constant stay exactly as they are; only the
  ACTION taken on a positive result changes. The overshoot asymmetry is unaffected.
- The pressure-branch formula stays for genuinely pressure-controlled windows. Nothing about
  pressure profiles changes.
- The debug line at the decision point changes from "treating as pressure-capped, using
  achieved-flow formula" to a skip, naming measured flow, target flow and deviation, so a submitted
  log still explains why a shot produced no ideal.
- One-time migration clears pending per-profile flow-cal batches (`calibration/flowCalBatch`) only,
  so no batch median mixes ideals computed under the reclassifying rule with ideals computed under
  this one. Applied multipliers are NOT reset — same reasoning as the v4 migration this supersedes.

Not in this change (deliberately — see `design.md`): the flatness-based window SELECTION rule. It is
supported by weaker evidence, needs threshold tuning, and is separable.

## Capabilities

### Modified Capabilities
- `auto-flow-calibration`: the achieved-flow deviation check now skips the window instead of
  switching formulas. Window selection, the ratio guard, batching, sanity bounds and the pressure
  branch are all unchanged.

## Impact

- `src/controllers/maincontroller.cpp` — `computeAutoFlowCalibration()`, the reclassification block
  and its log line
- `src/controllers/autoflowcalclassifier.{h,cpp}` — unchanged in behaviour; comments describing the
  consequence of a positive check need updating
- `src/core/settings.cpp` — v5 pending-batch clear, following the v4 pattern immediately above it
- `tests/tst_autoflowcal.cpp` — coverage for skip-instead-of-reroute
- `docs/CLAUDE_MD/AUTO_FLOW_CALIBRATION.md` — replace the v4 write-up's "re-routed to the pressure
  formula" description with the skip, and record why
- No wiki manual change: the user-facing contract (per-profile, automatic, 5-shot batches) is
  unchanged. What changes is which shots feed it.
- No MCP surface change.

## One formula for both control modes

Flow-controlled windows now use the same pump-model-error expression pressure-controlled windows have
always used. The target-anchored expression v3 introduced converges on the square root of the flow
pump model's error rather than the error itself, which is measurably where flow-profile machines sit
(this repo's DE1 on 0.8795 against sqrt(0.737) = 0.858; the #1872 reporter on 1.17 against
sqrt(1.44) = 1.200) while pressure-profile machines sit on the ratio itself. v3's premise — that
the pump-model expression is proportional to the current multiplier — requires the multiplier to scale
reporting only; the DE1 servos its calibrated flow instead, and across three machines the
multiplier moved 15-38% while the pump-model expression moved 4-15%. Full derivation and the falsifying
experiment are in `design.md`.

## Expected effect on users

Replayed over 75 shots from three machines (`evidence/`, method in `design.md`):

- **A machine that holds its target sees no change.** A third user's 30 D-Flow shots hit 1.69
  against a 1.70 target on 24 of 30; contribution drops 30/30 → 27/30 and the median ideal does not
  move at all (0.968 → 0.968), while the sampled flow range narrows from 1.22-1.71 ml/s to
  1.59-1.71. **Read this as a control, not as a live user**: he has auto flow calibration switched
  OFF and his multiplier is exactly 1.000, so these ideals are what the algorithm *would* have
  computed, never what it did. It is still the right comparison for "does the skip disturb a
  well-behaved machine" — nothing about the replay depends on the value being applied — but it is
  not evidence that a live well-dialled user is unaffected, and an earlier draft of this line
  implied that it was.
- **Pressure profiles are untouched**, as the branch is: his 20 lever shots are identical either
  way — 19/20 contributing, median 1.026, same flow range.
- **This repo's DE1**: the sampled flow range narrows from 0.72-1.90 ml/s to 1.26-1.90, contribution
  20/23 → 14/23, and the median ideal moves 0.937 → 0.886. That last figure is the honest cost line:
  ~5%, above the 3% update deadband, so this machine's stored multiplier will drift down over the
  next batches. That is the correction — the 0.937 was propped up by ideals of 0.95-1.08 measured at
  0.72-1.10 ml/s, which is not where this profile pours.
- **The #1872 reporter**: his capped shots stop contributing. From his August log only 3 of 8
  windows sat within 10% of target, so roughly 35-40% of his shots will feed calibration and a
  5-shot batch will close in ~13 shots. His multiplier will converge toward ~1.05 (the value his
  on-target windows measure) and stay there, instead of oscillating. It will NOT converge to the
  1.35 he set by hand — that number is his capped tail, not his pump model, and the honest answer
  to it is a dial-in conversation.
