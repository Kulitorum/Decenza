# Tasks

## 1. Cache field on ShotRecord

- [x] 1.1 Added `std::optional<ShotAnalysis::AnalysisResult> cachedAnalysis;` to `ShotRecord` in `src/history/shothistory_types.h`. Field docstring documents that it is populated by `loadShotRecordStatic` after the badge projection and consumed by `convertShotRecord`; consumers MUST reset to `std::nullopt` if any of the input curves are mutated after load. Also added `#include "ai/shotanalysis.h"` and `#include <optional>` to the header.
- [x] 1.2 No circular include risk — `shotanalysis.h` forward-declares `HistoryPhaseMarker`, so adding the reverse include path is clean.

## 2. Populate from loadShotRecordStatic

- [x] 2.1 In `loadShotRecordStatic`, after `decenza::applyBadgesToTarget(record, analysis.detectors);`, the result is moved into `record.cachedAnalysis = std::move(analysis);` so it survives the function return.
- [x] 2.2 Lazy-persist write-back block (which only reads the projected booleans) still works correctly — `applyBadgesToTarget` writes the booleans BEFORE the move, so the snapshot comparison sees the correct values.

## 3. Consume in convertShotRecord

- [x] 3.1 Gated the existing `analyzeShot` block on `record.cachedAnalysis.has_value()`:
  - Present: read `summaryLines` and `detectorResults` from the cached struct (no recomputation).
  - Absent: run `analyzeShot` inline as today (covers `ShotHistoryExporter`, direct test callers, etc. that bypass `loadShotRecordStatic`).
- [x] 3.2 Used a `const ShotAnalysis::AnalysisResult* analysisPtr` indirection so the existing serialization code that follows reads from a single `analysis` reference regardless of which path produced it.

## 4. Tests

- [x] 4.1 New file `tests/tst_shotrecord_cache.cpp` with 3 test methods:
  - `cachedAnalysis_isReusedByConvert` — sentinel-based check; pre-populated `summaryLines` come back unchanged.
  - `noCachedAnalysis_fallsBackToInlineAnalyzeShot` — direct-construction path produces non-empty output via inline `analyzeShot`.
  - `cachedAndFallback_paths_produceEquivalentOutput` — same shot through both paths produces byte-equal lines + matching `verdictCategory` / `pourTruncated`.
- [x] 4.2 Wired into `tests/CMakeLists.txt` (`tst_shotrecord_cache` target) using the same `HISTORY_SOURCES`/BLE/profile/core/controller/simulator deps + `de1transport.h` MOC trick as `tst_dbmigration`.

## 5. Verify

- [x] 5.1 Build clean (Qt Creator MCP).
- [x] 5.2 1802 tests pass (1797 prior + 3 new methods, with QCOMPARE-in-loop expansion accounting for the rest of the +5). All 3 new tests confirmed passing via `mcp__qtcreator__run_tests` with `scope:"named"`.

## 6. Documentation

- [x] 6.1 Cache invalidation contract documented inline on the new `ShotRecord::cachedAnalysis` field.
- [x] 6.2 Updated `docs/SHOT_REVIEW.md` §3 to note that `convertShotRecord` reads from `record.cachedAnalysis` when populated.
