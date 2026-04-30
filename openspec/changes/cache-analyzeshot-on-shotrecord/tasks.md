# Tasks

## 1. Cache field on ShotRecord

- [ ] 1.1 Add `std::optional<ShotAnalysis::AnalysisResult> cachedAnalysis;` to `ShotRecord` in `src/history/shothistory_types.h`. Document that it is populated by `loadShotRecordStatic` after the badge projection and consumed by `convertShotRecord`; reset to `std::nullopt` if any of the input curves are mutated after load.
- [ ] 1.2 Confirm `<optional>` is included (or add to the header). `ShotAnalysis::AnalysisResult` is already public.

## 2. Populate from loadShotRecordStatic

- [ ] 2.1 In `loadShotRecordStatic`, after `decenza::applyBadgesToTarget(record, analysis.detectors);`, also `record.cachedAnalysis = std::move(analysis);` so the result survives the function return.
- [ ] 2.2 Verify the lazy-persist write-back block (which only reads the projected booleans) still works correctly — it does, since `applyBadgesToTarget` already wrote the booleans onto `record`.

## 3. Consume in convertShotRecord

- [ ] 3.1 In `convertShotRecord`, gate the existing `analyzeShot` block on `record.cachedAnalysis.has_value()`:
  - If present: read `summaryLines` and `detectorResults` from the cached struct.
  - If absent: run `analyzeShot` inline as today (covers `ShotHistoryExporter`, direct test callers, etc. that didn't go through `loadShotRecordStatic`).
- [ ] 3.2 Keep the surrounding `analysisFlags` / `frameInfo` extraction inside the fallback branch — the cached path doesn't need them.

## 4. Tests

- [ ] 4.1 Extend an existing `loadShotRecordStatic` round-trip test (or add one to a new `tst_shothistorystorage_cache.cpp`) that asserts `record.cachedAnalysis.has_value()` after load and that `convertShotRecord(record)["summaryLines"]` equals `cachedAnalysis->lines`.
- [ ] 4.2 Add a fallback-path test: construct a `ShotRecord` directly (without `cachedAnalysis`), pass to `convertShotRecord`, assert `summaryLines` and `detectorResults` are still populated and identical to a fresh `analyzeShot` call. Locks in the legacy direct-construction path.
- [ ] 4.3 Add an equivalence test: same shot data through both paths produces byte-equal `summaryLines` and `detectorResults`. Catches drift if either path is modified in isolation.

## 5. Verify

- [ ] 5.1 Build clean (Qt Creator MCP).
- [ ] 5.2 All existing tests pass (currently 1793; this change should add 3, target 1796).
- [ ] 5.3 Manual smoke: open a few shots from history, watch a profiler / log to confirm `analyzeShot` is invoked once per detail load, not twice.

## 6. Documentation

- [ ] 6.1 Update `docs/SHOT_REVIEW.md` §3 to note that `convertShotRecord` reads from `record.cachedAnalysis` when populated by `loadShotRecordStatic`, falling back to its own `analyzeShot` call only for direct-construction callers.
- [ ] 6.2 Document the cache invalidation rule in the new field's docstring.
