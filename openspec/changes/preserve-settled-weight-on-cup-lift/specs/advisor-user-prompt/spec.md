## ADDED Requirements

### Requirement: Standalone shot block carries stoppedBy

The standalone shot block from `ShotSummarizer::buildShotBlock` (the `shot` field of `buildUserPromptObject`) SHALL include `stoppedBy` when the saved value is `"manual"`, `"weight"` or `"volume"`, the same allowlist as the dialing-context surfaces. It SHALL NOT emit `"profileEnd"` or an empty value: the system-prompt rubric already defines what an absent field means.

#### Scenario: SAW-stopped shot emits stoppedBy: "weight"

- **GIVEN** a `ShotSummary` with `stoppedBy = "weight"` (SAW or QML weight-stop fallback)
- **WHEN** `buildShotBlock(summary)` runs
- **THEN** the returned JSON SHALL contain `stoppedBy: "weight"`

#### Scenario: Manually-stopped shot emits stoppedBy: "manual"

- **GIVEN** a `ShotSummary` with `stoppedBy = "manual"` (user tapped Stop on the QML page)
- **WHEN** `buildShotBlock(summary)` runs
- **THEN** the returned JSON SHALL contain `stoppedBy: "manual"`

#### Scenario: Volume-stopped shot emits stoppedBy: "volume"

- **GIVEN** a `ShotSummary` with `stoppedBy = "volume"` (SAV)
- **WHEN** `buildShotBlock(summary)` runs
- **THEN** the returned JSON SHALL contain `stoppedBy: "volume"`

#### Scenario: Profile-end shot omits the field

- **GIVEN** a `ShotSummary` with `stoppedBy = "profileEnd"` OR empty
- **WHEN** `buildShotBlock(summary)` runs
- **THEN** the returned JSON SHALL NOT contain a `stoppedBy` key
