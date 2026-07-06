# Tasks: Unconfirmed Transition Reason

## 1. Recording (MainController)

- [x] 1.1 In `src/controllers/maincontroller.cpp` (~line 2702), change the ambiguous exit branch to record `pressure_unconfirmed`/`flow_unconfirmed` from `prevFrame.exitType.contains("pressure")` instead of `"time"`; update the qDebug line to log the recorded unconfirmed value
- [x] 1.2 Update the `HistoryPhaseMarker::transitionReason` field comment in `src/history/shothistory_types.h` with the full vocabulary (confirmed vs unconfirmed vs time vs empty)

## 2. Detectors (ShotAnalysis)

- [x] 2.1 In `src/ai/shotanalysis.cpp` `analyzeFlowVsGoal` (~line 413), extend the limiter-tail trim match to `pressure` OR `pressure_unconfirmed` (case-insensitive)
- [x] 2.2 Update the `detectSkipFirstFrame` guard comment (~line 680) to document that unconfirmed values intentionally fall through (no logic change)

## 3. Display mappings

- [x] 3.1 `qml/components/ShotGraph.qml` marker suffix switch: `pressure_unconfirmed` → `[P]`, `flow_unconfirmed` → `[F]`
- [x] 3.2 `qml/components/HistoryShotGraph.qml`: same suffix mapping
- [x] 3.3 `qml/pages/EspressoPage.qml` `_transitionText`/`_transitionColor`: map unconfirmed values to the pressure/flow text (reuse existing `espresso.transition.pressure`/`.flow` translation keys) and colors
- [x] 3.4 `src/network/shotserver_shots.cpp` embedded-JS suffix switches (~line 2012 and ~line 2848): same `[P]`/`[F]` mapping

## 4. Tests

- [x] 4.1 `tests/tst_shotanalysis.cpp` `skipFirstFrameDetection`: short first frame with `pressure_unconfirmed`/`flow_unconfirmed` still flags (guard is confirmed-only)
- [x] 4.2 `tests/tst_shotanalysis.cpp` grind/flow-vs-goal tests: limiter-tail trim fires on `pressure_unconfirmed`; does not fire on `flow_unconfirmed` or `time`
- [x] 4.3 Run the full test suite via Qt Creator MCP build and confirm green

## 5. Documentation

- [x] 5.1 `docs/SHOT_REVIEW.md` §2.3: update the exit-condition guard paragraph with the unconfirmed values and why they fall through
- [x] 5.2 `docs/SHOT_REVIEW.md` §2.2: document the tail-trim's `pressure`/`pressure_unconfirmed` gate and the cross-reference to MainController's recording semantics (the note deferred from PR #1421's review)
