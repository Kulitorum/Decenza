# Tasks

## 0. Prerequisites (recommended order)

- [x] 0.1 G (`dedup-phase-summary-builder`) — done as PR #943 (in review). I is stacked on H rather than G; the per-phase loop is unchanged on this branch and will rebase cleanly when G lands.
- [x] 0.2 H (`expose-pour-window-on-analysis-result`) — done as PR #944. I is stacked on H so the helper reads pourTruncated directly from `analyzeShot`'s `DetectorResults` (no `computePourWindow` round-trip).

## 1. Helper

- [x] 1.1 Added `void ShotSummarizer::runShotAnalysisAndPopulate(...)` declared private in `shotsummarizer.h`. Signature is the simpler form (no `cachedAnalysis` parameter) — see 3.1 for why.
- [x] 1.2 Implementation calls `ShotAnalysis::analyzeShot` with the typed inputs, copies `summaryLines` from `analysis.lines`, derives `pourTruncatedDetected` from `analysis.detectors.pourTruncated`, then runs `markPerPhaseTempInstability` under the existing gate (`!summary.pourTruncatedDetected && reachedExtractionPhase(markers, summary.totalDuration)`).

## 2. summarize() (live path) refactor

- [x] 2.1 `summarize()`'s detector orchestration block (analyzeShot call + post-state assignments + temp-instability gate) replaced with one call to `runShotAnalysisAndPopulate(summary, ...)`. The 17-line block shrinks to 4 lines.

## 3. summarizeFromHistory() refactor

- [x] 3.1 `summarizeFromHistory()`'s slow-path detector orchestration replaced with the same `runShotAnalysisAndPopulate(...)` call. The fast path (pre-computed `summaryLines` from change B / PR #934) intentionally stays inline rather than synthesising a partial `AnalysisResult` to feed the helper — it consumes a `QVariantMap`, runs the same gate (`!pourTruncatedDetected && reachedExtractionPhase` + `markPerPhaseTempInstability`) in 3 lines, and adding a cache parameter to the helper just to satisfy that path would re-introduce a parallel orchestration shape inside the helper itself. Keeping the fast path inline preserves "one place where analyzeShot is called and post-processing applied," which is what the proposal targets.
- [x] 3.2 Slow-path inline `analyzeShot` call deleted from `summarizeFromHistory()` — it lives inside the helper now.

## 4. Tests

- [x] 4.1 Equivalence test (live ↔ saved-shot for the same data) deferred to proposal K (`add-shotsummarizer-live-path-test`), which adds the MockShotDataModel infrastructure that direct `summarize()` tests require. The helper is private and exercised by both call sites, so the existing test suite serves as the regression lock for now: any orchestration drift would surface in the slow-path tests below (which exercise the same helper).
- [x] 4.2 Existing `pourTruncatedSuppressesChannelingAndTempLines`, `abortedPreinfusionDoesNotFlagPerPhaseTemp`, `summarizeFromHistory_fastAndSlowPathsAgree`, and `summarizeFromHistory_fastPathPreservesPourTruncatedCascade` all pass on the post-helper code (1806 total).

## 5. Verify

- [x] 5.1 Build clean (Qt Creator MCP, 0 errors / 0 warnings).
- [x] 5.2 All 1806 existing tests pass.
- [ ] 5.3 Manual smoke: AI advisor on saved + live shots produces the same observation lines as before. (Deferred — pure refactor with helper-level behaviour preserved by the existing test suite.)
