# Spec Delta: stop-at-weight-learning

## ADDED Requirements

### Requirement: Pressure-At-Stop Captured At SAW Trigger

The system SHALL capture the most recent pressure reading from the DE1 at the moment the SAW stop trigger fires and SHALL carry that value through the learning signal to `Settings::addSawLearningPoint` for storage in the per-pair history.

#### Scenario: ShotTimingController stores latest pressure
- **WHEN** `DE1Device::pressureChanged` (or equivalent signal) fires
- **THEN** `ShotTimingController` SHALL store the latest pressure value in a member variable
- **AND** that value SHALL be the `pressureAtStop` carried in the `sawLearningComplete` signal

#### Scenario: sawLearningComplete signal includes pressure
- **WHEN** SAW settling completes and the learning signal is emitted
- **THEN** the signal signature SHALL include `(double drip, double flowAtStop, double overshoot, double pressureAtStop)` — and additionally `flowSlope` if `add-saw-flow-trajectory-feature` is also active
- **AND** the connecting lambda in main.cpp SHALL forward `pressureAtStop` to `Settings::addSawLearningPoint`

#### Scenario: Pressure stored in per-pair JSON entry
- **WHEN** an entry is appended to `saw/perProfileHistory` or `saw/learningHistory`
- **THEN** the JSON object SHALL include a `"pressure"` field with the captured value
- **AND** the value SHALL be a JSON number (not a string)

### Requirement: Two-Dimensional Kernel for Drip Prediction

The system SHALL extend the SAW prediction kernel to weight historical entries by both flow-similarity (Gaussian σ_f = 0.25 ml/s, established by `tune-saw-old-prediction`) AND pressure-similarity (Gaussian σ_p, value set by Phase 0 production validation), producing a 2-D kernel weight per entry.

#### Scenario: Both flow and pressure available
- **WHEN** the predictor is called with `(flow, pressure)` and the historical entry has both `flow_i` and `pressure_i` ≥ 0
- **THEN** the kernel weight SHALL be `recencyWeight × exp(-(flowDiff² / 2σ_f²) − (pressureDiff² / 2σ_p²))`
- **AND** the result SHALL be `clamp(Σ(drip_i × weight_i) / Σ(weight_i), 0.5, 20.0)` g

#### Scenario: Entry has no pressure (legacy data)
- **WHEN** the historical entry has `pressure_i < 0` (sentinel for missing pressure)
- **THEN** the kernel weight SHALL collapse to the 1-D form: `recencyWeight × exp(-(flowDiff² / 2σ_f²))`
- **AND** the prediction SHALL still be valid; legacy entries contribute via the 1-D fallback

#### Scenario: Caller has no pressure (legacy API)
- **WHEN** the predictor is called via the single-arg overload `getExpectedDrip(flow)` (no pressure)
- **THEN** the kernel SHALL behave as if every entry's pressure-axis weight is 1.0
- **AND** the prediction SHALL be identical to the 1-D model from `tune-saw-old-prediction`
- **AND** this preserves backwards compatibility with QML and diagnostic surfaces that don't have pressure data

#### Scenario: σ_p value is locked in by Phase 0
- **WHEN** the production code is built
- **THEN** `kSawPressureSigma` SHALL be a `static constexpr double` defined in `settings.cpp`
- **AND** its value SHALL be the σ_p that passed the production gate in Phase 0c

### Requirement: Backwards-Compatible Prediction APIs

`Settings::getExpectedDrip` and `Settings::getExpectedDripFor` SHALL provide both pressure-aware and pressure-free overloads so existing callers can be migrated incrementally.

#### Scenario: Pressure-aware overload
- **WHEN** code calls `getExpectedDrip(flow, pressure)` with a non-negative pressure value
- **THEN** the predictor SHALL apply the 2-D kernel as defined above

#### Scenario: Single-arg overload (legacy)
- **WHEN** code calls `getExpectedDrip(flow)` with no pressure parameter
- **THEN** the predictor SHALL behave identically to calling `getExpectedDrip(flow, -1)`
- **AND** the 1-D form of the kernel SHALL apply

### Requirement: Pressure-Aware Live Prediction in WeightProcessor

The live SAW threshold computation in `WeightProcessor::getExpectedDrip` SHALL produce the same numeric output as `Settings::getExpectedDrip` when given the same flow, pressure, and pool snapshot.

#### Scenario: Snapshot includes pressure
- **WHEN** `WeightProcessor::configure` is called
- **THEN** the snapshot SHALL include `(learningDrips, learningFlows, learningPressures)`
- **AND** `getExpectedDrip(currentFlow, currentPressure)` SHALL apply the same 2-D kernel as `Settings::getExpectedDrip`
- **AND** any divergence SHALL be considered a bug (covered by tests)

### Requirement: Per-Shot Pressure Diagnostics

The system SHALL log pressure-related prediction state per shot during the post-deploy A/B period so that the 2-D kernel's contribution can be measured against the 1-D baseline.

#### Scenario: Accuracy log line includes both predictions
- **WHEN** a SAW learning point is added (the `[SAW] accuracy:` log line)
- **THEN** the log line SHALL include `pressureAtStop`, `pressurePredictedDrip` (the 2-D kernel result), and `flowOnlyPredictedDrip` (what the 1-D kernel would have returned)
- **AND** the shadow-logging fields SHALL remain for at least one full release cycle so post-deploy A/B is feasible
