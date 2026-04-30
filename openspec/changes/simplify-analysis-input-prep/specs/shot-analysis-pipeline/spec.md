# shot-analysis-pipeline

## ADDED Requirements

### Requirement: `analyzeShot` input preparation SHALL live in exactly one helper

The two helper lookups required to populate `analyzeShot`'s arguments — `ShotSummarizer::getAnalysisFlags(profileKbId)` and `profileFrameInfoFromJson(profileJson)` — SHALL be consolidated into a single `decenza::prepareAnalysisInputs` helper (declared in `src/history/shotanalysisinputs.h`). The helper SHALL accept any `ShotRecord`-shaped or `ShotSaveData`-shaped source that exposes `profileKbId` and `profileJson` fields, and return a typed `AnalysisInputs` struct containing the `analysisFlags`, `firstFrameSeconds`, and `frameCount` values.

The three storage-layer `analyzeShot` call sites (`saveShotData`, `loadShotRecordStatic`, `convertShotRecord`) SHALL each invoke `prepareAnalysisInputs` once and pass the resulting fields into `analyzeShot`. None of them SHALL retain inline calls to `getAnalysisFlags` or `profileFrameInfoFromJson` for the purpose of building `analyzeShot` arguments.

`analyzeShot`'s signature, the badge column projection, the prose `summaryLines`, and the structured `detectorResults` JSON SHALL all remain unchanged. This is a pure refactor.

A future addition to `analyzeShot`'s required inputs (e.g. a new `analysisFlags` flag, a new `ProfileFrameInfo` field) SHALL be made by extending `AnalysisInputs` and `prepareAnalysisInputs` once, with the three call sites picking up the new field automatically.

#### Scenario: Save-time call site uses the helper

- **GIVEN** a `ShotSaveData` with populated `profileKbId` and `profileJson`
- **WHEN** `saveShotData` reaches the `analyzeShot` call
- **THEN** `saveShotData` SHALL invoke `decenza::prepareAnalysisInputs(data)` exactly once
- **AND** SHALL pass `inputs.analysisFlags`, `inputs.firstFrameSeconds`, and `inputs.frameCount` into `analyzeShot`
- **AND** SHALL NOT have any inline `getAnalysisFlags` or `profileFrameInfoFromJson` call remaining

#### Scenario: All three storage call sites produce equivalent inputs

- **GIVEN** the same shot's data exposed via three lenses (the live `ShotSaveData` at save time, the loaded `ShotRecord` at load time, the same `ShotRecord` passed through `convertShotRecord`)
- **WHEN** `prepareAnalysisInputs` is invoked in each
- **THEN** the resulting `AnalysisInputs` SHALL be byte-equal across all three calls (same `analysisFlags`, same `firstFrameSeconds`, same `frameCount`)
