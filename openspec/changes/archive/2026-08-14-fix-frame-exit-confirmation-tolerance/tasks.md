## 1. Extract the inference

- [x] 1.1 Add `src/machine/frameexitreason.h` (header-only, no new .cpp): an `Inputs` struct carrying the frame's exit configuration, the transition sample, the preceding sample, elapsed frame time and the weight-skip flag; a `Result` carrying the reason string and whether extrapolation was what confirmed it. List the header in `CMakeLists.txt`.
- [x] 1.2 Move the precedence ladder from `MainController::onShotSample` into the helper unchanged (weight → sensor → time → unconfirmed), keeping the `> 0` guards on the `_under` arms.
- [x] 1.3 Add the rate-of-change tolerance to the four sensor arms.

## 2. Wire MainController

- [x] 2.1 Track the previous espresso-phase sample (`m_prevPressure`, `m_prevFlow`, `m_prevSensorValid`) alongside `m_lastPressure`/`m_lastFlow`; reset all of them where the shot resets.
- [x] 2.2 Replace the inline ladder with a call to the helper.
- [x] 2.3 Log the confirmed-by-extrapolation case alongside the existing unconfirmed line, with the threshold, both samples and the delta. (`maincontroller.cpp` is in `MARKER_ONLY_GLOBS` — bare `qDebug`, no bracketed marker.)

## 3. Tests

- [x] 3.1 Add a `frameExitReasonInference` slot to `tests/tst_shotanalysis.cpp` covering: plain confirmed exit; #1813's numbers (2.048 now, 1.869 prev, 2.10 threshold → `pressure`); flat sensor → unconfirmed; falling sensor on a `_over` arm → unconfirmed; no previous sample → unconfirmed; `_under` arms both ways; time expiry precedence; weight-skip precedence.
- [x] 3.2 Verify the new cases fail against the pre-change comparison (break the tolerance, watch them go red) before keeping them.

## 4. Docs

- [x] 4.1 Update `docs/SHOT_REVIEW.md` where it describes `detectSkipFirstFrame`'s reliance on confirmed reasons, so the confirmation rule it depends on is stated where it is read.
- [x] 4.2 No wiki manual entry — the user-visible change is a badge that stops firing wrongly, and the manual documents no confirmation semantics.

## 5. Close out

- [x] 5.1 Full test suite green via Qt Creator MCP.
- [x] 5.2 `openspec archive fix-frame-exit-confirmation-tolerance` as the last commit on the branch.
