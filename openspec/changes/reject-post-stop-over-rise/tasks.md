# Tasks

## 1. Implementation

- [x] 1.1 `settlingconstants.h`: `maxPlausibleDripG()` (3 s × flow, 5–12 g), `settleBaseG()`, `settleFlowRefGps()`, `classifySettleSample()` (flow test for every beverage, amount test for espresso), `isCupLift()`, `abandonedSettleWeight()`, `abandonedSettleSourceText()`; remove the unreachable scale-fault branch.
- [x] 1.2 `ShotTimingController`: two-sample disturbance from the settle base and flow reference, a pending reading counts when settling completes (`completeSettling()`), one `abandonSettling()` for lift and both disturbances, `weightChanged` on the saved weight; the amount test follows the profile's beverage group.
- [x] 1.3 `ScaleFlow` (`src/machine/scaleflow.h`) holds `WeightProcessor`'s flow estimator; `BeverageType` (`src/profile/beveragetype.h`) holds Profile's beverage mappings.
- [x] 1.4 `shot_eval`: `--settle-replay` computes the settling flow with `ScaleFlow` and calls the shared classifier and decision; `--settling` replays the amount test only (its logs have no times).
- [x] 1.5 `tst_settling`: adapt the lift tests; add fast stop, spiked flow, late stop, lone spike, pending-at-completion, frozen reading, fast rise inside the amount limit, filter drain, late-gusher flow reference.
- [x] 1.6 `SAW_LEARNING.md` and `TESTING.md`.

## 2. Validation

- [x] 2.1 Build in Qt Creator; full test suite green: 121/121 (Oct 14, 2026).
- [x] 2.2 Break each protection and watch `tst_settling` fail. Base = trigger only: `lateStopMeasuresDripFromWhereSettlingStarted`. One sample suffices: `loneSpikeIsDroppedNotActedOn`. Pending ignored at completion: `overRiseStillPendingWhenSettlingEndsCounts`. No ceiling: `spikedStopFlowDoesNotDisableOverRise`. No flow term: `fastStopDripIsNotAnOverRise`. Disturbance flag dropped from the fallback: `frozenHighReadingSavesStopWeight_1280`. Amount test off: five tests. Flow test off: `fastRiseWithinTheAmountLimitIsCaught`. Amount test on for filter: `filterDrainIsNotCutShort`. Flow reference = trigger only: `lateGusherFlowIsNotADisturbance`.
- [x] 2.3 `shot_eval --settle-replay` on the tablet corpus (406 shots), `main` vs both tests: 29 saved weights change, no clean shot among them; 24 closer to stop + median drip, 3 same, 2 further (worst +1.1 g); mean error 6.79 g → 1.01 g.
- [x] 2.4 `shot_eval --validate tests/data/shots/manifest.json`: 22/22.
- [x] 2.5 Update the replay's reproduction figure in `TESTING.md`: 224 of 406.
- [ ] 2.6 A second setup: replay a database export from another user's tablet (Mark Palmos is the candidate). Open until one is available; the thresholds were chosen with margin for it, not fitted to it.

## 3. Manual

- [x] 3.1 Not needed: no user-visible wording or setting changes; the saved yield is simply right.
