# Tasks

## 0. Prerequisites (recommended order)

- [ ] 0.1 Land change `dedup-phase-summary-builder` (G) first so the helper introduced here doesn't have to carry the per-marker phase-builder loop.
- [ ] 0.2 Land change `expose-pour-window-on-analysis-result` (H) first so the helper reads the pour window from `DetectorResults` directly (no `computePourWindow` round-trip).

## 1. Helper

- [ ] 1.1 Add `static void ShotSummarizer::runShotAnalysisAndPopulate(ShotSummary& summary, const QVector<QPointF>& pressure, flow, weight, temperature, temperatureGoal, conductanceDerivative, const QList<HistoryPhaseMarker>& markers, double duration, const QVector<QPointF>& pressureGoal, flowGoal, const QStringList& analysisFlags, double firstFrameSeconds, int frameCount, double targetWeightG, double finalWeightG, const std::optional<ShotAnalysis::AnalysisResult>& cachedAnalysis = std::nullopt);` declared private in `shotsummarizer.h`.
- [ ] 1.2 Implementation: if `cachedAnalysis.has_value()`, copy `summaryLines` from there and derive `pourTruncatedDetected`. Otherwise call `ShotAnalysis::analyzeShot(...)` with the inputs and populate from the result. Then run `markPerPhaseTempInstability` under the existing gate (`!summary.pourTruncatedDetected && reachedExtractionPhase(markers, summary.totalDuration)`).

## 2. summarize() (live path) refactor

- [ ] 2.1 In `summarize()`, after the existing curve extraction from `ShotDataModel*` and the (post-G) phase summaries, replace the detector orchestration block with one call to `runShotAnalysisAndPopulate(summary, ...)`. No cached AnalysisResult on the live path — pass `std::nullopt`.

## 3. summarizeFromHistory() refactor

- [ ] 3.1 In `summarizeFromHistory()`, similarly call `runShotAnalysisAndPopulate(summary, ...)`. The fast path from change B (pre-computed `summaryLines`) is preserved by passing the cached `AnalysisResult` if `shotData["summaryLines"]` is non-empty. The helper's `cachedAnalysis.has_value()` branch handles the rest.
- [ ] 3.2 The slow-path inline `analyzeShot` call is deleted from `summarizeFromHistory` — it's now inside the helper.

## 4. Tests

- [ ] 4.1 Add a regression test in `tst_shotsummarizer.cpp` that runs the same shot through both paths and asserts equivalent `ShotSummary` (lines, pourTruncatedDetected, per-phase markers).
- [ ] 4.2 Existing `pourTruncatedSuppressesChannelingAndTempLines` and `abortedPreinfusionDoesNotFlagPerPhaseTemp` tests must still pass — they're the canonical regression locks for the cascade and the per-phase gate.

## 5. Verify

- [ ] 5.1 Build clean (Qt Creator MCP).
- [ ] 5.2 All existing `tst_shotsummarizer` tests pass + the new equivalence test.
- [ ] 5.3 Manual smoke: AI advisor on saved + live shots produces the same observation lines as before.
