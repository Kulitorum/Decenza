## Purpose

Defines when and how the app tells a user which dial-in values in their own profile differ from the bundled
profile whose documented knowledge is being shown to them, so that advice written about the original can be
read against the copy the user actually brews with.

## ADDED Requirements

### Requirement: The dial-in difference block SHALL be gated on shape equality, not on how the knowledge entry was reached

Where the app shows a profile's knowledge entry, it SHALL also show a **dial-in difference block** naming the
bundled profile the entry was authored against and listing the values on which the user's profile differs
from it. The block SHALL be shown only when the user's profile and that bundled profile are the same shape as
defined by the profile-shape-equivalence capability.

The route by which the knowledge entry was reached SHALL NOT affect this. A profile whose title resolved to
the entry and a profile whose shape resolved to it SHALL both show the block when the shape gate is met, and
neither SHALL show it when the gate is not met.

Shape equality is the gate because it is what makes the comparison meaningful and bounded: everything the
shape fixes — frame count, pump mode, sensor, transition, exit condition type and frame durations — is equal
by construction, so only dial-in values can appear in the block.

#### Scenario: A title-resolved in-place edit shows its differences

- **GIVEN** a user profile that resolves to a knowledge entry through a title step
- **AND** it is the same shape as the bundled profile that entry was authored against
- **AND** its brew temperature differs from that bundled profile's
- **WHEN** the user opens its knowledge entry
- **THEN** the block SHALL be shown, naming that bundled profile
- **AND** the temperature difference SHALL be listed

#### Scenario: A title-resolved profile of a different shape shows no block

- **GIVEN** a user profile that resolves to a knowledge entry through a title step
- **AND** its frame structure differs from that of every bundled profile carrying that entry
- **WHEN** the user opens its knowledge entry
- **THEN** no block SHALL be shown
- **AND** the knowledge entry SHALL be presented exactly as it is without this capability

#### Scenario: A bundled profile compared with itself shows no block

- **GIVEN** a bundled profile
- **WHEN** the user opens its knowledge entry
- **THEN** no block SHALL be shown

### Requirement: The block SHALL compare only the values a user changes while dialling in

The fields compared SHALL be exactly those the shape deliberately ignores, and no others:

- target weight, target volume, maximum pressure, minimum pressure, maximum flow, tank preheat
  temperature, brew temperature and recommended dose, at the profile level;
- for each frame in order: its temperature; its active setpoint; its active exit threshold; its exit weight;
  its volume cap; its flow-or-pressure limiter value; and its display name.

A frame's **active setpoint** SHALL be the one its pump mode uses — the pressure setpoint for a
pressure-driven frame, the flow setpoint for a flow-driven frame. The inactive setpoint SHALL NOT be
compared: it carries a value the machine never applies, and reporting it would present a difference the user
cannot feel. A frame's **active exit threshold** SHALL likewise be the single threshold matching that frame's
exit condition type, and SHALL NOT be compared at all for a frame with no exit condition.

Frame popup text, the limiter's control range, profile notes, author, and every field that constitutes the
shape SHALL NOT appear in the block. Frame popup text is excluded on its own ground — it is not part of the
shape and two same-shape profiles may differ in it, but a reworded prompt is not a dialled value. Direct
Setpoint Control frame state is not compared because no writer serializes it, so two loaded profiles cannot
differ on it. The simple-editor scalars are not compared because for the profiles that have them they
restate the frames already compared. The shape fields cannot differ once the gate is met; the limiter range is
a control-loop constant rather than a dialled value; the rest are not dial-in values.

Where one field changes identically on every frame, it SHALL be reported once without a frame number rather
than once per frame. A user who raised the brew temperature of a three-frame profile made one change, and
reading it as three is both longer and less true.

Two values SHALL count as different only when they differ by more than half of the last decimal place their
serialized form preserves, so that a save-and-reload cannot manufacture a difference and one step of an
editor control is always reported.

Values SHALL be shown with their units, and temperatures SHALL follow the unit the user has configured for
temperature display.

#### Scenario: The inactive setpoint is not reported

- **GIVEN** two same-shape profiles whose first frame is pressure-driven
- **AND** their first frames carry different flow setpoint values and equal pressure setpoint values
- **WHEN** the block is produced
- **THEN** that frame SHALL contribute nothing to the block

#### Scenario: Only the matching exit threshold is reported

- **GIVEN** two same-shape profiles whose second frame exits on pressure over a threshold
- **AND** those frames carry different pressure-over thresholds and different flow-under thresholds
- **WHEN** the block is produced
- **THEN** the pressure-over difference SHALL be listed
- **AND** the flow-under difference SHALL NOT be listed

#### Scenario: A change repeated on every frame is reported once

- **GIVEN** two same-shape profiles whose every frame temperature differs by the same amount from the same
  starting value
- **WHEN** the block is produced
- **THEN** one temperature difference SHALL be listed, carrying no frame number

#### Scenario: A renamed frame is reported

- **GIVEN** two same-shape profiles differing only in the display name of one frame
- **WHEN** the block is produced
- **THEN** that rename SHALL be listed

### Requirement: When several bundled profiles share the shape, the block SHALL target the nearest and SHALL abstain on a tie

Where more than one bundled profile is the same shape as the user's profile, the block SHALL be produced
against the **nearest** of them. Nearness SHALL be the count of dial-in fields on which the user's profile
differs from the candidate: the candidate the user differs from on strictly the fewest fields is the nearest.

If no single candidate has strictly the fewest, the outcome SHALL depend on whether the tied candidates
describe the same knowledge entry. Across entries, no block SHALL be shown — naming one would assert a
relationship the comparison did not establish. Within a single entry, the block SHALL be shown and SHALL
name the ENTRY rather than any one of its bundled profiles, because the knowledge presented is the same
whichever was chosen and abstaining would exclude profiles the comparison can describe perfectly well.

A candidate that cannot be loaded SHALL cause the comparison to abstain rather than be skipped: an
incomplete candidate set cannot establish that any member is strictly nearest.

Nearness SHALL NOT be defined by how far apart the values are. A magnitude comparison would need a weighting
between bar, millilitres per second, degrees and grams that nothing in the domain supplies, and that
weighting would silently decide the outcome. Counting the fields that differ needs no such weighting, and it
is the same count the block itself is built from.

Selection SHALL target a bundled **profile**, never a knowledge entry: one entry can be authored against
several bundled profiles with different dial-in values, so a distance to an entry is not defined.

Where a block is shown and other same-shape bundled profiles exist, the surface SHALL continue to disclose
that the shape matched more than one profile, so the chosen base does not read as the only match.

#### Scenario: A clearly nearer candidate is chosen

- **GIVEN** a user profile the same shape as two bundled profiles
- **AND** it differs from the first on fewer dial-in fields than from the second
- **WHEN** the block is produced
- **THEN** it SHALL name the first bundled profile
- **AND** it SHALL list the user's differences from that profile

#### Scenario: A tie within one knowledge entry names the entry

- **GIVEN** a user profile the same shape as several bundled profiles that all resolve to one knowledge entry
- **AND** it differs from more than one of them on the same number of dial-in fields
- **WHEN** the block is produced
- **THEN** it SHALL be shown
- **AND** it SHALL name the knowledge entry rather than any one bundled profile

#### Scenario: A tie across entries produces no block

- **GIVEN** a user profile the same shape as two bundled profiles
- **AND** the two resolve to different knowledge entries
- **AND** it differs from each on the same number of dial-in fields
- **WHEN** the knowledge entry is opened
- **THEN** no block SHALL be shown
- **AND** the knowledge entry SHALL still be presented

### Requirement: Each surface SHALL compare against the profile that surface is about

The block SHALL be produced from the profile the user is looking at, not from whichever copy is most easily
reached:

- on a profile-browsing surface, from the profile as it currently exists in the user's catalog;
- on a surface describing a **shot**, from the profile stored with that shot.

A shot SHALL NOT report differences that exist only because the catalog profile was edited after the shot was
pulled.

#### Scenario: A shot reports the profile it was pulled with

- **GIVEN** a shot taken with a user profile
- **AND** that catalog profile's temperature has since been changed
- **WHEN** the block is shown on a surface describing that shot
- **THEN** the values compared SHALL be those stored with the shot
- **AND** the later catalog edit SHALL NOT appear as a difference

### Requirement: An identical copy SHALL be stated, not rendered as an empty block

Where the shape gate is met and a base is selected but no compared field differs, the surface SHALL state
that the profile is an unchanged copy of the named bundled profile, rather than showing an empty block or
omitting the block silently. A user who renamed a bundled profile and changed nothing needs to be told that
the knowledge shown applies without qualification.

#### Scenario: A renamed but unmodified copy says so

- **GIVEN** a user profile that is a copy of a bundled profile under a different name, with no dial-in value
  changed
- **WHEN** the user opens its knowledge entry
- **THEN** the surface SHALL state that it is an unchanged copy of that bundled profile
- **AND** no list of differences SHALL be shown
