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
`tank_desired_water_temperature`, and the target weight/volume fields covered below.

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
requirement exists because de1app and reaprime read the profile field, and a Decenza-written
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
rewritten, since they load through `fromJson` and may hold deliberate user edits.

#### Scenario: User profile with a non-default scalar

- **WHEN** the built-in corpus is re-synced
- **THEN** profiles in the user's own profile directory are byte-identical to before

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

