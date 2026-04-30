# Tasks

## 1. DetectorResults extension

- [ ] 1.1 Add `double pourStartSec = 0.0;` and `double pourEndSec = 0.0;` fields to `ShotAnalysis::DetectorResults`. Default `0.0` matches the function's local-variable initial values when no phase markers are present.
- [ ] 1.2 In `analyzeShot`'s body, after the existing `pourStart` / `pourEnd` computation (around the "Find phase boundaries" block), set `d.pourStartSec = pourStart; d.pourEndSec = pourEnd;` so the values are exposed to consumers.

## 2. ShotSummarizer cleanup

- [ ] 2.1 Replace the `computePourWindow(summary, pourStart, pourEnd);` calls in both `summarize()` and `summarizeFromHistory()` with reads from the `AnalysisResult.detectors`. The live path can read it directly from the result returned by `analyzeShot`/`generateSummary`. The history path's fast branch already carries the cached `AnalysisResult`; the slow branch's inline analyzeShot call also returns a fresh result.
- [ ] 2.2 Delete the file-static `computePourWindow` function from `shotsummarizer.cpp` (and its block comment).

## 3. MCP serialization

- [ ] 3.1 In `ShotHistoryStorage::convertShotRecord`, add `detectorResults["pourStartSec"] = d.pourStartSec; detectorResults["pourEndSec"] = d.pourEndSec;` alongside the existing `pourTruncated` / `peakPressureBar` emissions.
- [ ] 3.2 Update `docs/CLAUDE_MD/MCP_SERVER.md` "Shot Detector Outputs" section's example JSON to include the two new fields.

## 4. Tests

- [ ] 4.1 Add a regression test in `tst_shotanalysis.cpp` that asserts `analyzeShot`'s `DetectorResults.pourStartSec`/`pourEndSec` match the function's internal pour-window values for a few canonical shapes (preinfusion + pour, no-markers fallback, "End" phase boundary).
- [ ] 4.2 Confirm the existing `tst_shotsummarizer::abortedPreinfusionDoesNotFlagPerPhaseTemp` test still passes — it depends on the gate that previously used `computePourWindow`. Without `computePourWindow`, the gate now reads from `DetectorResults` directly.

## 5. Verify

- [ ] 5.1 Build clean (Qt Creator MCP).
- [ ] 5.2 All existing tests pass.
- [ ] 5.3 Inspect MCP `shots_get_detail` output on a sample shot — `detectorResults.pourStartSec` and `pourEndSec` should be present.
