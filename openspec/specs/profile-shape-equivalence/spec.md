# profile-shape-equivalence Specification

## Purpose
Defines when two extraction profiles count as the same shape — which frame properties constitute the shape
and which are dial-in variables that may differ freely — so that knowledge authored against one profile can
be applied to a user's derivative of it without depending on what the user named it.
## Requirements
### Requirement: Profile shape SHALL be defined by structure and frame durations, never by magnitudes

A profile's **shape** SHALL be the ordered tuple of: total frame count, the preinfuse frame count, the
beverage type, and for each frame in order its pump mode, sensor, transition, exit condition type, exit
condition direction, and duration in seconds.

The following frame properties SHALL NOT be part of the shape: temperature, pressure setpoint, flow
setpoint, volume, exit weight, exit threshold values, the flow/pressure limiter value and range, and the
frame's display name or popup text. These are the values a user changes while dialling in a coffee, and none
of them alters whether a suppression claim authored against the shape still holds.

Two profiles SHALL be *the same shape* when their shapes as defined above are equal, with durations compared
at a tolerance of 0.1 s to absorb serialization rounding. Shape comparison SHALL be a total, deterministic,
order-independent boolean predicate over the two profiles alone. It SHALL NOT use a distance metric, a
similarity score, a threshold, or any tunable constant.

#### Scenario: A dial-in derivative is the same shape

- **GIVEN** a profile derived from a documented profile whose only differences are per-frame temperature,
  pressure setpoint, flow setpoint, target volume, exit weight, or limiter values
- **WHEN** the two are compared for shape
- **THEN** they SHALL be the same shape, regardless of either profile's title

#### Scenario: A structural edit is a different shape

- **GIVEN** a profile that differs from a documented profile by adding or removing a frame, changing a
  frame's pump mode or sensor, changing a transition, or adding/removing/retyping an exit condition
- **WHEN** the two are compared for shape
- **THEN** they SHALL NOT be the same shape

#### Scenario: Frame durations are part of the shape

- **GIVEN** two profiles identical in structure but whose corresponding frames differ in duration by more
  than 0.1 s
- **WHEN** the two are compared for shape
- **THEN** they SHALL NOT be the same shape

#### Scenario: Duration comparison tolerates serialization rounding

- **GIVEN** two profiles whose corresponding frame durations differ by no more than 0.1 s and are otherwise
  structurally identical
- **WHEN** the two are compared for shape
- **THEN** they SHALL be the same shape

#### Scenario: Beverage type separates shapes

- **GIVEN** two profiles with identical frame structure and durations but different beverage types
- **WHEN** the two are compared for shape
- **THEN** they SHALL NOT be the same shape

#### Scenario: Shape comparison is order-independent and total

- **GIVEN** any two profiles
- **WHEN** they are compared for shape in either argument order
- **THEN** the result SHALL be identical, and SHALL depend on no state outside the two profiles

### Requirement: The shape predicate SHALL share one traversal with the existing exact-equality predicate

The shape predicate and the pre-existing exact profile-equality predicate used for import de-duplication
SHALL be expressed over a single field-by-field traversal of the frame list, differing only in which fields
each one consults. There SHALL NOT be two independent copies of the frame comparison logic.

Exact profile equality SHALL retain its current behaviour: a change to the shape predicate SHALL NOT alter
whether two profiles are considered identical for import de-duplication purposes.

#### Scenario: Import de-duplication behaviour is unchanged

- **GIVEN** any pair of profiles for which the import de-duplication check currently returns a given result
- **WHEN** the same pair is checked after the shape predicate is introduced
- **THEN** the de-duplication result SHALL be unchanged

#### Scenario: Same shape is implied by exact equality

- **GIVEN** two profiles that the exact-equality predicate reports as identical
- **WHEN** they are compared for shape
- **THEN** they SHALL be the same shape

