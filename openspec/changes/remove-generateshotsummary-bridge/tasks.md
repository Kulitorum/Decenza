# Tasks

## 1. Verify no other callers exist

- [x] 1.1 Verified via `grep -rn "generateShotSummary" src/ qml/ tests/`. The only `ShotHistoryStorage::generateShotSummary` reference outside the declaration/definition was `ShotAnalysisDialog.qml`'s fallback branch (and one stale comment in `shotsummarizer.cpp` and a stale comment in `tst_shotsummarizer.cpp` header — both addressed below). `AIManager::generateShotSummary` is a separately-scoped function with a different signature; not affected.

## 2. Delete the Q_INVOKABLE

- [x] 2.1 Removed `Q_INVOKABLE QVariantList generateShotSummary(const QVariantMap&) const;` from `src/history/shothistorystorage.h`.
- [x] 2.2 Removed the `ShotHistoryStorage::generateShotSummary` definition (~55 lines: variantToPoints lambda, curve extraction, phase-marker reconstruction, `getAnalysisFlags` lookup, `profileFrameInfoFromJson` parse, `analyzeShot` call) from `src/history/shothistorystorage.cpp`.

## 3. Simplify dialog binding

- [x] 3.1 Replaced the multi-branch `analysisLines` property in `qml/components/ShotAnalysisDialog.qml` with the simpler single-source binding:
  ```qml
  property var analysisLines: {
      if (!analysisDialog.visible) return []
      return Array.isArray(shotData?.summaryLines) ? shotData.summaryLines : []
  }
  ```
- [x] 3.2 Replaced the multi-line comment block with a brief one explaining the single source (`convertShotRecord`'s `analyzeShot` pass) and the empty-fallback rationale.

## 4. Tests

- [x] 4.1 Verified no tests in `tst_shotanalysis.cpp` or `tst_shotsummarizer.cpp` exercised the deleted Q_INVOKABLE — both files test `ShotAnalysis::analyzeShot` / `ShotAnalysis::generateSummary` directly, not the storage-layer bridge.
- [x] 4.2 Confirmed remaining tests still cover the canonical pipeline (`tst_shotanalysis::analyzeShot_*`, `tst_shotanalysis::badgeProjection_*`, `tst_shotsummarizer::*`).

## 5. Verify

- [x] 5.1 Build clean (Qt Creator MCP).
- [x] 5.2 1797 tests pass (no regressions; 5 tests removed compared to the cache-analyzeshot-on-shotrecord branch since this branch was cut from main before D landed).

## 6. Documentation

- [x] 6.1 Updated `docs/SHOT_REVIEW.md` §3 to drop references to `generateShotSummary` as the bridge function. Replaced with: "The dialog reads `shotData.summaryLines` directly (populated by `convertShotRecord`)." Updated three other references in §3, §5, and §7 (PR #930 historical entry).
- [x] 6.2 Removed the stale "matches generateShotSummary" reference in `src/ai/shotsummarizer.cpp` (the comment now describes only the analyzeShot delegation, no cross-reference).
