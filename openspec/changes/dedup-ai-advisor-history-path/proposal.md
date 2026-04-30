# Change: Dedup AI advisor's historical-shot path by reading pre-computed `summaryLines`

## Why

`ShotSummarizer::summarizeFromHistory(shotData)` builds the AI advisor prompt for a historical shot. It calls `ShotAnalysis::generateSummary(...)` (the wrapper around `analyzeShot`) inline, even though the input `shotData` map came out of `ShotHistoryStorage::convertShotRecord` and already carries `summaryLines` populated by `analyzeShot`. Result: every time the AI advisor is asked about a saved shot, the detector pipeline runs twice for the same data — once inside `convertShotRecord` and once inside `summarizeFromHistory` — and the answers must always be identical because both paths run the same code with the same inputs.

This is the historical-shot mirror of the dialog dedup (change `dedup-shot-summary-dialog`). The live-path `ShotSummarizer::summarize(ShotDataModel*)` still needs to compute, because no `convertShotRecord` runs for an in-progress shot.

## What Changes

- **MODIFY** `src/ai/shotsummarizer.cpp` `summarizeFromHistory` so that when `shotData["summaryLines"]` is a non-empty list, the function reuses those lines directly and skips the `ShotAnalysis::generateSummary(...)` invocation.
  - When the fast path triggers, derive `summary.pourTruncatedDetected` from `shotData["detectorResults"]["pourTruncated"]` (also pre-computed by `convertShotRecord`).
  - The per-phase temperature instability markers (`markPerPhaseTempInstability`) remain conditional on `!summary.pourTruncatedDetected` and `ShotAnalysis::reachedExtractionPhase(historyMarkers, summary.totalDuration)`, identical to today.
- **PRESERVE** the existing inline path as a fallback for legacy `shotData` maps that don't carry `summaryLines` / `detectorResults` (direct callers, imported shots without the new fields, tests).
- **NO change** to `summarize(ShotDataModel*)` (live path), `analyzeShot`, or `generateSummary`.

## Impact

- Affected specs: `shot-analysis-pipeline` (new requirement: AI advisor SHALL prefer pre-computed `summaryLines`/`detectorResults` when present in historical `shotData`).
- Affected code: `src/ai/shotsummarizer.cpp` (`summarizeFromHistory` only).
- User-visible behavior: identical AI prompts. Same lines, same severity tags, same verdict-line filtering before emission.
- Performance: one fewer `analyzeShot` pass per AI advisor invocation on a saved shot.
- Risk surface: the fast path must populate everything `summarizeFromHistory` currently sets on `summary` — `summaryLines`, `pourTruncatedDetected`, and the per-phase `temperatureUnstable` flags via `markPerPhaseTempInstability`. The bypass is around the *detector orchestration block* only, not around the curve-data and DYE-metadata population earlier in the function.
