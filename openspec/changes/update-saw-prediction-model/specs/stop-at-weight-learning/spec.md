# Spec Delta: stop-at-weight-learning

## ADDED Requirements

### Requirement: Drip Prediction Model

The system SHALL predict drip after the SAW stop trigger as a linear function of flow at stop, with intercept, fitted by recency-weighted least squares over historical (drip, flow) shot data.

#### Scenario: Two or more historical points produce a regression fit
- **WHEN** the predictor has at least 2 historical (drip, flow) points and their flow values are not collinear (denominator ≥ 0.001)
- **THEN** the predictor SHALL solve recency-weighted least squares for `drip = a·flow + b`
- **AND** the predictor SHALL clamp `a` to `[0, 5]` and `b` to `[−2, 2]` before evaluation
- **AND** the returned drip SHALL be `clamp(a·flow + b, 0.5, 20)` g

#### Scenario: Fewer than two points or collinear flows fall back to lag model
- **WHEN** the predictor has fewer than 2 historical points OR all historical points share a common flow value to within 0.001 ml/s
- **THEN** the predictor SHALL fall back to the legacy `flow × (sensorLag(scale) + 0.1)` formula
- **AND** the result SHALL be capped at 8 g (matching the legacy bootstrap clamp)

#### Scenario: Recency weighting matches existing convergence-aware schedule
- **WHEN** the regression fit is computed
- **THEN** point weights SHALL interpolate linearly from `recencyMax = 10` at the most recent point to `recencyMin = 3` (when the SAW model is converged for the scale type) or `1` (when not converged) at the oldest point used
- **AND** at most 12 points SHALL contribute to a single fit

### Requirement: Per-Pair Prediction with Pending-Batch Warm-Up

For a `(profile, scale)` pair that has not yet graduated, the predictor SHALL use pending-batch entries before falling back to the global bootstrap, so pair-specific predictions begin after the second SAW-triggering shot of a new pair rather than after graduation.

#### Scenario: Graduated pair uses its committed medians directly
- **WHEN** a pair has at least `kSawMinMediansForGraduation` committed medians
- **THEN** the predictor SHALL fit the regression over those medians' (drip, flow) values
- **AND** SHALL NOT consult the pending batch or the global bootstrap

#### Scenario: Pre-graduation pair with two or more pending-batch entries uses the pending batch
- **WHEN** a pair has fewer than `kSawMinMediansForGraduation` committed medians AND its pending batch has at least 2 entries
- **THEN** the predictor SHALL fit the regression over the pending-batch (drip, flow) entries
- **AND** SHALL NOT consult the global bootstrap

#### Scenario: Pre-graduation pair with insufficient pending-batch data falls through to global bootstrap
- **WHEN** a pair has fewer than 2 pending-batch entries
- **THEN** the predictor SHALL request a smart-bootstrap prediction for the pair's scale type

### Requirement: Smart Global Bootstrap

The bootstrap predictor SHALL fit one regression across pooled raw entries from all pairs sharing a scale type, providing a two-feature prediction even before any pair has graduated.

#### Scenario: Bootstrap pool aggregates per-scale entries
- **WHEN** the bootstrap predictor is called for a given scale type
- **THEN** the pool SHALL contain, for that scale type only:
  - the most recent committed median entry from each pair, AND
  - every entry from each pair's pending batch, AND
  - the legacy global pool entries (`saw/learningHistory`) tagged with that scale type, capped at the most recent 50 (existing cap)

#### Scenario: Bootstrap fit succeeds with two or more pooled points
- **WHEN** the pooled points contain at least 2 entries with non-collinear flows
- **THEN** the bootstrap SHALL fit `drip = a·flow + b` and return `clamp(a·flow + b, 0.5, 20)` g
- **AND** the same `(a, b)` clamps from the Drip Prediction Model requirement SHALL apply

#### Scenario: Empty bootstrap pool falls back to scale sensor lag
- **WHEN** the pooled points contain fewer than 2 entries
- **THEN** the bootstrap SHALL return `min(flow × (sensorLag(scale) + 0.1), 8)` g
- **AND** SHALL NOT attempt a regression

### Requirement: WeightProcessor Live Prediction Matches Settings Predictor

The live SAW threshold computation in `WeightProcessor::getExpectedDrip` SHALL produce the same numeric output as `Settings::getExpectedDripFor` when given the same flow and the same snapshot of pool data, so the post-shot accuracy log and the in-shot threshold are consistent.

#### Scenario: Snapshot at configure time produces consistent predictions
- **WHEN** `WeightProcessor::configure` is called with a `(learningDrips, learningFlows)` snapshot
- **THEN** subsequent `getExpectedDrip(currentFlowRate)` calls SHALL apply the same recency-weighted least-squares fit, the same clamps, and the same lag-fallback path as `Settings`
- **AND** any divergence SHALL be considered a bug (covered by tests)

### Requirement: Implausibility Gate Independent of Lag Interpretation

The pre-learning input filter SHALL reject only physically impossible drip values, without assuming a lag-based model.

#### Scenario: Drip exceeding absolute cap is rejected
- **WHEN** a SAW learning entry has `drip > 10 g`
- **THEN** the entry SHALL be rejected before being added to the pending batch
- **AND** a warning SHALL be logged

#### Scenario: All other entries pass the implausibility gate
- **WHEN** a SAW learning entry has `0 ≤ drip ≤ 10 g`
- **THEN** the entry SHALL pass the implausibility gate
- **AND** subsequent gates (converged-mode outlier rejection, IQR/deviation batch checks) SHALL apply unchanged

### Requirement: Per-Shot Prediction Diagnostics

The system SHALL log enough per-shot prediction state to support post-deploy analysis of model accuracy without requiring instrumentation that affects shot timing or storage.

#### Scenario: Accuracy log line includes model components
- **WHEN** a SAW learning point is added (the `[SAW] accuracy:` log line)
- **THEN** the log line SHALL include the predicted drip, actual drip, error, overshoot, flow at stop, scale type, profile filename
- **AND** SHALL also include `modelA`, `modelB`, and `modelSource` (one of `perPairRegression`, `pendingBatchRegression`, `globalRegression`, `lagFallback`, `sensorLagDefault`) so a Phase 3 analysis can join model state to outcome

#### Scenario: Per-pair entry-state log line is emitted at addSawPerPairEntry
- **WHEN** `Settings::addSawPerPairEntry` is invoked
- **THEN** before any state mutation it SHALL emit a log line with the pair key, current pending batch size, current committed median count, and graduated-or-not flag
- **AND** the line SHALL flow through the persistent debug log path so it survives past the per-shot capture window
