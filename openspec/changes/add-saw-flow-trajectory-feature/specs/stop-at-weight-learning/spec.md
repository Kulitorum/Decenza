# Spec Delta: stop-at-weight-learning

## ADDED Requirements

### Requirement: Stall-Recovery Detection at SAW Trigger

The system SHALL compute the trailing 1.5-second flow slope at the SAW trigger moment and SHALL use that slope to reject shots whose flow trajectory indicates a regime change (stall recovery, abrupt channeling) from being added to the learning model. SAW itself MUST still trigger normally; only the model-update path is gated.

#### Scenario: Flat-trajectory shot is accepted into the model
- **WHEN** a SAW learning point arrives with `|flowSlope| ≤ kStallRecoveryThreshold`
- **THEN** the entry SHALL be processed by the existing `addSawLearningPoint` / `addSawPerPairEntry` paths unchanged
- **AND** SHALL be eligible to participate in pending-batch accumulation, IQR-based batch outlier rejection, and committed-median promotion as today

#### Scenario: Spiking-trajectory shot is rejected from the model
- **WHEN** a SAW learning point arrives with `|flowSlope| > kStallRecoveryThreshold`
- **THEN** the entry SHALL NOT be appended to the per-pair pending batch or to the global pool
- **AND** the system SHALL emit a warning log line `[SAW] rejected (stall recovery): drip=X flow=Y slope=Z threshold=T` to the persistent debug logger
- **AND** the SAW stop trigger SHALL have completed normally (the gate is post-trigger, model-update only)

#### Scenario: Symmetric threshold catches both stall-recovery and channeling
- **WHEN** the flow slope is `+(kStallRecoveryThreshold + ε)` (stall-recovery: flow accelerating into trigger)
- **OR** the flow slope is `−(kStallRecoveryThreshold + ε)` (channeling: flow collapsing into trigger)
- **THEN** both cases SHALL be rejected
- **AND** the threshold check SHALL use absolute value, not signed comparison

#### Scenario: Insufficient buffer data falls through as zero slope
- **WHEN** the flow buffer contains fewer than 3 samples at SAW trigger time (e.g., scale just connected, or buffer reset)
- **THEN** the computed `flowSlope` SHALL be 0.0
- **AND** the entry SHALL pass the gate (no rejection on insufficient data)

### Requirement: Flow Buffer Maintained by ShotTimingController

`ShotTimingController` SHALL maintain a circular buffer of `(timestamp, flowRate)` pairs over the trailing 1.5 seconds and SHALL compute a least-squares slope from that buffer at SAW trigger time.

#### Scenario: Buffer is reset at extraction start
- **WHEN** `ShotTimingController::startExtraction` (or the equivalent extraction-start signal) fires
- **THEN** the flow buffer SHALL be cleared
- **AND** subsequent flow samples SHALL accumulate into the buffer

#### Scenario: Buffer is bounded by 1.5 seconds of history
- **WHEN** a flow sample arrives whose timestamp is more than 1.5 seconds older than the most recent sample
- **THEN** that sample SHALL be removed from the buffer

#### Scenario: Slope is computed via least-squares fit
- **WHEN** `flowSlope` is requested at SAW trigger time
- **THEN** the buffer's `(timestamp, flowRate)` points SHALL be fit with a least-squares slope via the standard one-pass formula
- **AND** the result SHALL be in units of ml/s²
- **AND** the buffer SHALL NOT be cleared by the slope computation (it remains valid for any subsequent settling-phase telemetry)

### Requirement: Learning Signal Carries Flow Slope

The `sawLearningComplete` signal and downstream `addSawLearningPoint` / `addSawPerPairEntry` calls SHALL include `flowSlope` so that the rejection gate can be applied at the model-update step.

#### Scenario: sawLearningComplete signal includes slope
- **WHEN** SAW settling completes and the learning signal is emitted
- **THEN** the signal signature SHALL include `(double drip, double flowAtStop, double overshoot, double flowSlope)`
- **AND** the connecting lambda in main.cpp SHALL forward `flowSlope` to `Settings::addSawLearningPoint`

#### Scenario: addSawLearningPoint and addSawPerPairEntry accept flow slope
- **WHEN** a SAW learning point is added through either entrypoint
- **THEN** the function signature SHALL accept `double flowSlope` as a parameter
- **AND** the stall-recovery gate SHALL fire before any pool-modifying operations
