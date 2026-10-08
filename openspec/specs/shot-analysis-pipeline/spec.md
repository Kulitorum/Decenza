# shot-analysis-pipeline Specification

## Purpose
The single source of truth for post-shot analysis: `ShotAnalysis::analyzeShot` runs exactly once per shot across the save, recompute-on-load, and detail-load paths, producing the `DetectorResults` that the Shot Summary dialog's prose (`summaryLines`), the AI advisor's historical-shot summarization, the four quality-badge booleans, and the profile-aware expert-band deviation check all derive from via documented, single-place projections. Covers the caching/dedup contracts that keep those consumers from re-running the cascade, and the detector-specific gating (grind coverage, skip-first-frame, channeling severity) that feeds it.
## Requirements
### Requirement: Shot Summary dialog SHALL render `shotData.summaryLines` with an empty fallback

`ShotAnalysisDialog.qml` SHALL bind `analysisLines` to `shotData.summaryLines` directly, falling back to an empty list when absent. The prose is computed once, in `ShotHistoryStorage::convertShotRecord`. The dialog SHALL NOT invoke any C++ helper that recomputes prose from a serialized shot.

#### Scenario: Modern shotData renders directly

- **GIVEN** a shotData QVariantMap produced by `convertShotRecord`, with `summaryLines` populated
- **WHEN** the user opens the Shot Summary dialog
- **THEN** the dialog SHALL render the prose lines from `shotData.summaryLines` directly
- **AND** the dialog SHALL NOT invoke any Q_INVOKABLE method on `ShotHistoryStorage`

#### Scenario: shotData without summaryLines renders empty

- **GIVEN** a shotData QVariantMap whose `summaryLines` field is absent or not a non-empty list
- **WHEN** the user opens the Shot Summary dialog
- **THEN** the dialog SHALL render its "Shot Summary" header with no observation lines
- **AND** the dialog SHALL NOT crash, fall back to recomputation, or display unrelated content

### Requirement: AI advisor's historical-shot path SHALL reuse pre-computed `summaryLines` when present

When `ShotSummarizer::summarizeFromHistory(shotData)` receives a non-empty `summaryLines` list, it SHALL use that list as `ShotSummary::summaryLines` without calling `ShotAnalysis::generateSummary(...)`, and SHALL derive `pourTruncatedDetected` from `detectorResults.pourTruncated` when present. Otherwise it SHALL run the inline detector path. The live `summarize(ShotDataModel*)` path is out of scope.

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

`ShotHistoryStorage::saveShot` and `loadShotRecordStatic` SHALL each call `ShotAnalysis::analyzeShot(...)` exactly once per shot and derive the four badge booleans from its `DetectorResults` via `decenza::deriveBadgesFromAnalysis`. Neither SHALL keep per-detector calls or cascade gates; the cascade SHALL live only in `analyzeShot`. The lazy-persist write-back in `loadShotRecordStatic` SHALL keep working.

#### Scenario: Clean shot projects to all-false badge columns

- **GIVEN** a shot whose `analyzeShot` returns `DetectorResults` with `verdictCategory = "clean"` and all detector gates clear
- **WHEN** save-time or load-time badge derivation runs
- **THEN** all four boolean badge columns SHALL be `false`

#### Scenario: Pour-truncated cascade dominates the projection

- **GIVEN** a shot whose `analyzeShot` returns `pourTruncated = true` (and consequently `channelingChecked = false`, `grindChecked = false`)
- **WHEN** save-time or load-time badge derivation runs
- **THEN** `pourTruncatedDetected` SHALL be `true`
- **AND** `channelingDetected`, `grindIssueDetected` SHALL each be `false`
- **AND** `skipFirstFrameDetected` SHALL reflect `d.skipFirstFrame` independently (skip-first-frame is NOT suppressed by the cascade, matching PR Kulitorum/Decenza#922's invariant)

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

### Requirement: Badge columns SHALL follow one documented DetectorResults mapping

`deriveBadgesFromAnalysis` (`src/history/shotbadgeprojection.h`) SHALL map these badge columns as below. `channelingDetected` SHALL be set only for sustained channeling.

| Badge column | `DetectorResults` projection |
|---|---|
| `pourTruncatedDetected` | `d.pourTruncated` |
| `channelingDetected` | `d.channelingSeverity == "sustained"` |
| `skipFirstFrameDetected` | `d.skipFirstFrame` |

#### Scenario: Transient channeling is not a channeling badge

- **GIVEN** a shot whose `analyzeShot` returns `channelingSeverity = "transient"`
- **WHEN** badge derivation runs
- **THEN** `channelingDetected` SHALL be `false`
- **AND** the Shot Summary dialog SHALL still show the transient caution line

### Requirement: The grind badge SHALL project grind fields with the flow threshold from ShotAnalysis

`grindIssueDetected` SHALL be `d.grindHasData && (d.grindChokedPuck || d.grindYieldOvershoot || std::abs(d.grindFlowDeltaMlPerSec) > FLOW_DEVIATION_THRESHOLD)`. `FLOW_DEVIATION_THRESHOLD` SHALL be read from `ShotAnalysis::FLOW_DEVIATION_THRESHOLD`, never inlined.

#### Scenario: Flow delta within tolerance does not fire the grind badge

- **GIVEN** a shot with `grindHasData = true`, neither choke arm set, and `|grindFlowDeltaMlPerSec| <= FLOW_DEVIATION_THRESHOLD`
- **WHEN** badge derivation runs
- **THEN** `grindIssueDetected` SHALL be `false`

### Requirement: Save and load SHALL pass the profile frame count to skip-first-frame detection

`ShotAnalysis::analyzeShot` SHALL accept an optional `expectedFrameCount` (default `-1`, unknown) and forward it to `detectSkipFirstFrame`. Save and load SHALL pass the profile's actual frame count.

#### Scenario: Unknown frame count keeps legacy behaviour

- **GIVEN** a caller that does not pass `expectedFrameCount`
- **WHEN** `analyzeShot` runs
- **THEN** `expectedFrameCount` SHALL default to `-1`
- **AND** skip-first-frame detection SHALL run as it did before the parameter existed

### Requirement: Shot detail loads SHALL run `analyzeShot` exactly once

`loadShotRecordStatic` SHALL cache the `AnalysisResult` it computes on the returned `ShotRecord`, and `convertShotRecord` SHALL read that cache rather than run `analyzeShot` again. When no cache is present (exporter, tests), `convertShotRecord` SHALL run `analyzeShot` inline. The cache SHALL be cleared if any input curve is mutated after load.

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

`decenza::prepareAnalysisInputs` (`src/history/shotanalysisinputs.h`) SHALL be the only place that builds `analyzeShot`'s `analysisFlags`, `firstFrameSeconds` and `frameCount` from a `ShotRecord`- or `ShotSaveData`-shaped source. `saveShot`, `loadShotRecordStatic` and `convertShotRecord` SHALL each call it once and SHALL NOT keep inline `getAnalysisFlags` or `profileFrameInfoFromJson` calls.

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

#### Scenario: A new analyzeShot input is added in one place

- **GIVEN** `analyzeShot` gains a new required input
- **WHEN** the input is added
- **THEN** it SHALL be added by extending `AnalysisInputs` and `prepareAnalysisInputs` only
- **AND** the three storage call sites SHALL pick it up without edits

### Requirement: ShotHistoryStorage's implementation SHALL be split across multiple translation units by concern

`ShotHistoryStorage`'s implementation SHALL be split across at least three translation units: `shothistorystorage.cpp` (DB lifecycle, save, load), `shothistorystorage_queries.cpp` (read-only queries and the distinct-value cache) and `shothistorystorage_serialize.cpp` (`convertShotRecord`). The class declaration SHALL remain in `shothistorystorage.h`, with no public API change.

#### Scenario: Splitting does not change observable behavior

- **GIVEN** the codebase before the split, with all tests passing
- **WHEN** the split is applied
- **THEN** every test in `tst_dbmigration`, `tst_shotanalysis`, `tst_shotrecord_cache`, `tst_shotsummarizer`, and other suites that exercise `ShotHistoryStorage` SHALL continue to pass without modification

#### Scenario: Header surface is unchanged

- **GIVEN** the post-split codebase
- **WHEN** an external caller `#include "history/shothistorystorage.h"`
- **THEN** the same set of public methods SHALL be visible, with the same signatures, as before the split

### Requirement: Per-marker `PhaseSummary` construction SHALL live in exactly one helper

`ShotSummarizer::buildPhaseSummariesForRange` SHALL be the only loop that builds `PhaseSummary` entries, called by both `summarize()` and `summarizeFromHistory()`. It SHALL skip degenerate phases (`endTime <= startTime`), while the caller still keeps their `HistoryPhaseMarker`s for `analyzeShot`. The four curve-helper functions remain the math source of truth.

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

`ShotAnalysis::DetectorResults` SHALL expose `pourStartSec` and `pourEndSec`, populated from the same locals `analyzeShot` uses for its cascade gates. `ShotSummarizer::computePourWindow` SHALL be deleted, and consumers SHALL read these fields. `convertShotRecord` SHALL serialize both onto the MCP `detectorResults` object.

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

`ShotSummarizer::summarize` and `summarizeFromHistory` SHALL each delegate their detector-orchestration block to the private static helper `runShotAnalysisAndPopulate`, which takes typed inputs plus an optional cached `AnalysisResult` and populates `summaryLines` and `pourTruncatedDetected`. Each caller keeps only its input adapter and SHALL NOT duplicate the `analyzeShot` call or result unpacking.

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

`ShotSummarizer::summarize` (live path) SHALL have at least two direct tests in `tst_shotsummarizer.cpp`: a puck-failure shot that sets `pourTruncatedDetected` with no channeling lines, and a healthy shot that yields observation lines, a verdict line and `pourTruncatedDetected = false`. They SHALL use a `MockShotDataModel` that does not depend on the full `ShotDataModel` runtime.

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

`ShotAnalysis::analyzeFlowVsGoal` SHALL gate the choked-puck arms as follows. The flow-choked arm SHALL fire only when `flowSamples >= 5`, `pressurizedDuration >= 15.0 s` and mean pressurized flow is below 0.5 mL/s. The yield-shortfall arm SHALL fire when `flowSamples >= 5`, both weights are positive and `finalWeightG / targetWeightG < 0.70`, and SHALL NOT require the 15 s duration.

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

### Requirement: Grind arm gates SHALL set hasData and verifiedClean

`GrindCheck::hasData` SHALL be true when `flowSamples >= 5` and either `pressurizedDuration >= 15.0 s` or the yield-shortfall arm fired. `GrindCheck::verifiedClean` SHALL be true only when `flowSamples >= 5`, `pressurizedDuration >= 15.0 s`, neither choke arm fired, `|delta| <= FLOW_DEVIATION_THRESHOLD` and `yieldOvershoot` is false.

#### Scenario: Verified-clean requires the strong flow-arm gates

- **GIVEN** an espresso shot with `flowSamples >= 5` but `pressurizedDuration < 15 s` and no yield shortfall
- **WHEN** `analyzeShot` runs
- **THEN** `GrindCheck::hasData` SHALL be `false`
- **AND** `verifiedClean` SHALL be `false`

### Requirement: Grind coverage SHALL classify data availability, not health

`DetectorResults::grindCoverage` SHALL be `verified` whenever `GrindCheck.hasData` is true, healthy or not. It SHALL be `skipped` when `GrindCheck.skipped` is true. It SHALL be `notAnalyzable` when `hasData` is false, `skipped` is false, the espresso pour window is non-degenerate (`pourEndSec > pourStartSec`) and the beverage is not on the non-espresso skip list. It SHALL be omitted when the pourTruncated cascade is active.

#### Scenario: Skipped grind reports skipped coverage

- **GIVEN** a non-espresso beverage or a profile carrying the `grind_check_skip` analysis flag
- **WHEN** `analyzeShot` runs
- **THEN** `grindCoverage` SHALL equal `"skipped"`

### Requirement: Grind badge projection SHALL be unchanged by coverage

`grindIssueDetected` SHALL still require `grindHasData && (grindChokedPuck || grindYieldOvershoot || |grindFlowDeltaMlPerSec| > FLOW_DEVIATION_THRESHOLD)`. A verified-clean result SHALL project `grindIssueDetected = false`. A yield-shortfall-only result SHALL project `grindIssueDetected = true`, because `chokedPuck` is set when either arm fires.

#### Scenario: Verified-clean grind does not set the badge

- **GIVEN** a grind result with `verifiedClean = true`
- **WHEN** badge derivation runs
- **THEN** `grindIssueDetected` SHALL be `false`

### Requirement: Verdict cascade SHALL acknowledge a not-analyzable grind result

When the verdict cascade reaches its terminal "Otherwise" branch with `grindCoverage == "notAnalyzable"`, it SHALL emit "Verdict: Clean shot, but grind could not be evaluated for this profile shape." and set `verdictCategory` to `cleanGrindNotAnalyzable`. With `verified`, absent or skipped coverage, it SHALL keep "Verdict: Clean shot. Puck held well." and `clean`.

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

`analyzeShot` and `analyzeFlowVsGoal` SHALL accept `bool profileKbResolved` (default `true`), which is true when `ShotSummarizer::matchProfileKey` returns a non-empty id. When it is false, `analyzeFlowVsGoal` SHALL skip Arm 1 (flow-vs-goal averaging over flow-mode phases) entirely. It SHALL NOT set `GrindCheck::skipped`. Arm 2 (choked-puck arms over pressure-mode phases) SHALL run unchanged.

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

### Requirement: Resolved profiles SHALL run Arm 1 unchanged

When `profileKbResolved` is true, both grind arms SHALL run exactly as before. Detectors other than grind (pourTruncated, skipFirstFrame, channeling) SHALL run identically whether or not the profile resolved.

#### Scenario: Profile-agnostic detectors run on unresolved profiles

- **GIVEN** an espresso shot on a KB-unresolved profile where peak pressure is below 2.5 bar
- **WHEN** `analyzeShot` runs with `profileKbResolved = false`
- **THEN** `pourTruncated` SHALL fire identically to the resolved-profile case

### Requirement: Verified-clean grind line SHALL reflect whether Arm 1 ran

When `verifiedClean` is true, the `[good]` line SHALL read "Grind tracked goal during pour" if `GrindCheck.sampleCount > 0`, and "Puck sustained healthy pressure during pour" otherwise. Its `type` and `kind` SHALL be unchanged.

#### Scenario: Arm 1 ran keeps the tracked-goal wording

- **GIVEN** `verifiedClean == true` and `GrindCheck.sampleCount > 0`
- **WHEN** `analyzeShot` runs
- **THEN** the `[good]` line SHALL read "Grind tracked goal during pour"

### Requirement: A profile-aware expert-band check SHALL run within the single analyzeShot cascade

`analyzeShot` SHALL run a profile-aware band check in its single cascade against a cited per-profile expert band. Each entry SHALL carry an axis, a band, a `[SRC:...]` tag and a confidence marker, copied verbatim from the citation; no value SHALL be invented. Entries SHALL be keyed by `ShotSummarizer::canonicalNameForKbId(profileKbId)`. `analyzeFlowVsGoal` SHALL be unmodified.

#### Scenario: A profile with a cited pressure band exposes the check on the pressure axis

- **WHEN** `analyzeShot` resolves a profile whose cited entry is `pressure-peak` 6–9 bar (e.g. D-Flow / Q, `[SRC:profile-notes]`)
- **THEN** the check SHALL compare the shot's peak pressure to 6–9 bar
- **AND** `Damian's Q` (which shares the `## D-Flow Q variant` KB section) SHALL resolve to the same single entry by canonical-section identity, with no duplicate row
- **AND** `D-Flow / La Pavoni` (its own `## D-Flow La Pavoni variant` section, since PR Kulitorum/Decenza#1175) SHALL resolve to its own distinct entry, and `D-Flow / default` (`## D-Flow`, no cited band) SHALL resolve to no entry

#### Scenario: A profile with a cited flow band exposes the check on the flow axis

- **WHEN** `analyzeShot` resolves a profile whose cited entry is `extraction-flow` (e.g. Rao Allongé "reach ~4.5 ml/s", `[SRC:light-video]`)
- **THEN** the check SHALL compare the shot's extraction flow to that band
- **AND** the existing `analyzeFlowVsGoal` commanded-flow result SHALL be unchanged and SHALL be able to emit independently of this check

#### Scenario: A profile with no cited band is a strict no-op

- **WHEN** `analyzeShot` resolves a profile with no cited band entry
- **THEN** no expert-band line SHALL be produced, no band or axis SHALL be fabricated, and the entire `AnalysisResult` (lines, detectors, badges) SHALL be byte-identical to the pre-change behavior for that shot

#### Scenario: The entry axis selects the observed value

- **GIVEN** a profile whose cited entry is `pressure-peak`, or `extraction-flow`
- **WHEN** the band check runs
- **THEN** a `pressure-peak` entry SHALL compare peak pressure to its band
- **AND** an `extraction-flow` entry SHALL compare extraction flow to its band, using values the cascade already computes

### Requirement: An out-of-band shot SHALL emit one soft, observational, taste-deferring summary line

When the observed value is outside the cited band by the configured margin and the hard AND-gate passes, `analyzeShot` SHALL append exactly one `observation` summary line naming the observed value and the band, and deferring to taste. The line SHALL NOT state or imply a grind direction. The gate SHALL suppress the line when pour-truncated or channeling fired; ambiguous cases SHALL be silent.

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

### Requirement: An out-of-band shot with no fault SHALL resolve to expertBandDeviation

When the band line fires and no higher-severity verdict applies, `verdictCategory` SHALL be `expertBandDeviation`. It SHALL sit below every fault verdict and above `cleanGrindNotAnalyzable` and `clean`. Its verdict text SHALL be non-directional and taste-deferring. The line's type SHALL remain `observation`.

#### Scenario: Band line reaches the advisor through summaryLines

- **GIVEN** the expert-band line has been appended to `summaryLines`
- **WHEN** the AI advisor summarizes the shot
- **THEN** the line SHALL arrive via the existing `summaryLines` path, with no separate copy
- **AND** bean freshness SHALL NOT be an input to the band gate

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

The expert-band check SHALL contribute only to `summaryLines` and the `verdictCategory` value `expertBandDeviation`. It SHALL NOT alter any badge or `decenza::deriveBadgesFromAnalysis`, SHALL NOT synthesize a confidence or quality score, and SHALL NOT persist anything beyond the recomputed summary line.

#### Scenario: Four-badge projection is byte-identical with and without the band line

- **WHEN** the same shot is analyzed and the expert-band line is appended
- **THEN** `decenza::deriveBadgesFromAnalysis` SHALL produce the same four booleans it would produce if the band line were absent
- **AND** no new column, record field, synthesized score, or Visualizer payload field SHALL exist; the only trace SHALL be the recomputed `summaryLines` entry

#### Scenario: A real intrinsic fault still drives the grind badge independently

- **WHEN** a shot is a true gusher/choke (an intrinsic `detectGrindIssue`/`GrindCheck` fault) AND is also outside the cited band
- **THEN** the grind badge SHALL be set by the intrinsic detector exactly as today
- **AND** the expert-band line SHALL render as additional corroborating prose without being the cause of the badge state

### Requirement: The band line and tint SHALL be recomputed every open from current code and table, never frozen at save

`analyzeShot` SHALL recompute the band line and `verdictCategory` on every open. Only profile identity SHALL be persisted; the band table, margin and gate logic SHALL be resolved from shipped code at each compute. Any serialized `verdictCategory` or line SHALL be a non-authoritative cache, and display SHALL bind to the recomputed value.

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

The `QualityBadges` affordance that emits `summaryRequested()` SHALL show a single calm tint whenever `verdictCategory` is anything other than exactly `clean`, and SHALL stay untinted for `clean`. The tint SHALL NOT be severity-graded, SHALL NOT alter `analyzeShot`, the badge projection or the dialog, and SHALL NOT be persisted.

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

### Requirement: The Shot Summary affordance SHALL be reachable for every shot

The Shot Summary affordance SHALL be present on the post-shot review page and the shot detail page for every shot. Its visibility SHALL NOT depend on KB resolution or on whether any badge fired. Badge chips keep their own conditions: a flag chip appears only when its flag fired, and the clean-extraction chip only when no flag fired.

#### Scenario: Clean shot on an unresolved profile still offers the summary

- **GIVEN** a shot whose profile resolves to no KB entry and on which no quality-badge flag fired
- **WHEN** the post-shot review page or the shot detail page is shown
- **THEN** the Shot Summary affordance SHALL be present and SHALL open the dialog

#### Scenario: Badge chips keep their own conditions

- **GIVEN** any shot
- **WHEN** its badge row is shown
- **THEN** each flag chip SHALL appear only if its flag fired, and the clean-extraction chip SHALL appear
  only if no flag fired

#### Scenario: The affordance tint is unchanged

- **GIVEN** a shot whose recomputed verdict category is exactly `clean`
- **WHEN** the affordance is shown
- **THEN** it SHALL be untinted, per the existing affordance-tint requirement, which this requirement does
  not modify

