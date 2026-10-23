## MODIFIED Requirements

### Requirement: Final Weight Capture After Settling

The system SHALL abandon settling when the reading stops being drip: the cup is lifted, or two consecutive readings are disturbed. A reading is disturbed when its short flow exceeds the settle flow reference (the higher of the flow at the stop and at the first settling sample) by more than 4 g/s, or, for an espresso-group profile only, when it is more than `maxPlausibleDripG(flowAtStop)` = `clamp(3 s × flow at the stop, 5 g, 12 g)` above the settle base, the higher of the stop weight and the weight when settling started. An abandoned settle SHALL skip learning and save the most recent clean settling average. Capturing it SHALL require a full rolling window and the stability gate to hold for `SETTLING_CLEAN_CAPTURE_MS`. If no clean average exists, the fallback SHALL save the settle base as specified below.

#### Scenario: Clean settle uses the settled rolling-window average

- **WHEN** settling completes via `SETTLING_STABLE_MS` accumulation or the BLE-silence override timer
- **THEN** `finalWeightG` SHALL be the rolling-window average at the moment of completion (existing behavior, unchanged)

#### Scenario: Cup lifted mid-settle falls back to the last clean average

- **GIVEN** the rolling window is full and its stability gate (`avgDrift < SETTLING_AVG_THRESHOLD`, `weight ≤ avg + SETTLING_ABOVE_AVG_MARGIN`, `avg ≥ m_weightAtStop − 0.5`) has held *continuously* for at least `SETTLING_CLEAN_CAPTURE_MS` (250 ms)
- **AND** the controller has captured that last clean average into `m_lastCleanSettlingAvg`
- **WHEN** the cup-removal detector fires during settling
- **THEN** `m_weight` SHALL be restored to that last clean average
- **AND** `finalWeightG` SHALL equal that value when persisted by `MainController::onShotEnded`

#### Scenario: A rise drip cannot produce falls back to the last clean average

- **GIVEN** a captured `m_lastCleanSettlingAvg`
- **WHEN** two consecutive settling samples exceed the settle base by more than `maxPlausibleDripG(m_flowRateAtStop)`
- **THEN** settling SHALL end on the second without learning
- **AND** `m_weight` SHALL be restored to that last clean average

#### Scenario: Weight rising faster than drip can run falls back to the last clean average

- **GIVEN** a captured `m_lastCleanSettlingAvg` and readings still inside the amount limit
- **WHEN** two consecutive settling samples have a short flow more than 4 g/s above the settle flow reference
- **THEN** settling SHALL end on the second without learning
- **AND** `m_weight` SHALL be restored to that last clean average

#### Scenario: A filter bed's drain is not cut short

- **GIVEN** a filter, pourover or tea profile
- **WHEN** the reading drains 22 g past the stop weight with a slowing flow
- **THEN** settling SHALL continue, since the amount test does not apply and the flow test does not fire

#### Scenario: A lone reading past the limit is dropped

- **WHEN** one settling sample exceeds the limit and the next does not
- **THEN** settling SHALL continue and the lone sample SHALL NOT become the current weight
- **AND** if settling would complete before the next sample arrives, the lone sample SHALL count as an over-rise

#### Scenario: A rejected packet cannot confirm itself through residual flow

- **GIVEN** one disturbed reading followed by a return to the last accepted weight's 0.2 g band
- **WHEN** the rejected reading still raises the worker's flow estimate
- **THEN** the recovered reading SHALL NOT confirm the disturbance
- **AND** flow-only candidates in that recovered band SHALL be ignored until normal flow or a fresh rise outside the band ends recovery
- **AND** the amount test SHALL remain active throughout recovery

#### Scenario: A fast stop's real drip is not an over-rise

- **GIVEN** a stop at 8.5 g/s
- **WHEN** the reading rises 7 g above the stop weight
- **THEN** settling SHALL continue, since the limit at that flow is the 12 g ceiling

#### Scenario: A late stop's extra pour is not an over-rise

- **GIVEN** SAW fired at 30 g and the weight was 38 g when settling started
- **WHEN** the reading rises to 42 g
- **THEN** settling SHALL continue, since drip is measured from 38 g

#### Scenario: Transient gate-fires during a noisy settle do not pollute the fallback

- **GIVEN** settling samples whose rolling-window average transiently satisfies the stability gate but breaks again within a single sample (the gate's `m_settlingAvgStableSince` clock resets before `SETTLING_CLEAN_CAPTURE_MS` accumulates)
- **WHEN** settling is abandoned
- **THEN** `m_lastCleanSettlingAvg` SHALL still be zero
- **AND** the fallback SHALL save the settle base as in "Cup lifted before any clean average is observed"

#### Scenario: Implausible clean average is rejected as scale fault

- **GIVEN** a reading frozen far above the stop weight (shot 825: ~75 g on a ~40 g target)
- **WHEN** it arrives during settling and repeats on the next sample
- **THEN** it SHALL abandon settling as an over-rise before it can enter the rolling window
- **AND** so no clean average more than `maxPlausibleDripG(flowAtStop)` above the settle base SHALL ever be captured or saved

#### Scenario: Cup lifted before any clean average is observed

- **GIVEN** no clean average was captured and the settle base is above zero
- **WHEN** settling is abandoned by an over-rise, OR by a cup lift with `m_weight` below the base
- **THEN** `m_weight` SHALL be set to the settle base (drip only adds weight, and the reading before an over-rise may be the disturbance starting)

#### Scenario: Cup-removal fallback state resets between shots

- **WHEN** a new shot starts (via `startShot()`) or a new settling cycle begins (via `startSettlingTimer()`)
- **THEN** the `m_lastCleanSettlingAvg` tracker SHALL be reset to 0 so a value from a prior shot cannot leak into the current shot's recorded weight
