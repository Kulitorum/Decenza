# Reject a post-stop rise that drip cannot produce

## Why

On Oct 14, 2026 a D-Flow / Q shot stopped at weight on target (34.9 g, settling at 36.1 g a second later), then the scale climbed 8–15 g/s to 57 g with the pump off. Settling waited out its 10 s timeout and saved 57.3 g, and the shot review called it a gusher: "grind way too coarse. Grind much finer." SAW learning had already rejected the same number as unreasonable.

The tablet's history shows this is not rare: replaying 406 SAW-stopped shots, 24 had readings 5.7–36 g past the stop weight after it, most of them a hand gripping or resting on the cup. The cup-lift path already caught the ones that ended in a drop; a rise with no drop was saved as yield.

## What Changes

- Two disturbed settling readings in a row abandon settling exactly as a cup lift does: learning is skipped and the last clean settling average is saved. A reading is disturbed when its short flow is more than 4 g/s above the flow at the stop (every beverage), or, for the espresso group only, when it is more than `clamp(3 s × flow at stop, 5 g, 12 g)` above the settle base. A lone disturbed reading is dropped, unless settling would end before the next sample.
- The flow comes from `WeightProcessor`'s estimator, moved to `ScaleFlow` (`src/machine/scaleflow.h`) so the replay computes the same thing. The beverage mappings move from `Profile` to `BeverageType` (`src/profile/beveragetype.h`) so `shot_eval` can use them without Profile's dependencies.
- With no clean average, an abandoned settle saves the settle base when the event was an over-rise, since the reading before it may be the disturbance starting.
- The "implausible clean average = scale fault" branch is removed: the over-rise check makes it unreachable.
- The saved-weight decision moves into `Settling::abandonedSettleWeight()`, called by the app and by `shot_eval`, replacing a hand-mirrored copy.
- `shot_eval --settle-replay` models cup lifts and over-rises, and reads older logs' stop line.

## Capabilities

### Modified Capabilities

- `stop-at-weight-learning`: Final Weight Capture After Settling.

## Impact

`src/controllers/settlingconstants.h`, `src/controllers/shottimingcontroller.{h,cpp}`, `src/machine/scaleflow.h`, `src/machine/weightprocessor.{h,cpp}`, `src/profile/beveragetype.h`, `src/profile/profile.h`, `tools/shot_eval/main.cpp`, `tests/tst_settling.cpp`, `docs/CLAUDE_MD/SAW_LEARNING.md`, `docs/CLAUDE_MD/TESTING.md`. Saved yields on new shots only; no migration. The grind-check change considered alongside this was not made — see design.md.
