## ADDED Requirements

### Requirement: The driver can request an averaged reading over multiple tests

The DiFluid R2 driver SHALL be able to request an averaged measurement (Device Action, Cmd 1) carrying a test count, in addition to the existing single test (Device Action, Cmd 0). The test count SHALL be clamped to the device's supported range of 1–10 before transmission. An averaged reading SHALL pass the same out-of-range plausibility gate as a single reading — averaging changes how a number was produced, not what is allowed to escape the driver.

#### Scenario: Averaged measurement requested

- **WHEN** an averaged measurement of 3 tests is requested
- **THEN** the driver writes a Device Action Cmd 1 packet whose single data byte is 3, with a valid additive checksum
- **AND** the driver enters the measuring state

#### Scenario: Test count outside the device range is clamped

- **WHEN** an averaged measurement is requested with a count of 0, or of 25
- **THEN** the transmitted count is 1, or 10, respectively
- **AND** no packet carrying an out-of-range count is ever written

#### Scenario: Averaged result is gated identically to a single reading

- **WHEN** an averaged result arrives carrying a concentration above the plausible-TDS ceiling
- **THEN** it is rejected and an error is surfaced, exactly as the same value in a single result would be
- **AND** no reading is emitted

#### Scenario: Single test remains the default

- **WHEN** a measurement is requested without specifying averaging
- **THEN** the driver sends a single test (Device Action Cmd 0), preserving today's behaviour

### Requirement: Responses are interpreted according to the action they belong to

During an averaged run the R2 emits a complete packet set per individual test — including the single-test result packet, which carries that one test's concentration rather than the average. The driver SHALL therefore interpret result packets according to the action code carried in the response, treating the single-test result packet as a final reading only when it belongs to a single-test action.

Any action code the driver does not recognise SHALL fall back to the existing single-test interpretation. The driver does not know what action code a physical-button measurement carries, and the cost of guessing wrong must be "no better than today", never a path that goes silent.

#### Scenario: Per-test result during an averaged run is not a final reading

- **WHEN** an averaged run is in progress
- **AND** a single-test result packet arrives for one of its constituent tests
- **THEN** no reading is emitted from it
- **AND** the run is not treated as complete

#### Scenario: Single-test result during a single-test run is a final reading

- **WHEN** a single test is performed
- **AND** its result packet arrives
- **THEN** a reading is emitted and the run completes, exactly as before this capability existed

#### Scenario: Unrecognised action code behaves as it does today

- **WHEN** a result packet arrives under an action code the driver has no handling for
- **THEN** it is interpreted as a single-test result
- **AND** a reading is emitted rather than silently discarded

### Requirement: An averaged reading is delivered as it converges

The averaged-result packet arrives once per constituent test, each carrying the average so far. The driver SHALL emit each of them, so the last emission is the completed average and the delivery of a reading never depends on a terminal packet arriving. Consumers take the most recent value.

Withholding a reading until a terminal status would make a working path contingent on a single packet: a dropped terminal status would mean the user gets nothing where today they get a value. Delivering as it converges cannot lose a reading.

#### Scenario: Each averaged result is delivered

- **WHEN** an averaged run of 3 tests produces its per-test averaged results
- **THEN** a reading is emitted for each
- **AND** the final emission carries the completed average

#### Scenario: A missing terminal status does not lose the reading

- **WHEN** an averaged run produces its averaged results
- **AND** no terminal status ever arrives
- **THEN** the user still has the completed average from the last emission

### Requirement: Completion is signalled separately from the reading

Emitting a reading and declaring the measurement over SHALL be distinct. The measurement-complete signal and the clearing of the measuring state SHALL follow the device's terminal status, not each delivery of a value, so a multi-test run does not report itself finished after its first test. Where no terminal status arrives, the liveness watchdog clears the measuring state as it does for any unresponsive device.

#### Scenario: A multi-test run does not complete after its first test

- **WHEN** an averaged run of 3 tests emits its first averaged result
- **THEN** the measuring state remains true
- **AND** no measurement-complete signal is emitted

#### Scenario: Terminal status completes the run

- **WHEN** the terminal status for an averaged run arrives
- **THEN** the measuring state becomes false
- **AND** the measurement-complete signal is emitted once

#### Scenario: Single test completes on its result, as before

- **WHEN** a single test's result packet arrives
- **THEN** the reading is emitted and the run completes in the same step, unchanged from today

### Requirement: Device-initiated averaged runs are handled identically

The R2's own test-count setting governs measurements started on the device itself, so a run the app never requested can be a multi-test average. Handling of averaged responses SHALL NOT depend on the driver having requested the measurement. A device-initiated averaged run SHALL deliver its reading exactly as an app-initiated one does.

#### Scenario: Averaged run started on the device delivers its reading

- **WHEN** the user starts a multi-test measurement using the control on the R2 itself
- **AND** the app never requested a measurement
- **THEN** the averaged reading is emitted as it converges, as for an app-initiated run
- **AND** it reaches consumers under the same gating as any other reading

#### Scenario: Device-initiated run does not disturb the watchdog

- **WHEN** a device-initiated run is in progress and no app-initiated measurement is outstanding
- **THEN** no liveness watchdog is started or restarted on its account

### Requirement: A multi-test run is kept alive by device progress, not by a fixed deadline

The measurement liveness watchdog exists to recover from a device that stops responding entirely — a case that produces no event and so cannot be detected by events alone. It SHALL NOT double as a bound on how long a legitimate measurement may take. Any device packet indicating the measurement is still progressing — including the status the R2 emits specifically to report that an individual test is running long — SHALL restart the watchdog interval.

#### Scenario: Long averaged run is not aborted

- **WHEN** an averaged run of 5 tests is in progress
- **AND** the device emits progress packets at intervals shorter than the watchdog interval
- **AND** the total run exceeds the watchdog interval
- **THEN** the measurement is not aborted
- **AND** the measuring state remains true until a terminal status arrives

#### Scenario: Slow-test status keeps the run alive

- **WHEN** the device reports that an individual test is running long
- **THEN** the watchdog is restarted
- **AND** no timeout warning is logged
- **AND** the measuring state is unchanged

#### Scenario: Genuinely unresponsive device still recovers

- **WHEN** a measurement has been requested
- **AND** no packet of any kind arrives for longer than the watchdog interval
- **THEN** the measuring state is cleared and a timeout is logged
- **AND** the UI is not left waiting indefinitely

### Requirement: The user can choose an averaged read

The TDS capture affordance SHALL let the user take an averaged reading rather than a single one, and SHALL indicate while a multi-test run is in progress that more than one test is being taken. The choice SHALL default to the existing single-test behaviour, so an untouched installation behaves exactly as before.

#### Scenario: Averaged read from the review page

- **WHEN** the user chooses an averaged read on the post-shot review page
- **THEN** an averaged measurement is requested
- **AND** the resulting averaged TDS populates the review page's TDS field under the existing active-page and threshold gating

#### Scenario: Progress is visible during a multi-test run

- **WHEN** an averaged run of N tests is in progress and the device reports completing test M of N
- **THEN** the interface reflects that a multi-test measurement is underway rather than appearing hung

#### Scenario: Default is unchanged

- **WHEN** a user who has never chosen averaging presses the TDS read control
- **THEN** a single test is performed
