## 1. Regression test from shot 5470 (red)

- [ ] 1.1 Add `cupLiftMidSettlePreservesLastStableAvg` to `tests/tst_settling.cpp` that constructs `ShotTimingController`, fires `onSawTriggered(41.2, 2.5, 42.0)`, calls `endShot()`, then replays the 19-sample stream extracted from `[SAW] Settling:` log lines for shot 5470: `41.5, 41.7, 42.0, 42.2, 42.3×7, 42.4×2, 42.5, 44, 48.4, 51, 38.5, −28`.
- [ ] 1.2 Assert the final `tc.currentWeight()` is between 41.5 and 43.0 g (target ≈ 42.3) and that `isSawSettling()` is false. Use `QTest::ignoreMessage` for the `[SAW] Cup removed during settling` warning the cup-removed branch emits.
- [ ] 1.3 Run `ctest -R settling` and confirm the new test FAILS before any code fix (returns ~38.5 g).

## 2. ShotTimingController: capture + restore clean settling avg

- [ ] 2.1 Add `double m_lastCleanSettlingAvg = 0.0;` to `src/controllers/shottimingcontroller.h` near `m_lastSettlingAvg`.
- [ ] 2.2 In `src/controllers/shottimingcontroller.cpp` `onWeightSample` settling branch, inside the existing `if (avgDrift < SETTLING_AVG_THRESHOLD && !avgBelowStop && !weightAboveAvg)` block (around line 293), capture `m_lastCleanSettlingAvg = avg;` before `m_settlingAvgStableSince = now;`.
- [ ] 2.3 Mirror the same capture inside the corresponding stable branch in `onDisplayTimerTick` (around line 421+) for symmetry.
- [ ] 2.4 In the cup-removed branch of `onWeightSample` (lines 210–226), before the existing `return;`, restore `m_weight` using the fallback chain: clean avg → stop-weight floor → unchanged. Update the existing NOTE comment to describe the new behavior.
- [ ] 2.5 Reset `m_lastCleanSettlingAvg = 0.0` in `startSettlingTimer()` (around line 466+).
- [ ] 2.6 Reset `m_lastCleanSettlingAvg = 0.0` in `startShot()` alongside the existing `m_settlingPeakWeight = 0.0` reset (around line 111).
- [ ] 2.7 Re-run `ctest -R settling` and confirm the new test now passes (returns ≈ 42.3 g).

## 3. ShotSummarizer: plumb stoppedBy into standalone shot block

- [ ] 3.1 Add `QString stoppedBy;` to `ShotSummary` in `src/ai/shotsummarizer.h` (in the cluster of shot-VARIABLE fields).
- [ ] 3.2 Update `ShotSummarizer::summarize(...)` signature to accept an optional `const QString& stoppedBy = QString()` parameter and populate `summary.stoppedBy` from it.
- [ ] 3.3 Update `summarizeFromHistory(...)` to populate `summary.stoppedBy` from `shotData.stoppedBy`.
- [ ] 3.4 In `buildShotBlock` (line 684+), after the existing field emissions, add:
  ```cpp
  if (summary.stoppedBy == QStringLiteral("manual")
      || summary.stoppedBy == QStringLiteral("weight")
      || summary.stoppedBy == QStringLiteral("volume"))
      shot["stoppedBy"] = summary.stoppedBy;
  ```
- [ ] 3.5 Update all `MainController` callers of `m_summarizer->summarize(...)` to pass the already-resolved `stoppedBy` (the same value persisted via `m_shotHistory->saveShot(... stoppedBy)`).
- [ ] 3.6 Add a focused test in `tests/tst_shotsummarizer.cpp` that verifies the standalone shot block carries `stoppedBy: "weight"` for a SAW-stopped shot and omits the field for `stoppedBy == "profileEnd"` or empty.

## 4. Documentation

- [ ] 4.1 Add a new section "Settling and final-weight capture" to `docs/CLAUDE_MD/SAW_LEARNING.md` between the existing "Algorithm Details" and "User Experience" sections. Document the rolling-window stability gate, the `m_lastCleanSettlingAvg` capture, and the fallback chain on cup removal.
- [ ] 4.2 Update the SAW_LEARNING.md "Files" section to mention the cup-removed fallback alongside the existing references.

## 5. Offline replay tool

- [ ] 5.1 Create `tools/settling_replay/main.cpp`: parse a shot JSON file, extract `app.data.debug_log` (or accept a raw debug log file as input), regex out `[SAW] Settling: <weight> g` lines and the `[SAW] Stop triggered: weight=<x>` line, instantiate a `ShotTimingController`, fire `onSawTriggered(stopWeight, 2.0, target)`, call `endShot()`, then replay each weight sample through `onWeightSample`, and print `<file>: stopWeight=<x>  preFinalWeight=<old>  postFinalWeight=<new>`.
- [ ] 5.2 Wire the tool into the desktop-only `tools/` block in the root `CMakeLists.txt` (follow the existing `shot_eval` pattern).
- [ ] 5.3 Add a brief README at `tools/settling_replay/README.md` explaining usage.
- [ ] 5.4 Run the tool against `shot5470.json` and confirm `postFinalWeight ≈ 42.3` (vs `preFinalWeight = 38.5`).
- [ ] 5.5 Run the tool against `~/shot_corpus/*.json`. Most shots should print equal pre/post values (no cup-lift-mid-settle). Inspect any flips to confirm they are legitimate improvements, not regressions.

## 6. OpenSpec validation

- [ ] 6.1 `openspec validate preserve-settled-weight-on-cup-lift --strict` passes.

## 7. Build + regression sweep

- [ ] 7.1 Build via Qt Creator MCP (Debug, with `BUILD_TESTS=ON`).
- [ ] 7.2 Run full test suite: `ctest --output-on-failure`. All existing tests pass.
- [ ] 7.3 Run `ctest -R shot_corpus_regression`. Detector verdicts unchanged (expected — detectors do not read `TC.currentWeight`).
- [ ] 7.4 Lint: review the diff for stray includes, `qDebug` left in for development, etc.

## 8. Review and PR

- [ ] 8.1 Open PR linking issue [#1280](https://github.com/Kulitorum/Decenza/issues/1280). Summary: shot 5470 root cause + before/after `finalWeight` numbers + validation evidence.
- [ ] 8.2 Run `/pr-review-toolkit:review-pr`.
- [ ] 8.3 After deploy, ask Mark to pull a fresh shot and confirm the advisor no longer claims "stopped manually" framing on SAW-stopped shots.
