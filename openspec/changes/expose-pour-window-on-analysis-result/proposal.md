# Change: Expose `analyzeShot`'s pour window on `AnalysisResult` so `computePourWindow` can be deleted

## Why

`ShotSummarizer` has a file-static helper `computePourWindow(const ShotSummary&, double& pourStart, double& pourEnd)` that approximates `analyzeShot`'s internal pour-window logic. It exists to gate a single call to `markPerPhaseTempInstability`. After the cascade-unification work (PR #936), `markPerPhaseTempInstability` is the only per-phase temp logic that lives outside `analyzeShot` — and the gate it needs (`pourStart > 0 && reachedExtractionPhase(...)`) is the same gate `analyzeShot` already evaluates internally.

`computePourWindow` is therefore a duplicate of logic that already runs inside `analyzeShot`. The risk is real: if `analyzeShot` ever refines its pour-window heuristic (e.g. tighter handling of the "End" phase label, or a new "extraction" phase name), `computePourWindow` will silently diverge.

The cheapest fix is to expose the pour window `analyzeShot` computed via the `DetectorResults` struct so callers that need it can read it. `ShotSummarizer` becomes the only consumer; `computePourWindow` is deleted.

## What Changes

- **MODIFY** `ShotAnalysis::DetectorResults` — add `double pourStartSec` and `double pourEndSec` fields, populated by `analyzeShot` from the same `pourStart`/`pourEnd` locals it already computes.
- **MODIFY** `ShotSummarizer::summarize()` and `summarizeFromHistory()` to read `summary.pourStartSec` / `summary.pourEndSec` from the `AnalysisResult` (live path) or from the cached/recomputed `DetectorResults` (history path) rather than calling `computePourWindow`.
- **DELETE** `computePourWindow` from `shotsummarizer.cpp`.
- **DOCUMENT** the new fields in `docs/CLAUDE_MD/MCP_SERVER.md` under "Shot Detector Outputs" — they're now part of the `detectorResults` JSON surface MCP consumers see.

## Impact

- Affected specs: `shot-analysis-pipeline` (new requirement: pour window is exposed on `DetectorResults`; `computePourWindow` is removed).
- Affected code: `src/ai/shotanalysis.{h,cpp}` (struct + populate), `src/ai/shotsummarizer.{h,cpp}` (delete helper, update two call sites), `src/history/shothistorystorage.cpp` (`convertShotRecord` now serializes the two new fields), `docs/CLAUDE_MD/MCP_SERVER.md` (new fields).
- User-visible behavior: identical. `markPerPhaseTempInstability` runs under the same gate.
- MCP consumers: gain two new informational fields on `detectorResults` (`pourStartSec`, `pourEndSec`). Backwards-compatible — additive.
- Risk: low. The pour-window value is already computed; this just exposes it. The only correctness concern is that `computePourWindow` and `analyzeShot`'s internal computation might *currently* disagree on edge cases (e.g. an unusual phase label set). If they do, deleting `computePourWindow` makes the per-phase temp-instability gate consistent with the rest of the cascade — which is the correct behavior, not a regression.

## Out of scope

- Moving `markPerPhaseTempInstability` itself into `analyzeShot` would be a larger change (it would need to expand `DetectorResults` with per-phase data). Deferred to a future change if MCP consumers ever want per-phase temp markers.
