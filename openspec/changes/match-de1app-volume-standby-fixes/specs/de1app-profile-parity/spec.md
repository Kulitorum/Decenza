## ADDED Requirements

### Requirement: A generated Pressure profile's forced-rise ramp is excluded from Stop-at-Volume

For a Pressure-type profile, the frame generator produces a "forced rise without limit" frame
to bring the group to the target pressure before the Hold stage begins, and — when a Decline
stage follows a nonzero-duration Hold — a second such frame before Decline. These frames deliver
water to fill machine headspace and pressurize the puck; no coffee has begun pouring into the cup.

The profile's preinfusion frame count SHALL include every such forced-rise frame, so the DE1
reports them under the Preinfusion substate rather than Pouring. A shot using Stop-at-Volume
SHALL NOT count any flow delivered while the substate is Preinfusion toward its target volume.

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

#### Scenario: Flow-type profiles are unaffected

- **WHEN** a Flow-type profile is generated, which produces no forced-rise frame
- **THEN** its preinfusion frame count and Stop-at-Volume behavior are unchanged by this
  requirement
