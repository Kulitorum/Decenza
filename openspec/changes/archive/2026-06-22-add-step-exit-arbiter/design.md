## Context

`WeightProcessor` runs on a worker thread and owns two weight-driven decisions: the shot-level SAW stop and the **per-frame weight exit**. The per-frame exit ([weightprocessor.cpp:310-319](../../../src/machine/weightprocessor.cpp)) fires `skipFrame(frame)` → `DE1Device::skipToNextFrame()` → `requestState(SkipToNext)` the moment the current weight reaches the frame's `exitWeight`. A `QSet<int> m_frameWeightSkipSent` guarantees at most one tablet skip per frame.

`SkipToNext` is **blind/relative**: it advances whatever frame the DE1 is currently in — there is no frame index in the command. On a frame that *also* carries a firmware exit (`exitIf` + pressure/flow over/under), the firmware can autonomously advance the frame in the BLE round-trip window between the tablet deciding to skip and the command landing. The firmware advance plus the late tablet skip = a double frame-advance. On short 2–3 frame profiles (e.g. the imported Visualizer "soup"/"soup no bloom" profiles, whose `fill` frame has `pressure_over 2.0` + `weight 1.0`) this truncates the shot.

`WeightProcessor` is fed the current frame on the DE1 shot-sample tick (`setCurrentFrame`, ~5 Hz) and weight on the scale tick (`processWeight`, ~5 Hz). It currently has **no awareness** of firmware exits or live pressure/flow, so it cannot arbitrate today.

## Goals / Non-Goals

**Goals:**
- Eliminate the double frame-advance on mixed-exit frames.
- Preserve today's behavior exactly for weight-only and firmware-only frames.
- Keep the at-most-once-per-frame skip guarantee.
- Deterministic and unit-testable; no new user-facing setting; on by default.

**Non-Goals:**
- No BLE protocol change — the DE1 offers no frame-targeted skip, so we cannot close the window by making the skip absolute.
- No change to profile JSON format, recipe generation (the D-Flow 5 g fill fallback stays — it is simply protected), or the shot-level SAW stop logic.

## Decisions

### 1. New `StepExitArbiter` class
`src/machine/stepexitarbiter.{h,cpp}`, owned by `WeightProcessor` (one instance, per-shot). Mirrors reaprime PR #371. API:

- `Verdict evaluate(int frame, FrameExitCondition fw, double pressure, double flow)` → `Fire` | `Defer`.
- `void onFrameAdvanced(int newFrame)` — drop deferral state for frames `< newFrame`.
- `void reset()` — clear all state at shot start.

`FrameExitCondition` is a small struct: `{ Kind kind; double value; }` where `Kind ∈ { None, PressureOver, PressureUnder, FlowOver, FlowUnder }`. The arbiter picks `pressure` or `flow` as the relevant sensor from `kind`.

Decision logic per `evaluate` (only reached when weight threshold met **and** frame has a firmware exit):
- `value <= 0` (non-actionable exit) → **Fire** (treat as weight-only).
- sensor **outside** proximity window of `value` → **Fire** (no race risk).
- inside window, deferral count `>= maxDeferralSamples` → **Fire** (cap the wait).
- inside window **and trending** toward `value` → **Defer**.
- inside window but **not trending** → **Fire** (firmware unlikely to fire).

Proximity window (carried from reaprime, calibrated to DE1 sensor noise):
- pressure: `max(0.20 * value, 0.3 bar)`
- flow: `max(0.25 * value, 0.2 ml/s)`

Trend: across the recorded readings for this frame, every consecutive pair must step toward the threshold (`>` for `*_over`, `<` for `*_under`); a single reversal → not trending. First sample (no prior) → assume trending (give firmware the benefit of the doubt).

`maxDeferralSamples`: must be ≥ 3 so the not-trending branch (which needs ≥ 2 recorded readings) is reachable before the cap fires — otherwise the early-fire trend logic is bypassed. `WeightProcessor`'s per-frame check runs on the scale tick (~5–10 Hz); firing on the 3rd recorded sample spans 2 inter-sample intervals, so worst-case deferral is ~200–400 ms. Value lives as a named constant with the cadence assumption documented.

### 2. Feed live sensors into `WeightProcessor`
Extend the DE1-tick path to carry pressure/flow alongside the frame: `setCurrentFrame(int frame, double pressure = 0.0, double flow = 0.0)` (defaults keep existing tests/callers valid). It stores `m_currentPressure` / `m_currentFlow` and, when `frame` changes, calls `m_arbiter.onFrameAdvanced(frame)`. The main.cpp connection ([main.cpp:676-680](../../../src/main.cpp)) already receives pressure (arg 2) and flow (arg 3) from `ShotTimingController::sampleReady` — it just forwards them now.

The per-frame check in `processWeight` reads the **latest cached** `m_currentPressure`/`m_currentFlow`. These are ≤ ~200 ms old (DE1 tick cadence), an acceptable approximation of reaprime's unified snapshot; documented in code.

### 3. Per-frame firmware-exit metadata
Add `QVector<FrameExitCondition> m_frameExitConditions` to `WeightProcessor`, set via an added parameter on `configure()`. Built in main.cpp ([main.cpp:719-728](../../../src/main.cpp)) from `profile.steps()` parallel to `frameExitWeights`: `exitIf && exitType non-empty` maps to the matching `Kind`/`value` (`exitPressureOver`, etc.), else `None`.

### 4. Gate the skip
At [weightprocessor.cpp:311](../../../src/machine/weightprocessor.cpp), when `exitWeight` is met and not yet sent:
- If the frame's `FrameExitCondition.kind == None` (or no metadata) → fire as today.
- Else consult `m_arbiter.evaluate(...)`. On `Defer`, **do not send and do not mark sent** — the next sample re-evaluates. On `Fire`, mark sent and emit `skipFrame`.

Because a deferred frame is never marked sent and `onFrameAdvanced` drops its state, if the firmware advances mid-deferral the next sample sees the new `m_currentFrame` and never fires a skip for the frame the firmware already left — this is the double-skip protection.

### 5. Reset wiring
`startExtraction()` and `resetForRetare()` (which already clear `m_frameWeightSkipSent`) also call `m_arbiter.reset()`.

## Risks / Trade-offs

- **Cached-sensor staleness.** The arbiter reads pressure/flow cached on the last DE1 tick rather than a snapshot atomic with the weight sample (~≤200 ms skew). Mitigated by short windows and the trend check; documented.
- **Slightly delayed legitimate weight exit.** When firmware is near-and-trending but ends up *not* firing, the weight exit is deferred until the cap (~≤400 ms). Bounded and rare; the not-trending branch fires early in the common case.
- **Sensor noise vs trend.** Requiring all-pairwise-toward-threshold can read noise as a reversal and fire early — the safe direction (no double-skip; weight exit still happens). Acceptable.
- **Cadence assumption for the cap.** `maxDeferralSamples` is tuned to the ~5 Hz scale tick; if the per-frame check ever moves to the DE1 tick the constant must be revisited. Documented at the constant.
