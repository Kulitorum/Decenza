# Tasks

## 1. Helper

- [ ] 1.1 Add `static QList<PhaseSummary> ShotSummarizer::buildPhaseSummariesForRange(const QVector<QPointF>& pressure, flow, temperature, weight, const QList<HistoryPhaseMarker>& markers, double totalDuration);` declared in `shotsummarizer.h`.
- [ ] 1.2 Implementation in `shotsummarizer.cpp` mirrors today's per-marker loop: walk markers, skip degenerate spans (`endTime <= startTime`), compute per-phase metrics via the existing `findValueAtTime` / `calculateAverage` / `calculateMax` / `calculateMin` static helpers. Return the list.

## 2. Use the helper

- [ ] 2.1 In `summarize()`, replace the inline `for (qsizetype i = 0; i < markers.size(); i++)` loop with `summary.phases = buildPhaseSummariesForRange(pressureData, flowData, tempData, cumulativeWeightData, historyMarkers, summary.totalDuration);`. Keep the parallel `historyMarkers` build immediately before — it runs once and is consumed both by `analyzeShot` and (in this PR) by `buildPhaseSummariesForRange`.
- [ ] 2.2 In `summarizeFromHistory()`, similarly replace the `if (!phases.isEmpty())` loop body with the helper call. The post-loop `if (summary.phases.isEmpty())` no-markers fallback (`makeWholeShotPhase`) stays — it's a different code path.

## 3. Tests

- [ ] 3.1 Add a regression test in `tst_shotsummarizer.cpp` that builds a shot with 3+ phases, runs both `summarize()` (via a fake `ShotDataModel`) and `summarizeFromHistory()` for the equivalent QVariantMap, and asserts `summary.phases` byte-equal across paths. Locks in the dedup contract.
- [ ] 3.2 Add a degenerate-span test: build a marker list where one phase has `endTime <= startTime`. Assert the helper skips it but still appends the corresponding `HistoryPhaseMarker` (the parallel build path is unchanged).

## 4. Verify

- [ ] 4.1 Build clean (Qt Creator MCP).
- [ ] 4.2 All existing `tst_shotsummarizer` tests pass + 2 new ones.
- [ ] 4.3 Manual smoke: open the AI advisor on a saved shot with multiple phases. Per-phase prompt block should be unchanged.
