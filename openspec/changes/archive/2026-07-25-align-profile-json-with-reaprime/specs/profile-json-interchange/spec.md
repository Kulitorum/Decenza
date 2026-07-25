## ADDED Requirements

### Requirement: Single canonical profile serializer

Decenza SHALL serialize a profile to JSON through exactly one canonical code path (`Profile::toJson`). Any other producer of profile JSON — including the Visualizer upload payload — SHALL derive its output from that path rather than re-listing profile fields independently.

#### Scenario: Visualizer upload delegates to the canonical serializer

- **WHEN** the Visualizer upload payload for a shot is built
- **THEN** the embedded `profile` object is produced by the canonical serializer plus only Visualizer-specific additions
- **AND** no profile field is emitted by an independent second code path

#### Scenario: A newly added profile field appears in every output

- **WHEN** a new profile field is added to the canonical serializer
- **THEN** it appears in both the on-disk/exported profile and the Visualizer upload payload without a separate edit to the upload builder

### Requirement: Ecosystem-required keys are always present

Every profile Decenza emits SHALL include `tank_temperature` and `target_volume_count_start`, in addition to Decenza's existing `tank_desired_water_temperature` and `number_of_preinfuse_frames`. Their values SHALL equal the corresponding existing keys.

#### Scenario: Exported profile carries the required keys

- **WHEN** a profile is exported, shared, or written to disk
- **THEN** the JSON contains `tank_temperature` equal to `tank_desired_water_temperature`
- **AND** the JSON contains `target_volume_count_start` equal to `number_of_preinfuse_frames`

#### Scenario: reaprime accepts the exported profile

- **WHEN** a Decenza-exported profile is parsed by reaprime's `Profile.fromJson`
- **THEN** parsing succeeds without a missing-required-field error

### Requirement: Standard DE1 metadata keys are emitted

Every profile Decenza emits SHALL include the standard DE1 v2 metadata keys `type`, `lang`, `hidden`, `reference_file`, and `changes_since_last_espresso`, with `type` derived from the profile type (`settings_2a` → `pressure`, `settings_2b` → `flow`, otherwise `advanced`).

#### Scenario: Metadata keys present with derived type

- **WHEN** a `settings_2a` profile is serialized
- **THEN** the JSON contains `"type": "pressure"` and the keys `lang`, `hidden`, `reference_file`, `changes_since_last_espresso`

### Requirement: Numeric fields are string-encoded

Decenza SHALL serialize numeric step fields and numeric profile-level fields as JSON strings (e.g. `"9.0"`), matching the de1app / tablet / Visualizer / reaprime convention. Decenza SHALL continue to parse both string- and number-encoded values on import.

#### Scenario: Step values serialize as strings

- **WHEN** a profile step with pressure 9.0 is serialized
- **THEN** the JSON encodes `"pressure": "9.0"` rather than `"pressure": 9.0`

#### Scenario: Round-trip is lossless in either encoding

- **WHEN** a profile is serialized and re-parsed
- **THEN** the re-parsed profile is functionally equal to the original
- **AND** a profile authored with number-encoded values still parses correctly

### Requirement: Emitted profiles always have a non-empty steps array

Any profile file Decenza emits SHALL contain a non-empty `steps` array. Simple `settings_2a`/`settings_2b` profiles that carry no explicit frames SHALL have their frames materialized before serialization.

#### Scenario: Simple profile is expanded on export

- **WHEN** a `settings_2a` profile with no explicit steps is exported
- **THEN** the emitted JSON contains explicit generated frames
- **AND** reaprime parses it as a valid, runnable profile

#### Scenario: Weight exit omitted when zero

- **WHEN** a step has no weight-exit (weight 0)
- **THEN** the `weight` key is omitted from that step rather than emitted as `"0.0"`

### Requirement: Built-in profiles pass a reaprime-readability check

The built-in profile set SHALL be regenerated in the canonical format, and a lint SHALL verify that every emitted built-in satisfies reaprime's contract: required keys present, `steps` non-empty, and enum-valued fields (`pump`, `sensor`, `transition`, exit `type`/`condition`) within reaprime's accepted vocabulary.

#### Scenario: Lint fails on a non-conforming built-in

- **WHEN** a built-in profile is missing `tank_temperature`, has empty `steps`, or uses an out-of-vocabulary enum value
- **THEN** the readability lint reports it as a failure

#### Scenario: Lint passes on the regenerated set

- **WHEN** the lint runs over the regenerated built-in profiles
- **THEN** every built-in passes
