# shot-analysis-pipeline

## ADDED Requirements

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
