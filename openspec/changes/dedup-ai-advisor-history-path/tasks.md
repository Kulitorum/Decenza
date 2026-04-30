# Tasks

## 1. Fast-path implementation

- [x] 1.1 In `src/ai/shotsummarizer.cpp` `summarizeFromHistory`, after the existing curve-data and DYE-metadata population (everything before the detector orchestration block), check for the pre-computed fields and short-circuit:
  ```cpp
  const QVariantList preComputedLines = shotData.value("summaryLines").toList();
  if (!preComputedLines.isEmpty()) {
      summary.summaryLines = preComputedLines;
      const QVariantMap preDetectors = shotData.value("detectorResults").toMap();
      summary.pourTruncatedDetected = preDetectors.value("pourTruncated").toBool();
      if (!summary.pourTruncatedDetected
          && ShotAnalysis::reachedExtractionPhase(historyMarkers, summary.totalDuration))
          markPerPhaseTempInstability(summary, summary.tempCurve, summary.tempGoalCurve);
      return summary;
  }
  ```
- [x] 1.2 Confirmed `historyMarkers` and `summary.totalDuration` are populated *before* the fast-path branch — the existing function ordering puts both ahead of the detector orchestration block, so the fast path has all the inputs it needs for `reachedExtractionPhase` and per-phase marker gating.
- [x] 1.3 Added inline comments explaining why the fast path is safe (the slow path eventually runs the same analyzeShot body) and that the bypass is around the detector orchestration block only — not around curve-data or DYE-metadata population.

## 2. Tests

- [x] 2.1 `summarizeFromHistory_usesPreComputedLines` — feeds a sentinel `summaryLines` list and asserts it comes back unchanged. Also stashes `detectorResults.pourTruncated = false` and asserts `pourTruncatedDetected` is derived from there, not recomputed.
- [x] 2.2 `summarizeFromHistory_fallsBackWhenNoSummaryLines` — omits `summaryLines` from `shotData` and asserts the inline detector path still produces non-empty `summaryLines` and a verdict line.
- [x] 2.3 `summarizeFromHistory_fastAndSlowPathsAgree` — runs the slow path first to capture real lines, feeds them into the fast path on a fresh map, asserts byte-equal output. Catches drift if either path is later modified in isolation.
- [x] 2.4 `summarizeFromHistory_fastPathPreservesPourTruncatedCascade` — exercises the cascade through the fast path: stashes `detectorResults.pourTruncated = true`, asserts `summary.pourTruncatedDetected == true` and per-phase temperature markers are NOT set. Locks in the cascade integrity through the fast path.

## 3. Verify

- [x] 3.1 Build clean (Qt Creator MCP, 0 errors / 0 warnings).
- [x] 3.2 1782 tests pass (1778 prior + 4 new). No regressions.

## 4. Docs

- [x] 4.1 Updated `docs/SHOT_REVIEW.md` §3 "AI advisor consumes the same line list" to document the fast/fallback pattern matching the dialog dedup.
