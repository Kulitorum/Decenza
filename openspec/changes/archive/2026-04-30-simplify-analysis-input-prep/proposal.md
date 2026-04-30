# Change: Consolidate analyzeShot input preparation into a small helper

## Why

Three call sites of `ShotAnalysis::analyzeShot` (`saveShotData`, `loadShotRecordStatic`, `convertShotRecord`) each independently look up the same two helper values for the same shot:

- `ShotSummarizer::getAnalysisFlags(profileKbId)` — reads from a static-cached profile knowledge map
- `profileFrameInfoFromJson(profileJson)` — parses the profile JSON to extract `firstFrameSeconds` and `frameCount`

Each call site has its own four-line inline preparation block. The lookups are independently cheap, but the duplication means any future addition to `analyzeShot`'s required inputs (e.g. a new `analysisFlags` flag, a new `ProfileFrameInfo` field) needs to be added in three places — exactly the kind of "you have to update N call sites identically" invariant the cascade-unification work was trying to eliminate.

This change folds the preparation into a single `decenza::AnalysisInputs prepareAnalysisInputs(const ShotRecord&)` (or equivalent for the save-time `ShotSaveData` shape) so call sites become one line each, and adding a new input field is a one-place change.

Same scope: stale comment in `tst_shotsummarizer.cpp`'s file-level header still references "the same call ShotHistoryStorage::generateShotSummary makes for the dialog" (post-`#933` and `#936` the canonical path is `analyzeShot`). Easy to update alongside the helper introduction.

## What Changes

- **ADD** `decenza::AnalysisInputs` struct (in `src/history/shotanalysisinputs.h` or similar) with fields for the analysis flags + frame info + first-frame seconds. Also export `prepareAnalysisInputs(const ShotRecord&)` and `prepareAnalysisInputs(const ShotSaveData&)` overloads (or one templated helper).
- **MODIFY** the three call sites in `shothistorystorage.cpp` to use the helper instead of inline lookups. Reduces ~12 lines to ~3 each.
- **UPDATE** the file-level header comment in `tests/tst_shotsummarizer.cpp` (lines 1-13) to reflect the post-#933 canonical pipeline.
- **NO change** to behavior — the helper just consolidates lookups that were already happening per call site.

## Impact

- Affected specs: `shot-analysis-pipeline` — new requirement: analyzeShot input preparation lives in exactly one helper.
- Affected code: `src/history/shotanalysisinputs.h` (new), `src/history/shothistorystorage.cpp` (three call sites simplified), `tests/tst_shotsummarizer.cpp` (header comment).
- User-visible behavior: identical.
- Performance: identical (the lookups are unchanged, just consolidated).
- Risk: low. Pure refactor with no behavior change. Both saved and loaded `ShotRecord` shapes already have `profileKbId` and `profileJson` fields; the helper just reads those.
