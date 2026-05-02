## ADDED Requirements

### Requirement: dialing-context payload SHALL include grinderCalibration block

The system SHALL compute a `grinderCalibration` block and include it in the dialing-context payload delivered by both `dialing_get_context` (MCP) and the in-app advisor's user-prompt enrichment path. The two surfaces SHALL call the same `DialingBlocks::buildGrinderCalibrationBlock` helper and produce byte-equivalent JSON for the same input.

#### Preconditions — block is present when ALL of the following hold:

1. The resolved shot's `grinderModel` is non-empty.
2. The resolved shot's `beverageType` is espresso (NOT `filter` or `pourover`).
3. At least 2 profiles in the all-time history (same grinder model + burrs) have qualifying shots with numeric grinder settings AND a canonical (non-inferred) UGS value in the knowledge base.

A shot qualifies when: `final_weight >= 5g` (not aborted) AND no quality badge is set (`grind_issue_detected = 0`, `channeling_detected = 0`, `pour_truncated_detected = 0`, `temperature_unstable = 0`, `skip_first_frame_detected = 0`). Badge columns default to 0 for shots predating the badge migrations, so all old shots pass this filter.

The block SHALL be omitted (no key, no `null`) when any precondition fails.

#### Anchor selection

From the qualifying profiles, the system SHALL select a **fine anchor** (lowest UGS) and **coarse anchor** (highest UGS) to maximize the calibrated span. The conversion key SHALL be:

```
conversionKey = (coarseAnchorMedianSetting − fineAnchorMedianSetting)
              / (coarseAnchorUGS − fineAnchorUGS)
```

#### Block shape

`grinderCalibration` SHALL be a JSON object with:

| Field | Type | Description |
|-------|------|-------------|
| `grinderModel` | string | Grinder model from the resolved shot |
| `fineAnchor` | object | Fine-end anchor: `profileName`, `ugs`, `medianSetting` (string), `sampleCount` |
| `coarseAnchor` | object | Coarse-end anchor: same shape as `fineAnchor` |
| `conversionKey` | number | Settings per UGS unit, rounded to 2 decimal places |
| `calibratedUgsRange` | [number, number] | `[fineAnchorUGS, coarseAnchorUGS]` — the UGS span covered by real data |
| `profiles` | array | RGS for every KB profile with a known UGS (see below) |

Each entry in `profiles` SHALL carry:

| Field | Type | Description |
|-------|------|-------------|
| `profileName` | string | Profile name from the KB |
| `ugs` | number | UGS value (canonical or inferred) |
| `rgs` | string | Derived grinder setting, formatted as the median (trailing zeros stripped) |
| `source` | string | `"history"` — median from real shots; `"derived"` — within calibrated range; `"extrapolated"` — outside calibrated range or UGS is inferred |

Profiles SHALL be ordered by UGS value ascending.

#### Source tagging rules

- `"history"`: the profile has a qualifying median in the all-time history (includes anchor profiles).
- `"derived"`: profile has a canonical UGS within `[fineAnchorUGS, coarseAnchorUGS]` and no history entry.
- `"extrapolated"`: profile's UGS is outside `[fineAnchorUGS, coarseAnchorUGS]`, OR the UGS value is marked inferred in the KB.

#### Scenario: User has two espresso profiles with canonical UGS values in history

- **GIVEN** history containing 4 shots on "80's Espresso" (numeric settings 4–6, canonical UGS 0.25) and 3 shots on "Adaptive v2" (settings 9–10, canonical UGS 1.25), same Niche Zero grinder, same burrs, all shots with no quality badges set
- **WHEN** `buildGrinderCalibrationBlock` is called for an "80's Espresso" shot
- **THEN** `fineAnchor.profileName` SHALL be `"80's Espresso"`, `fineAnchor.ugs` SHALL be `0.25`
- **AND** `coarseAnchor.profileName` SHALL be `"Adaptive v2"`, `coarseAnchor.ugs` SHALL be `1.25`
- **AND** `conversionKey` SHALL equal `(medianAdaptive − median80s) / (1.25 − 0.25)` rounded to 2 dp
- **AND** `calibratedUgsRange` SHALL be `[0.25, 1.25]`
- **AND** `profiles` SHALL include entries for every KB profile with a known UGS
- **AND** "80's Espresso" and "Adaptive v2" SHALL have `source: "history"`
- **AND** profiles with UGS between 0.25 and 1.25 (e.g. "D-Flow" at 0.5) SHALL have `source: "derived"`
- **AND** profiles outside that range (e.g. "Rao Allongé" at UGS 8) SHALL have `source: "extrapolated"`

#### Scenario: Fewer than 2 profiles with canonical UGS in history — block omitted

- **GIVEN** all-time history has shots on only one profile with a canonical KB UGS value
- **WHEN** `buildGrinderCalibrationBlock` is called
- **THEN** the return value SHALL be an empty `QJsonObject`
- **AND** `grinderCalibration` SHALL be absent from the response

#### Scenario: Filter beverage type — block omitted

- **GIVEN** the resolved shot has `beverageType: "filter"`
- **WHEN** `buildGrinderCalibrationBlock` is called
- **THEN** the return value SHALL be an empty `QJsonObject`
- **AND** `grinderCalibration` SHALL be absent from the response

#### Scenario: Grinder model changed — old shots excluded

- **GIVEN** the user switched grinders 30 days ago; earlier shots have a different grinder model
- **WHEN** `buildGrinderCalibrationBlock` is called
- **THEN** only shots matching the resolved shot's `grinderModel` AND `grinderBurrs` SHALL contribute
- **AND** profiles that only have shots from the old grinder SHALL NOT qualify as anchors

#### Scenario: History profile has no canonical KB UGS — included as history-only

- **GIVEN** the user has history on "Custom Title Profile" which has no entry in the KB (or only an inferred UGS)
- **WHEN** `buildGrinderCalibrationBlock` is called
- **THEN** "Custom Title Profile" SHALL NOT be used as an anchor
- **AND** it SHALL appear in `profiles` with `source: "history"`, its actual `medianSetting`, and no `ugs` field (or with `source: "extrapolated"` when UGS is inferred)

### Requirement: ProfileKnowledge SHALL expose UGS as a parsed numeric field

The `ShotSummarizer::ProfileKnowledge` struct SHALL carry a `double ugs` field (default `NaN` — not present) and a `bool ugsInferred` field (default `false`). The `loadProfileKnowledge()` parser SHALL populate these from `UGS:` lines in `profile_knowledge.md`:

- Strip a leading `~` character and set `ugsInferred = true`.
- Strip parenthetical annotations (everything from `(` to end of line).
- Parse the remaining token as a `double`. If parsing fails, leave `ugs` as `NaN`.

Skip-Catalog sections (cross-profile reference material) do not carry UGS lines and SHALL have `ugs = NaN`.

#### Scenario: Canonical UGS line parsed correctly

- **GIVEN** a section containing `UGS: 0.5`
- **WHEN** `loadProfileKnowledge()` parses the section
- **THEN** `pk.ugs` SHALL be `0.5` and `pk.ugsInferred` SHALL be `false`

#### Scenario: Inferred UGS line parsed with flag

- **GIVEN** a section containing `UGS: ~0.25 (inferred — low-temp regime requires finer grind)`
- **WHEN** `loadProfileKnowledge()` parses the section
- **THEN** `pk.ugs` SHALL be `0.25` and `pk.ugsInferred` SHALL be `true`
