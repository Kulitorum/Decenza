# refractometer-device-diagnostics Specification

## Purpose
TBD - created by archiving change expand-difluid-r2-protocol. Update Purpose after archive.
## Requirements
### Requirement: Every device status code is logged by name

The driver SHALL log R2 test status codes by their documented meaning across the full range the device emits (0–12), not as bare integers. A status the driver does not recognise SHALL still be logged, with its numeric value, rather than silently discarded.

This exists because field logs are read by people and by AI assistants triaging a user's problem through MCP. `Status: 5` communicates nothing; "Average Test Ongoing" tells the reader the device is healthy and working.

#### Scenario: Averaging statuses are named

- **WHEN** the device emits status 4, 5, 6, or 10 during a run
- **THEN** each is logged as its documented meaning — Average Test Started, Average Test Ongoing, Average Test Finished, and the long-test variant of Average Test Ongoing respectively

#### Scenario: Calibration statuses are named

- **WHEN** the device emits status 12 and later status 1
- **THEN** they are logged as Calibration Started and Calibration Finished

#### Scenario: Unknown status is still reported

- **WHEN** the device emits a status code the driver has no name for
- **THEN** the numeric value is logged
- **AND** no measurement state is changed on account of it

### Requirement: Every documented error code is reported by name

The driver SHALL name all documented R2 error codes: class 2 (General) codes 1 Test Error, 2 Calibration Failed, 3 No Liquid, and 4 Beyond Range; and class 3 (Hardware), whose code is the number shown on the device's own screen and SHALL be included in the message so a user can match it to what they are looking at.

Naming a code is distinct from surfacing it to the user. The existing division SHALL be preserved: only user-actionable measurement failures raise a user-visible error; everything else is logged, because the R2 also emits benign class/code combinations around a successful read and surfacing those spams the error dialog with nothing useful.

#### Scenario: Hardware error names the on-screen code

- **WHEN** an error packet arrives with class 3 and code 7
- **THEN** the log identifies it as a hardware error and states the code shown on the device screen is 7

#### Scenario: Newly named codes do not become new dialogs

- **WHEN** an error packet arrives with class 2 code 1 (Test Error)
- **THEN** it is logged by name
- **AND** whether a user-visible error is raised follows the existing user-actionable division, unchanged by this capability

#### Scenario: Measurement state is cleared on any error

- **WHEN** any error packet arrives while a measurement is in flight
- **THEN** the measuring state is cleared so the interface does not hang

### Requirement: The device serial number is reassembled and recorded

The R2 transmits its serial number as three packets, each carrying a part index in Data0 followed by five bytes of serial data. The driver SHALL reassemble these into the complete serial number regardless of the order the packets arrive in, and record it alongside the model and firmware strings it already captures.

This is identification data, not measurement data: it supports telling a genuine R2 Extract from a Brix-reporting variant or rebrand, where the model string alone has proven insufficient.

#### Scenario: Serial number assembled from three packets

- **WHEN** the three serial-number packets have all arrived
- **THEN** the driver holds the complete serial number as a single string
- **AND** logs it once complete

#### Scenario: Packets arriving out of order

- **WHEN** the part-index-2 packet arrives before the part-index-0 packet
- **THEN** each part is placed at its indicated offset
- **AND** the assembled serial number is identical to the in-order case

#### Scenario: Incomplete serial number is not reported as complete

- **WHEN** only some of the serial-number packets have arrived
- **THEN** no partial serial number is logged as if it were the device's identity

### Requirement: A multi-reading run started by the device ends once, not once per reading

The R2 re-measures a single sample repeatedly when the prism is not thermally settled, ending with its own terminal status. Each reading SHALL be delivered — the settled value is the last one, and latest-wins leaves it in place — but the run SHALL be declared complete only on the terminal status, not on each reading.

Treating each reading as a completed measurement was observed on hardware to fire completion five times across a 16-second run, and would let a value saved mid-run persist a reading the device had already superseded.

#### Scenario: Each reading of a settling run is delivered

- **WHEN** the device performs a settling run that reports several readings
- **THEN** each reading reaches consumers
- **AND** the last one is the value that remains

#### Scenario: A settling run completes once

- **WHEN** a settling run reports several readings and then its terminal status
- **THEN** the measurement-complete signal is emitted exactly once
- **AND** the measuring state remains true until that terminal status

#### Scenario: A single test is unaffected

- **WHEN** an ordinary single test reports its result
- **THEN** it completes on that result, as it did before settling runs were handled

### Requirement: Device-initiated measurement can be enabled

The driver SHALL be able to set the R2's Auto Test setting, which makes the device start a measurement on its own when it detects a prism or sample-tank temperature change — so a reading arrives when the sample is loaded, with no button press. Readings produced this way are device-initiated and SHALL be subject to exactly the same consumer-side gating as a button-initiated reading.

#### Scenario: Auto Test enabled

- **WHEN** Auto Test is enabled
- **THEN** the driver writes the corresponding Device Settings command
- **AND** the device's echoed confirmation of the setting is logged

#### Scenario: Device-initiated reading is gated normally

- **WHEN** Auto Test is on and the device starts and completes a measurement with no request from the app
- **THEN** the resulting reading flows through the same plausibility gate and the same active-page gating as any other reading
- **AND** it does not populate any field that a button-initiated reading would not

