# Change: Cache `analyzeShot` output on `ShotRecord` to eliminate the dual detail-load pass

## Why

`ShotHistoryStorage::loadShotRecordStatic` runs `ShotAnalysis::analyzeShot` once to recompute the badge boolean columns. Then `ShotHistoryStorage::convertShotRecord` (called immediately after, on the same `ShotRecord`, with the same inputs) runs `analyzeShot` *again* to populate `summaryLines` and the structured `detectorResults` JSON for MCP / dialog consumers. Two full detector passes per shot detail load on byte-identical inputs.

The post-`unify-detector-cascade-save-load` design.md flagged this as the natural next step: "A future change can elevate `analyzeShot`'s output into `ShotRecord` so a single pipe through both functions reuses one pass." This is that change.

The win is twofold: (1) detector work cuts in half on every shot detail load (the dominant CPU path on the post-shot review and history-detail pages), and (2) it eliminates one more "do the two `analyzeShot` calls agree?" contract — the answer becomes "they're literally the same call" by construction.

## What Changes

- **ADD** an optional `std::optional<ShotAnalysis::AnalysisResult> analysis` member on `ShotRecord` (or a small side struct cached alongside it). Populated by `loadShotRecordStatic` after the badge projection runs; consumed by `convertShotRecord` if present, otherwise `convertShotRecord` falls back to running `analyzeShot` itself.
- **MODIFY** `loadShotRecordStatic` so it stashes the `AnalysisResult` it just computed into the field before returning.
- **MODIFY** `convertShotRecord` to read from the cached field when present and skip the second `analyzeShot` call. When absent (e.g. callers that bypass `loadShotRecordStatic` and construct `ShotRecord` directly — `ShotHistoryExporter`, tests), fall back to the existing inline `analyzeShot` call so behavior stays correct end-to-end.
- **NO change** to `analyzeShot`'s signature, the badge projection, the prose lines, or the JSON shape exposed via MCP. Pure caller-side dedup.

## Impact

- Affected specs: `shot-analysis-pipeline` — new requirement: shot detail loads SHALL run `analyzeShot` exactly once, with the result reused across the badge-recompute and the convert-to-QVariantMap stages.
- Affected code: `src/history/shothistory_types.h` (new field), `src/history/shothistorystorage.cpp` (`loadShotRecordStatic` writes; `convertShotRecord` reads-or-fallbacks), tests covering the cached vs. fallback paths.
- User-visible behavior: identical. Same prose lines, same badges, same MCP JSON. The cache hit is invisible.
- Performance: ~50% reduction in detector CPU on the shot-detail-load path. Bounded but real on shots with long curves.
- Risk surface: the cache must be invalidated if any input changes between `loadShotRecordStatic` and `convertShotRecord`. They run sequentially in `requestShot` with no mutation in between, so this is structurally safe today, but the new field's lifecycle should be documented to avoid future bugs (e.g. callers that mutate `ShotRecord.pressure` after load and expect `convertShotRecord` to reflect the change).
