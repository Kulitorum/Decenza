# Spec Delta: de1app-profile-parity

## ADDED Requirements

### Requirement: A pressure step always carries a flow limit

When a profile becomes the current profile, every step whose pump is `pressure` and whose
limiter value is absent or `<= 0` SHALL be given the default pressure flow limit of 8 mL/s,
with a limiter range taken from the profile's own default range. A basic Pressure profile
(`settings_2a`), whose single flow-limit knob is the scalar `maximum_flow`, SHALL have that
scalar defaulted the same way before its frames are generated.

This matches de1app, which applies the same cap in `select_profile` and at startup so that a
profile pours the same way on a DE1 and on a higher-flow machine.

The change SHALL be in memory only: it is visible in the profile editor and is persisted only
if the user saves the profile. Profile files on disk — shipped, downloaded, or user-created —
SHALL NOT be rewritten by the normalization itself.

#### Scenario: Shipped profile with an unlimited pressure step

- **WHEN** a profile whose "rise and hold" step has `limiter.value` of `0` is loaded
- **THEN** that step's limiter value is 8 mL/s and its range is the profile's default range
- **AND** the profile's file on disk still reads `0`

#### Scenario: An explicit limit is left alone

- **WHEN** a pressure step already carries a limiter value of `2.5`
- **THEN** it stays `2.5`

#### Scenario: Flow steps are untouched

- **WHEN** a step's pump is `flow`, where the limiter is a PRESSURE limit and "off" remains legal
- **THEN** its limiter value is unchanged, including when it is `0`

#### Scenario: Basic Pressure profile with no flow limit

- **WHEN** a `settings_2a` profile with `maximum_flow` of `0` is loaded
- **THEN** `maximum_flow` reads 8 mL/s and the generated hold, decline and forced-rise frames
  all carry that limiter

### Requirement: The editor cannot turn a pressure step's flow limit off

An editor control bound to a pressure step's flow limit SHALL floor at 0.1 mL/s and SHALL NOT
offer or display an "off" state. Switching a step's pump from `flow` to `pressure` reinterprets
the limiter as a flow limit, so a value of 0 carried over from the flow step SHALL be replaced
with the default pressure flow limit.

A flow step's limiter is a PRESSURE limit and MAY still be off; this requirement does not apply
to it.

Flow goals and flow limits SHALL be settable up to 20 mL/s, so that a profile authored for a
higher-flow machine can be opened and saved here without its values being clamped. The DE1 runs
at its own maximum when a profile asks for more than it can deliver.

#### Scenario: A pressure step's flow limit is turned down

- **WHEN** the user holds the minus button on a pressure step's flow limit
- **THEN** the value stops at 0.1 mL/s and never reads "off"

#### Scenario: A flow step is switched to pressure

- **WHEN** a flow step whose pressure limit is off is switched to a pressure step
- **THEN** its flow limit reads the default 8 mL/s rather than off

#### Scenario: A high-flow profile round-trips

- **WHEN** a profile with a 12 mL/s flow goal is opened in the editor and saved unchanged
- **THEN** the saved profile still reads 12 mL/s

## MODIFIED Requirements

### Requirement: A generated Pressure profile's forced-rise ramp is excluded from Stop-at-Volume

For a Pressure-type profile, the frame generator produces a forced-rise frame to bring the group
to the target pressure before the Hold stage begins, and — when a Decline stage follows a
nonzero-duration Hold — a second such frame before Decline. These frames deliver water to fill
machine headspace and pressurize the puck; no coffee has begun pouring into the cup.

The generator SHALL name these frames `"forced rise"` and SHALL attach the profile's flow limiter
to them, matching de1app, which stopped leaving the rise unlimited so that a high-flow machine
cannot push unbounded flow during the ramp. The historical name `"forced rise without limit"`
SHALL continue to be recognised wherever a forced-rise frame is identified by name, so profiles
authored before this change keep their behaviour.

The profile's preinfusion frame count SHALL include every forced-rise frame under either name, so
the DE1 reports them under the Preinfusion substate rather than Pouring. A shot using
Stop-at-Volume SHALL NOT count any flow delivered while the substate is Preinfusion toward its
target volume.

This does not apply to Flow-type profiles, which generate no forced-rise frame and are already
unaffected — nor to a de1app-imported Advanced profile carrying a stored, unregenerated frame
list, which is out of scope for this requirement (see "Simple profiles derive frames from their
scalars").

#### Scenario: 40 mL Stop-at-Volume on a Pressure profile with a forced-rise stage

- **WHEN** a Pressure-type profile with a Hold stage generates a forced-rise frame ahead of it,
  and a shot runs with Stop-at-Volume set to 40 mL
- **THEN** the water delivered during the forced-rise frame is not counted toward the 40 mL
- **AND** the shot stops once 40 mL has been delivered after pouring begins, not 40 mL after the
  rise frame started

#### Scenario: Hold-then-Decline profile counts both forced-rise frames

- **WHEN** a Pressure-type profile generates a forced-rise frame before Hold and a second before
  Decline
- **THEN** the preinfusion frame count includes both frames

#### Scenario: A profile carrying the historical frame name still counts it

- **WHEN** a profile whose stored frame is named `"forced rise without limit"` is loaded
- **THEN** that frame is counted as a forced-rise frame

#### Scenario: Flow-type profiles are unaffected

- **WHEN** a Flow-type profile is generated, which produces no forced-rise frame
- **THEN** its preinfusion frame count and Stop-at-Volume behavior are unchanged by this
  requirement
