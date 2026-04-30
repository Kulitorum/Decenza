# Tasks

## 1. Helper

- [ ] 1.1 Add `src/history/shotanalysisinputs.h` (header-only, namespace `decenza`) declaring `struct AnalysisInputs { QStringList analysisFlags; double firstFrameSeconds; int frameCount; };` plus a templated `inline AnalysisInputs prepareAnalysisInputs(const T& source)` that reads `source.profileKbId` and `source.profileJson` and assembles the struct via the existing helpers.
- [ ] 1.2 The template MUST be header-only so unit tests can include it without linking the storage TU.

## 2. Use the helper in storage call sites

- [ ] 2.1 Replace the inline preparation block in `saveShotData` with `const auto inputs = decenza::prepareAnalysisInputs(data);` and pass `inputs.analysisFlags`, `inputs.firstFrameSeconds`, `inputs.frameCount` into `analyzeShot`.
- [ ] 2.2 Same for `loadShotRecordStatic` (reads from `record`).
- [ ] 2.3 Same for `convertShotRecord` (reads from `record`).
- [ ] 2.4 Confirm the existing `firstFrameSec`-vs-`firstFrameSeconds` naming mismatch between save and load resolves consistently (rename whichever is inconsistent).

## 3. Tests

- [ ] 3.1 Add a unit test in `tst_shotanalysis.cpp` (or new file) that exercises `prepareAnalysisInputs` on a synthetic `ShotRecord` and asserts the three output fields match what direct calls to `getAnalysisFlags` and `profileFrameInfoFromJson` would produce.
- [ ] 3.2 Add a regression test that all three call sites (save, load, convert) produce equivalent inputs for the same shot — locks in that the helper's invocation contract doesn't drift across sites.

## 4. Cleanup tst_shotsummarizer.cpp header

- [ ] 4.1 Update the file-level header comment in `tests/tst_shotsummarizer.cpp` (lines 1-13) to reference `analyzeShot` as the canonical pipeline entry point (post-#933) and `convertShotRecord`'s pre-population of `summaryLines` as the dedup mechanism (post-#935). The current text references "ShotHistoryStorage::generateShotSummary" and "the same call" patterns that pre-date the unification.

## 5. Verify

- [ ] 5.1 Build clean (Qt Creator MCP).
- [ ] 5.2 All existing tests pass.
- [ ] 5.3 Spot-check that the three call sites produce identical `analyzeShot` arguments for the same shot (via debug log or unit-test assertion).
