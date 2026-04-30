# shot-analysis-pipeline

## ADDED Requirements

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
