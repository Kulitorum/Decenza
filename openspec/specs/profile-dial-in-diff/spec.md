# profile-dial-in-diff Specification

## Purpose
Defines when and how the app tells a user which dial-in values in their own profile differ from the bundled
profile whose documented knowledge is being shown to them, so that advice written about the original can be
read against the copy the user actually brews with.
## Requirements
### Requirement: The dial-in difference block SHALL be gated on shape equality, not on how the knowledge entry was reached

Where the app shows a profile's knowledge entry, it SHALL also show a dial-in difference block naming the bundled profile the entry was authored against and listing the user's differing dial-in values. The block SHALL be shown only when the user's profile and that bundled profile are the same shape, as defined by the profile-shape-equivalence capability. The route by which the entry was reached SHALL NOT affect this gate.

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

The block SHALL compare exactly the dial-in fields the shape ignores: at profile level, target weight, target volume, maximum and minimum pressure, maximum flow, tank preheat temperature, brew temperature and recommended dose; and per frame in order, its temperature, active setpoint, active exit threshold, exit weight, volume cap, limiter value and display name. No other field SHALL be compared.

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

### Requirement: Frame text and shape fields SHALL NOT appear in the block

Frame popup text, the limiter's control range, profile notes, author and every shape field SHALL NOT appear in the block. Direct Setpoint Control frame state and the simple-editor scalars SHALL NOT be compared.

#### Scenario: Profile notes are not reported

- **GIVEN** two same-shape profiles differing only in their profile notes
- **WHEN** the block is produced
- **THEN** the notes difference SHALL NOT be listed

### Requirement: Repeated changes SHALL be reported once, within tolerance

A change identical on every frame SHALL be reported once, without a frame number. Two values SHALL count as different only when they differ by more than half the last decimal place their serialized form preserves. Values SHALL be shown with units, and temperatures in the user's configured unit.

#### Scenario: A save-and-reload difference is not reported

- **GIVEN** two same-shape profiles whose values differ only below the precision their serialized form preserves
- **WHEN** the block is produced
- **THEN** no difference SHALL be listed for that value

### Requirement: Only the active setpoint and matching exit threshold SHALL be compared

A frame's active setpoint SHALL be the one its pump mode uses: the pressure setpoint for a pressure-driven frame, the flow setpoint for a flow-driven frame. Its active exit threshold SHALL be the one matching its exit condition type. The inactive values SHALL NOT be compared.

#### Scenario: A frame with no exit condition compares no threshold

- **GIVEN** two same-shape profiles whose frame has no exit condition
- **WHEN** the block is produced
- **THEN** no exit threshold for that frame SHALL be listed

### Requirement: When several bundled profiles share the shape, the block SHALL target the nearest and SHALL abstain on a tie unless the tied candidates agree

Where several bundled profiles share the shape, the block SHALL be produced against the nearest: the candidate the user differs from on strictly the fewest dial-in fields. Nearness SHALL be a count of differing fields, never a magnitude. Selection SHALL target a bundled profile, never a knowledge entry.

#### Scenario: A clearly nearer candidate is chosen

- **GIVEN** a user profile the same shape as two bundled profiles
- **AND** it differs from the first on fewer dial-in fields than from the second
- **WHEN** the block is produced
- **THEN** it SHALL name the first bundled profile
- **AND** it SHALL list the user's differences from that profile

#### Scenario: A tie between candidates that say the same thing names the entry

- **GIVEN** a user profile the same shape as several bundled profiles that all resolve to one knowledge entry
- **AND** it differs from more than one of them on the same number of dial-in fields
- **AND** those tied candidates produce equivalent difference lists
- **WHEN** the block is produced
- **THEN** it SHALL be shown
- **AND** it SHALL name the knowledge entry rather than any one bundled profile

#### Scenario: A tie within one entry on different values produces no block

- **GIVEN** a user profile the same shape as several bundled profiles that all resolve to one knowledge entry
- **AND** it differs from each on the same number of dial-in fields
- **AND** the candidates disagree with each other on those fields' values
- **WHEN** the knowledge entry is opened
- **THEN** no block SHALL be shown
- **AND** the knowledge entry SHALL still be presented

#### Scenario: A tie across entries produces no block

- **GIVEN** a user profile the same shape as two bundled profiles
- **AND** the two resolve to different knowledge entries
- **AND** it differs from each on the same number of dial-in fields
- **WHEN** the knowledge entry is opened
- **THEN** no block SHALL be shown
- **AND** the knowledge entry SHALL still be presented

### Requirement: Multiple same-shape matches SHALL be disclosed

Where a block is shown and other same-shape bundled profiles exist, the surface SHALL disclose that the shape matched more than one profile.

#### Scenario: Other matches are disclosed

- **GIVEN** a block is shown against one bundled profile and another same-shape bundled profile exists
- **WHEN** the surface is shown
- **THEN** it SHALL disclose that the shape matched more than one profile

### Requirement: A tie SHALL produce a block only when tied candidates agree

If no single candidate is strictly nearest, the block SHALL be shown only when every tied candidate resolves to the same knowledge entry and produces equivalent difference lists, and it SHALL then name the entry. Otherwise no block SHALL be shown. A candidate that cannot be loaded SHALL make the comparison abstain.

#### Scenario: A candidate that cannot be loaded makes the comparison abstain

- **GIVEN** a same-shape candidate set in which one candidate cannot be loaded
- **WHEN** the block is produced
- **THEN** no block SHALL be shown

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

