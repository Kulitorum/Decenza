## Why

[#1813](https://github.com/Kulitorum/Decenza/issues/1813): a D-Flow shot on firmware v1352 was badged "First step skipped" when frame 0 executed exactly as designed.

Frame 0 ("Filling") is configured `exit_type pressure_over`, `exit_pressure_over 2.10`, `seconds 25.00`. The machine exited it at 1.888 s. The BLE sample at the transition read 2.048 bar, and the previous sample (210 ms earlier) read 1.869 bar — a rise of 0.179 bar per sample. The 2.10 bar threshold was therefore crossed roughly 60 ms after the last sample the app saw, between two BLE notifications.

`MainController`'s confirmation compares `m_lastPressure >= prevFrame.exitPressureOver` with no tolerance, so a crossing that lands between samples records `pressure_unconfirmed`. `ShotAnalysis::detectSkipFirstFrame` trusts only confirmed `pressure`/`flow`/`weight`, falls through to its duration branch, and flags 1.888 s against a 2.0 s cutoff.

The DE1 evaluates its exit condition on its own internal cadence, far faster than the ~10 Hz BLE sample stream. A threshold crossed between two samples is the normal case for a fast-rising fill frame, not an ambiguity — so the confirmation as written can never confirm exactly the frames that exit fastest.

## What Changes

- Frame-exit inference gains a **rate-of-change tolerance**: a `*_over` / `*_under` threshold that falls within one sample-interval's worth of the observed sensor change is treated as a confirmed sensor exit, not an unconfirmed one. The tolerance is the measured per-sample delta, not a constant — a signal that is not moving toward the threshold gets no tolerance at all.
- The inference moves out of `MainController::onShotSample`'s frame-change block into a header-only helper (`src/machine/frameexitreason.h`) so it is unit-testable. Behaviour is otherwise unchanged: the same vocabulary, the same precedence (weight → sensor → time → unconfirmed).
- The unconfirmed-exit log line gains its confirmed-by-extrapolation counterpart, so a submitted log says which of the two paths a marker took and with what numbers.

## Capabilities

### New Capabilities

_None._

### Modified Capabilities

- `frame-transition-reason`: a confirmed `pressure`/`flow` reason may now also be recorded when the transition sample did not itself satisfy the threshold but the threshold lies within one sample of the observed rate of change toward it.

## Impact

- `src/controllers/maincontroller.cpp` / `.h`: frame-change block calls the helper; two new members track the previous sample.
- `src/machine/frameexitreason.h` (new, header-only).
- `tests/tst_shotanalysis.cpp`: new test slot covering the helper, including the #1813 numbers.
- No BLE/protocol change, no schema change, no migration. Existing shots keep their recorded strings verbatim (the persistence requirement is untouched); the badge recomputes on load, so shots saved with `pressure_unconfirmed` keep that marker and keep their badge — only new shots benefit.
