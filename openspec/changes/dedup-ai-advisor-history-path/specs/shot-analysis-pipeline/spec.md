# shot-analysis-pipeline

## ADDED Requirements

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
