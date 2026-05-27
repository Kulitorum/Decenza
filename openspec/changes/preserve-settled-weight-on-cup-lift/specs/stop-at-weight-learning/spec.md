## ADDED Requirements

### Requirement: Final Weight Capture After Settling

The system SHALL persist `finalWeightG` as the cup's stable settled weight at shot end, even when the user lifts the cup before the settling timer declares stability. The `ShotTimingController` SHALL track the most recent clean rolling-window average — one observed while the existing stability gate (window full, `avgDrift < SETTLING_AVG_THRESHOLD`, `weight ≤ avg + SETTLING_ABOVE_AVG_MARGIN`, `avg ≥ m_weightAtStop − 0.5`) holds — and use it as the cup-removal fallback for `m_weight`.

#### Scenario: Clean settle uses the settled rolling-window average

- **WHEN** settling completes via `SETTLING_STABLE_MS` accumulation or the BLE-silence override timer
- **THEN** `finalWeightG` SHALL be the rolling-window average at the moment of completion (existing behavior, unchanged)

#### Scenario: Cup lifted mid-settle falls back to the last clean average

- **GIVEN** the stability gate above has held *continuously* for at least `SETTLING_CLEAN_CAPTURE_MS` (250 ms; approximately 3 consecutive samples at typical scale rates)
- **AND** the controller has captured that last clean average into `m_lastCleanSettlingAvg`
- **WHEN** the cup-removal detector fires during settling
- **THEN** `m_weight` SHALL be restored to that last clean average
- **AND** `finalWeightG` SHALL equal that value when persisted by `MainController::onShotEnded`

#### Scenario: Transient gate-fires during a noisy settle do not pollute the fallback

- **GIVEN** settling samples whose rolling-window average transiently satisfies the stability gate but breaks again within a single sample (the gate's `m_settlingAvgStableSince` clock resets before `SETTLING_CLEAN_CAPTURE_MS` accumulates)
- **WHEN** the cup-removal detector fires during settling
- **THEN** `m_lastCleanSettlingAvg` SHALL still be zero
- **AND** the fallback chain SHALL fall through to the `m_weightAtStop` floor scenario below

#### Scenario: Cup lifted before any clean average is observed

- **GIVEN** the cup is lifted before the rolling window has filled OR before any sample satisfied the stability gate
- **WHEN** the cup-removal detector fires AND `m_weightAtStop > 0` AND `m_weight < m_weightAtStop`
- **THEN** `m_weight` SHALL be raised to `m_weightAtStop` (post-stop drip can only add weight; an observed value below the SAW trigger weight is a measurement artifact)

#### Scenario: Cup-removal fallback state resets between shots

- **WHEN** a new shot starts (via `startShot()`) or a new settling cycle begins (via `startSettlingTimer()`)
- **THEN** the `m_lastCleanSettlingAvg` tracker SHALL be reset to 0 so a value from a prior shot cannot leak into the current shot's recorded weight
