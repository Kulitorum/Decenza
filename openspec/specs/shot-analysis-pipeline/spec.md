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

`ShotHistoryStorage::saveShot` (save-time badge computation) and `ShotHistoryStorage::loadShotRecordStatic` (recompute-on-load) SHALL invoke `ShotAnalysis::analyzeShot(...)` exactly once per shot and project the five boolean badge columns from the returned `DetectorResults` struct using the documented mapping. Neither function SHALL retain hand-rolled per-detector calls or hand-rolled cascade gate conditions; the cascade SHALL live in exactly one place — `ShotAnalysis::analyzeShot`.

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

The three storage-layer `analyzeShot` call sites (`saveShot`, `loadShotRecordStatic`, `convertShotRecord`) SHALL each invoke `prepareAnalysisInputs` once and pass the resulting fields into `analyzeShot`. None of them SHALL retain inline calls to `getAnalysisFlags` or `profileFrameInfoFromJson` for the purpose of building `analyzeShot` arguments.

`analyzeShot`'s signature, the badge column projection, the prose `summaryLines`, and the structured `detectorResults` JSON SHALL all remain unchanged. This is a pure refactor.

A future addition to `analyzeShot`'s required inputs (e.g. a new `analysisFlags` flag, a new `ProfileFrameInfo` field) SHALL be made by extending `AnalysisInputs` and `prepareAnalysisInputs` once, with the three call sites picking up the new field automatically.

#### Scenario: Save-time call site uses the helper

- **GIVEN** a `ShotSaveData` with populated `profileKbId` and `profileJson`
- **WHEN** `saveShot` reaches the `analyzeShot` call
- **THEN** `saveShot` SHALL invoke `decenza::prepareAnalysisInputs(data)` exactly once
- **AND** SHALL pass `inputs.analysisFlags`, `inputs.firstFrameSeconds`, and `inputs.frameCount` into `analyzeShot`
- **AND** SHALL NOT have any inline `getAnalysisFlags` or `profileFrameInfoFromJson` call remaining

#### Scenario: All three storage call sites produce equivalent inputs

- **GIVEN** the same shot's data exposed via three lenses (the live `ShotSaveData` at save time, the loaded `ShotRecord` at load time, the same `ShotRecord` passed through `convertShotRecord`)
- **WHEN** `prepareAnalysisInputs` is invoked in each
- **THEN** the resulting `AnalysisInputs` SHALL be byte-equal across all three calls (same `analysisFlags`, same `firstFrameSeconds`, same `frameCount`)

### Requirement: ShotHistoryStorage's implementation SHALL be split across multiple translation units by concern

`ShotHistoryStorage`'s ~2500-line implementation SHALL be split across at least three translation units to make navigation tractable for future contributors:

1. `shothistorystorage.cpp` — DB lifecycle, save path, load + recompute. Core lifecycle for a shot record.
2. `shothistorystorage_queries.cpp` — `requestShotsFiltered`, `requestRecentShotsByKbId`, `requestAutoFavorites`, distinct-value cache, `queryGrinderContext`. Read-only query helpers.
3. `shothistorystorage_serialize.cpp` — `convertShotRecord` and its `pointsToVariant` lambdas. Serialization to QVariantMap for QML / MCP / web consumption.

The class declaration SHALL remain in a single header (`shothistorystorage.h`); only the implementation is split. No public API change. The split SHALL preserve behavior — `git log --follow` continues to work for individual function histories because functions move atomically with their callers.

#### Scenario: Splitting does not change observable behavior

- **GIVEN** the codebase before the split, with all tests passing
- **WHEN** the split is applied
- **THEN** every test in `tst_dbmigration`, `tst_shotanalysis`, `tst_shotrecord_cache`, `tst_shotsummarizer`, and other suites that exercise `ShotHistoryStorage` SHALL continue to pass without modification

#### Scenario: Header surface is unchanged

- **GIVEN** the post-split codebase
- **WHEN** an external caller `#include "history/shothistorystorage.h"`
- **THEN** the same set of public methods SHALL be visible, with the same signatures, as before the split

### Requirement: Per-marker `PhaseSummary` construction SHALL live in exactly one helper

The per-marker loop that builds `PhaseSummary` entries from a curve set + phase markers SHALL be implemented in a single static helper `ShotSummarizer::buildPhaseSummariesForRange`. Both call sites — `ShotSummarizer::summarize()` (live path) and `ShotSummarizer::summarizeFromHistory()` (saved-shot path) — SHALL invoke this helper instead of their own inline loops.

The helper SHALL preserve the existing semantics: degenerate phases (`endTime <= startTime`) are skipped, but the corresponding `HistoryPhaseMarker` is still appended to the marker list (which is built in parallel by the caller and used by both `analyzeShot` and the helper). The four curve-helper static functions (`findValueAtTime`, `calculateAverage`, `calculateMax`, `calculateMin`) remain the math source of truth.

A future addition to `PhaseSummary`'s field set or per-phase metric computation SHALL be a one-place change in the helper, not a two-place edit across the two call sites.

#### Scenario: Live and history paths produce identical PhaseSummary lists

- **GIVEN** a shot with 3 phases (preinfusion, pour, decline)
- **WHEN** the same curve data is fed through both `summarize()` (with a `ShotDataModel*`) and `summarizeFromHistory()` (with the equivalent `QVariantMap`)
- **THEN** the resulting `ShotSummary::phases` lists SHALL be byte-equal across the two calls

#### Scenario: Degenerate phase is skipped but marker is preserved

- **GIVEN** a marker list with one phase where `endTime <= startTime`
- **WHEN** `buildPhaseSummariesForRange` is invoked
- **THEN** the returned `QList<PhaseSummary>` SHALL NOT include an entry for the degenerate phase
- **AND** the caller's `historyMarkers` list (built in parallel) SHALL still contain the corresponding `HistoryPhaseMarker` so `analyzeShot`'s skip-first-frame detection sees it

### Requirement: `DetectorResults` SHALL expose the pour window `analyzeShot` computed

`ShotAnalysis::DetectorResults` SHALL include `pourStartSec` and `pourEndSec` fields populated by `analyzeShot` from the same `pourStart` / `pourEnd` locals it uses internally for the suppression cascade and detector gates. These fields SHALL be the canonical pour-window values for any consumer that needs them.

`ShotSummarizer::computePourWindow` SHALL be deleted. Its sole consumer (the `markPerPhaseTempInstability` gate) SHALL read from `AnalysisResult::detectors::pourStartSec` / `pourEndSec` instead.

`ShotHistoryStorage::convertShotRecord` SHALL serialize the two new fields onto the MCP `detectorResults` JSON object so external agents have access to the same pour-window values the in-app cascade uses.

#### Scenario: Pour window matches analyzeShot's internal computation

- **GIVEN** any shot with phase markers
- **WHEN** `analyzeShot` is invoked
- **THEN** `result.detectors.pourStartSec` SHALL equal the `pourStart` value `analyzeShot` uses internally for its suppression-cascade gates
- **AND** `result.detectors.pourEndSec` SHALL equal the corresponding `pourEnd` value

#### Scenario: ShotSummarizer's per-phase temp gate uses the exposed window

- **GIVEN** a shot for which `markPerPhaseTempInstability` would run (not pour-truncated, reached extraction phase)
- **WHEN** `ShotSummarizer::summarize` or `summarizeFromHistory` runs
- **THEN** the gate's pour-window inputs SHALL come from `summary.pourStartSec` / `pourEndSec` (or equivalent cached `AnalysisResult` fields)
- **AND** `computePourWindow` SHALL no longer exist in the codebase

#### Scenario: MCP consumers see the pour window

- **GIVEN** a shot served via `shots_get_detail`
- **WHEN** the response is rendered
- **THEN** `detectorResults.pourStartSec` and `detectorResults.pourEndSec` SHALL be present and reflect the same values used internally for cascade gating

### Requirement: ShotSummarizer's detector-orchestration glue SHALL live in exactly one helper

`ShotSummarizer::summarize` (live shot) and `ShotSummarizer::summarizeFromHistory` (saved shot) SHALL each delegate their final detector-orchestration block to a single private static helper `ShotSummarizer::runShotAnalysisAndPopulate`. The helper accepts pre-extracted typed inputs (curves, markers, beverage type, analysis flags, frame info, target/final weights) plus an optional cached `AnalysisResult`, and populates the passed-in `ShotSummary`'s `summaryLines`, `pourTruncatedDetected`, and per-phase `temperatureUnstable` markers.

The two callers SHALL retain their respective input-adapter roles (live extracts from `ShotDataModel*`; history extracts from `QVariantMap` via `variantListToPoints`) but SHALL NOT contain duplicated `analyzeShot`-call + result-unpacking + `markPerPhaseTempInstability`-gating logic.

The fast-path optimization from change `dedup-ai-advisor-history-path` (reading pre-computed `summaryLines` when present on `summarizeFromHistory`'s input) SHALL be preserved by passing the cached `AnalysisResult` to the helper when applicable; the helper's `cachedAnalysis.has_value()` branch handles the rest.

#### Scenario: Live and history paths reuse the same orchestration helper

- **GIVEN** the same shot data presented to both `summarize(ShotDataModel*, ...)` and `summarizeFromHistory(QVariantMap)`
- **WHEN** the two functions run
- **THEN** both SHALL call `ShotSummarizer::runShotAnalysisAndPopulate` with equivalent inputs
- **AND** the resulting `ShotSummary` SHALL be byte-equal across the two paths (same `summaryLines`, same `pourTruncatedDetected`, same per-phase `temperatureUnstable` flags)

#### Scenario: Cached AnalysisResult fast-path is preserved through the helper

- **GIVEN** a `summarizeFromHistory` call where `shotData["summaryLines"]` is non-empty
- **WHEN** the helper is invoked with a cached `AnalysisResult` derived from those lines
- **THEN** the helper SHALL NOT re-run `analyzeShot`
- **AND** SHALL populate `summary.summaryLines` from the cache and derive `pourTruncatedDetected` from `cachedAnalysis.detectors.pourTruncated`

### Requirement: `ShotSummarizer::summarize` (live shot path) SHALL have direct unit-test coverage

The live-shot summary path `ShotSummarizer::summarize(const ShotDataModel*, const Profile*, const ShotMetadata&, double doseWeight, double finalWeight)` SHALL be exercised by at least three direct unit tests in `tst_shotsummarizer.cpp`, mirroring the canonical scenarios already covered for the saved-shot path:

1. **Puck-failure suppression cascade**: a shot whose pressure stays below `PRESSURE_FLOOR_BAR` produces `pourTruncatedDetected = true`, the `"Pour never pressurized"` warning line, the puck-failed verdict, and NO channeling / temp-drift lines.
2. **Aborted-during-preinfusion gate**: a shot that died during preinfusion-start does NOT flag per-phase `temperatureUnstable`, even with a large measured temp deviation, because `reachedExtractionPhase` returns false.
3. **Healthy shot baseline**: a clean shot produces non-empty observation lines, a verdict line, and `pourTruncatedDetected = false`.

The tests SHALL use a `MockShotDataModel` (or equivalent test double) that exposes the curve and phase-marker accessor methods `summarize()` reads. The mock SHALL NOT depend on the full `ShotDataModel` runtime (no signals, no `QObject`-machinery beyond what's needed to satisfy the function signature).

#### Scenario: Live path puck-failure test passes

- **GIVEN** a `MockShotDataModel` populated with puck-failure curves (pressure flat at 1.0 bar, flow at preinfusion goal, conductance derivative spikes)
- **WHEN** `summarize(mock, profile, metadata, 18.0, 36.0)` runs
- **THEN** the resulting `ShotSummary::pourTruncatedDetected` SHALL be `true`
- **AND** `summaryLines` SHALL contain the `"Pour never pressurized"` warning
- **AND** SHALL NOT contain `"Sustained channeling"` or `"Temperature drifted"` lines

#### Scenario: Live and history paths produce equivalent summaries (optional)

- **GIVEN** the same shot data presented to `summarize()` (via mock) and `summarizeFromHistory()` (via QVariantMap)
- **WHEN** both run
- **THEN** the resulting `ShotSummary::summaryLines`, `pourTruncatedDetected`, and `phases` SHALL be byte-equal

### Requirement: Grind detector SHALL emit a coverage signal distinguishing verified-clean from not-analyzable

The grind detector SHALL emit a `grindCoverage` signal taking one of three values (`"verified"`, `"notAnalyzable"`, `"skipped"`) so that the system can distinguish a positively-verified clean grind from the absence of analyzable data. When `ShotAnalysis::analyzeFlowVsGoal` runs against an espresso shot whose beverage type and analysis flags do not gate it out (`skipped == false`), the function SHALL set `GrindCheck::hasData = true` whenever its choked-puck loop sees BOTH `flowSamples >= 5` AND `pressurizedDuration >= 15.0 s` (the existing gate constants `CHOKED_PRESSURE_MIN_BAR = 4.0` and `CHOKED_DURATION_MIN_SEC = 15.0`), regardless of whether `flowChoked` or `yieldShortfall` fires. When neither sub-arm fires, the function SHALL ALSO set a new `GrindCheck::verifiedClean = true` flag.

`ShotAnalysis::analyzeShot` SHALL populate a new `DetectorResults::grindCoverage`
field with one of three string values:

- `"verified"` — `GrindCheck.hasData == true`. The detector ran with enough
  data to produce a result. Set whether or not the result is healthy: a
  shot whose Arm 2 gates passed without a choke firing AND a shot that
  fired `chokedPuck`, `yieldOvershoot`, or a large `delta` BOTH carry
  `coverage = "verified"`. The verdict and `grindDirection` already
  carry the specific diagnosis; coverage signals data availability,
  not health outcome. Consumers wanting "verified clean" specifically
  should read `grindVerifiedClean` directly.
- `"notAnalyzable"` — `GrindCheck.hasData == false && GrindCheck.skipped == false`,
  AND the espresso shot's pour window was non-degenerate (`pourEndSec > pourStartSec`),
  AND the beverage type is not in the non-espresso skip list.
- `"skipped"` — `GrindCheck.skipped == true` (non-espresso beverages or profiles
  carrying the `grind_check_skip` analysis flag).

When the pourTruncated cascade is active, the field SHALL be omitted entirely
(consistent with how the channeling, flow-trend, temp, and grind blocks are
already suppressed in that cascade).

The five quality-badge boolean projections in `src/history/shotbadgeprojection.h`
SHALL NOT change. Specifically: `grindIssueDetected` SHALL still require
`grindHasData && (grindChokedPuck || grindYieldOvershoot || |grindFlowDeltaMlPerSec| > FLOW_DEVIATION_THRESHOLD)`.
A verified-clean result SHALL project `grindIssueDetected = false`.

#### Scenario: Verified-clean shot emits a positive signal

- **GIVEN** an espresso shot with beverage type `"espresso"` and a healthy
  pressurized pour (≥ 5 flow samples, ≥ 15 s sustained at ≥ 4 bar)
- **AND** mean pressurized flow ≥ 0.5 mL/s
- **AND** either `targetWeightG == 0` OR `finalWeightG / targetWeightG >= 0.85`
- **WHEN** `analyzeShot` runs
- **THEN** `DetectorResults.grindCoverage` SHALL equal `"verified"`
- **AND** `summaryLines` SHALL contain one entry with `type = "good"` and text
  "Grind tracked goal during pour"
- **AND** `grindIssueDetected` SHALL be `false`

#### Scenario: Profile shape that defeats both arms emits an honest signal

- **GIVEN** an espresso shot whose phase markers are exclusively flow-mode OR
  whose pressurized duration is below 15 s
- **AND** the choked-puck arm produces no usable data (`flowSamples < 5` OR
  `pressurizedDuration < 15.0 s`)
- **AND** the flow-vs-goal arm produces no usable data (no flow-mode samples
  in the pour window with `goal >= 0.3 mL/s`)
- **AND** the beverage type is `"espresso"`
- **AND** the pour window is non-degenerate (`pourEndSec > pourStartSec`)
- **WHEN** `analyzeShot` runs
- **THEN** `DetectorResults.grindCoverage` SHALL equal `"notAnalyzable"`
- **AND** `summaryLines` SHALL contain one entry with `type = "observation"`
  and text starting with "Could not analyze grind on this profile shape"
- **AND** `grindIssueDetected` SHALL be `false`

#### Scenario: Choked puck verdict is unchanged

- **GIVEN** an espresso shot whose mean pressurized flow is below 0.5 mL/s
  AND the choked-puck arm gates pass
- **WHEN** `analyzeShot` runs
- **THEN** `DetectorResults.grindCoverage` SHALL equal `"verified"` (the gates
  passed, so the loop has data) — but `chokedPuck` SHALL be `true` and the
  existing "Puck choked" warning line and verdict SHALL fire identically to
  prior behavior
- **AND** `verifiedClean` SHALL be `false`
- **AND** `grindIssueDetected` SHALL be `true`

#### Scenario: Pour-truncated cascade suppresses the coverage signal

- **GIVEN** an espresso shot where `pourTruncated == true` (peak pressure
  inside the pour window is below `PRESSURE_FLOOR_BAR`)
- **WHEN** `analyzeShot` runs
- **THEN** `DetectorResults.grindCoverage` SHALL be absent from the structured
  output
- **AND** `summaryLines` SHALL NOT contain the new "Grind tracked goal" line
  NOR the new "Could not analyze grind" line
- **AND** the existing pourTruncated cascade behavior SHALL apply unchanged

### Requirement: Verdict cascade SHALL acknowledge a not-analyzable grind result

The verdict cascade SHALL distinguish "clean and verified" from "clean but grind not analyzable" so users on lever / two-frame profiles see honest UI text. When the verdict cascade in `analyzeShot` reaches the "Otherwise" terminal branch (no warnings, no cautions) AND `DetectorResults.grindCoverage == "notAnalyzable"`, the cascade SHALL emit the verdict text "Clean shot, but grind could not be evaluated for this profile shape." instead of the existing "Clean shot. Puck held well."

When the cascade reaches the same terminal branch with `grindCoverage ==
"verified"` or `grindCoverage` absent (e.g. non-espresso skipped path), the
cascade SHALL emit the existing "Verdict: Clean shot. Puck held well." text
unchanged.

The `verdictCategory` enum string emitted on `DetectorResults` SHALL gain a
new value `"cleanGrindNotAnalyzable"` for the new branch. The existing
`"clean"` value SHALL continue to apply when grind was verified or skipped.

#### Scenario: Verified-clean shot keeps the existing clean verdict

- **GIVEN** an espresso shot with `grindCoverage == "verified"` AND no
  warning/caution lines
- **WHEN** the verdict cascade runs
- **THEN** the verdict line SHALL read "Verdict: Clean shot. Puck held well."
- **AND** `verdictCategory` SHALL equal `"clean"`

#### Scenario: Not-analyzable shot gets the honest verdict

- **GIVEN** an espresso shot with `grindCoverage == "notAnalyzable"` AND no
  warning/caution lines
- **WHEN** the verdict cascade runs
- **THEN** the verdict line SHALL read "Verdict: Clean shot, but grind could
  not be evaluated for this profile shape."
- **AND** `verdictCategory` SHALL equal `"cleanGrindNotAnalyzable"`

#### Scenario: Warnings or cautions take precedence over the new verdict

- **GIVEN** an espresso shot with `grindCoverage == "notAnalyzable"` AND
  any other detector emits a warning or caution line
- **WHEN** the verdict cascade runs
- **THEN** the verdict SHALL come from the existing precedence rules
  (pourTruncated → skipFirstFrame → chokedPuck → "Puck integrity issue" →
  caution-grind direction → "Decent shot with minor issues to watch"),
  NOT from the new "grind could not be evaluated" branch

