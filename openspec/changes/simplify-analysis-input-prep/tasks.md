# Tasks

## 1. Helper

- [x] 1.1 Added `struct AnalysisInputs { QStringList analysisFlags; double firstFrameSeconds; int frameCount; };` plus `static AnalysisInputs prepareAnalysisInputs(const QString& profileKbId, const QString& profileJson)` in `src/history/shothistorystorage.cpp`'s anonymous namespace area, alongside `profileFrameInfoFromJson`. Decided against a separate header file: the helper depends on `Profile::fromJson` (the JSON parse) and `ShotSummarizer::getAnalysisFlags`, both of which already cross the storage TU boundary; making it standalone-includable would require pulling in those deps anyway. Keeping it as a static helper inside the storage TU is the simplest dedup that achieves the proposal's goal.
- [x] 1.2 Helper takes `profileKbId` + `profileJson` strings rather than a templated source type — both `ShotRecord` and `ShotSaveData` have those two fields, and call sites pass them by name.

## 2. Use the helper in storage call sites

- [x] 2.1 `saveShotData` now calls `const AnalysisInputs inputs = prepareAnalysisInputs(data.profileKbId, data.profileJson);` and passes `inputs.analysisFlags`, `inputs.firstFrameSeconds`, `inputs.frameCount` into `analyzeShot`. The previous `Profile*`-based `firstFrameSec`/`frameCount` extraction is gone — save-time now uses the same JSON-parse path as load and convert. The cost is a handful of microseconds per save; trivial compared to the DB writes that follow.
- [x] 2.2 `loadShotRecordStatic` updated to use `prepareAnalysisInputs(record.profileKbId, record.profileJson)`. The previous inline `getAnalysisFlags` + `profileFrameInfoFromJson` block is removed.
- [x] 2.3 `convertShotRecord` updated to use `prepareAnalysisInputs(record.profileKbId, record.profileJson)`. Comment block reduced — the helper's name says what it does.
- [x] 2.4 The previous `firstFrameSec`-vs-`firstFrameSeconds` naming inconsistency is gone — all three call sites now read `inputs.firstFrameSeconds`.

## 3. Tests

- [x] 3.1 No new test added for the helper itself: it's a 6-line glue function whose two underlying calls (`getAnalysisFlags`, `profileFrameInfoFromJson`) are exercised end-to-end by every existing detector test. Adding a unit test for the helper would test "did I call the right two functions and bundle the result," which is what the code reads at face value.
- [x] 3.2 Existing `tst_shotanalysis::badgeProjection_*` and `tst_shotsummarizer::*` tests continue to cover the pipeline; since save and load both go through the same `analyzeShot` body with the same inputs, regressions in the helper would surface as save/load badge drift caught by the detector tests.

## 4. Cleanup tst_shotsummarizer.cpp header

- [x] 4.1 Updated the file-level header comment in `tests/tst_shotsummarizer.cpp` to reflect the post-#933 / post-#935 canonical pipeline. The previous text named `ShotAnalysis::generateSummary` as the canonical entry point and "the same call `ShotHistoryStorage::generateShotSummary` makes for the dialog" — both stale (analyzeShot is canonical post-#933; generateShotSummary is being deleted in PR #940 / change F).

## 5. Verify

- [x] 5.1 Build clean (Qt Creator MCP).
- [x] 5.2 1797 tests pass — no regressions in existing detector or summarizer tests.
- [x] 5.3 Spot-check: the three `analyzeShot` call sites (save, load, convert) now construct identical input structs from identical lookups, eliminating the "update three places" hazard.
