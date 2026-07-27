## ADDED Requirements

### Requirement: The driver can request an averaged reading over multiple tests

The DiFluid R2 driver SHALL be able to request an averaged measurement (Device Action, Cmd 1) carrying a test count, in addition to the existing single test (Device Action, Cmd 0). The test count SHALL be clamped to the device's supported range of 1–10 before transmission. The averaged result arrives as pack 3 and SHALL reach consumers through the same `tdsChanged` path, and the same out-of-range plausibility gate, as a single reading — averaging changes how a number was produced, not what is allowed to escape the driver.

#### Scenario: Averaged measurement requested

- **WHEN** an averaged measurement of 3 tests is requested
- **THEN** the driver writes a Device Action Cmd 1 packet whose single data byte is 3, with a valid additive checksum
- **AND** the driver enters the measuring state

#### Scenario: Test count outside the device range is clamped

- **WHEN** an averaged measurement is requested with a count of 0, or of 25
- **THEN** the transmitted count is 1, or 10, respectively
- **AND** no packet carrying an out-of-range count is ever written

#### Scenario: Averaged result is gated identically to a single reading

- **WHEN** a pack 3 averaged result arrives carrying a concentration above the plausible-TDS ceiling
- **THEN** it is rejected and an error is surfaced, exactly as the same value in a pack 2 single result would be
- **AND** no `tdsChanged` is emitted

#### Scenario: Single test remains the default

- **WHEN** a measurement is requested without specifying averaging
- **THEN** the driver sends a single test (Device Action Cmd 0), preserving today's behaviour

### Requirement: A multi-test run is kept alive by device progress, not by a fixed deadline

The measurement liveness watchdog exists to recover from a device that stops responding entirely — a case that produces no event and so cannot be detected by events alone. It SHALL NOT double as a bound on how long a legitimate measurement may take. Any device packet indicating the measurement is still progressing — including status 5 (Average Test Ongoing) and status 10, which the R2 emits precisely to report that an individual test is taking a long time — SHALL restart the watchdog interval.

#### Scenario: Long averaged run is not aborted

- **WHEN** an averaged run of 5 tests is in progress
- **AND** the device emits progress packets at intervals shorter than the watchdog interval
- **AND** the total run exceeds the watchdog interval
- **THEN** the measurement is not aborted
- **AND** the measuring state remains true until the averaged result or a terminal status arrives

#### Scenario: Slow-test status keeps the run alive

- **WHEN** the device emits status 10 during an averaged run
- **THEN** the watchdog is restarted
- **AND** no timeout warning is logged
- **AND** the measuring state is unchanged

#### Scenario: Genuinely unresponsive device still recovers

- **WHEN** a measurement has been requested
- **AND** no packet of any kind arrives for longer than the watchdog interval
- **THEN** the measuring state is cleared and a timeout is logged
- **AND** the UI is not left waiting indefinitely

#### Scenario: Terminal status ends the run

- **WHEN** status 6 (Average Test Finished) arrives after the averaged result
- **THEN** the watchdog is stopped
- **AND** the measuring state is false

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
