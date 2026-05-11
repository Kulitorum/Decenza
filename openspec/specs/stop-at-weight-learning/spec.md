# stop-at-weight-learning Specification

## Purpose
Define the post-shot stop-at-weight (SAW) prediction model that drives both the live SAW stop trigger and the per-shot accuracy log. The predictor estimates post-stop drip from recency-weighted, Gaussian-flow-similarity weighted historical (drip, flow) data, with a pre-graduation scalar bootstrap path and per-pair refinement once enough committed medians exist.

## Requirements

### Requirement: Drip Prediction Model

The system SHALL predict drip after the SAW stop trigger as a recency-weighted Gaussian-flow-similarity weighted average of historical (drip, flow) shot data, with the Gaussian σ set to 0.25 ml/s.

#### Scenario: At least one historical entry produces a weighted-average prediction
- **WHEN** the predictor has at least 1 historical (drip, flow) entry for the relevant scale type and the entries' total weight does not collapse to below 0.01
- **THEN** the predictor SHALL compute `weight_i = recencyWeight_i × exp(-(flow_i − queryFlow)² / 0.125)` for each entry
- **AND** SHALL return `clamp(Σ(drip_i × weight_i) / Σ(weight_i), 0.5, 20.0)` g

#### Scenario: Empty pool or collapsed weights fall back to lag model
- **WHEN** the predictor has zero historical entries for the relevant scale type OR the entries' total weight collapses to below 0.01 (queries too far in flow from any sample)
- **THEN** the predictor SHALL fall back to the legacy `flow × (sensorLag(scale) + 0.1)` formula
- **AND** the result SHALL be capped at 8 g

#### Scenario: Recency weighting matches existing convergence-aware schedule
- **WHEN** the predictor evaluates the global-pool path (`Settings::getExpectedDrip`)
- **THEN** point weights SHALL interpolate linearly from `recencyMax = 10` at the most recent point to `recencyMin = 3` (when the SAW model is converged for the scale type) or `1` (when not converged) at the oldest point used
- **AND** at most 12 points (converged) or 8 points (unconverged) SHALL contribute to a single fit

#### Scenario: Per-pair path uses fixed converged recency
- **WHEN** the predictor evaluates the per-pair path (`Settings::getExpectedDripFor` with a graduated pair)
- **THEN** point weights SHALL interpolate linearly from `recencyMax = 10` to `recencyMin = 3`
- **AND** the convergence test SHALL NOT be consulted (per-pair history is treated as already representative)

### Requirement: Per-Pair Prediction After Graduation

For a `(profile, scale)` pair that has at least `kSawMinMediansForGraduation = 2` committed medians, the predictor SHALL fit the weighted-average smoother over those medians (newest-first, capped at 12).

#### Scenario: Graduated pair uses its committed medians
- **WHEN** a pair has 2 or more committed medians
- **THEN** the predictor SHALL build the smoother input from the most recent committed medians (up to 12, newest-first)
- **AND** SHALL run the same Gaussian-weighted-average smoother as the global-pool path, with σ=0.25

### Requirement: Pre-Graduation Bootstrap Falls Through to Existing Scalar Path

For a `(profile, scale)` pair that has not yet graduated, the predictor SHALL retain the existing scalar-bootstrap behavior (`flow × globalSawBootstrapLag(scale)`, capped at 8 g) followed by the scale-default lag fallback. Phase 0 evaluation showed that replacing this path with a Gaussian-weighted aggregated pool fails the gate (overall MAE delta below threshold and shot-887-class predictions get worse). The σ change therefore ships alone, leaving the bootstrap path untouched.

#### Scenario: Non-graduated pair queries existing scalar bootstrap
- **WHEN** a pair has fewer than `kSawMinMediansForGraduation` committed medians
- **THEN** the predictor SHALL return `min(flow × globalSawBootstrapLag(scale), 8)` g if `globalSawBootstrapLag(scale) > 0`
- **AND** otherwise SHALL fall through to `min(flow × (sensorLag(scale) + 0.1), 8)` g

### Requirement: WeightProcessor Live Prediction Matches Settings Predictor

The live SAW threshold computation in `WeightProcessor::getExpectedDrip` SHALL produce the same numeric output as `Settings::getExpectedDrip` when given the same flow and the same snapshot of pool data, so the post-shot accuracy log and the in-shot threshold are consistent.

#### Scenario: Snapshot at configure time produces consistent predictions
- **WHEN** `WeightProcessor::configure` is called with a `(learningDrips, learningFlows)` snapshot
- **THEN** subsequent `getExpectedDrip(currentFlowRate)` calls SHALL apply the same recency-weighted Gaussian-flow-similarity smoother as `Settings::getExpectedDrip`, with σ=0.25
- **AND** any divergence SHALL be considered a bug (covered by tests)

### Requirement: Per-Shot Prediction Diagnostics

The system SHALL log per-shot prediction state to support post-deploy validation of SAW predictions on real shots.

#### Scenario: Accuracy log line is emitted on each SAW learning point
- **WHEN** a SAW learning point is added
- **THEN** the system SHALL emit a `[SAW] accuracy:` log line containing the predicted drip, actual drip, delta, overshoot, flow at stop, scale type, and profile filename
