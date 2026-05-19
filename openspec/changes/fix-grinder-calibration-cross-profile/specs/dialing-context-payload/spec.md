## MODIFIED Requirements

### Requirement: dialing-context payload SHALL include grinderCalibration block

The system SHALL compute a `grinderCalibration` block and include it in the dialing-context payload delivered by both `dialing_get_context` (MCP) and the in-app advisor's user-prompt enrichment path. The two surfaces SHALL call the same `DialingBlocks::buildGrinderCalibrationBlock` helper and produce byte-equivalent JSON for the same input.

The block models grind as `grind(profile, coffee) ≈ coffeeBaseline(coffee) + UGS·conversionKey`. The `conversionKey` is a property of the grinder + burrs and is coffee-independent; the per-coffee intercept (`coffeeBaseline`) is supplied by a recent dialed-in shot on the current coffee. The block SHALL NEVER emit a numeric grinder setting for a profile whose UGS is outside the validated range (extrapolation cap).

#### Preconditions — block is present when ALL of the following hold:

1. The resolved shot's `grinderModel` is non-empty.
2. The resolved shot's `beverageType` is espresso (NOT `filter` or `pourover`).
3. At least one of: (a) a within-coffee conversion key can be derived (see "Conversion key"), or (b) a Phase 2 deliberate calibration is stored for this grinder + burrs.

When no precondition (3) source exists, the block SHALL still be present but in **directional** form (no numeric `conversionKey`, no numeric `rgs`), conveying only relative ordering. The block SHALL be omitted entirely (no key, no `null`) only when precondition (1) or (2) fails.

#### Dialed-in qualification filter

A shot qualifies to anchor calibration only when ALL hold:

- `final_weight >= 15g`, AND
- no quality badge is set (`grind_issue_detected = 0`, `channeling_detected = 0`, `pour_truncated_detected = 0`, `skip_first_frame_detected = 0`), AND
- at least one quality signal: `enjoyment >= 50`, OR `|final_weight − targetWeightG| <= 0.10 · targetWeightG`, OR a refractometer reading is present.

Badge columns default to 0 for shots predating the badge migrations. The weak `final_weight >= 5g`/"no badge only" filter is REMOVED — it admitted undershoot and aborted experiments that corrupted the per-profile medians.

#### Conversion key — within-coffee paired derivation

The system SHALL NOT derive the conversion key from pooled all-coffee medians. Instead:

1. Group qualifying shots by `(coffeeBatch, profile)`. `coffeeBatch` SHALL be batch-level, NOT bean-level: `beanBrand + beanType + roastDate` when `roastDate` is a real value; when `roastDate` is empty or the `"--"` undated sentinel, `beanBrand + beanType` with **single-linkage 90-day clustering** (per bean, shots in time order, a gap > 90 days starts a new batch — a sliding window, NOT a fixed calendar bucket). A within-coffee pair SHALL only cancel the coffee baseline when both profiles were pulled on the same `coffeeBatch`; bean-only grouping (ignoring roast batch) SHALL NOT be used.
2. For each `coffeeBatch` with shots on ≥2 profiles at distinct canonical UGS values, compute the per-profile median setting and form within-coffee pairwise slopes `Δsetting / ΔUGS`.
3. Exclude pairs with `ΔUGS < minPairSpan` and pairs where either endpoint has fewer than `minEndpointSamples` shots (named constants; tuned empirically — see tasks).
4. `conversionKey` SHALL be the median (Theil–Sen) of the surviving pooled within-coffee slopes, rounded to 2 decimal places. It is a per-`(grinderModel, grinderBurrs)` runtime value and SHALL NOT be a shipped constant.
5. The block SHALL be **directional only** (no numeric `conversionKey`) unless there are at least `minValidatedPairs` surviving pairs AND the pairwise-slope spread gate passes. The spread gate SHALL be **dimensionless**: `IQR(pairwiseSlopes) ≤ maxSpreadRatio · |conversionKey|`. An absolute steps/UGS spread threshold SHALL NOT be used — slope magnitude is grinder-specific, so an absolute threshold is not portable across grinders. (`minPairSpan` and the extrapolation cap remain absolute because they are measured on the universal UGS axis, not in grinder setting units.)

When a Phase 2 deliberate calibration is stored for this grinder + burrs, its Conversion Key SHALL take precedence over the mined within-coffee key, and `confidence` SHALL be `"calibrated"`.

#### Per-coffee anchor (intercept)

The numeric intercept SHALL be the user's most recent dialed-in shot whose `coffeeBatch` (same batch-level identity as the conversion-key derivation) matches the resolved shot's, on any profile with a known canonical UGS. The anchor SHALL NOT be drawn from a different roast batch of the same bean. A profile's recommended setting SHALL be:

```
rgs(target) = anchorSetting + (UGS_target − UGS_anchorProfile) · conversionKey
```

If no recent dialed-in shot exists for the resolved shot's `coffeeBatch`, the block SHALL be **directional only** — `conversionKey` MAY be present for context but no `rgs` numbers SHALL be emitted (the intercept is unknown).

#### Extrapolation cap (mandatory)

Let `loUGS` / `hiUGS` be the min/max UGS of the validated anchor set (within-coffee pair endpoints, or the Phase 2 calibration anchors). A profile SHALL receive a numeric `rgs` ONLY when its UGS lies within `[loUGS − cap, hiUGS + cap]`, where `cap` is a single named constant (≈1.5 UGS). For any profile outside that window the entry SHALL have `source: "directional"`, NO `rgs`, and a `direction` field. The system SHALL NOT, under any circumstances, emit a numeric grinder setting for an out-of-window profile.

#### Directional reference and language (always available, anchor-free)

`direction` SHALL be computed purely from KB UGS ordering against the **resolved shot's own profile UGS** — `direction = "coarser"` when `UGS_target > UGS_currentProfile`, `"finer"` when `UGS_target < UGS_currentProfile`, omitted when equal. It SHALL NOT depend on the conversion key, any anchor, the per-coffee intercept, or the grinder's numeric convention — so it is correct in the no-anchor / no-calibration case (the primary Phase 1 state). The phrase "nearest anchor" SHALL NOT be used as the reference; there may be no anchor.

`direction` SHALL be expressed only as a **grind-size term** (`"finer"` / `"coarser"`). The block SHALL NOT emit a dial-number delta, "turn up/down by N", or any setting-unit statement for a directional profile — those require the grinder's finer-direction convention and reintroduce the #1223 sign risk; grind-size language does not.

When the resolved shot's own profile has no known canonical UGS, ordering against it is impossible: such target entries SHALL be marked `source: "directional"` with NO `direction` field and a flag indicating the current profile is not UGS-placed, rather than guessing a direction.

#### Block shape

`grinderCalibration` SHALL be a JSON object with:

| Field | Type | Description |
|-------|------|-------------|
| `grinderModel` | string | Grinder model from the resolved shot |
| `confidence` | string | `"calibrated"` (Phase 2 stored key), `"approximate"` (mined within-coffee key passed the gates), or `"directional"` (no usable numeric key/anchor) |
| `usageConstraint` | string | Short directive the prompt repeats verbatim (see advisor-user-prompt) — states UGS is relative and numbers are valid only within the calibrated range |
| `conversionKey` | number? | Settings per UGS unit, 2 dp. ABSENT when `confidence` is `"directional"` |
| `coffeeAnchor` | object? | `profileName`, `ugs`, `setting`, `coffee` — the current-coffee intercept. ABSENT when no recent dialed-in shot for the current coffee |
| `calibratedUgsRange` | [number, number]? | Validated UGS span. ABSENT when `confidence` is `"directional"` |
| `profiles` | array | One entry per KB profile with a known UGS |

Each `profiles` entry SHALL carry `profileName`, `ugs`, and `source`. `source` is one of:

- `"history"` — the profile has a qualifying within-coffee median for the current coffee; `rgs` is that measured median.
- `"derived"` — UGS within the validated range; `rgs` is computed from anchor + conversionKey.
- `"directional"` — UGS outside the extrapolation window OR no numeric key/anchor available; NO `rgs`; carries `direction` (`"finer"`/`"coarser"`) derived from KB UGS ordering vs the resolved shot's own profile (anchor-free; omitted when the current profile has no canonical UGS).

`rgs` (string) SHALL be present ONLY for `"history"` and `"derived"`. Profiles SHALL be ordered by UGS ascending. The legacy `"extrapolated"` source value and its numeric `rgs` are REMOVED.

#### Scenario: User dialed in on the current coffee, asks about a near profile

- **GIVEN** the current shot's coffee has dialed-in shots on "D-Flow / Q" (UGS 1.0, median setting 6) and the within-coffee conversion key from history is +1.5 steps/UGS passing all gates
- **WHEN** `buildGrinderCalibrationBlock` is called
- **THEN** `confidence` SHALL be `"approximate"`
- **AND** `coffeeAnchor` SHALL reference the recent "D-Flow / Q" shot on this coffee at setting 6
- **AND** a profile within the validated window (e.g. "Adaptive v2" UGS 1.25) SHALL have `source: "derived"` with `rgs` ≈ `6 + (1.25 − 1.0)·1.5`
- **AND** the conversion key SHALL NOT be derived from pooled all-coffee medians

#### Scenario: Far-profile request is capped to directional (the #1223 fix)

- **GIVEN** the validated anchor set spans UGS 0.0–1.5 and the user asks for a grind for "TurboTurbo" (UGS 6.0)
- **WHEN** `buildGrinderCalibrationBlock` is called
- **THEN** the "TurboTurbo" entry SHALL have `source: "directional"` and `direction: "coarser"`
- **AND** the entry SHALL NOT contain any numeric `rgs`
- **AND** no negative or out-of-range grinder number SHALL appear anywhere in the block

#### Scenario: Wrong-signed pooled slope can no longer be produced

- **GIVEN** a history that under the old pooled-median algorithm yielded `conversionKey = −2.4` for a Niche-style grinder (lower = finer)
- **WHEN** `buildGrinderCalibrationBlock` is called with the within-coffee derivation
- **THEN** the conversion key SHALL be computed from within-coffee pairwise slopes only
- **AND** if the surviving pairs fail the spread/sign gates the block SHALL be `confidence: "directional"` with no `conversionKey`
- **AND** the block SHALL never emit a `conversionKey` whose sign contradicts the grinder's finer-direction

#### Scenario: No dialed-in data for the current coffee — directional only

- **GIVEN** the resolved shot's coffee has no qualifying dialed-in shot on any known-UGS profile
- **WHEN** `buildGrinderCalibrationBlock` is called
- **THEN** `confidence` SHALL be `"directional"`
- **AND** `coffeeAnchor` SHALL be absent and no `rgs` numbers SHALL be emitted
- **AND** every `profiles` entry SHALL be `source: "directional"` carrying only relative `direction`

#### Scenario: Direction is correct with zero anchors and zero calibration

- **GIVEN** a brand-new user: the resolved shot is on "D-Flow / Q" (UGS 1.0), there is no conversion key, no per-coffee anchor, and no history at all
- **WHEN** `buildGrinderCalibrationBlock` is called
- **THEN** `confidence` SHALL be `"directional"` and no numeric fields SHALL be present
- **AND** "TurboTurbo" (UGS 6.0) SHALL be `direction: "coarser"` and "Blooming Espresso" (UGS −0.5) SHALL be `direction: "finer"`, derived solely from KB UGS ordering vs the current profile's UGS
- **AND** the result SHALL NOT depend on the grinder's finer-direction convention or contain any dial-number language

#### Scenario: Current profile has no canonical UGS — direction withheld, not guessed

- **GIVEN** the resolved shot is on a fully-custom profile with no canonical KB UGS
- **WHEN** `buildGrinderCalibrationBlock` is called
- **THEN** target entries SHALL be `source: "directional"` with NO `direction` field
- **AND** the block SHALL flag that the current profile is not UGS-placed rather than emit a guessed finer/coarser

#### Scenario: Both surfaces remain byte-equivalent

- **GIVEN** the same resolved shot and database
- **WHEN** the block is built via `dialing_get_context` and via the in-app advisor enrichment path
- **THEN** both SHALL call `DialingBlocks::buildGrinderCalibrationBlock` and produce byte-identical JSON

#### Scenario: Filter beverage type — block omitted

- **GIVEN** the resolved shot has `beverageType: "filter"`
- **WHEN** `buildGrinderCalibrationBlock` is called
- **THEN** the return value SHALL be an empty `QJsonObject`
- **AND** `grinderCalibration` SHALL be absent from the response

#### Scenario: Grinder model changed — old shots excluded

- **GIVEN** the user switched grinders 30 days ago; earlier shots have a different grinder model
- **WHEN** `buildGrinderCalibrationBlock` is called
- **THEN** only shots matching the resolved shot's `grinderModel` AND `grinderBurrs` SHALL contribute to within-coffee pairs and the coffee anchor

## ADDED Requirements

### Requirement: dialing_get_grinder_calibration SHALL return an explicit directional/unavailable response

The `dialing_get_grinder_calibration` MCP tool SHALL NOT return a numeric profiles table when no validated within-coffee conversion key and current-coffee anchor exist. It SHALL instead return a structured response indicating directional-only guidance, with a human-readable `reason` and an instruction to give relative direction and ask the user to pull a reference shot on the target profile.

#### Scenario: Unavailable numeric calibration returns guidance, not a table

- **WHEN** `dialing_get_grinder_calibration` is called and no validated within-coffee key + current-coffee anchor exist
- **THEN** the response SHALL set `confidence: "directional"` (or `available: false` with a `reason`)
- **AND** the response SHALL NOT contain any numeric `rgs` values
- **AND** the `reason` SHALL instruct giving relative direction and pulling a reference shot rather than quoting a number

#### Scenario: Capped profile is reported as directional in the tool response

- **GIVEN** the user explicitly asks for a profile whose UGS is outside the validated window
- **WHEN** `dialing_get_grinder_calibration` is called
- **THEN** that profile SHALL be reported with a finer/coarser direction and no number
- **AND** the response SHALL state the calibrated UGS range so the model can explain why a number was withheld
