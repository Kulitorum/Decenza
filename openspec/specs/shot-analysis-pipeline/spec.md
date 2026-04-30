# shot-analysis-pipeline Specification

## Purpose
TBD - created by archiving change dedup-shot-summary-dialog. Update Purpose after archive.
## Requirements
### Requirement: Shot Summary dialog SHALL prefer pre-computed `summaryLines` when present

The in-app Shot Summary dialog (`ShotAnalysisDialog.qml`) SHALL render its observation list from the `summaryLines` field of its input `shotData` map when that field is a non-empty list. The dialog SHALL invoke the `MainController.shotHistory.generateShotSummary(shotData)` Q_INVOKABLE bridge ONLY as a fallback when `summaryLines` is absent or empty (e.g. for legacy callers whose `shotData` did not flow through `ShotHistoryStorage::convertShotRecord`).

The dialog SHALL render identical prose lines (same text, same `type` values, same order) regardless of which path produced the list. The fallback exists for backwards compatibility with legacy entry points; it MUST NOT introduce visible differences.

#### Scenario: Modern shotData carries pre-computed summaryLines

- **GIVEN** a shot record loaded via `ShotHistoryStorage::convertShotRecord` (so `shotData.summaryLines` is populated by the `analyzeShot` call inside `convertShotRecord`)
- **WHEN** the user opens the Shot Summary dialog on that shot
- **THEN** the dialog SHALL render the lines from `shotData.summaryLines` directly
- **AND** the dialog SHALL NOT invoke `MainController.shotHistory.generateShotSummary(shotData)` (no second `analyzeShot` pass)

#### Scenario: Legacy shotData without summaryLines falls back to the wrapper

- **GIVEN** a `shotData` map whose `summaryLines` field is missing or an empty list
- **WHEN** the user opens the Shot Summary dialog
- **THEN** the dialog SHALL invoke `MainController.shotHistory.generateShotSummary(shotData)`
- **AND** SHALL render the returned line list with the same per-line dot colors as before this change

#### Scenario: Identical rendering across paths

- **GIVEN** two shotData maps `A` and `B` representing the same shot, where `A.summaryLines` is populated and `B.summaryLines` is empty
- **WHEN** the dialog opens on each
- **THEN** both renderings SHALL contain the same observation lines in the same order with the same `type` values

### Requirement: AI advisor's historical-shot path SHALL reuse pre-computed `summaryLines` when present

When `ShotSummarizer::summarizeFromHistory(shotData)` is invoked with a `shotData` map whose `summaryLines` field is a non-empty list, the function SHALL populate `ShotSummary::summaryLines` from that field directly AND SHALL skip its inline `ShotAnalysis::generateSummary(...)` invocation. The function SHALL also derive `ShotSummary::pourTruncatedDetected` from `shotData["detectorResults"]["pourTruncated"]` when the `detectorResults` field is present.

When `summaryLines` is absent or empty (legacy shots, direct test callers, imported shots without the new fields), the function SHALL fall back to the existing inline detector orchestration path. The fast and fallback paths SHALL produce equivalent `ShotSummary::summaryLines` for identical shot data — they cannot drift because both ultimately rely on the same `ShotAnalysis::analyzeShot` body.

The per-phase temperature instability markers (`markPerPhaseTempInstability`) SHALL remain gated on `!summary.pourTruncatedDetected AND ShotAnalysis::reachedExtractionPhase(historyMarkers, summary.totalDuration)` regardless of which path produced the summary lines.

The live-path entry point `ShotSummarizer::summarize(ShotDataModel*)` is OUT OF SCOPE — it operates on an in-progress shot for which no `convertShotRecord` has run, and SHALL continue to call `analyzeShot` (via `generateSummary`) inline.

#### Scenario: Modern shotData with pre-computed lines bypasses recomputation

- **GIVEN** a `shotData` map produced by `ShotHistoryStorage::convertShotRecord`, with `summaryLines` containing a known list and `detectorResults.pourTruncated == true`
- **WHEN** the AI advisor invokes `summarizeFromHistory(shotData)`
- **THEN** the resulting `ShotSummary.summaryLines` SHALL equal the input `shotData.summaryLines`
- **AND** `ShotSummary.pourTruncatedDetected` SHALL be `true`
- **AND** `ShotAnalysis::generateSummary(...)` SHALL NOT be invoked by `summarizeFromHistory` for this call

#### Scenario: Legacy shotData without summaryLines uses the inline path

- **GIVEN** a `shotData` map whose `summaryLines` field is missing or empty
- **WHEN** the AI advisor invokes `summarizeFromHistory(shotData)`
- **THEN** the function SHALL invoke `ShotAnalysis::generateSummary(...)` inline
- **AND** the resulting `ShotSummary.summaryLines` SHALL be non-empty for shots with sufficient curve data

#### Scenario: Fast and fallback paths produce equivalent results

- **GIVEN** two equivalent `shotData` maps for the same shot, `A` with `summaryLines` pre-populated and `B` without
- **WHEN** `summarizeFromHistory(A)` and `summarizeFromHistory(B)` are both invoked
- **THEN** the resulting `ShotSummary::summaryLines` from each call SHALL contain the same lines in the same order with the same `text` and `type` values

#### Scenario: Fast path preserves the pour-truncated cascade

- **GIVEN** a `shotData` map with non-empty `summaryLines` and `detectorResults.pourTruncated == true`
- **WHEN** the AI advisor invokes `summarizeFromHistory(shotData)`
- **THEN** `ShotSummary::pourTruncatedDetected` SHALL be `true`
- **AND** no `PhaseSummary::temperatureUnstable` markers SHALL be set on the summary's phase list (the cascade gates the per-phase pass identically to the slow path)

### Requirement: Save and load paths SHALL derive boolean quality badges from `DetectorResults` via a single documented projection

`ShotHistoryStorage::saveShotData` (save-time badge computation) and `ShotHistoryStorage::loadShotRecordStatic` (recompute-on-load) SHALL invoke `ShotAnalysis::analyzeShot(...)` exactly once per shot and project the five boolean badge columns from the returned `DetectorResults` struct using the documented mapping. Neither function SHALL retain hand-rolled per-detector calls or hand-rolled cascade gate conditions; the cascade SHALL live in exactly one place — `ShotAnalysis::analyzeShot`.

The projection mapping SHALL be implemented in `decenza::deriveBadgesFromAnalysis` (header-only, in `src/history/shotbadgeprojection.h`) as:

| Badge column | `DetectorResults` projection |
|---|---|
| `pourTruncatedDetected` | `d.pourTruncated` |
| `channelingDetected` | `d.channelingSeverity == "sustained"` (Transient does NOT set the badge) |
| `temperatureUnstable` | `d.tempUnstable` |
| `grindIssueDetected` | `d.grindHasData && (d.grindChokedPuck || d.grindYieldOvershoot || std::abs(d.grindFlowDeltaMlPerSec) > FLOW_DEVIATION_THRESHOLD)` |
| `skipFirstFrameDetected` | `d.skipFirstFrame` |

`FLOW_DEVIATION_THRESHOLD` SHALL be read from `ShotAnalysis::FLOW_DEVIATION_THRESHOLD`; consumers MUST NOT inline the numeric value.

The lazy-persist write-back in `loadShotRecordStatic` (when stored badge columns differ from recomputed values, the recomputed values are written back to the DB) SHALL continue to function; the recomputed values just come from the projection helper instead of from per-detector calls.

`ShotAnalysis::analyzeShot` SHALL accept an optional `expectedFrameCount` parameter and forward it to `detectSkipFirstFrame`. Save and load paths SHALL pass the profile's actual frame count so 1-frame profiles correctly suppress the skip-first-frame detector. Backwards-compatible: the parameter defaults to `-1` (unknown), preserving behavior for callers (legacy `generateSummary` wrapper) that don't pass it.

The DB schema, the badge column types, and the badge-driven UI surfaces (history-list filter chips, badge UI, `shots_list` MCP) are OUT OF SCOPE — they continue to read the same five boolean columns; only the *production* of those values is unified.

The Sustained-only semantic for `channelingDetected` SHALL be preserved exactly. A shot whose `DetectorResults.channelingSeverity` is `"transient"` MUST result in `channelingDetected = false`. This matches the badge behavior established by PR #922 and is documented in the projection table.

#### Scenario: Clean shot projects to all-false badge columns

- **GIVEN** a shot whose `analyzeShot` returns `DetectorResults` with `verdictCategory = "clean"` and all detector gates clear
- **WHEN** save-time or load-time badge derivation runs
- **THEN** all five boolean badge columns SHALL be `false`

#### Scenario: Pour-truncated cascade dominates the projection

- **GIVEN** a shot whose `analyzeShot` returns `pourTruncated = true` (and consequently `channelingChecked = false`, `grindChecked = false`, `tempUnstable = false`)
- **WHEN** save-time or load-time badge derivation runs
- **THEN** `pourTruncatedDetected` SHALL be `true`
- **AND** `channelingDetected`, `temperatureUnstable`, `grindIssueDetected` SHALL each be `false`
- **AND** `skipFirstFrameDetected` SHALL reflect `d.skipFirstFrame` independently (skip-first-frame is NOT suppressed by the cascade, matching PR #922's invariant)

#### Scenario: Transient channeling does NOT set the badge

- **GIVEN** a shot whose `analyzeShot` returns `channelingSeverity = "transient"`
- **WHEN** save-time or load-time badge derivation runs
- **THEN** `channelingDetected` SHALL be `false`
- **AND** the Shot Summary dialog SHALL still render the "Transient channel at Xs (self-healed)" caution line (the dialog reads `summaryLines`, not the boolean badge)

#### Scenario: Sustained channeling sets the badge

- **GIVEN** a shot whose `analyzeShot` returns `channelingSeverity = "sustained"`
- **WHEN** save-time or load-time badge derivation runs
- **THEN** `channelingDetected` SHALL be `true`

#### Scenario: Choked-puck shot fires the grind badge via the chokedPuck arm

- **GIVEN** a shot whose `analyzeShot` returns `grindHasData = true`, `grindChokedPuck = true`, `grindFlowDeltaMlPerSec` near zero
- **WHEN** save-time or load-time badge derivation runs
- **THEN** `grindIssueDetected` SHALL be `true`

#### Scenario: Yield-overshoot shot fires the grind badge via the yieldOvershoot arm

- **GIVEN** a shot whose `analyzeShot` returns `grindHasData = true`, `grindYieldOvershoot = true`
- **WHEN** save-time or load-time badge derivation runs
- **THEN** `grindIssueDetected` SHALL be `true`

#### Scenario: Flow delta within tolerance does NOT fire the grind badge

- **GIVEN** a shot whose `analyzeShot` returns `grindHasData = true`, both `grindChokedPuck` and `grindYieldOvershoot` false, and `|grindFlowDeltaMlPerSec| <= FLOW_DEVIATION_THRESHOLD`
- **WHEN** save-time or load-time badge derivation runs
- **THEN** `grindIssueDetected` SHALL be `false`

#### Scenario: Intentional temperature stepping does NOT fire the temp badge

- **GIVEN** a shot using a profile whose temperature goal range exceeds `TEMP_STEPPING_RANGE` (e.g. D-Flow 84→94°C)
- **AND** `analyzeShot` returns `tempIntentionalStepping = true`, `tempUnstable = false`
- **WHEN** save-time or load-time badge derivation runs
- **THEN** `temperatureUnstable` SHALL be `false`

#### Scenario: 1-frame profile suppresses skip-first-frame detection

- **GIVEN** a shot from a profile with `frameCount = 1`
- **WHEN** save or load runs `analyzeShot` with `expectedFrameCount = 1`
- **THEN** `analyzeShot` SHALL pass `expectedFrameCount` through to `detectSkipFirstFrame`
- **AND** `detectSkipFirstFrame` SHALL return `false` (no second frame to skip to)
- **AND** `skipFirstFrameDetected` SHALL be `false`

### Requirement: Shot detail loads SHALL run `analyzeShot` exactly once

When a shot is loaded via `ShotHistoryStorage::loadShotRecordStatic` and immediately serialized via `ShotHistoryStorage::convertShotRecord` (the canonical detail-load path), the application SHALL invoke `ShotAnalysis::analyzeShot` exactly once for that shot. The `AnalysisResult` produced by `loadShotRecordStatic`'s recompute SHALL be cached on the returned `ShotRecord` (via an optional field), and `convertShotRecord` SHALL read from that cache when present instead of running `analyzeShot` a second time.

When `convertShotRecord` is called on a `ShotRecord` that was NOT produced by `loadShotRecordStatic` — direct construction in `ShotHistoryExporter`, in tests, or any other path that bypasses the load helper — the cache MAY be absent. In that case, `convertShotRecord` SHALL fall back to running `analyzeShot` inline so behavior remains correct end-to-end.

The cached `AnalysisResult` SHALL be invalidated (cleared / reset) if any input curve on the `ShotRecord` is mutated after load. Today no caller mutates `ShotRecord` between `loadShotRecordStatic` and `convertShotRecord`, but the field's docstring SHALL document this invariant so future callers don't introduce a stale-cache bug.

`analyzeShot`'s signature, the badge column projection (`decenza::applyBadgesToTarget`), the prose `summaryLines`, and the structured `detectorResults` JSON SHALL all be unchanged. This is a pure caller-side dedup.

#### Scenario: Standard detail-load path runs analyzeShot once

- **GIVEN** a shot record loaded via `loadShotRecordStatic`
- **WHEN** the same `ShotRecord` is then passed to `convertShotRecord`
- **THEN** `loadShotRecordStatic` SHALL have populated `record.cachedAnalysis` with the `AnalysisResult` it computed
- **AND** `convertShotRecord` SHALL read `summaryLines` and `detectorResults` from the cached struct without invoking `ShotAnalysis::analyzeShot` itself

#### Scenario: Direct-construction caller falls back to inline analyzeShot

- **GIVEN** a `ShotRecord` constructed directly (e.g. `ShotHistoryExporter`) without `cachedAnalysis`
- **WHEN** `convertShotRecord(record)` is invoked
- **THEN** `convertShotRecord` SHALL invoke `ShotAnalysis::analyzeShot` inline
- **AND** the resulting `summaryLines` and `detectorResults` SHALL be identical to what the cached path would produce for the same input

#### Scenario: Cached and fallback paths produce equivalent output

- **GIVEN** two equivalent `ShotRecord`s for the same shot, `A` with `cachedAnalysis` populated and `B` without
- **WHEN** `convertShotRecord(A)` and `convertShotRecord(B)` are both invoked
- **THEN** the resulting QVariantMap's `summaryLines`, `detectorResults`, and all five badge boolean fields SHALL be byte-equal across the two calls

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

