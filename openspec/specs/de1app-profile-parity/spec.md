# de1app-profile-parity Specification

## Purpose
TBD - created by archiving change fix-de1app-profile-drift. Update Purpose after archive.

## Requirements

### Requirement: Profile-level scalars survive a de1app Tcl import

`Profile::loadFromTclString` SHALL read every profile-level scalar de1app writes and store it
on the profile, so that re-serializing an imported profile reproduces the source values
rather than Decenza's defaults. Reading these scalars MUST NOT be conditional on the profile
carrying an empty `advanced_shot`, and MUST NOT be conditional on `settings_profile_type` —
de1app writes the full scalar set on every profile regardless of type.

The scalars are: `espresso_pressure`, `espresso_hold_time`, `espresso_decline_time`,
`pressure_end`, `preinfusion_time`, `preinfusion_flow_rate`, `preinfusion_stop_pressure`,
`flow_profile_hold`, `flow_profile_hold_time`, `flow_profile_decline`,
`flow_profile_decline_time`, `flow_profile_preinfusion`, `flow_profile_preinfusion_time`,
`flow_profile_minimum_pressure`, `maximum_flow`, `maximum_pressure`,
`maximum_flow_range_default`, `maximum_pressure_range_default`, `espresso_temperature`,
`espresso_temperature_0..3`, `espresso_temperature_steps_enabled`,
`tank_desired_water_temperature`, `profile_grinder_dose_weight`, and the target weight/volume
fields covered below.

`profile_grinder_dose_weight` is de1app's per-profile dose. It SHALL be read into Decenza's
`recommended_dose`, enabling `has_recommended_dose` only when the imported value is greater than
zero, so that a dose a de1app user set on a profile survives the import instead of being replaced
by Decenza's default. It has no absent-value substitute: a profile that omits the key SHALL be
left at Decenza's default with no recommendation enabled, because supplying one would make every
built-in fail the de1app drift comparison, which walks this same table.

Unlike the other scalars, this one has no `.tcl` output side — Decenza writes no Tcl — so
"reproduces the source values" applies to it on import only.

#### Scenario: Simple profile that also carries a stored advanced_shot

- **WHEN** a `settings_2a` `.tcl` with a non-empty `advanced_shot` and `espresso_pressure 7.8`
  is imported
- **THEN** the resulting profile's `espresso_pressure` is `7.8`, not the `9.2` default

#### Scenario: Advanced profile carrying simple-profile scalars

- **WHEN** a `settings_2c` `.tcl` with `flow_profile_decline 1.0` is imported
- **THEN** the resulting profile's `flow_profile_decline` is `1.0`, not the `1.2` default

#### Scenario: Round-trip through the canonical serializer

- **WHEN** any profile in `tests/data/de1app_profiles/` is imported and re-serialized
- **THEN** every scalar listed in this requirement equals the value in the `.tcl` source,
  after applying the type-dependent selection rule below

#### Scenario: A de1app per-profile dose is preserved

- **WHEN** a `.tcl` assigning `profile_grinder_dose_weight 20.5` is imported
- **THEN** the profile's recommended dose is `20.5` and its recommendation is enabled

#### Scenario: A zero or absent per-profile dose enables nothing

- **WHEN** a `.tcl` assigning `profile_grinder_dose_weight 0`, or omitting it, is imported
- **THEN** no recommendation is enabled and the profile keeps the default dose

#### Scenario: The built-in drift gate still passes

- **WHEN** the built-in corpus is compared against its de1app sources, none of which carries the
  key
- **THEN** no profile is reported as drifted on account of the dose

### Requirement: Dual-spelled fields resolve by profile type

Four de1app fields exist in both a plain/`_default` and an `_advanced` spelling. The import
SHALL select the authoritative spelling from `settings_profile_type`, matching de1app's own
dispatch: `pressure_to_advanced_list` (`settings_2a`) and `flow_to_advanced_list`
(`settings_2b`) overwrite the `_advanced` fields from their counterparts before conversion,
while `settings_to_advanced_list` (`settings_2c`, `settings_2c2`) does not.

| Canonical key | `settings_2a` / `2b` reads | `settings_2c` / `2c2` reads |
|---|---|---|
| `target_weight` | `final_desired_shot_weight` | `final_desired_shot_weight_advanced` |
| `target_volume` | `final_desired_shot_volume` | `final_desired_shot_volume_advanced` |
| `maximum_pressure_range_advanced` | `maximum_pressure_range_default` | `maximum_pressure_range_advanced` |
| `maximum_flow_range_advanced` | `maximum_flow_range_default` | `maximum_flow_range_advanced` |

#### Scenario: Simple profile with conflicting spellings

- **WHEN** a `settings_2a` `.tcl` has `maximum_pressure_range_default 0.9` and
  `maximum_pressure_range_advanced 0.6`
- **THEN** the imported profile's `maximum_pressure_range_advanced` is `0.9`

#### Scenario: Advanced profile with conflicting spellings

- **WHEN** a `settings_2c` `.tcl` has `final_desired_shot_weight 0` and
  `final_desired_shot_weight_advanced 36`
- **THEN** the imported profile's `target_weight` is `36`

### Requirement: de1app's hidden flag is preserved

The import SHALL map de1app's `profile_hide` to the canonical `hidden` key. Absence of
`profile_hide` continues to mean not hidden.

This flag does not drive Decenza's own profile list, which filters through
`SettingsApp::isHiddenProfile()` — a separate per-user list keyed by filename. The
requirement exists because de1app and Decaid read the profile field, and a Decenza-written
file that drops it makes hidden profiles reappear in those apps.

#### Scenario: Hidden de1app profile

- **WHEN** a `.tcl` containing `profile_hide 1` is imported and re-serialized
- **THEN** the emitted JSON has `"hidden": "1"`

#### Scenario: Decenza's own list is unaffected

- **WHEN** a built-in profile's `hidden` value changes from `"0"` to `"1"`
- **THEN** its visibility in Decenza's profile selector is unchanged

### Requirement: Built-in profiles are gated against de1app drift

The `profile_sync` tool SHALL compare profile-level scalars — not frames alone — between the
de1app `.tcl` corpus and the shipped built-ins, applying the type-dependent selection rule.
An automated test SHALL fail when any shipped built-in drifts from its de1app source on a
compared field.

This gate MUST be in place and passing before any bulk rewrite of `resources/profiles/`, so
that the data change is verified by a checked-in test rather than by an ad-hoc script.

#### Scenario: Drifting built-in is caught

- **WHEN** a shipped built-in's `espresso_pressure` differs from its de1app source
- **THEN** the parity test fails and names the file, the field, and both values

#### Scenario: Comparison respects profile type

- **WHEN** a `settings_2a` built-in matches its source under the type-dependent rule but not
  under the raw `_advanced` spelling
- **THEN** the parity test passes

#### Scenario: Field coverage is explicit

- **WHEN** a de1app profile-level scalar is present in the corpus but absent from the
  comparison's field map
- **THEN** the tool reports it as uncompared rather than silently ignoring it

### Requirement: User-saved profiles are not rewritten

Correcting import fidelity SHALL apply to newly imported profiles and to the app-authored
built-ins in `resources/profiles/` only. Profiles a user has saved MUST NOT be retroactively
rewritten to change a value, since they load through `fromJson` and may hold deliberate user
edits.

Removing a key that no reader consults is not such a rewrite, and is permitted. The distinction
is whether a stored value the user could have set is altered: correcting a scalar to match an
import rule would be, while dropping the `recipe` block is not, because every value in it is
re-derived on read, duplicated by a top-level key, or read by nothing — except `dose`, which is
preserved as `recommended_dose` rather than discarded.

#### Scenario: User profile with a non-default scalar

- **WHEN** the built-in corpus is re-synced
- **THEN** profiles in the user's own profile directory are byte-identical to before

#### Scenario: A user profile keeps every value it holds

- **WHEN** a user-saved profile has its recipe block removed
- **THEN** every scalar, frame, target and flag it holds is unchanged
- **AND** a dose the user set is preserved as the profile's recommended dose

### Requirement: Simple profiles derive frames from their scalars

For `settings_2a` and `settings_2b`, the extraction frames SHALL be generated from the
profile's scalars rather than taken from a stored `advanced_shot`, matching de1app, which
regenerates the frame list on load and never reads the stored array for a simple profile.

A simple profile's `.tcl` may therefore carry frames that contradict its own scalars; the
scalars are authoritative.

#### Scenario: Stored frames contradict the scalars

- **WHEN** a `settings_2a` `.tcl` stores frames at 82/80/72 °C while `espresso_temperature`
  is `0`
- **THEN** the imported profile's frames are generated at `0` °C, matching what de1app brews

#### Scenario: Advanced profiles keep their stored frames

- **WHEN** a `settings_2c` `.tcl` with a stored `advanced_shot` is imported
- **THEN** the stored frames are preserved verbatim

### Requirement: A bare multi-word value is read whole for free-text keys

A Tcl assignment whose value is neither braced nor quoted SHALL be read to the end of the line
when the key is free text — `profile_title`, `author`, `profile_notes`. Every other key SHALL
keep taking the first whitespace-delimited token.

The list is confined to keys whose value is prose. An enum or a code MUST NOT be included:
`beverage_type` is written bare across the de1app corpus with values such as `espresso`,
`cleaning` and `tea_portafilter`, and reading a malformed line whole would produce an unmatchable
string and silently drop the classification — a worse outcome than truncation, which at least
recovers the intended value.

This deliberately diverges from Tcl's own `array set`, which reads only the first word and turns
the remainder into stray keys. The divergence is confined to fields that cannot reach a frame, a
machine value or a classification, and it recovers what the author wrote in files that are
malformed but common — Visualizer's `.tcl` export does not brace multi-word titles.

#### Scenario: An unbraced multi-word title imports whole

- **WHEN** a `.tcl` assigning `profile_title D-Flow / Q` unbraced is imported
- **THEN** the profile's title is `D-Flow / Q`
- **AND** its editor type and slash-prefix category resolve as they would for a braced title

#### Scenario: An enum keeps the first-token rule

- **WHEN** a `.tcl` assigns a bare `beverage_type` followed by trailing text
- **THEN** the beverage type is the first token, and the profile's classification is preserved

#### Scenario: Numeric keys are unaffected

- **WHEN** a `.tcl` assigning a bare numeric value is imported
- **THEN** that value is read exactly as before

#### Scenario: Braced and quoted values are unaffected

- **WHEN** a free-text key's value is braced or quoted
- **THEN** it is read exactly as before, by the same path as before

#### Scenario: The de1app corpus is unchanged

- **WHEN** every profile in the de1app corpus is imported and re-serialized
- **THEN** the output is identical to the output before this rule, because no de1app profile
  carries a bare multi-word value

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
