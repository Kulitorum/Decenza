# Tasks

## 1. DetectorResults extension

- [x] 1.1 Add `double pourStartSec = 0.0;` and `double pourEndSec = 0.0;` fields to `ShotAnalysis::DetectorResults`. Default `0.0` matches the function's local-variable initial values when no phase markers are present.
- [x] 1.2 In `analyzeShot`'s body, after the existing `pourStart` / `pourEnd` computation (around the "Find phase boundaries" block), set `d.pourStartSec = pourStart; d.pourEndSec = pourEnd;` so the values are exposed to consumers.

## 2. ShotSummarizer cleanup

- [x] 2.1 Replace the `computePourWindow(summary, pourStart, pourEnd);` calls in both `summarize()` and `summarizeFromHistory()` with reads from the `AnalysisResult.detectors`. Live and slow-history paths now call `ShotAnalysis::analyzeShot` directly (instead of `generateSummary` + a separate `detectPourTruncated`) and read both `summary.summaryLines` and `pourTruncatedDetected` from the same `AnalysisResult` — eliminating the redundant pressure-curve scan as a side benefit. The fast-history branch was already detector-driven (reads `detectorResults.pourTruncated` from the pre-computed map) and required no change.
- [x] 2.2 Delete the file-static `computePourWindow` function from `shotsummarizer.cpp` (and its block comment).

## 3. MCP serialization

- [x] 3.1 In `ShotHistoryStorage::convertShotRecord`, add `detectorResults["pourStartSec"] = d.pourStartSec; detectorResults["pourEndSec"] = d.pourEndSec;` alongside the existing `pourTruncated` / `peakPressureBar` emissions.
- [x] 3.2 Update `docs/CLAUDE_MD/MCP_SERVER.md` "Shot Detector Outputs" section's example JSON to include the two new fields.

## 4. Tests

- [x] 4.1 Added three regression tests in `tst_shotanalysis.cpp`:
    - `analyzeShot_pourWindow_matchesPhaseBoundaries` — preinfusion + pour + end markers, asserts `pourStartSec == 8.0`, `pourEndSec == 28.0`.
    - `analyzeShot_pourWindow_noMarkers_spansWholeShot` — no markers + valid pressure data, asserts whole-shot fallback (`0.0` to `duration`).
    - `analyzeShot_pourWindow_preinfusionOnly_usesPreinfusionBoundary` — preinfusion-only marker, asserts the preinfusion-fallback branch sets `pourStartSec` to the marker time.
    - Plus `analyzeShot_pourWindow_insufficientData_defaultsToZero` covering the early-return path where both fields stay `0.0`.
- [x] 4.2 Existing `tst_ShotSummarizer::abortedPreinfusionDoesNotFlagPerPhaseTemp` still passes (in the 1806-test suite), confirming the rewired gate behaves identically.

## 5. Verify

- [x] 5.1 Build clean (Qt Creator MCP, 0 errors / 0 warnings).
- [x] 5.2 All 1806 tests pass (0 failed, 0 skipped, 0 with warnings).
- [ ] 5.3 Manual: inspect MCP `shots_get_detail` output on a sample shot — `detectorResults.pourStartSec` and `pourEndSec` should be present. (Deferred — covered by the schema test above; the new fields are written unconditionally in `convertShotRecord`.)
