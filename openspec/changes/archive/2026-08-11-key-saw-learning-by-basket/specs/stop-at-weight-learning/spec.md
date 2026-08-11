## ADDED Requirements

### Requirement: SAW Learning Is Keyed By Basket

SAW learning history SHALL be isolated per `(profile, scale, basket)`. The basket key value
SHALL be derived from the active equipment package's basket identity (brand and model), and a package
with no basket SHALL key on a distinct "no basket" value rather than sharing a bucket with any real
basket. Switching basket SHALL NOT contaminate another basket's learned drip model, in either
direction.

#### Scenario: Two baskets on the same profile and scale learn independently

- **WHEN** shots are recorded on one profile and one scale type, alternating between two baskets
  whose post-stop drip differs
- **THEN** each basket SHALL accumulate its own pending batch and its own committed medians
- **AND** a prediction made while one basket is active SHALL NOT be influenced by the other basket's
  entries

#### Scenario: A package with no basket is its own bucket

- **WHEN** a shot is recorded while the active equipment package has no basket component
- **THEN** the learning entry SHALL be keyed on the distinct "no basket" value
- **AND** it SHALL NOT be readable as history for any package that has a basket

#### Scenario: Changing basket mid-batch does not commit mixed data

- **WHEN** a pending batch for one basket is incomplete and the user switches to a different basket
- **THEN** the incomplete batch SHALL remain pending against its original basket
- **AND** the new basket's entries SHALL accumulate separately, so no committed median mixes entries
  from two baskets

### Requirement: Pre-Basket History Is Copied Once Into The Combinations Actually Pulled

History recorded before the basket dimension existed carries no basket. For each profile, the
system SHALL copy that profile's pre-basket history exactly once into a bucket for every basket
the recent shot history shows THAT PROFILE was pulled with, so each such combination predicts
what the single shared model predicted before the upgrade and then diverges as it earns its own
committed medians. The system SHALL NOT retain a permanent basket-blind fallback tier, SHALL NOT
attribute the history to a single basket, and SHALL NOT seed a combination that was never
pulled.

Because no reader consults pre-basket keys once the copy is marked complete, the system SHALL
treat any answer it cannot distinguish from failure as failure: it SHALL mark the copy complete
only on a history read that demonstrably succeeded, and SHALL leave it open — to retry on a later
launch — on a failed read, an unavailable store, or an empty result over a store that still holds
pre-basket buckets.

#### Scenario: Every basket in recent use keeps predicting what it predicted before

- **WHEN** a store containing pre-basket `(profile, scale)` history is opened by a build that
  keys on the basket, and the recent shot history shows two baskets in use
- **THEN** both baskets SHALL read that history as their own
- **AND** the predicted drip and reported lag for each SHALL be unchanged from what the
  previous build produced

#### Scenario: The basket set comes from shots taken, not from baskets owned

- **WHEN** the user owns many baskets but has taken recent shots with only some of them
- **THEN** only the baskets appearing in the recent shot history SHALL be seeded
- **AND** a basket with no recent shots SHALL NOT be given a bucket of borrowed data

#### Scenario: An untried profile-and-basket combination is not seeded

- **WHEN** a basket appears in the recent shot history but never with a given profile
- **THEN** that profile's history SHALL NOT be copied into that basket

#### Scenario: A profile absent from the history window is left alone

- **WHEN** a profile has pre-basket history but no shots inside the queried window
- **THEN** its pre-basket bucket SHALL be left exactly as it is
- **AND** no bucket SHALL be fabricated for it

#### Scenario: A basket that already has its own data is never overwritten

- **WHEN** a basket already has committed medians of its own at seed time
- **THEN** its history SHALL be left exactly as it is

#### Scenario: Shots with no equipment recorded seed the no-basket value

- **WHEN** the recent shot history contains shots with no equipment package
- **THEN** the pre-basket history SHALL be seeded onto the distinct "no basket" value

#### Scenario: A copied median does not vote once per basket in the bootstrap

- **WHEN** the cross-basket bootstrap lag is recomputed and some buckets hold only copied
  history
- **THEN** those buckets SHALL NOT contribute, so one batch of shots cannot count once per
  basket
- **AND** the pre-basket bucket the copies came from SHALL NOT contribute either, since nothing
  updates it again and it would otherwise vote a frozen snapshot of its own past forever

#### Scenario: A failed or unavailable history read does not close the copy

- **WHEN** the query behind the basket set fails, or the shot store cannot be opened or is not
  ready
- **THEN** no result SHALL be delivered to the copy step
- **AND** the copy SHALL remain open so a later launch retries it

#### Scenario: An empty result over a store holding pre-basket buckets is refused

- **WHEN** the copy step is handed an empty basket set while pre-basket buckets still exist
- **THEN** it SHALL refuse the answer, copy nothing, and leave itself open
- **AND** it SHALL record that it did so

#### Scenario: Every outcome is recorded, including copying nothing

- **WHEN** the copy step finishes, whether it created buckets or not
- **THEN** it SHALL emit a user-visible record of the outcome, including how many pre-basket
  buckets were left behind, since after closing they are no longer read

#### Scenario: Importing pre-basket learning reopens the copy

- **WHEN** stop-at-weight learning is imported from a store written before basket keying (device
  transfer or backup restore)
- **THEN** the copy SHALL be reopened, so the imported buckets are carried onto baskets rather
  than restored where no reader looks

#### Scenario: An incomplete seed does not foreclose the rest

- **WHEN** the seed runs before the full set of baskets is known
- **THEN** the guard flag SHALL NOT be set
- **AND** a later run with the full set SHALL still seed the remaining baskets

#### Scenario: A pending batch is carried across but not committed

- **WHEN** a pending batch exists under a pre-basket key at seed time, including one already at
  the commit threshold
- **THEN** it SHALL be carried into the basket-keyed bucket
- **AND** it SHALL NOT be committed as a median by the seed, so the dispersion gate and the
  auto-reset check still apply on the next shot

## MODIFIED Requirements

### Requirement: Per-Pair Prediction After Graduation

For a `(profile, scale, basket)` triple that has at least `kSawMinMediansForGraduation`
committed medians, the predictor SHALL fit the weighted-average smoother over those medians
(newest-first, bounded by the per-pair trim). The documented value of `kSawMinMediansForGraduation` SHALL match the
value the code uses.

#### Scenario: Graduated pair uses its committed medians

- **WHEN** a `(profile, scale, basket)` triple has at least `kSawMinMediansForGraduation`
  committed medians
- **THEN** the predictor SHALL build the smoother input from the most recent committed medians for
  that triple (newest-first, bounded by the per-pair trim)
- **AND** SHALL run the same Gaussian-weighted-average smoother as the global-pool path, with σ=0.25

### Requirement: Pre-Graduation Bootstrap Falls Through to Existing Scalar Path

For a triple that has not graduated, the predictor SHALL retain the existing scalar-bootstrap behavior (`flow × globalSawBootstrapLag(scale)`,
capped at 8 g) followed by the scale-default lag fallback. Phase 0 evaluation showed that replacing
this path with a Gaussian-weighted aggregated pool fails the gate (overall MAE delta below threshold
and shot-887-class predictions get worse), so the bootstrap path is unchanged: its key stays the
per-transport scale type and only its contributor set widens to cover per-basket buckets.

#### Scenario: Non-graduated pair queries existing scalar bootstrap

- **WHEN** a triple has fewer than `kSawMinMediansForGraduation` committed medians
- **THEN** the predictor SHALL return `min(flow × globalSawBootstrapLag(scale), 8)` g if
  `globalSawBootstrapLag(scale) > 0`
- **AND** otherwise SHALL fall through to `min(flow × (sensorLag(scale) + 0.1), 8)` g

#### Scenario: Bootstrap is pooled across baskets

- **WHEN** the global bootstrap lag for a scale type is recomputed
- **THEN** every `(profile, scale, basket)` bucket with committed history on that scale type SHALL
  be eligible to contribute its most recent committed median lag
- **AND** the result SHALL remain a single scalar per scale type, so a brand-new basket still gets a
  device-specific cold-start prior

### Requirement: Per-Shot Prediction Diagnostics

The system SHALL log per-shot prediction state to support post-deploy validation of SAW predictions on
real shots. The logged state SHALL identify the basket and the scale type the prediction was keyed
on, so a saved shot's log is sufficient to reconstruct which bucket drove it.

#### Scenario: Accuracy log line is emitted on each SAW learning point

- **WHEN** a SAW learning point is added
- **THEN** the system SHALL emit a `[SAW] accuracy:` log line containing the predicted drip, actual
  drip, delta, overshoot, flow at stop, scale type, profile filename, and basket

#### Scenario: Model log line names the bucket that drove the prediction

- **WHEN** the SAW model snapshot is taken at extraction start
- **THEN** the emitted model line SHALL name the model source tier, the effective lag, the profile,
  the scale type, the basket, and the number of committed medians in the bucket used
