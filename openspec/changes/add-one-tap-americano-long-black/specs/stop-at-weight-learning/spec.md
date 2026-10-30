## ADDED Requirements

### Requirement: Hot water starting during settling ends it
When hot water starts while an espresso is still settling, the system SHALL end settling at that moment, before the hot water's tare can reach the reading. If a clean settling average has been captured, the system SHALL save it and treat the settle as complete, so SAW learning still runs. If none has been captured, the system SHALL save the last reading before the hot water, or the settle base if that reading is below it, and skip learning. The log line SHALL name hot water as the reason.

#### Scenario: Hot water after a settled reading
- **GIVEN** an espresso whose settling has captured a clean average
- **WHEN** hot water starts before settling completes
- **THEN** the saved final weight is that clean average
- **AND** SAW learning records the shot

#### Scenario: Hot water before the weight settles
- **GIVEN** an espresso whose settling has not captured a clean average
- **WHEN** hot water starts
- **THEN** the saved final weight is the last reading before the hot water, or the settle base if that reading is below it
- **AND** SAW learning skips the shot

#### Scenario: The hot water tare never reaches the shot
- **WHEN** hot water starts during settling and its tare drops the reading to zero
- **THEN** the shot's saved weight is unaffected by the tare
