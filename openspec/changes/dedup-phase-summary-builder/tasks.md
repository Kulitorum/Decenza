# Tasks

## 1. Helper

- [x] 1.1 Add `static QList<PhaseSummary> ShotSummarizer::buildPhaseSummariesForRange(const QVector<QPointF>& pressure, flow, temperature, weight, const QList<HistoryPhaseMarker>& markers, double totalDuration);` declared in `shotsummarizer.h`.
- [x] 1.2 Implementation in `shotsummarizer.cpp` mirrors today's per-marker loop: walk markers, skip degenerate spans (`endTime <= startTime`), compute per-phase metrics via the existing `findValueAtTime` / `calculateAverage` / `calculateMax` / `calculateMin` static helpers. Return the list.

## 2. Use the helper

- [x] 2.1 In `summarize()`, replace the inline `for (qsizetype i = 0; i < markers.size(); i++)` loop with `summary.phases = buildPhaseSummariesForRange(pressureData, flowData, tempData, cumulativeWeightData, historyMarkers, summary.totalDuration);`. Keep the parallel `historyMarkers` build immediately before — it runs once and is consumed both by `analyzeShot` and (in this PR) by `buildPhaseSummariesForRange`.
- [x] 2.2 In `summarizeFromHistory()`, similarly replace the `if (!phases.isEmpty())` loop body with the helper call. The post-loop `if (summary.phases.isEmpty())` no-markers fallback (`makeWholeShotPhase`) stays — it's a different code path.

## 3. Tests

- [x] 3.1 Add a regression test in `tst_shotsummarizer.cpp`. Implemented as `summarizeFromHistory_perPhaseMetricsAreCorrect`: builds a shot with 3 phase markers and validates per-phase metrics (duration, avgPressure, avgFlow, weightGained) for the saved-shot path. Live-path equivalence is deferred to proposal K (`add-shotsummarizer-live-path-test`), which adds the MockShotDataModel infrastructure needed for direct `summarize()` tests.
- [x] 3.2 Add a degenerate-span test: implemented as `summarizeFromHistory_degenerateSpansSkipped`. Builds 3 markers where two share a timestamp; asserts the resulting `summary.phases` skips the degenerate span (2 phases, not 3).

## 4. Verify

- [x] 4.1 Build clean (Qt Creator MCP).
- [x] 4.2 All existing `tst_shotsummarizer` tests pass + 2 new ones (1804 total).
- [ ] 4.3 Manual smoke: open the AI advisor on a saved shot with multiple phases. Per-phase prompt block should be unchanged. (Deferred — pure refactor with byte-equal helper output covered by tests.)
