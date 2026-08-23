# Change: Commit A Batch Median That Describes A Real Shot

## Why

A committed SAW batch stores `medianOf(drips)` and `medianOf(flows)` computed
**independently** (`settings_calibration.cpp`, `addSawPerPairEntry`). With `kBatchSize = 3`
the two medians usually come from *different shots*, so the stored `(drip, flow)` pair is a
point that never happened and its implied lag is a lag no shot in the batch had.

Every reader inherits it: `getExpectedDripFor` feeds the pair to the Gaussian smoother,
`sawLearnedLagFor` divides it, `globalSawBootstrapLag` takes the median of pair lags. The
outlier gate one block above is affected too — it compares each shot's lag against that same
quotient rather than against the batch's actual median lag, which is never computed.

Real batch from this maintainer's DE1: drips 1.30 / 1.90 / 1.53 against flows 1.87 / 2.10 /
1.60. Independent medians store 1.53 g at 1.87 g/s — 0.818 s. The batch's median lag is
0.905 s, and it belongs to the second shot. Across four committed batches: −3.0% mean,
−9.6% worst.

This is a data-integrity defect, not a model preference: the stored pair is wrong under any
prediction form.

## What Changes

- **Commit the median-lag shot's own `(drip, flow)`.** `medianLag` becomes the median of the
  batch's per-shot lags, and the committed entry is the shot that produced it. `medianOver`
  stays an independent median — it gates the auto-reset and is never divided by anything.
- **No migration, no schema change.** The entry keeps `drip` and `flow`; entries committed
  before the change are left alone and age out under the existing 10-entry trim.
- **Extend the evaluation corpus** from 63 shots to 250 (2026-04-01 to 2026-08-23) and teach
  `tools/saw_parity` to pass the basket key, which its default argument was silently
  collapsing.
- **Record the measured result**, including that it is not a performance win.

## What This Change Does NOT Do

- **No prediction-form change.** The archived `update-saw-prediction-model` (withdrawn) and
  `tune-saw-old-prediction` (shipped) established on a 63-shot corpus that `drip = lag × flow`
  over-predicts at low flow and that regression replacements are worse in every bucket.
  Nothing here overturns that.
- **No change to σ, the recency schedule, the read window, the batch size, or graduation.**
- **No change to the basket segment of the key.** See "The basket segment" below.

## Measured Result

Full tables in `analysis.md`. Baseline for the shipped model over 250 shots is 0.4008 g MAE
overall, with the high-flow bucket at 1.1070 g — three times the others. The fix moves overall
MAE to 0.3967 g (−1.0%), mid-flow to 0.3531 g (−2.3%), and high-flow to 1.1334 g (+2.4%
*worse*, n=14). Worst-case error is unchanged. 218 of 250 predictions change; 111 improve and
107 get worse.

**So the change ships on correctness, not accuracy.** The numbers say what it costs — nothing
measurable — not that it is an improvement.

Two earlier claims are withdrawn. The indicative −8.5% overall / +5.9% worse at low flow came
from an ad-hoc Python simulator and reproduced in neither magnitude nor sign. And the claim
that `medianLag` is "already computed for the gate, then discarded" was wrong: what is computed
is the quotient of independent medians, and the gate uses it.

## The basket segment

`saw_parity --ignore-basket` measures what the third key segment buys. Keying by basket is
worse in every slice — whole corpus +0.0014 g, the window since the second basket arrived
+0.0160 g, and the second basket's own shots +0.0171 g — because a fresh bucket restarts from
bootstrap. The n=3 finding that motivated `key-saw-learning-by-basket` (Graph dripping half of
Decent) has not reproduced: the four Graph shots since 2026-08-21 average 0.882 s of lag
against Decent's 0.819 s in the same window, the opposite sign.

**The segment stays.** The two baskets measured here are both 58 mm and differ mainly in wall
shape, which is a weak test of the hypothesis; a genuinely high-flow basket is where a real
difference would show. Seven shots on the second basket is enough to say no effect is
demonstrated, not enough to say none exists. Revisit with a basket that differs materially in
outlet geometry, or at ≥30 shots on a second basket.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `stop-at-weight-learning`: what a committed batch median stores. The spec describes the
  smoother's inputs as historical `(drip, flow)` entries without saying how a committed median
  derives its pair; this change makes it one batch shot's own pair rather than a quotient of
  two independent medians.

## Impact

**Affected code**
- `src/core/settings_calibration.cpp` — `addSawPerPairEntry`: the median-lag computation, the
  outlier gate that reads it, and the commit step. `globalSawBootstrapLag` inherits the
  corrected value.
- `tests/tst_saw_settings.cpp` — a batch whose median-drip shot and median-flow shot differ.
- `tools/saw_parity/main.cpp`, `tools/saw_replay/data/` — corpus extension and the basket key.
  No production behavior.
- `docs/CLAUDE_MD/SAW_LEARNING.md` — the storage-schema table and the "why median" section.

**Not affected**
- `SawPrediction::weightedDripPrediction` and its σ — unchanged, deliberately.
- Stored settings schema — no new keys, no migration.

**Risk**
- Predictions rise slightly, and the high-flow bucket regresses 2.4% on n=14. Accepted: the
  bucket's absolute error (1.13 g against ~0.35 g elsewhere) is dominated by something this
  change does not touch, and 14 shots cannot separate a real regression from batch assignment.
- Entries committed before the change stay as they are for up to ~30 shots, until the 10-entry
  trim retires them. No migration: rewriting them would need the raw batches, which are not
  retained.
