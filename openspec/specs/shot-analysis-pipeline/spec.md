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

### Requirement: Save and load paths SHALL derive boolean quality badges from `DetectorResults` via a single documented projection

`ShotHistoryStorage::saveShot` (save-time badge computation) and `ShotHistoryStorage::loadShotRecordStatic` (recompute-on-load) SHALL invoke `ShotAnalysis::analyzeShot(...)` exactly once per shot and project the four boolean badge columns from the returned `DetectorResults` struct using the documented mapping. Neither function SHALL retain hand-rolled per-detector calls or hand-rolled cascade gate conditions; the cascade SHALL live in exactly one place — `ShotAnalysis::analyzeShot`.

The projection mapping SHALL be implemented in `decenza::deriveBadgesFromAnalysis` (header-only, in `src/history/shotbadgeprojection.h`) as:

| Badge column | `DetectorResults` projection |
|---|---|
| `pourTruncatedDetected` | `d.pourTruncated` |
| `channelingDetected` | `d.channelingSeverity == "sustained"` (Transient does NOT set the badge) |
| `grindIssueDetected` | `d.grindHasData && (d.grindChokedPuck || d.grindYieldOvershoot || std::abs(d.grindFlowDeltaMlPerSec) > FLOW_DEVIATION_THRESHOLD)` |
| `skipFirstFrameDetected` | `d.skipFirstFrame` |

`FLOW_DEVIATION_THRESHOLD` SHALL be read from `ShotAnalysis::FLOW_DEVIATION_THRESHOLD`; consumers MUST NOT inline the numeric value.

The lazy-persist write-back in `loadShotRecordStatic` (when stored badge columns differ from recomputed values, the recomputed values are written back to the DB) SHALL continue to function; the recomputed values just come from the projection helper instead of from per-detector calls.

`ShotAnalysis::analyzeShot` SHALL accept an optional `expectedFrameCount` parameter and forward it to `detectSkipFirstFrame`. Save and load paths SHALL pass the profile's actual frame count so 1-frame profiles correctly suppress the skip-first-frame detector. Backwards-compatible: the parameter defaults to `-1` (unknown), preserving behavior for callers (legacy `generateSummary` wrapper) that don't pass it.

The DB schema, the badge column types, and the badge-driven UI surfaces (history-list filter chips, badge UI, `shots_list` MCP) are OUT OF SCOPE — they continue to read the same four boolean columns; only the *production* of those values is unified.

The Sustained-only semantic for `channelingDetected` SHALL be preserved exactly. A shot whose `DetectorResults.channelingSeverity` is `"transient"` MUST result in `channelingDetected = false`. This matches the badge behavior established by PR #922 and is documented in the projection table.

#### Scenario: Clean shot projects to all-false badge columns

- **GIVEN** a shot whose `analyzeShot` returns `DetectorResults` with `verdictCategory = "clean"` and all detector gates clear
- **WHEN** save-time or load-time badge derivation runs
- **THEN** all four boolean badge columns SHALL be `false`

#### Scenario: Pour-truncated cascade dominates the projection

- **GIVEN** a shot whose `analyzeShot` returns `pourTruncated = true` (and consequently `channelingChecked = false`, `grindChecked = false`)
- **WHEN** save-time or load-time badge derivation runs
- **THEN** `pourTruncatedDetected` SHALL be `true`
- **AND** `channelingDetected`, `grindIssueDetected` SHALL each be `false`
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

`ShotSummarizer::computePourWindow` SHALL be deleted. MCP consumers reading `pourStartSec` / `pourEndSec` SHALL read from `AnalysisResult::detectors::pourStartSec` / `pourEndSec` instead of re-deriving the window themselves.

`ShotHistoryStorage::convertShotRecord` SHALL serialize the two new fields onto the MCP `detectorResults` JSON object so external agents have access to the same pour-window values the in-app cascade uses.

#### Scenario: Pour window matches analyzeShot's internal computation

- **GIVEN** any shot with phase markers
- **WHEN** `analyzeShot` is invoked
- **THEN** `result.detectors.pourStartSec` SHALL equal the `pourStart` value `analyzeShot` uses internally for its suppression-cascade gates
- **AND** `result.detectors.pourEndSec` SHALL equal the corresponding `pourEnd` value

#### Scenario: computePourWindow is gone

- **GIVEN** the current codebase
- **WHEN** any consumer needs the pour window
- **THEN** it SHALL read from `AnalysisResult::detectors::pourStartSec` / `pourEndSec`
- **AND** `computePourWindow` SHALL no longer exist in the codebase

#### Scenario: MCP consumers see the pour window

- **GIVEN** a shot served via `shots_get_detail`
- **WHEN** the response is rendered
- **THEN** `detectorResults.pourStartSec` and `detectorResults.pourEndSec` SHALL be present and reflect the same values used internally for cascade gating

### Requirement: ShotSummarizer's detector-orchestration glue SHALL live in exactly one helper

`ShotSummarizer::summarize` (live shot) and `ShotSummarizer::summarizeFromHistory` (saved shot) SHALL each delegate their final detector-orchestration block to a single private static helper `ShotSummarizer::runShotAnalysisAndPopulate`. The helper accepts pre-extracted typed inputs (curves, markers, beverage type, analysis flags, frame info, target/final weights) plus an optional cached `AnalysisResult`, and populates the passed-in `ShotSummary`'s `summaryLines` and `pourTruncatedDetected`.

The two callers SHALL retain their respective input-adapter roles (live extracts from `ShotDataModel*`; history extracts from `QVariantMap` via `variantListToPoints`) but SHALL NOT contain duplicated `analyzeShot`-call + result-unpacking logic.

The fast-path optimization from change `dedup-ai-advisor-history-path` (reading pre-computed `summaryLines` when present on `summarizeFromHistory`'s input) SHALL be preserved by passing the cached `AnalysisResult` to the helper when applicable; the helper's `cachedAnalysis.has_value()` branch handles the rest.

#### Scenario: Live and history paths reuse the same orchestration helper

- **GIVEN** the same shot data presented to both `summarize(ShotDataModel*, ...)` and `summarizeFromHistory(QVariantMap)`
- **WHEN** the two functions run
- **THEN** both SHALL call `ShotSummarizer::runShotAnalysisAndPopulate` with equivalent inputs
- **AND** the resulting `ShotSummary` SHALL be byte-equal across the two paths (same `summaryLines`, same `pourTruncatedDetected`)

#### Scenario: Cached AnalysisResult fast-path is preserved through the helper

- **GIVEN** a `summarizeFromHistory` call where `shotData["summaryLines"]` is non-empty
- **WHEN** the helper is invoked with a cached `AnalysisResult` derived from those lines
- **THEN** the helper SHALL NOT re-run `analyzeShot`
- **AND** SHALL populate `summary.summaryLines` from the cache and derive `pourTruncatedDetected` from `cachedAnalysis.detectors.pourTruncated`

### Requirement: `ShotSummarizer::summarize` (live shot path) SHALL have direct unit-test coverage

The live-shot summary path `ShotSummarizer::summarize(const ShotDataModel*, const Profile*, const ShotMetadata&, double doseWeight, double finalWeight)` SHALL be exercised by at least two direct unit tests in `tst_shotsummarizer.cpp`, mirroring the canonical scenarios already covered for the saved-shot path:

1. **Puck-failure suppression cascade**: a shot whose pressure stays below `PRESSURE_FLOOR_BAR` produces `pourTruncatedDetected = true`, the `"Pour never pressurized"` warning line, the puck-failed verdict, and NO channeling lines.
2. **Healthy shot baseline**: a clean shot produces non-empty observation lines, a verdict line, and `pourTruncatedDetected = false`.

The tests SHALL use a `MockShotDataModel` (or equivalent test double) that exposes the curve and phase-marker accessor methods `summarize()` reads. The mock SHALL NOT depend on the full `ShotDataModel` runtime (no signals, no `QObject`-machinery beyond what's needed to satisfy the function signature).

#### Scenario: Live path puck-failure test passes

- **GIVEN** a `MockShotDataModel` populated with puck-failure curves (pressure flat at 1.0 bar, flow at preinfusion goal, conductance derivative spikes)
- **WHEN** `summarize(mock, profile, metadata, 18.0, 36.0)` runs
- **THEN** the resulting `ShotSummary::pourTruncatedDetected` SHALL be `true`
- **AND** `summaryLines` SHALL contain the `"Pour never pressurized"` warning
- **AND** SHALL NOT contain `"Sustained channeling"` lines

#### Scenario: Live and history paths produce equivalent summaries (optional)

- **GIVEN** the same shot data presented to `summarize()` (via mock) and `summarizeFromHistory()` (via QVariantMap)
- **WHEN** both run
- **THEN** the resulting `ShotSummary::summaryLines`, `pourTruncatedDetected`, and `phases` SHALL be byte-equal

### Requirement: Grind detector SHALL emit a coverage signal distinguishing verified-clean from not-analyzable

The grind detector SHALL emit a `grindCoverage` signal taking one of three values (`"verified"`, `"notAnalyzable"`, `"skipped"`) so that the system can distinguish a positively-verified clean grind from the absence of analyzable data. When `ShotAnalysis::analyzeFlowVsGoal` runs against an espresso shot whose beverage type and analysis flags do not gate it out (`skipped == false`), the function SHALL evaluate the choked-puck arms with split gating: the **flow-choked arm** SHALL fire only when ALL of `flowSamples >= 5` AND `pressurizedDuration >= CHOKED_DURATION_MIN_SEC` (15.0 s) AND mean pressurized flow `< CHOKED_FLOW_MAX_MLPS` (0.5 mL/s) hold. The **yield-shortfall arm** SHALL fire when ALL of `flowSamples >= 5` AND `targetWeightG > 0.0` AND `finalWeightG > 0.0` AND `(finalWeightG / targetWeightG) < CHOKED_YIELD_RATIO_MAX` (0.70 — tightened from a prior 0.85 by audit) hold. The yield arm SHALL NOT require `pressurizedDuration >= 15.0 s` — its diagnosis is yield-based and does not read pressurized flow.

The function SHALL set `GrindCheck::hasData = true` whenever EITHER arm could speak: when `flowSamples >= 5` AND (`pressurizedDuration >= 15.0 s` OR the yield-shortfall arm fired). The function SHALL set `GrindCheck::verifiedClean = true` only when `flowSamples >= 5` AND `pressurizedDuration >= 15.0 s` AND neither sub-arm fired AND `|delta| <= FLOW_DEVIATION_THRESHOLD` AND `yieldOvershoot == false`. The verified-clean signal still requires the strong flow-arm gates because it asserts a healthy sustained pressurized pour.

`ShotAnalysis::analyzeShot` SHALL populate a `DetectorResults::grindCoverage` field with one of three string values:

- `"verified"` — `GrindCheck.hasData == true`. The detector ran with enough data to produce a result. Set whether or not the result is healthy: a verified-clean pour AND a chokedPuck/yieldOvershoot/large-delta pour BOTH carry `coverage = "verified"`. Coverage signals data availability, not health outcome — read `grindVerifiedClean` / `grindDirection` / verdict for the diagnosis.
- `"notAnalyzable"` — `GrindCheck.hasData == false && GrindCheck.skipped == false`, AND the espresso shot's pour window was non-degenerate (`pourEndSec > pourStartSec`), AND the beverage type is not in the non-espresso skip list.
- `"skipped"` — `GrindCheck.skipped == true` (non-espresso beverages or profiles carrying the `grind_check_skip` analysis flag).

When the pourTruncated cascade is active, the field SHALL be omitted entirely (consistent with how the channeling, flow-trend, and grind blocks are already suppressed in that cascade).

The four quality-badge boolean projections in `src/history/shotbadgeprojection.h` SHALL NOT change. Specifically: `grindIssueDetected` SHALL still require `grindHasData && (grindChokedPuck || grindYieldOvershoot || |grindFlowDeltaMlPerSec| > FLOW_DEVIATION_THRESHOLD)`. A verified-clean result SHALL project `grindIssueDetected = false`. A yield-shortfall-only result (yield arm fired, flow arm gates didn't pass) SHALL project `grindIssueDetected = true` because `chokedPuck` is set when either choke sub-arm fires.

#### Scenario: Verified-clean shot emits a positive signal

- **GIVEN** an espresso shot with beverage type `"espresso"` and a healthy pressurized pour (≥ 5 flow samples, ≥ 15 s sustained at ≥ 4 bar)
- **AND** mean pressurized flow ≥ 0.5 mL/s
- **AND** either `targetWeightG == 0` OR `finalWeightG / targetWeightG >= 0.70`
- **WHEN** `analyzeShot` runs
- **THEN** `DetectorResults.grindCoverage` SHALL equal `"verified"`
- **AND** `summaryLines` SHALL contain one entry with `type = "good"` and text "Grind tracked goal during pour"
- **AND** `grindIssueDetected` SHALL be `false`

#### Scenario: Profile shape that defeats both arms emits an honest signal

- **GIVEN** an espresso shot whose phase markers are exclusively flow-mode OR whose pressurized duration is below 15 s
- **AND** the choked-puck arm produces no usable data (`flowSamples < 5` — i.e. no pressurized samples at all)
- **AND** the flow-vs-goal arm produces no usable data (no flow-mode samples in the pour window with `goal >= 0.3 mL/s`)
- **AND** the beverage type is `"espresso"`
- **AND** the pour window is non-degenerate (`pourEndSec > pourStartSec`)
- **WHEN** `analyzeShot` runs
- **THEN** `DetectorResults.grindCoverage` SHALL equal `"notAnalyzable"`
- **AND** `summaryLines` SHALL contain one entry with `type = "observation"` and text starting with "Could not analyze grind on this profile shape"
- **AND** `grindIssueDetected` SHALL be `false`

#### Scenario: Choked puck verdict is unchanged on the flow arm

- **GIVEN** an espresso shot whose mean pressurized flow is below 0.5 mL/s AND the flow-choked arm gates pass (≥ 5 samples AND ≥ 15s pressurized)
- **WHEN** `analyzeShot` runs
- **THEN** `DetectorResults.grindCoverage` SHALL equal `"verified"` (the flow-arm gates passed)
- **AND** `chokedPuck` SHALL be `true` and the existing "Puck choked" warning line and verdict SHALL fire identically to prior behavior
- **AND** `verifiedClean` SHALL be `false`
- **AND** `grindIssueDetected` SHALL be `true`

#### Scenario: Yield-shortfall arm fires on a brief-pressurized shot

- **GIVEN** an espresso shot with `flowSamples >= 5` (puck saw meaningful pressure briefly) AND `pressurizedDuration < 15 s` (flow-arm gate did NOT pass)
- **AND** `targetWeightG > 0` AND `finalWeightG > 0`
- **AND** `(finalWeightG / targetWeightG) < 0.70` (e.g. 23.1g of a 36g target = 0.64)
- **WHEN** `analyzeShot` runs
- **THEN** `chokedPuck` SHALL be `true` (yield arm fired)
- **AND** `hasData` SHALL be `true`
- **AND** `verifiedClean` SHALL be `false` (flow-arm gates required for verification)
- **AND** `DetectorResults.grindCoverage` SHALL equal `"verified"` (an arm produced data)
- **AND** `grindIssueDetected` SHALL be `true`
- **AND** `summaryLines` SHALL include the existing "Pour produced near-zero flow while pressure held — puck choked" warning

#### Scenario: Borderline yield ratio between 0.70 and 0.85 stays silent

- **GIVEN** an espresso shot with `flowSamples >= 5` AND `pressurizedDuration < 15 s`
- **AND** `(finalWeightG / targetWeightG) = 0.75` (above 0.70 threshold but below the prior 0.85)
- **WHEN** `analyzeShot` runs
- **THEN** `chokedPuck` SHALL be `false`
- **AND** the audit-driven 0.70 threshold SHALL hold; no warning line about "Pour produced near-zero flow" SHALL fire
- **AND** `grindIssueDetected` SHALL be `false`

#### Scenario: Pour-truncated cascade suppresses the coverage signal

- **GIVEN** an espresso shot where `pourTruncated == true` (peak pressure inside the pour window is below `PRESSURE_FLOOR_BAR`)
- **WHEN** `analyzeShot` runs
- **THEN** `DetectorResults.grindCoverage` SHALL be absent from the structured output
- **AND** `summaryLines` SHALL NOT contain the new "Grind tracked goal" line NOR the new "Could not analyze grind" line
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

### Requirement: Grind Arm 1 (flow-vs-goal averaging) SHALL be skipped on KB-unresolved profiles

`ShotAnalysis::analyzeShot` and `ShotAnalysis::analyzeFlowVsGoal` SHALL accept a `bool profileKbResolved` parameter (defaulting to `true` for backward compatibility with direct test callers and other in-process invocations). The parameter SHALL be `true` when the profile's KB resolution via `ShotSummarizer::matchProfileKey(profileTitle, profileType)` produced a non-empty id (exact alias hit, #1198 longest-boundary-prefix hit, or editor-type-default hit), and `false` otherwise.

When `profileKbResolved == false`, `analyzeFlowVsGoal` SHALL skip Arm 1 (the flow-vs-goal averaging block over flow-mode phases) entirely. Specifically: the flow-mode-range builder SHALL NOT populate `flowModeRanges`, the sample-averaging loop SHALL NOT run, and `GrindCheck::delta`, `GrindCheck::sampleCount`, and the Arm 1 contribution to `GrindCheck::hasData` SHALL remain at their default-zero / `false` values. The function SHALL NOT set `GrindCheck::skipped = true` — that field remains reserved for the existing non-espresso-beverage and `grind_check_skip` analysis-flag paths whose semantics differ (full grind detector suppression, distinct `grindCoverage = "skipped"` projection).

When `profileKbResolved == false`, Arm 2 (the choked-puck flow-arm and yield-shortfall yield-arm over pressure-mode phases) SHALL continue to run unchanged. The two arms read sustained-pressurized mean flow and `finalWeightG / targetWeightG` — physics-level signals that do not depend on profile shape and remain meaningful on profiles with no KB context.

When `profileKbResolved == true`, both arms SHALL run exactly as they do today. This change SHALL NOT alter behaviour for any KB-resolvable profile (exact alias, #1198 prefix match, or editor-type default).

The existing `grindCoverage` projection rules SHALL apply unchanged. With Arm 1 skipped and Arm 2's outcome driving `GrindCheck::hasData`:

- Arm 2 produced data (its `flowSamples >= 5` AND (`pressurizedDuration >= 15 s` OR yield-shortfall arm fired)) → `grindCoverage = "verified"`.
- Arm 2 had no data → `grindCoverage = "notAnalyzable"` (for an espresso shot with a non-degenerate pour window).
- Pour-truncated cascade active → `grindCoverage` omitted, as today.

The existing `[observation]` summary line "Could not analyze grind on this profile shape — …" SHALL fire on the resulting `"notAnalyzable"` coverage value via the existing emission path. No new prose, no new badge, no new verdict text is introduced by this change — the existing notAnalyzable infrastructure already covers the user-visible surface. The existing `verdictCategory = "cleanGrindNotAnalyzable"` SHALL apply when the cascade reaches the terminal "Otherwise" branch with this coverage.

The four quality-badge boolean projections in `src/history/shotbadgeprojection.h` SHALL NOT change. `grindIssueDetected` still requires `grindHasData && (grindChokedPuck || grindYieldOvershoot || |grindFlowDeltaMlPerSec| > FLOW_DEVIATION_THRESHOLD)`. With Arm 1 skipped and Arm 2 silent, `grindHasData` is `false` and `grindIssueDetected` projects to `false` — the false-positive surface this change targets.

Detectors other than grind SHALL be unaffected by `profileKbResolved`. `pourTruncated` (peak-pressure floor), `skipFirstFrame` (phase-marker-based), and channeling (with its existing `channeling_expected` flag and `shouldSkipChannelingCheck` heuristics) SHALL run identically on KB-resolved and KB-unresolved profiles.

The `[good]` line text on `verifiedClean == true` SHALL branch on `GrindCheck.sampleCount`: a positive count (Arm 1 ran and confirmed) keeps the existing "Grind tracked goal during pour" wording; a zero count with `verifiedClean == true` (Arm 2 alone verified a sustained pressurized pour) emits "Puck sustained healthy pressure during pour" instead. The `type` (`"good"`) and `kind` (`"grind_clean"`) are unchanged across the two branches.

#### Scenario: Unresolved profile with a clean Arm 2 result projects as verified-clean

- **GIVEN** an espresso shot on a profile whose title and editor type both fail to resolve via `ShotSummarizer::matchProfileKey` (`profileKbResolved == false`)
- **AND** the shot's pressure-mode phases produce a healthy pressurized pour (≥ 5 flow samples at ≥ 4 bar, ≥ 15 s pressurized duration, mean pressurized flow ≥ 0.5 mL/s)
- **AND** `finalWeightG / targetWeightG >= 0.70`
- **WHEN** `analyzeShot` runs with `profileKbResolved = false`
- **THEN** Arm 1 SHALL be skipped (`GrindCheck.sampleCount == 0` AND `GrindCheck.delta == 0`)
- **AND** Arm 2 SHALL run and set `GrindCheck.hasData = true` AND `GrindCheck.verifiedClean = true`
- **AND** `DetectorResults.grindCoverage` SHALL equal `"verified"`
- **AND** `grindIssueDetected` SHALL be `false`
- **AND** `summaryLines` SHALL contain a `[good]` line whose text is "Puck sustained healthy pressure during pour" (because Arm 1 did not run — `sampleCount == 0` — so the existing "Grind tracked goal during pour" wording would falsely cite a measurement that wasn't taken)

#### Scenario: Unresolved profile with no Arm 2 data projects as not-analyzable

- **GIVEN** an espresso shot on a KB-unresolved profile whose pour window is non-degenerate
- **AND** the pour produces fewer than 5 samples at ≥ 4 bar (Arm 2 flow-arm gate fails on `flowSamples`)
- **AND** either `targetWeightG == 0` OR `finalWeightG / targetWeightG >= 0.70` (Arm 2 yield-arm also silent)
- **WHEN** `analyzeShot` runs with `profileKbResolved = false`
- **THEN** Arm 1 SHALL be skipped (no flow-vs-goal averaging)
- **AND** Arm 2 SHALL run but produce `hasData == false`
- **AND** `DetectorResults.grindCoverage` SHALL equal `"notAnalyzable"`
- **AND** `summaryLines` SHALL contain one `[observation]` entry whose text starts with "Could not analyze grind on this profile shape"
- **AND** `grindIssueDetected` SHALL be `false`
- **AND** the verdict cascade's terminal branch SHALL emit "Clean shot, but grind could not be evaluated for this profile shape." with `verdictCategory = "cleanGrindNotAnalyzable"` when no other detector fires

#### Scenario: Unresolved profile with a yield shortfall still fires the grind badge via Arm 2

- **GIVEN** an espresso shot on a KB-unresolved profile
- **AND** `flowSamples >= 5` AND `targetWeightG > 0` AND `finalWeightG > 0`
- **AND** `(finalWeightG / targetWeightG) < 0.70` (e.g. 23 g of a 36 g target)
- **WHEN** `analyzeShot` runs with `profileKbResolved = false`
- **THEN** Arm 1 SHALL be skipped
- **AND** Arm 2's yield-shortfall arm SHALL fire (`chokedPuck = true`, `hasData = true`)
- **AND** `DetectorResults.grindCoverage` SHALL equal `"verified"`
- **AND** `grindIssueDetected` SHALL be `true`
- **AND** the existing "Pour produced near-zero flow while pressure held — puck choked" warning SHALL be emitted unchanged

#### Scenario: Resolved profile runs Arm 1 exactly as before

- **GIVEN** an espresso shot on a profile whose title or editor type DOES resolve via `ShotSummarizer::matchProfileKey` (exact alias, #1198 prefix, or editor-type default)
- **AND** the shot data is otherwise unchanged from a pre-change run
- **WHEN** `analyzeShot` runs with `profileKbResolved = true`
- **THEN** Arm 1's flow-mode-range builder, stationarity gate, and averaging loop SHALL execute identically to the pre-change behaviour
- **AND** `GrindCheck.delta`, `GrindCheck.sampleCount`, `GrindCheck.hasData`, and `DetectorResults.grindCoverage` SHALL all carry the same values they would have on the pre-change build

#### Scenario: Profile-agnostic detectors run on unresolved profiles

- **GIVEN** an espresso shot on a KB-unresolved profile where the puck fails to build pressure (peak pressure < 2.5 bar) OR phase markers show frame 0 was never observed before a non-zero frame at phase.time < 2.0 s
- **WHEN** `analyzeShot` runs with `profileKbResolved = false`
- **THEN** `pourTruncated` and/or `skipFirstFrame` SHALL fire identically to the resolved-profile case
- **AND** the existing badge cascades (pourTruncated dominating channeling/grind; skipFirstFrame independent) SHALL apply unchanged

#### Scenario: Direct test callers default profileKbResolved to true

- **GIVEN** a unit test that calls `ShotAnalysis::analyzeShot(...)` or `ShotAnalysis::analyzeFlowVsGoal(...)` without supplying the new `profileKbResolved` parameter
- **WHEN** the call executes
- **THEN** the default value `profileKbResolved = true` SHALL apply
- **AND** Arm 1 SHALL run with its existing behaviour, preserving every pre-change test scenario without modification

### Requirement: A profile-aware expert-band check SHALL run within the single analyzeShot cascade

`ShotAnalysis::analyzeShot` SHALL run, as part of the single cascade (no second pass, no separate orchestrator), a profile-aware check that compares the shot against a citation-bound per-profile expert-recommended operating band. Each band entry SHALL carry an **axis** (`pressure-peak` or `extraction-flow`), a **band**, a `[SRC:...]` provenance tag, and a confidence marker, all taken verbatim from a cited source (the grading in `capture-dialin-coaching-guidance` design D9/D10/D10b is authoritative; no value is invented). Entries SHALL be keyed by **canonical KB-section identity** (`ShotSummarizer::canonicalNameForKbId(profileKbId)`, the existing `ugsForKbId`/`allKbUgsEntries` dedup-by-name precedent), so multiple titles that share one KB section resolve to one entry and distinctly-sectioned profiles resolve to distinct entries. The observed value SHALL be read from values the cascade already computes — peak pressure for `pressure-peak`, extraction flow for `extraction-flow` — and `analyzeFlowVsGoal` (the profile's *commanded*-flow check) SHALL be left unmodified.

#### Scenario: A profile with a cited pressure band exposes the check on the pressure axis

- **WHEN** `analyzeShot` resolves a profile whose cited entry is `pressure-peak` 6–9 bar (e.g. D-Flow / Q, `[SRC:profile-notes]`)
- **THEN** the check SHALL compare the shot's peak pressure to 6–9 bar
- **AND** `Damian's Q` (which shares the `## D-Flow Q variant` KB section) SHALL resolve to the same single entry by canonical-section identity, with no duplicate row
- **AND** `D-Flow / La Pavoni` (its own `## D-Flow La Pavoni variant` section, post-#1175) SHALL resolve to its own distinct entry, and `D-Flow / default` (`## D-Flow`, no cited band) SHALL resolve to no entry

#### Scenario: A profile with a cited flow band exposes the check on the flow axis

- **WHEN** `analyzeShot` resolves a profile whose cited entry is `extraction-flow` (e.g. Rao Allongé "reach ~4.5 ml/s", `[SRC:light-video]`)
- **THEN** the check SHALL compare the shot's extraction flow to that band
- **AND** the existing `analyzeFlowVsGoal` commanded-flow result SHALL be unchanged and SHALL be able to emit independently of this check

#### Scenario: A profile with no cited band is a strict no-op

- **WHEN** `analyzeShot` resolves a profile with no cited band entry
- **THEN** no expert-band line SHALL be produced, no band or axis SHALL be fabricated, and the entire `AnalysisResult` (lines, detectors, badges) SHALL be byte-identical to the pre-change behavior for that shot

### Requirement: An out-of-band shot SHALL emit one soft, observational, taste-deferring summary line

When the observed value on the entry's axis is outside the cited band by the configured margin AND the hard AND-gate passes, `analyzeShot` SHALL append exactly one `summaryLines` entry of `type` **`observation`** (lowest authority) that names the observed value and the cited band and defers to taste. The line SHALL NOT state or imply a grind direction (band-vs-actual is a confounded signal; a directional verdict here is the #1155 failure). The hard AND-gate SHALL suppress the line when the cascade already fired pour-truncated or channeling; anything ambiguous SHALL be silent. (Bean freshness is out of scope for the deterministic emitter — `analyzeShot` has no freshness input; freshness suppression remains the advisor-prose layer's responsibility, unchanged. D8.) This guidance SHALL reach the in-app Shot Summary and (incidentally, identically) the AI advisor via the existing `summaryLines` path; no separate copy is authored and the advisor is not required.

When the line fires AND no higher-severity verdict applies, `analyzeShot` SHALL set `verdictCategory` to the dedicated value **`expertBandDeviation`** — a value distinct from `clean` and from every fault category. This branch SHALL be ordered in the verdict cascade **below** every pour-truncated / skip-first-frame / yield-overshoot / choked-puck / `hasWarning` / `hasCaution` verdict (a real fault always dominates; the band line then remains only a corroborating summary line) and **above** `cleanGrindNotAnalyzable` and `clean` (a band-only, otherwise-clean shot resolves to `expertBandDeviation`). The accompanying verdict-line text SHALL be non-directional and taste-deferring (e.g. "Ran outside this profile's expert-recommended band — judge by taste"). The band line's own `summaryLines` `type` SHALL remain `observation`; it SHALL NOT be raised to `caution`/`warning` to achieve the verdict change.

#### Scenario: Band-only on an otherwise-clean shot → expertBandDeviation, observation line, tint on

- **WHEN** the only finding is an out-of-band value (no pour-truncated/channeling/grind fault, gate clear)
- **THEN** the appended `summaryLines` entry SHALL have `type` `observation`
- **AND** `verdictCategory` SHALL be `expertBandDeviation` (not `clean`, not `cleanGrindNotAnalyzable`)
- **AND** the verdict-line text SHALL be non-directional and taste-deferring
- **AND** the four-boolean badge projection (`deriveBadgesFromAnalysis`) SHALL be byte-identical to the same shot computed without the band line

#### Scenario: A real fault dominates; the band finding does not change the verdict

- **WHEN** the shot is outside the cited band AND a higher-severity detector fired (pour-truncated, channeling, choked-puck, yield-overshoot, or a `hasWarning`/`hasCaution` verdict)
- **THEN** `verdictCategory` SHALL be the higher-severity fault's category, NOT `expertBandDeviation`
- **AND** the band line SHALL still appear as a corroborating `observation` `summaryLines` entry

#### Scenario: Outside the cited band, gate clear → one taste-deferring line

- **WHEN** the observed value on the cited axis is outside the band by the margin AND pour-truncated/channeling did not fire
- **THEN** exactly one `summaryLines` entry SHALL be appended, naming the observed value and the cited band and deferring to taste
- **AND** it SHALL NOT assert or imply "grind coarser/finer"

#### Scenario: Gate blocks the line

- **WHEN** the observed value is outside the band BUT the cascade already fired pour-truncated or channeling
- **THEN** no expert-band line SHALL be produced

#### Scenario: Inside the band → silent

- **WHEN** the observed value on the cited axis is within the band
- **THEN** no expert-band line SHALL be produced

### Requirement: The firmware limiter SHALL be a corroborating clause only, never the band

On the `pressure-peak` axis, when an out-of-band shot also pegged the machine's pressure limiter, the line MAY append a corroborating clause noting the limiter hit. The limiter value SHALL NOT be used as the band. The line SHALL be able to fire with no limiter hit, and a limiter touch with the peak still inside the cited band SHALL NOT fire the line.

#### Scenario: Out of band and limiter pegged → line with corroborating clause

- **WHEN** the peak is outside the cited pressure band AND the shot pegged the machine pressure limiter
- **THEN** the line SHALL fire AND MAY append the corroborating limiter clause

#### Scenario: Out of band, no limiter → line without clause

- **WHEN** the peak is outside the cited pressure band AND no limiter hit occurred
- **THEN** the line SHALL fire without any limiter clause

#### Scenario: Limiter pegged but inside the band → no line

- **WHEN** the shot pegged the machine pressure limiter BUT the peak is within the cited band
- **THEN** no expert-band line SHALL be produced

### Requirement: The expert-band signal SHALL NOT influence any badge

The expert-band check SHALL contribute only to `summaryLines` and the `verdictCategory` value `expertBandDeviation`. It SHALL NOT set, clear, or otherwise influence the grind badge or any other badge, and SHALL NOT alter `decenza::deriveBadgesFromAnalysis`. `verdictCategory` is **not** one of the four badge booleans, so introducing the `expertBandDeviation` value SHALL leave the four-boolean badge projection byte-identical; that projection SHALL remain driven solely by the existing intrinsic detectors. No confidence/quality/trust score SHALL be synthesized, and nothing SHALL be written to the shot record, any database column, or visualizer.coffee.

#### Scenario: Four-badge projection is byte-identical with and without the band line

- **WHEN** the same shot is analyzed and the expert-band line is appended
- **THEN** `decenza::deriveBadgesFromAnalysis` SHALL produce the same four booleans it would produce if the band line were absent
- **AND** no new column, record field, synthesized score, or Visualizer payload field SHALL exist; the only trace SHALL be the recomputed `summaryLines` entry

#### Scenario: A real intrinsic fault still drives the grind badge independently

- **WHEN** a shot is a true gusher/choke (an intrinsic `detectGrindIssue`/`GrindCheck` fault) AND is also outside the cited band
- **THEN** the grind badge SHALL be set by the intrinsic detector exactly as today
- **AND** the expert-band line SHALL render as additional corroborating prose without being the cause of the badge state

### Requirement: The band line and tint SHALL be recomputed every open from current code and table, never frozen at save

The expert-band check and the resulting `verdictCategory`/tint SHALL be produced by `analyzeShot` on every open (save / recompute-on-load / detail-load), inheriting the existing badge recompute-on-load contract. The **only** band-relevant value persisted on the shot SHALL be profile identity (already persisted); the citation-graded band table, the firing margin, and the gate logic SHALL be resolved fresh from shipped code/data at every compute and SHALL NOT be snapshotted onto the shot record. Any serialized `verdictCategory`/line SHALL be a non-authoritative cache that the recompute refreshes; display SHALL bind to the recomputed value. `analyzeShot` SHALL still be invoked exactly once on the canonical detail-load path.

#### Scenario: Same shot, same table → same line on save / load / detail

- **WHEN** the same shot is analyzed on the save path, the recompute-on-load path, and the detail-load path with the band table unchanged
- **THEN** the same `summaryLines` entry SHALL appear in the same position on all three
- **AND** `analyzeShot` SHALL be invoked exactly once on the canonical detail-load path

#### Scenario: Improving the table retroactively improves historical shots

- **WHEN** a shot is saved under one band table, the table is later corrected/expanded or the firing margin retuned, and the same historical shot is then re-opened
- **THEN** the re-opened shot SHALL reflect the **new** table/margin (a previously-absent line may now appear, a previously-present line may now be absent or changed, and the tint SHALL track the recomputed `verdictCategory`)
- **AND** no value from the original save SHALL shadow or override the recomputed result

#### Scenario: Tint binds to the recomputed verdict, not a stale cache

- **WHEN** a historical shot whose serialized `verdictCategory` differs from what current code would compute is opened
- **THEN** the tint SHALL reflect the freshly recomputed `verdictCategory`, not the stale serialized value

### Requirement: The Shot Summary entry affordance SHALL signal whether the analysis has anything worth reading

The `QualityBadges` affordance that emits `summaryRequested()` SHALL present a single calm tint derived purely from the already-computed `DetectorResults::verdictCategory`: when `verdictCategory` is exactly `clean` the affordance SHALL retain its current untinted appearance; for **any** other `verdictCategory` value it SHALL show the tint, indicating the summary is worth opening. The tint SHALL NOT be severity-graded into an error/alarm state, SHALL introduce no new threshold/score/classification, and SHALL NOT cause anything to be persisted or exported beyond the already-serialized `verdictCategory`. The tint SHALL NOT alter `analyzeShot`, the four-boolean badge projection, or the dialog contents.

#### Scenario: Clean verdict → untinted affordance

- **WHEN** the resolved shot's `verdictCategory` is exactly `clean`
- **THEN** the affordance SHALL retain its current untinted appearance and SHALL NOT show the tint

#### Scenario: Any non-clean verdict → calm "worth opening" tint, not an alarm

- **WHEN** `verdictCategory` is any value other than `clean` (including a deliberately-pulled experimental shot that produced `minorIssues*`)
- **THEN** the affordance SHALL show the single calm tint
- **AND** the tint SHALL NOT use error/alarm styling or a severity-graded scale

#### Scenario: The cue adds no judgment and no persistence

- **WHEN** the cue is rendered for a shot
- **THEN** it SHALL be a pure function of the existing `verdictCategory` with no new threshold/score
- **AND** no new column, record field, synthesized score, or Visualizer payload SHALL be introduced by the cue
- **AND** the four-boolean badge projection and the dialog contents SHALL be unchanged by the cue

#### Scenario: The expert-band line reaches the cue through the existing verdict

- **WHEN** the expert-band check appends its `summaryLines` entry and the resulting `verdictCategory` is non-clean
- **THEN** the affordance SHALL show the accent via the same `verdictCategory` path, with no separate coupling between the band check and the cue

### Requirement: Skip-first-frame guard suppresses only on confirmed exit reasons

`detectSkipFirstFrame` SHALL suppress the "First step skipped" detection only when the first non-zero frame's marker `transitionReason` is exactly a confirmed `pressure`, `flow`, or `weight` (case-insensitive). Unconfirmed reasons (`pressure_unconfirmed`, `flow_unconfirmed`), `time`, and empty values SHALL fall through to the duration-based checks, so a genuinely skipped or too-short first frame still flags.

#### Scenario: Confirmed sensor exit suppresses the badge

- **WHEN** the first non-zero frame's marker records `transitionReason = "pressure"` and the first frame ran shorter than the duration cutoff
- **THEN** `detectSkipFirstFrame` SHALL return false

#### Scenario: Unconfirmed sensor exit does not suppress the badge

- **WHEN** the first non-zero frame's marker records `transitionReason = "pressure_unconfirmed"` and the first frame ran shorter than the duration cutoff
- **THEN** `detectSkipFirstFrame` SHALL flag according to the duration-based checks (unchanged by the unconfirmed hint)

### Requirement: Grind limiter-tail trim fires on confirmed and unconfirmed pressure exits

`analyzeFlowVsGoal` SHALL trim the trailing limiter-tail window (`GRIND_LIMITER_TAIL_SKIP_SEC`) from a flow-mode phase when the next marker's `transitionReason` is `pressure` OR `pressure_unconfirmed` (case-insensitive), subject to the existing minimum post-trim window guard. Flow-flavored reasons (`flow`, `flow_unconfirmed`) SHALL NOT trigger the trim.

#### Scenario: Unconfirmed pressure exit trims the tail

- **WHEN** a flow-mode phase is followed by a marker with `transitionReason = "pressure_unconfirmed"` and the window is long enough to satisfy the post-trim minimum
- **THEN** the trailing `GRIND_LIMITER_TAIL_SKIP_SEC` SHALL be excluded from the flow-vs-goal averaging window

#### Scenario: Time exit leaves the window untrimmed

- **WHEN** a flow-mode phase is followed by a marker with `transitionReason = "time"`
- **THEN** the full window SHALL be used for flow-vs-goal averaging

