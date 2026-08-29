## Purpose

Gives Decenza one guided, supervised path for correcting the DE1's pressure and temperature sensors against an external instrument — the operation Decent's calibration test profiles instruct the user to perform and that Decenza previously had no way to complete — structured so the machine's own half of the correction is measured by the app rather than typed by the user.

## ADDED Requirements

### Requirement: A guided wizard is the only path to sensor calibration

Decenza SHALL expose exactly one way to change a DE1 sensor's calibration: a guided wizard for that sensor. No settings field, card, popup, MCP tool, or web endpoint SHALL offer to read, write, or reset sensor calibration.

#### Scenario: No second entry point exists
- **WHEN** a user looks anywhere in settings, the MCP surface, or the web surfaces for a calibration value
- **THEN** none is offered, and sensor calibration is reachable only through the wizard

#### Scenario: Existing calibration surfaces are unaffected
- **WHEN** the user opens Settings → Calibration
- **THEN** the Flow Calibration, Weight Stop Timing and Heater Calibration cards are present and behave exactly as before this change

### Requirement: Each sensor is a separate operation on the Calibration settings tab

Settings → Calibration SHALL offer **Pressure Calibration** and **Temperature Calibration** as two separate operations, in a Sensor Calibration card beside the existing Flow Calibration, Weight Stop Timing and Heater Calibration cards, each opening its own guided session for that sensor alone. They belong with the other calibration surfaces, not with machine maintenance.

Their rows SHALL use the same visual grammar as the Maintenance card's guided operations, and that grammar SHALL have one definition shared by both surfaces rather than a copy per card. There SHALL NOT be a combined operation that asks the user to choose a sensor after entering. No operation SHALL be offered for flow.

Each row SHALL state the external instrument its sensor requires before the user enters it, because the two need different equipment and a user may own one and not the other — a pressure-gauge portafilter is a purchase, a thermocouple basket is a fabrication. A user without the instrument SHALL be able to tell that from the Maintenance card without opening the wizard.

Each operation SHALL be disabled, with a stated reason, when no DE1 is connected.

#### Scenario: Two separate operations are listed
- **WHEN** the user opens Settings → Calibration
- **THEN** a Sensor Calibration card lists Pressure Calibration and Temperature Calibration as separate rows
- **AND** no single combined row asks the user to pick a sensor afterwards

#### Scenario: Row format has one definition
- **WHEN** the Maintenance card's operations and the Sensor Calibration rows are rendered
- **THEN** both are produced from the same shared row component

#### Scenario: Required instrument is visible before entering
- **WHEN** the user reads the Temperature Calibration row on the Calibration tab
- **THEN** it states that a basket fitted with a temperature probe is required, without the user having to open it

#### Scenario: One operation calibrates one sensor
- **WHEN** the user opens Pressure Calibration
- **THEN** the session works in bar, uses the pressure test profile, and touches only the pressure calibration
- **AND** it never offers to calibrate temperature

#### Scenario: Flow is not offered
- **WHEN** the user reads the Maintenance card
- **THEN** no flow calibration operation is listed

#### Scenario: Disabled without a machine
- **WHEN** no DE1 is connected
- **THEN** both calibration rows are disabled and state that a connected machine is required

### Requirement: The wizard prepares the machine and states the hardware needed

Before any run, the wizard SHALL state in full the physical preparation its sensor requires — repeating and expanding what the Maintenance row summarised — and SHALL make that sensor's test profile the active profile. It SHALL NOT start the shot itself; the user starts it as they normally would. A user who finds at this step that they lack the instrument SHALL be able to leave without anything having been written.

#### Scenario: Preparation is stated before the run
- **WHEN** the user reaches the prepare step for pressure
- **THEN** the wizard states that a portafilter with a pressure gauge that leaks or flows slowly is required
- **AND** the pressure calibration test profile has been made active

#### Scenario: Leaving before a run changes nothing
- **WHEN** the user reads the prepare step, finds they lack the instrument, and leaves
- **THEN** no calibration value has been written

#### Scenario: The user starts the shot
- **WHEN** the prepare step is complete
- **THEN** the wizard waits for the user to start the shot and does not start it

### Requirement: The machine-reported value is measured by the wizard, never entered

The value sent as the machine's reported reading SHALL be derived by the wizard from live telemetry observed during a run it was watching, and SHALL NOT be typed, defaulted, or taken from a past shot, a profile target, or any stored value. The wizard SHALL derive it only from a run that reached a steady hold; a run that never held steadily SHALL yield no value.

#### Scenario: Value comes from the observed run
- **WHEN** a run completes with a steady hold while the wizard is watching
- **THEN** the wizard reports the machine's own reading over that hold as the reported value

#### Scenario: No steady hold yields no value
- **WHEN** a run never holds steadily
- **THEN** the wizard reports that it could not measure a hold and offers another run
- **AND** no correction can be submitted from that run

#### Scenario: A past shot is not usable
- **WHEN** the user opens the wizard after running the test profile earlier without it
- **THEN** the wizard does not offer that earlier run's values and requires a run of its own

### Requirement: Applying a correction is unavailable until a run has been measured

The step that submits a correction SHALL be unreachable until the wizard holds a measured value from the current session. Before submitting, the wizard SHALL show the machine's reported value, the user's instrument reading, and the resulting correction together, and SHALL require an explicit confirmation.

#### Scenario: Cannot apply before measuring
- **WHEN** the user has not completed a measured run
- **THEN** no control to apply a correction is present

#### Scenario: The pair is shown before writing
- **WHEN** the user has entered their instrument reading
- **THEN** the wizard shows the machine's reading, the entered reading, and the resulting correction, and requires confirmation before writing

### Requirement: Instrument readings are range- and sanity-checked

An instrument reading SHALL be accepted only when it is a finite number inside the sensor's physical range and within that sensor's maximum accepted correction from the machine's measured reading. A refused reading SHALL say which check it failed.

#### Scenario: Unit mix-up is refused
- **WHEN** the machine measured 9.0 bar and the user enters a value far outside a plausible correction, such as a PSI reading
- **THEN** the entry is refused and the wizard says the value is too far from what the machine reported

#### Scenario: Plausible correction is accepted
- **WHEN** the machine measured 9.0 bar and the user enters 8.2
- **THEN** the entry is accepted and the resulting correction is shown for confirmation

### Requirement: Temperature is taken in Celsius without changing the user's preference

Where the wizard shows or accepts a temperature, it SHALL use Celsius and SHALL label the unit explicitly, regardless of the user's temperature-unit preference, and SHALL NOT modify that preference.

#### Scenario: Fahrenheit user calibrates in Celsius
- **WHEN** a user whose preference is Fahrenheit runs a temperature calibration
- **THEN** every temperature in the wizard is shown and entered in Celsius and labelled as such

#### Scenario: Preference survives the wizard
- **WHEN** that user leaves the wizard
- **THEN** temperatures elsewhere in the app are still shown in Fahrenheit

### Requirement: The wizard closes the loop by re-running

After a correction is written, the wizard SHALL re-read the sensor's stored calibration from the machine and SHALL offer another run so the user can confirm the readings now agree. Where a previous cycle exists in the session, the wizard SHALL show how the difference between the machine and the instrument changed.

#### Scenario: Convergence is visible
- **WHEN** the user completes a second cycle
- **THEN** the wizard shows the previous difference and the current one

#### Scenario: Stored calibration is re-read after writing
- **WHEN** a correction has been written
- **THEN** the calibration the wizard displays is the value read back from the machine, not the value it sent

### Requirement: The stored correction is shown; no factory value is claimed

Each wizard SHALL show the correction its sensor currently holds, read from the machine.

It SHALL NOT show a factory value and SHALL NOT offer to restore one. Measured on hardware: the machine answers the read-factory command with the CURRENT stored value, not a distinct factory one — before any write it reported the same number for both, and after a write both moved together. A "Factory" row would therefore mirror the stored row while reading as a safety net that does not exist.

#### Scenario: The stored correction is shown
- **WHEN** the wizard opens with the machine connected
- **THEN** it shows the correction that sensor currently holds, read from the machine

#### Scenario: No factory value is offered
- **WHEN** the user looks for a factory value or a way to return to one
- **THEN** the wizard offers neither

#### Scenario: An unanswered read blocks writing
- **WHEN** the machine does not answer the calibration read
- **THEN** the wizard says the value is unavailable and does not offer to write a correction

### Requirement: The user is told corrections apply gradually

The machine applies a fraction of each requested correction rather than all of it, so agreement takes several runs. The wizard SHALL say so where the user is about to submit and where it invites the next run, so a correct entry that moves the reading only slightly does not read as a failure.

#### Scenario: The entry step says corrections are gradual
- **WHEN** the user has entered a valid instrument reading
- **THEN** the wizard states that the machine applies part of a correction at a time and that several runs are expected

### Requirement: A lost connection or an abandoned run is never treated as success

The wizard SHALL treat a run as measurable only when the machine reached and left the pouring phase for a settled state. A disconnection, a sleep, or a user-stopped run SHALL NOT produce a measured value or a completion.

#### Scenario: Disconnection mid-run
- **WHEN** the DE1 disconnects during a run
- **THEN** the wizard reports the run as incomplete and offers to try again once the machine is back
- **AND** no value from that run can be submitted

#### Scenario: User stops the shot early
- **WHEN** the user stops the run before a steady hold is reached
- **THEN** the wizard reports that no hold was measured and offers another run

### Requirement: Calibration is machine state, not app state

Calibration values SHALL be read from the machine and SHALL NOT be persisted in Decenza settings, included in settings backup or restore, or written to the machine on connect.

#### Scenario: Values survive an app reinstall
- **WHEN** the user reinstalls Decenza and opens the wizard against the same machine
- **THEN** the wizard shows the calibration the machine still holds

#### Scenario: Restore does not write to the machine
- **WHEN** the user restores a settings backup taken from a different machine
- **THEN** no calibration value is written to the connected machine
