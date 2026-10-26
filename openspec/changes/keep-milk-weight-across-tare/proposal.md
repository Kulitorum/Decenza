# Keep the milk weight across a tare

## Why

In #2058's log the user tared the scale with the pitcher of milk on it seven times. Each tare discarded the learned empty reading, the loaded pitcher was learned as empty, and the milk was never captured.

## What Changes

- A tare while preparing (machine idle, heating or ready) moves the learned empty reading with the scale's zero, so a pitcher tared on the scale keeps its measured milk.
- A tare whose reading has not moved after 3 s never landed, and the empty reading is kept.
- A tare during an operation (espresso auto-tare, hot water) discards the learned reading as before.

## Capabilities

### Modified Capabilities

- `weight-timed-steaming`: what a tare does to the learned empty reading.

## Impact

`src/core/stableweightcapture.{h,cpp}`, `src/controllers/preparationcontroller.cpp`, their tests. No settings or UI change.
