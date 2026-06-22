## 1. StepExitArbiter

- [x] 1.1 Add `src/machine/stepexitarbiter.h` + `.cpp`: `FrameExitCondition` struct (`Kind { None, PressureOver, PressureUnder, FlowOver, FlowUnder }`, `double value`), `StepExitArbiter::Verdict { Fire, Defer }`, and methods `evaluate(frame, fw, pressure, flow)`, `onFrameAdvanced(newFrame)`, `reset()`.
- [x] 1.2 Implement decision logic: non-actionable (`value<=0`)→Fire; outside proximity window→Fire; deferral cap reached→Fire; near+trending→Defer; near+not-trending→Fire. Proximity windows `max(0.20*value,0.3bar)` / `max(0.25*value,0.2ml/s)`; per-frame deferral state with reading history and `frameCount`.
- [x] 1.3 Implement trend check (all consecutive pairs step toward threshold by `Kind`; single reversal → not trending; first sample → trending) and named constants (`kMaxDeferralSamples=3`, proximity fractions/minimums) with the ~5–10 Hz cadence assumption documented.
- [x] 1.4 Add both files to `CMakeLists.txt` and to the three test targets that compile `weightprocessor.cpp` in `tests/CMakeLists.txt`.

## 2. Wire firmware-exit metadata + live sensors into WeightProcessor

- [x] 2.1 Add `QVector<FrameExitCondition> m_frameExitConditions`, `double m_currentPressure`, `double m_currentFlow`, and a `StepExitArbiter m_stepExitArbiter` member to `WeightProcessor`.
- [x] 2.2 Add a `QVector<FrameExitCondition>` parameter to `WeightProcessor::configure()` and store it.
- [x] 2.3 Change `setCurrentFrame(int frame, double pressure = 0.0, double flow = 0.0)`: store sensors, and on frame change call `m_stepExitArbiter.onFrameAdvanced(frame)` (keep the existing stall-check call).
- [x] 2.4 Call `m_stepExitArbiter.reset()` in `startExtraction()` and `resetForRetare()` (alongside the existing `m_frameWeightSkipSent.clear()`).

## 3. Gate the per-frame skip

- [x] 3.1 In `processWeight()`: when `exitWeight` met and not sent, if the frame's `FrameExitCondition.kind == None` fire as today; otherwise consult `m_stepExitArbiter.evaluate(...)` — on `Defer` do not send/do not mark sent; on `Fire` mark sent and `emit skipFrame`.
- [x] 3.2 Add concise `qDebug` lines for defer/fire decisions on mixed frames.

## 4. Build the descriptor at shot start

- [x] 4.1 In `src/main.cpp` build a `QVector<FrameExitCondition>` from `profile.steps()` (map `exitIf && !exitType.isEmpty()` to the matching `Kind`/`value`, else `None`) parallel to `frameExitWeights`, and pass it through the `configure()` invoke.
- [x] 4.2 In the `sampleReady`→`setCurrentFrame` connection capture pressure (arg 2) and flow (arg 3) and forward them to `setCurrentFrame`.

## 5. Tests

- [x] 5.1 `tst_weightprocessor.cpp`: weight-only frame still fires immediately (existing `perFrameExit*` tests, now passing empty conditions); firmware-only frame unaffected.
- [x] 5.2 Mixed frame, firmware far from threshold → fires immediately (`mixedFrameFiresWhenFirmwareFar`).
- [x] 5.3 Mixed frame: near + trending → defers to cap (`mixedFrameDefersWhenNearTrending`); near + not-trending → fires early (`mixedFrameFiresWhenNearNotTrending`).
- [x] 5.4 Firmware advances during deferral → no skip for the passed frame (`firmwareAdvanceDuringDeferralNoDoubleSkip`); arbiter state pruning (`arbiterOnFrameAdvancedPrunes`).
- [x] 5.5 Regression case from the "soup" layout (`soupProfileNoDoubleSkip`).
- [x] 5.6 Direct `StepExitArbiter` checks: proximity + trend reversal (`arbiterProximityAndTrend`).

## 6. Docs & verification

- [x] 6.1 Update `docs/CLAUDE_MD/RECIPE_PROFILES.md` to document the arbitration and the double-skip it prevents.
- [x] 6.2 Build via Qt Creator (0 errors / 0 warnings) and run the full test suite (2448 passed, 0 warnings).
- [x] 6.3 Run `/opsx:archive` as the final commit on the branch before merge.
