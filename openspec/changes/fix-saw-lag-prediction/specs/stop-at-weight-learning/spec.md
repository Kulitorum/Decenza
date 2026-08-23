## MODIFIED Requirements

### Requirement: Per-Pair Prediction After Graduation

For a `(profile, scale, basket)` triple that has at least `kSawMinMediansForGraduation`
committed medians, the predictor SHALL fit the weighted-average smoother over those medians
(newest-first, bounded by the per-pair trim). The documented value of `kSawMinMediansForGraduation` SHALL match the
value the code uses.

A committed median SHALL represent a `(drip, flow)` pair that a single shot in the batch
actually produced. The system SHALL NOT commit a pair assembled from the drip of one shot
and the flow of another, because the lag such a pair implies is one no shot exhibited and
every reader of the entry derives from that lag — the entries reader that feeds the live
stop threshold, the smoother, the learned-lag reader, and the global bootstrap recompute.

Where no shot in the batch has a usable flow, the system SHALL drop the batch rather than
commit a pair, since there is no real pair available to commit.

#### Scenario: Graduated pair uses its committed medians

- **WHEN** a `(profile, scale, basket)` triple has at least `kSawMinMediansForGraduation`
  committed medians
- **THEN** the predictor SHALL build the smoother input from the most recent committed medians for
  that triple (newest-first, bounded by the per-pair trim)
- **AND** SHALL run the same Gaussian-weighted-average smoother as the global-pool path, with σ=0.25

#### Scenario: The committed pair is one the batch actually contained

- **WHEN** a batch reaches its commit size and passes the outlier gate
- **THEN** the committed entry's `drip` and `flow` SHALL both come from the same shot in
  that batch — the shot whose lag is nearest the median of the batch's per-shot lags

#### Scenario: A batch in which no shot had a usable flow

- **WHEN** every shot in a batch has a flow at or below the usable threshold
- **THEN** the system SHALL drop the batch and report why
- **AND** SHALL NOT append an entry to the committed history

#### Scenario: A batch whose median drip and median flow come from different shots

- **WHEN** the shot holding the batch's median drip is not the shot holding its median flow
- **THEN** the committed entry SHALL still be a single shot's `(drip, flow)` pair
- **AND** the entry's implied lag SHALL NOT be the quotient of the two independent medians

### Requirement: Batch Dispersion Gate

Before a batch commits, the system SHALL reject it if any shot's lag deviates from the
reference lag by more than the configured deviation bound. The reference SHALL be the lag of
the shot the batch would commit, not the quotient of the batch's median drip and median flow —
those two medians can come from different shots, making the comparison reference a lag no shot
in the batch exhibited, and one that can fall outside the range of the lags being tested.

#### Scenario: The gate compares against a lag some shot had

- **WHEN** a batch reaches its commit size
- **THEN** the reference each shot's lag is compared against SHALL be a lag one of the shots
  in that batch produced
- **AND** it SHALL be the lag of the pair the batch commits, so the gate and the committed
  entry describe the same shot

### Requirement: Per-Shot Prediction Diagnostics

The system SHALL log per-shot prediction state to support post-deploy validation of SAW predictions on
real shots. The logged state SHALL identify the basket and the scale type the prediction was keyed
on, so a saved shot's log is sufficient to reconstruct which bucket drove it.

On commit, the logged entry SHALL carry its lag together with the `(drip, flow)` pair that
lag came from, so a reader can confirm the two describe the same shot.

#### Scenario: Accuracy log line is emitted on each SAW learning point

- **WHEN** a SAW learning point is added
- **THEN** the system SHALL emit a `[SAW] accuracy:` log line containing the predicted drip, actual
  drip, delta, overshoot, flow at stop, scale type, profile filename, and basket

#### Scenario: Model log line names the bucket that drove the prediction

- **WHEN** the SAW model snapshot is taken at extraction start
- **THEN** the emitted model line SHALL name the model source tier, the effective lag, the profile,
  the scale type, the basket, and the number of committed medians in the bucket used

#### Scenario: A commit records the pair behind its lag

- **WHEN** a batch commits a median entry
- **THEN** the emitted commit line SHALL carry that entry's lag and the `(drip, flow)` pair
  it was taken from
