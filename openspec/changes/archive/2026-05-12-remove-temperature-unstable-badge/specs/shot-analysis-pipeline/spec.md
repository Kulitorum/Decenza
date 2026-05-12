## MODIFIED Requirements

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

## REMOVED Requirements

### Requirement: Intentional temperature stepping does NOT fire the temp badge

**Reason**: The temperature-unstable badge is being removed entirely. The Shot Summary dialog and badge UI no longer carry a temp-stability concept, so suppression rules for it become moot. Audit found 12+ built-in profiles where actual-vs-goal temperature gap is by design (D-Flow family with `espresso_temperature_steps_enabled = 0`, Extractamundo / TurboBloom 16°C bloom drops, 80's Espresso / TurboTurbo / Classic Italian / etc. with 6–10°C designed swings). The `hasIntentionalTempStepping` curve-based suppression is fragile (collapses when the captured `temperatureGoal` series is sparse), and the underlying detector measures *deviation from goal* while being labeled *unstable* — the math doesn't match the label.

**Migration**: There is no replacement detector. External MCP consumers that read `detectorResults.temperature` lose the field; consumers that filter shots by `temperature_unstable` must drop that predicate. The diagnostic value the badge attempted to surface (cold portafilter crash, heater failure, boiler exhaustion) remains visible in the post-shot temperature chart. A future change MAY reintroduce a true *instability* detector (variance / mid-shot crash) but it is out of scope for this change.

This requirement was previously a scenario under "Save and load paths SHALL derive boolean quality badges from `DetectorResults` via a single documented projection." Its removal is reflected in the modified version of that requirement (the `temperatureUnstable` row is dropped from the projection table, and the `temperatureUnstable` clause is removed from the pour-truncated-cascade scenario).
