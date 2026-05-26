## ADDED Requirements

### Requirement: Standalone shot block carries stoppedBy

The standalone shot JSON block emitted by `ShotSummarizer::buildShotBlock` (as part of the `shot` field on `buildUserPromptObject`'s output) SHALL include `stoppedBy` when the saved value is one of `"manual"`, `"weight"`, or `"volume"`. This mirrors the field's allowlist on dialing-context surfaces (`bestRecentShot`, `dialInSessions[].history`), giving the LLM a stop-reason anchor on every shot-analysis prompt rather than only on dial-in surfaces.

The allowlist intentionally omits `"profileEnd"` and the empty string. The system-prompt rubric already documents how the model should treat an absent field (profile-end vs DE1 hardware button — the BLE protocol cannot distinguish), and emitting `"profileEnd"` explicitly would conflict with the rubric's "ABSENT" branch.

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
