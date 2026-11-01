## ADDED Requirements

### Requirement: Mass analysis preserves observed coverage

PORTAL mass analysis SHALL use physical-scale evidence and all recorded EC changes.
It SHALL preserve scale plateaus and SHALL NOT bridge a recorded interruption.
Tail metrics SHALL be unavailable when the last five grams contain missing EC data.

#### Scenario: A scale interval spans a PORTAL gap

- **WHEN** weight packets bracket an interruption without a weight packet inside it
- **THEN** the interrupted mass SHALL remain uncovered rather than integrating between its endpoints

#### Scenario: Only the tail is incomplete

- **WHEN** whole-shot coverage is sufficient but the final five grams contain a gap
- **THEN** the analyzer SHALL report insufficient coverage rather than a low-concentration tail

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

Calibration references SHALL record the actual serving weight source, mounting mode and PORTAL presence from samples. Unknown or virtual sources SHALL be ineligible. Reads SHALL NOT backfill the database or parse diagnostic logs; legacy backfill SHALL run once during migration.

#### Scenario: FlowScale serves a shot with a saved physical-scale SAW key

- **WHEN** FlowScale supplies any weight samples despite a physical-scale key in the log
- **THEN** the shot SHALL remain ineligible as a calibration reference

#### Scenario: Dose correction crosses the thermal gate

- **WHEN** a dose correction makes a reference thermally unreliable
- **THEN** it SHALL be excluded from subsequent calibration immediately after the write

#### Scenario: History contains no PORTAL data

- **WHEN** a non-PORTAL history row has a refractometer reading
- **THEN** its existing visible and spoken presentation SHALL remain unchanged
- **AND** no PORTAL calibration query SHALL be needed for that row

#### Scenario: An older coffee has valid references

- **WHEN** more than 200 newer measurements exist for other coffees
- **THEN** the older coffee SHALL retain its matching references
