## ADDED Requirements

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

## MODIFIED Requirements

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
