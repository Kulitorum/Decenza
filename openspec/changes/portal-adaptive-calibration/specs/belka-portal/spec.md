## ADDED Requirements

### Requirement: Mass analysis preserves observed coverage

PORTAL mass analysis SHALL use physical-scale evidence and all recorded EC changes.
It SHALL preserve scale plateaus and SHALL NOT bridge a recorded interruption.
Tail metrics SHALL be unavailable when the last five grams contain missing EC data.
Same-timestamp scale updates SHALL resolve to the latest monotonic value; reversed
time and resets SHALL remain invalid. Recorded-curve failures SHALL be distinguished
from missing physical-scale evidence.

#### Scenario: A scale interval spans a PORTAL gap

- **WHEN** weight packets bracket an interruption without a weight packet inside it
- **THEN** the interrupted mass SHALL remain uncovered rather than integrating between its endpoints

#### Scenario: Only the tail is incomplete

- **WHEN** whole-shot coverage is sufficient but the final five grams contain a gap
- **THEN** the analyzer SHALL report insufficient coverage rather than a low-concentration tail

#### Scenario: Ordered scale updates share a clock tick

- **WHEN** multiple recorded scale snapshots share a timestamp
- **THEN** analysis SHALL use the latest monotonic snapshot at that timestamp without double-counting mass
- **AND** reversed timestamps and scale resets SHALL remain invalid
- **AND** an invalid recorded curve SHALL be distinguished from a missing physical scale in Shot Review

### Requirement: Refractometer estimates use independent comparable measurements

A shot SHALL be excluded from its own calibration. Calibration SHALL use the
same bag or bean and mounting mode, excluding measurements with uncertainty
warnings. Fewer than five independent references SHALL be marked preliminary.

#### Scenario: No matching reference exists

- **WHEN** only the target shot or a different bean has a paired refractometer reading
- **THEN** the target SHALL retain relative EC metrics without an estimated TDS/EY

### Requirement: Advice does not invent taste or unmeasured solids

Ratio advice SHALL use explicit feedback on the current cup and SHALL describe
experiments rather than a proven optimum. Rated references SHALL match coffee,
equipment, profile and a close dose. EC SHALL NOT be reported as remaining coffee
solids. The app SHALL NOT append warnings to the user's tasting notes when saving or reading a shot.

#### Scenario: Another cup was marked sour

- **WHEN** the current cup has no taste feedback
- **THEN** another cup's feedback SHALL NOT mark the current cup under-extracted

#### Scenario: Empirical calibration is preliminary

- **WHEN** the same reliable measurement has zero or fewer than five refractometer references
- **THEN** its measurement-based ratio advice SHALL remain unchanged
- **AND** any available TDS/EY estimate SHALL still report calibration uncertainty

#### Scenario: Metadata changes in Shot Review

- **WHEN** taste or refractometer metadata is saved
- **THEN** the shared outcome response SHALL refresh PORTAL analysis without replacing unsaved editing fields

### Requirement: Settings exposes the current empirical calibration

The PORTAL settings panel SHALL show the current coffee and mounting mode's
proportional k, active model, included/excluded reference shots, individual k,
measured ratio range and leave-one-out error when at least two references exist.
It SHALL distinguish measurement eligibility from prediction uncertainty, and
SHALL NOT present extrapolations outside the measured EC/ratio envelope as reliable.
The fit SHALL be derived from stored measurements rather than a hardcoded factor.

#### Scenario: A saved TDS reading is corrected

- **WHEN** a user saves or corrects refractometer TDS in shot metadata
- **THEN** the Settings model SHALL refresh after the write succeeds
- **AND** references with measurement warnings SHALL remain excluded
- **AND** the database read SHALL run off the main thread

#### Scenario: The selected coffee changes while a calibration read is pending

- **WHEN** a prior coffee's read completes after another coffee is selected
- **THEN** its result SHALL NOT replace the current calibration
- **AND** calibration SHALL use the same shared service in native and Wasm clients

### Requirement: Calibration uses saved measurement provenance

Calibration references SHALL record the actual serving weight source, mounting mode and PORTAL presence from samples. Unknown or virtual sources SHALL be ineligible. Reads SHALL NOT backfill the database or parse diagnostic logs; schema migration SHALL NOT inspect historical sample blobs. Legacy presence backfill SHALL run once on the database worker after a PORTAL is owned or active, with completion persisted in the same transaction as the updates.

#### Scenario: FlowScale serves a shot with a saved physical-scale SAW key

- **WHEN** FlowScale supplies any weight samples despite a physical-scale key in the log
- **THEN** the shot SHALL remain ineligible as a calibration reference

#### Scenario: Dose correction crosses the thermal gate

- **WHEN** a dose correction makes a reference thermally unreliable
- **THEN** it SHALL be excluded from subsequent calibration immediately after the write
- **AND** an open Shot Review SHALL refresh its saved outcome after a Visualizer metadata pull without replacing the editing form

#### Scenario: History contains no PORTAL data

- **WHEN** a non-PORTAL history row has a refractometer reading
- **THEN** its existing visible and spoken presentation SHALL remain unchanged
- **AND** no PORTAL calibration query SHALL be needed for that row

#### Scenario: An older coffee has valid references

- **WHEN** more than 200 newer measurements exist for other coffees
- **THEN** the older coffee SHALL retain its matching references

#### Scenario: No PORTAL has been attached

- **WHEN** history is initialized without an owned or active PORTAL
- **THEN** the PORTAL migration SHALL change schema only
- **AND** no legacy sample scan SHALL be scheduled

#### Scenario: First PORTAL attachment and restart

- **WHEN** a PORTAL is owned or active and legacy presence backfill is incomplete
- **THEN** the backfill SHALL run on the database worker without blocking UI event delivery
- **AND** legacy missing provenance SHALL remain ineligible for calibration
- **AND** a completed pass SHALL NOT run again after restart
- **AND** a failed pass SHALL roll back its updates and remain retryable

### Requirement: Empirical models demonstrate predictive value

Fitted coefficients SHALL remain inspectable independently of prediction support.
Missing cross-validation SHALL NOT be reported as zero error. TDS/EY predictions
SHALL require complete leave-one-out evaluation, positive EC/TDS association and
lower RMSE than the leave-one-out reference-mean baseline. Predictions SHALL be
withheld on measurement warnings or outside the measured EC/ratio envelope.

#### Scenario: Only one independent reference exists

- **WHEN** a coefficient can be fitted but independent prediction error is unavailable
- **THEN** review, history and structured payloads SHALL withhold estimated TDS/EY
- **AND** measured refractometer data and relative EC metrics SHALL remain available

#### Scenario: More EC accompanies less measured TDS

- **WHEN** paired measurements fail the positive-association or held-out baseline comparison
- **THEN** the calibration SHALL report inconsistent prediction support rather than a reliable estimate
- **AND** measurement eligibility and measurement-based ratio advice SHALL remain independent of that result

### Requirement: Preserve uninterpreted temperature evidence

The auxiliary BLE float SHALL be retained optionally as `auxiliaryTemperatureRaw`
through capture, compressed storage, import/export and shared shot projections.
Its meaning and units SHALL NOT be assumed or used for a correction. Temperature
diagnostics SHALL use the same covered positive mass increments as EC integration,
without changing the legacy sample-temperature mean or its persisted column.

#### Scenario: Loading a recording without the auxiliary field

- **WHEN** an old sample has no auxiliary value
- **THEN** its EC and primary temperature SHALL remain usable
- **AND** the auxiliary value SHALL remain absent rather than becoming zero

#### Scenario: Temperature changes while no coffee mass arrives

- **WHEN** the scale remains on a plateau before, during or after the pour
- **THEN** that interval SHALL contribute no weight to the mass-temperature diagnostics
