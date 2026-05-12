# dialing-context-payload Specification

## Purpose
TBD - created by archiving change optimize-dialing-context-payload. Update Purpose after archive.
## Requirements
### Requirement: dialInSessions SHALL hoist common shot identity to a session-level context

Each session in `dialInSessions` SHALL carry a `context` object holding the identity fields shared by every shot in the session: `grinderBrand`, `grinderModel`, `grinderBurrs`, `beanBrand`, `beanType`. When a field's value is identical across all shots in the session, it SHALL appear in `context` only — not on the per-shot entries. When a field's value differs on a particular shot in the session, that shot's entry SHALL carry the field directly, overriding the session context for that shot.

The first shot of every session SHALL be the reference for the `context` object's values when at least one shot in the session has a non-empty value for the field. When no shot in the session has a non-empty value, the field SHALL be omitted from `context` entirely.

The session's `shotCount`, `sessionStart`, `sessionEnd`, and `shots[]` array SHALL remain. Per-shot entries SHALL continue to carry shot-variable fields (`id`, `timestamp`, `doseG`, `yieldG`, `durationSec`, `grinderSetting`, `notes`, `enjoyment0to100`, `temperatureOverrideC`, `targetWeightG`, `changeFromPrev`).

#### Scenario: All shots share identity → context has all fields, shots have no overrides

- **GIVEN** a 3-shot session where every shot has the same grinder (Niche Zero, 63mm Kony) and bean (Northbound Single Origin)
- **WHEN** `dialing_get_context` builds the session
- **THEN** `session.context` SHALL contain `grinderBrand: "Niche"`, `grinderModel: "Zero"`, `grinderBurrs: "63mm Kony"`, `beanBrand: "Northbound"`, `beanType: "Single Origin"`
- **AND** none of `shots[0]`, `shots[1]`, `shots[2]` SHALL contain any of those five fields

#### Scenario: One shot in a session has a different bean → only that shot carries the override

- **GIVEN** a 3-shot session where shot 1 and 3 are on Northbound and shot 2 is on Prodigal
- **WHEN** `dialing_get_context` builds the session
- **THEN** `session.context.beanBrand` SHALL be `"Northbound"` (the value shared by the first shot and at least half of the others)
- **AND** `shots[1]` SHALL carry `beanBrand: "Prodigal"` directly
- **AND** `shots[0]` and `shots[2]` SHALL NOT carry `beanBrand`
- **AND** the same logic SHALL apply field-by-field independently (a shared grinder with a differing bean still hoists the grinder fields)

#### Scenario: Single-shot session puts identity in context, shot is empty of identity

- **GIVEN** a session with one shot
- **WHEN** `dialing_get_context` builds the session
- **THEN** `session.context` SHALL carry the shot's full identity (whichever of the five fields are non-empty)
- **AND** `shots[0]` SHALL NOT carry any of the hoisted fields

### Requirement: dialing_get_context response SHALL NOT include a separate `shot` block

The response SHALL NOT carry a top-level `result["shot"]` block summarizing the resolved shot's profile, dose, yield, duration, ratio, grinder, or bean. All of those fields are rendered in `shotAnalysis` prose; shipping both forced consumers to choose a canonical version when they could disagree on precision (`durationSec: 31.26` JSON vs `"Duration: 31s"` prose).

The fields that previously lived under `result["shot"]` SHALL continue to appear in the `shotAnalysis` prose body produced by `ShotSummarizer::buildUserPrompt` — that prose is now the canonical surface for shot-summary metadata.

#### Scenario: Response has no top-level `shot` field

- **GIVEN** any successful `dialing_get_context` call
- **WHEN** the response is assembled
- **THEN** `result.shot` SHALL be absent
- **AND** the same dose / yield / duration / grinder / bean information SHALL still appear inside `result.shotAnalysis`

### Requirement: shotAnalysis prose SHALL NOT carry the static detector-observations legend

The `## Detector Observations` section in `shotAnalysis` prose SHALL retain its section header and per-line severity tags (`[warning]`, `[caution]`, `[good]`, `[observation]`), but SHALL NOT carry the seven-line preamble explaining what those tags mean. That preamble is static framing and belongs in the system prompt, where it ships once per conversation.

The system prompt produced by `ShotSummarizer::shotAnalysisSystemPrompt` (espresso and filter variants) SHALL include the legend text so the AI's interpretation of severity tags is unchanged.

#### Scenario: Detector legend moves from per-call prose to per-conversation system prompt

- **GIVEN** any `shotAnalysis` prose body produced after this change
- **WHEN** the prose is rendered
- **THEN** the prose SHALL NOT contain the line `"The lines below come from the same deterministic detectors that drive the in-app Shot Summary badges the user sees."`
- **AND** the prose SHALL NOT contain any of the four severity-tag explanation bullets
- **AND** the espresso `shotAnalysisSystemPrompt` output SHALL contain those bullets

#### Scenario: Per-line severity tags survive in the prose

- **GIVEN** a shot whose detector orchestration produced a `[warning]` line and a `[good]` line
- **WHEN** `shotAnalysis` prose renders the `## Detector Observations` section
- **THEN** the rendered lines SHALL still carry their `[warning]` / `[good]` prefixes
- **AND** the AI SHALL be able to interpret them via the system prompt's legend

### Requirement: dialing_get_context response SHALL move static framing strings to the system prompt

The response SHALL NOT include the static framing strings `currentBean.inferredNote`, `currentBean.daysSinceRoastNote`, or `tastingFeedback.recommendation`. These are taught once in the system prompt rather than shipped on every call. The structural fields they previously qualified — `currentBean.beanFreshness`, `tastingFeedback.hasEnjoymentScore` / `.hasNotes` / `.hasRefractometer` — SHALL remain.

The `currentBean.inferredFromShotId` and `currentBean.inferredFields` fields SHALL NOT appear in the response. The DYE-with-shot-fallback machinery they qualified is removed; `currentBean` is now sourced solely from the resolved shot (see "currentBean SHALL describe the resolved shot's setup, not live DYE").

The `shotAnalysisSystemPrompt` SHALL include a "How to read structured fields" section covering: (a) `tastingFeedback` gating (when all `has*` booleans are false, ASK the user about taste before suggesting changes), (b) `beanFreshness` gating (never quote calendar age until `freshnessKnown == true`), and (c) the meaning of empty-string fields on `currentBean` — an empty value means the shot did not record that field (common on legacy shots), NOT that the user has no grinder / bean / etc.; the AI must ask before recommending a change to a blank field. The `inferredFields` clause that previously appeared in this section SHALL be removed.

The `currentBean.daysSinceRoastNote` field is replaced by the `beanFreshness.instruction` field defined in the next requirement, NOT by a system-prompt entry — the freshness instruction is bean-state-specific and benefits from sitting next to the `roastDate` field.

#### Scenario: currentBean ships without inferred-field fallback machinery

- **GIVEN** a `dialing_get_context` call where the resolved shot's grinder fields are populated but live DYE is blank, OR vice versa
- **WHEN** the response is built
- **THEN** `currentBean.inferredFromShotId` SHALL NOT be present
- **AND** `currentBean.inferredFields` SHALL NOT be present
- **AND** `currentBean.inferredNote` SHALL NOT be present
- **AND** `currentBean`'s grinder / bean / dose values SHALL be those of the resolved shot

#### Scenario: Tasting feedback ships booleans only

- **GIVEN** a shot with no enjoyment score, no notes, and no TDS
- **WHEN** the response is built
- **THEN** `tastingFeedback.hasEnjoymentScore`, `.hasNotes`, `.hasRefractometer` SHALL all be `false`
- **AND** `tastingFeedback.recommendation` SHALL NOT be present

#### Scenario: System prompt teaches the field semantics

- **GIVEN** the espresso `shotAnalysisSystemPrompt` output
- **WHEN** rendered
- **THEN** the output SHALL contain guidance for `tastingFeedback` (when all `has*` are false, ask first)
- **AND** SHALL contain guidance for `beanFreshness` (never quote age until `freshnessKnown == true`)
- **AND** SHALL contain guidance teaching that an empty-string `currentBean` field means "the shot did not record that field" (not "the user has no grinder / bean")
- **AND** SHALL NOT contain a section describing `inferredFields` semantics

### Requirement: dialInSessions[].shots[] SHALL NOT include roastDate

Per-shot entries in `dialInSessions[].shots[]` SHALL NOT carry a `roastDate` field. Aging-trend reasoning across an iteration session would require subtracting `roastDate` from each shot's `timestamp` — a calculation that is misleading when storage history is unknown (frozen vs counter), and within-session bean rotation is already encoded in `changeFromPrev.beanBrand`.

The single canonical surface for `roastDate` in the response is `currentBean.beanFreshness.roastDate` (defined in the next requirement). The AI can quote that one field; it cannot silently derive aging trends from a sequence of historical shots.

#### Scenario: No per-shot roastDate in dialInSessions

- **GIVEN** any `dialing_get_context` response with a populated `dialInSessions`
- **WHEN** the response is inspected
- **THEN** for every session and every shot within `shots[]`, the `roastDate` field SHALL be absent

### Requirement: currentBean SHALL expose a beanFreshness block instead of precomputed days-since-roast

`currentBean.daysSinceRoast` and `currentBean.daysSinceRoastNote` SHALL NOT be present in the response. They are replaced by `currentBean.beanFreshness`, a structured block with three fields:

- `roastDate` — the resolved shot's saved `roastDate` string, surfaced verbatim.
- `freshnessKnown` — boolean. Until a separate change introduces storage-mode tracking, this SHALL always be `false`.
- `instruction` — a fixed imperative string: `"Calendar age from roastDate is NOT freshness — many users freeze and thaw weekly. ASK the user about storage before applying any bean-aging guidance."`

The block SHALL be omitted entirely when the resolved shot's `roastDate` is empty. The block SHALL NOT contain a precomputed day count under any field name; the AI MUST do the subtraction itself, in front of the user, to make the assumption visible.

The `shotAnalysis` prose SHALL NOT contain the parenthetical "(N days since roast, not necessarily freshness — ask about storage)" that previously rendered next to the bean name. It is replaced by the lighter "(roasted YYYY-MM-DD; ask user about storage before reasoning about age)" — same caveat, no day count.

#### Scenario: beanFreshness emits with freshnessKnown false and the imperative instruction

- **GIVEN** a resolved shot with `roastDate = "2026-04-15"` and no storage-mode information
- **WHEN** the response is built
- **THEN** `currentBean.beanFreshness.roastDate` SHALL be `"2026-04-15"`
- **AND** `currentBean.beanFreshness.freshnessKnown` SHALL be `false`
- **AND** `currentBean.beanFreshness.instruction` SHALL match the fixed imperative string verbatim
- **AND** `currentBean` SHALL NOT contain `daysSinceRoast` or `daysSinceRoastNote` under any spelling
- **AND** `currentBean.beanFreshness` SHALL NOT contain a `daysSinceRoast`, `calendarDaysSinceRoast`, `effectiveAgeDays`, or any other precomputed-day-count field

#### Scenario: Empty shot roastDate omits the block entirely

- **GIVEN** a resolved shot with no `roastDate` (the shot's saved `roastDate` field is empty)
- **WHEN** the response is built
- **THEN** `currentBean.beanFreshness` SHALL be absent
- **AND** the block SHALL be absent regardless of whether live DYE has a `dyeRoastDate` value

#### Scenario: shotAnalysis prose mirrors the no-day-count contract

- **GIVEN** a shot whose bean has a populated `roastDate`
- **WHEN** `shotAnalysis` prose is rendered
- **THEN** the prose SHALL contain `"roasted YYYY-MM-DD"` for the date itself
- **AND** SHALL contain a phrase pointing at storage uncertainty (e.g., `"ask user about storage"`)
- **AND** SHALL NOT contain any phrase of the form `"N days since roast"` or `"N days post-roast"` or any standalone integer adjacent to a roast-date string

### Requirement: grinderContext.settingsObserved SHALL be scoped to the current bean

`grinderContext.settingsObserved` SHALL be filtered to shots whose `bean_brand` matches the resolved shot's `beanBrand`. The current cross-bean list invites the AI to suggest grind settings the user used on a different bean — a misleading recommendation surface when bean rotation is the variable being held constant in dial-in.

When the bean-scoped query returns at least 2 distinct settings, the response SHALL emit `settingsObserved` with the bean-scoped list and SHALL NOT emit `allBeansSettings`.

When the bean-scoped query returns fewer than 2 distinct settings (the user just switched beans, only one shot on the new bean), the response SHALL also emit `allBeansSettings` carrying the cross-bean list. This field exists so the AI can still see the user's overall range — explicitly tagged so it does not get misread as bean-specific. `settingsObserved` (bean-scoped) is still emitted alongside it.

When the resolved shot's `beanBrand` is empty (no bean recorded), the response SHALL emit `settingsObserved` with the cross-bean list (legacy behavior) and SHALL NOT emit `allBeansSettings`.

#### Scenario: Sufficient bean-scoped history yields a clean settingsObserved

- **GIVEN** the user has 8 shots on `Northbound Single Origin` with grind settings `{4, 4.5, 5}` and 12 shots on `Prodigal Buenos Aires` with grind setting `{9}`
- **WHEN** `dialing_get_context` resolves a shot on `Northbound`
- **THEN** `grinderContext.settingsObserved` SHALL contain `[4, 4.5, 5]` (sorted, deduped)
- **AND** `grinderContext.allBeansSettings` SHALL NOT be present
- **AND** the value `9` SHALL NOT appear in `settingsObserved`

#### Scenario: Sparse bean-scoped history surfaces both lists

- **GIVEN** the user has 1 shot on a freshly-loaded `New Bean` with grind setting `4.5`, and 12 shots across other beans with settings `{3.5, 4, 4.5, 5, 9}`
- **WHEN** `dialing_get_context` resolves a shot on `New Bean`
- **THEN** `grinderContext.settingsObserved` SHALL be `[4.5]`
- **AND** `grinderContext.allBeansSettings` SHALL be `[3.5, 4, 4.5, 5, 9]`
- **AND** the spec note `"do NOT recommend specific values from allBeansSettings — they were observed on different beans"` SHALL be discoverable from the system prompt's "How to read structured fields" section

#### Scenario: Empty beanBrand falls back to cross-bean list as legacy

- **GIVEN** a resolved shot with no `beanBrand` recorded (legacy import or DYE-blank shot)
- **WHEN** `dialing_get_context` is called
- **THEN** `grinderContext.settingsObserved` SHALL contain the unscoped (cross-bean) settings list
- **AND** `grinderContext.allBeansSettings` SHALL NOT be present

### Requirement: Profile metadata SHALL appear in exactly one structured block [PR 2 scope]

The response SHALL carry a top-level `result.profile` block with `filename`, `title`, `intent`, `recipe`, `targetWeightG`, `targetTemperatureC`, and `recommendedDoseG` (the last omitted when the profile has no recommended dose). This block is the single canonical source for profile metadata — both invariant (`filename`, `title`, `intent`, `recipe`) and runtime targets (`targetWeightG`, `targetTemperatureC`, `recommendedDoseG`).

The legacy `result.currentProfile` block SHALL NOT be emitted; its fields are subsumed by `result.profile`.

The `shotAnalysis` prose body SHALL NOT contain a `Profile:` line, a `Profile intent:` line, or a `## Profile Recipe` section. Profile metadata is read from `result.profile` only. The system prompt SHALL teach the AI to read profile metadata from `result.profile.*`.

When `AIManager::buildRecentShotContext` and `ShotSummarizer::buildHistoryContext` render multiple historical shots, they SHALL emit a single profile-level header at the top of the history section (covering `Profile`, `Profile intent`, `Profile Recipe`) and SHALL NOT emit those fields in per-shot blocks.

#### Scenario: Profile metadata lives only in result.profile

- **GIVEN** any successful `dialing_get_context` response
- **WHEN** the response is inspected
- **THEN** `result.profile` SHALL contain `filename`, `title`, `intent`, `recipe`, `targetWeightG`, `targetTemperatureC` (all populated when the source profile has them)
- **AND** `result.currentProfile` SHALL NOT be present
- **AND** `result.shotAnalysis` SHALL NOT contain the substring `"Profile:"` (as a section/field label) or `"Profile intent:"` or `"## Profile Recipe"`

#### Scenario: History rendering hoists profile constants to one section header

- **GIVEN** the in-app advisor renders 4 historical shots on the same profile via `AIManager::buildRecentShotContext`
- **WHEN** the rendered prose is inspected
- **THEN** `Profile intent:` SHALL appear at most once in the prose
- **AND** `## Profile Recipe` SHALL appear at most once in the prose
- **AND** the per-shot blocks under `### Shot (date)` SHALL NOT carry those fields individually

### Requirement: shotAnalysis prose SHALL carry only shot-variable fields in its summary block [PR 2 scope]

The `## Shot Summary` block at the top of the `shotAnalysis` prose body SHALL contain only shot-variable fields: `Dose`, `Yield` (with delta-from-target when present), `Ratio`, `Duration`, `Extraction` (TDS / EY), `Overall shot peaks` (pressure / flow with timing). Per-shot variable grinder data (`grinderSetting`) MAY appear in this block when present.

The block SHALL NOT contain shot-invariant identity:
- `Coffee:` line (bean brand / type / roast level / roast-date string) — bean identity lives in `currentBean.*`.
- `Grinder:` line carrying brand / model / burrs — grinder identity lives in `dialInSessions[<resolved-shot-session>].context` for the resolved shot's session, and in `currentBean.grinderBrand` / `grinderModel` / `grinderBurrs` for the live setup.
- `Profile:` and `Profile intent:` lines — covered by the previous requirement.

The system prompt SHALL teach the AI: "Shot-invariant identity (bean, grinder model+burrs, profile, roast date) lives in structured JSON blocks (`currentBean`, `result.profile`, `dialInSessions[].context`). The `shotAnalysis` prose carries shot-variable data only — what happened in this specific shot."

#### Scenario: Shot Summary prose carries dose/yield/ratio/duration but no identity

- **GIVEN** a resolved shot whose `shotAnalysis` prose is rendered
- **WHEN** the `## Shot Summary` block is inspected
- **THEN** the block SHALL contain `Dose`, `Yield`, `Duration`, `ratio` (or equivalent ratio expression)
- **AND** the block SHALL NOT contain `Coffee:`, `Beans:`, or any bean brand/type string
- **AND** the block SHALL NOT contain a `Grinder:` line carrying the model or burrs identifier; only `grinderSetting` values are permitted in the prose
- **AND** the block SHALL NOT contain the literal substring `"roasted "` followed by a date
- **AND** the prose body MAY still carry per-shot `grinderSetting` inline (e.g., as part of a comparison line) since `grinderSetting` is a shot-variable

### Requirement: dialing_get_context response SHALL contain a single canonical surface for the user's roast date

The response SHALL contain at most one field whose key matches the regex `(?i)roast`: `currentBean.beanFreshness.roastDate`. No other key path in the response — including any nested object, array element, or prose body — SHALL contain the substring `roast` as part of a key name. This pins the no-aging-derivation contract end-to-end: the AI receives the user's entered date once, in a structured block whose adjacent `instruction` field forbids quoting age until storage is known, and has no other JSON path from which to silently derive aging information.

This requirement is verifiable by structural inspection — independent of which parts of the payload are populated for any given call. When `currentBean.beanFreshness` is omitted (no roast date entered), the requirement is satisfied trivially.

The `shotAnalysis` prose body is also subject to this requirement at the *content* level: the prose SHALL NOT contain any phrase of the form `"N days since roast"`, `"N days post-roast"`, `"N-day-old"`, or any standalone integer immediately adjacent to a roast date string. **[PR 1, in effect now]**

**[PR 2 scope]** Per the canonical-source separation requirement above, the prose SHALL NOT contain the literal `"roasted YYYY-MM-DD"` string either — the date lives exclusively in `currentBean.beanFreshness.roastDate`. PR 1 still carries `, roasted YYYY-MM-DD (ask user about storage before reasoning about age)` in the prose Coffee line; this strict-strip rule activates with PR 2's prose Coffee/Grinder removal. The system prompt teaches the AI that bean-age reasoning starts from `currentBean.beanFreshness`, never from the prose.

#### Scenario: Single canonical roast key in JSON

- **GIVEN** any `dialing_get_context` response
- **WHEN** the response JSON is recursively walked for keys containing the substring `"roast"` (case-insensitive)
- **THEN** the only matching key path SHALL be `currentBean.beanFreshness.roastDate` (when present)
- **AND** specifically `currentBean.daysSinceRoast`, `currentBean.daysSinceRoastNote`, `dialInSessions[*].shots[*].roastDate`, and `bestRecentShot.roastDate` SHALL all be absent

#### Scenario: Empirical anchor against the Northbound 80's Espresso conversation

- **GIVEN** a 4-shot iteration session on `80's Espresso` profile + `Niche Zero` grinder + `Northbound Coffee Roasters Spring Tour 2026 #2` bean (the conversation captured 2026-04-30)
- **WHEN** `dialing_get_context` is called and the response is fully assembled
- **THEN** `dialInSessions[0].context` SHALL carry `grinderBrand: "Niche"`, `grinderModel: "Zero"`, `grinderBurrs: "63mm Mazzer Kony conical"`, `beanBrand: "Northbound Coffee Roasters"`, `beanType: "Spring Tour 2026 #2"`
- **AND** none of the four entries in `dialInSessions[0].shots[]` SHALL carry any of those five fields
- **AND** the response (JSON keys + `shotAnalysis` prose content) SHALL contain zero occurrences of the substring `"days since roast"` and zero occurrences of `"days post-roast"`
- **AND** the response SHALL contain exactly one occurrence of the substring `"2026-03-30"`: under `currentBean.beanFreshness.roastDate`. The date SHALL NOT appear inside `dialInSessions[*].shots[*]`, inside `bestRecentShot`, or anywhere in the `shotAnalysis` prose body
- **AND** the `shotAnalysis` prose SHALL NOT contain `"## Profile Recipe"` (it lives in `result.profile.recipe`)
- **AND** the `shotAnalysis` prose SHALL NOT contain a `"Coffee:"`, `"Beans:"`, or `"Grinder:"` line for the resolved shot (these live in `currentBean` and `dialInSessions[].context`)

### Requirement: dialing-context payload SHALL include grinderCalibration block

The system SHALL compute a `grinderCalibration` block and include it in the dialing-context payload delivered by both `dialing_get_context` (MCP) and the in-app advisor's user-prompt enrichment path. The two surfaces SHALL call the same `DialingBlocks::buildGrinderCalibrationBlock` helper and produce byte-equivalent JSON for the same input.

#### Preconditions — block is present when ALL of the following hold:

1. The resolved shot's `grinderModel` is non-empty.
2. The resolved shot's `beverageType` is espresso (NOT `filter` or `pourover`).
3. At least 2 profiles in the all-time history (same grinder model + burrs) have qualifying shots with numeric grinder settings AND a canonical (non-inferred) UGS value in the knowledge base.

A shot qualifies when: `final_weight >= 5g` (not aborted) AND no quality badge is set (`grind_issue_detected = 0`, `channeling_detected = 0`, `pour_truncated_detected = 0`, `skip_first_frame_detected = 0`). Badge columns default to 0 for shots predating the badge migrations, so all old shots pass this filter. (The historical `temperature_unstable` predicate was retired alongside its column in the openspec change `remove-temperature-unstable-badge`.)

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

### Requirement: currentBean SHALL describe the resolved shot's setup, not live DYE

`currentBean` SHALL be built from the resolved shot's saved bean / grinder / dose / roastDate metadata on every surface that emits it. The two surfaces — `dialing_get_context.currentBean` (the MCP read tool's top-level block) and the in-app advisor's user-prompt `currentBean` (rendered by `ShotSummarizer::buildUserPromptObject` from `summarizeFromHistory(shot)`) — SHALL produce byte-equivalent JSON for the same resolved shot.

The block SHALL contain `brand`, `type`, `roastLevel`, `grinderBrand`, `grinderModel`, `grinderBurrs`, `grinderSetting`, `doseWeightG` keys with values read directly from the shot's saved fields. The block SHALL NOT read from `Settings::dye()` or any other "live machine state" source on either surface. When a shot string field is empty, its value SHALL be the empty string; `doseWeightG` SHALL be the shot's saved numeric value (which may be `0` when the shot has no recorded dose). The block SHALL NOT fall back to a different shot or to live DYE.

The system prompt's "How to read structured fields" section SHALL describe `currentBean` as "the setup that produced the resolved shot." The prompt SHALL NOT teach an `inferredFields` reading.

#### Scenario: Both surfaces produce equal currentBean for the same shot under divergent live DYE

- **GIVEN** a saved shot with `beanType = "Spring Tour 2026 #2"`, `roastLevel = "Dark"`, `doseWeightG = 20`, `roastDate = "2026-03-30"`
- **AND** a live `Settings::dye()` state with `dyeBeanType = "TypeA"`, `dyeRoastLevel = ""`, `dyeBeanWeight = 18`, `dyeRoastDate = ""` (the user changed DYE since the shot was pulled)
- **WHEN** `dialing_get_context` builds `result["currentBean"]` for that shot
- **AND** `ShotSummarizer::buildUserPromptObject(summarizeFromHistory(shot))` builds `payload["currentBean"]` for the same shot
- **THEN** the two `currentBean` `QJsonObject`s SHALL be equal under `==`
- **AND** both SHALL carry `type = "Spring Tour 2026 #2"`, `roastLevel = "Dark"`, `doseWeightG = 20`
- **AND** both SHALL carry a `beanFreshness` block with `roastDate = "2026-03-30"`
- **AND** neither SHALL contain `inferredFields`, `inferredFromShotId`, or `inferredNote`

#### Scenario: Empty shot fields render as empty strings, not DYE fallback

- **GIVEN** a saved shot with `grinderBrand = ""`, `grinderModel = ""`, `grinderBurrs = ""`, `grinderSetting = ""`
- **AND** a live `Settings::dye()` state with `dyeGrinderBrand = "Niche"`, `dyeGrinderModel = "Zero"`, `dyeGrinderBurrs = "63mm Kony"`, `dyeGrinderSetting = "4.5"`
- **WHEN** `dialing_get_context` builds `result["currentBean"]` for that shot
- **THEN** `currentBean.grinderBrand`, `.grinderModel`, `.grinderBurrs`, `.grinderSetting` SHALL all be the empty string `""`
- **AND** `currentBean.inferredFields` SHALL be absent
- **AND** `currentBean.inferredFromShotId` SHALL be absent

### Requirement: dialing_get_context.shotAnalysis SHALL be prose, not a JSON envelope

The `dialing_get_context` response field `result.shotAnalysis` SHALL be a prose markdown string carrying the `## Shot Summary` block, `## Phase Data` block, and `## Detector Observations` block — the same content the in-app advisor's user-prompt envelope carries under its own `shotAnalysis` key. The field SHALL NOT carry a JSON-encoded object; specifically, it SHALL NOT carry an embedded copy of `currentBean`, `profile`, `tastingFeedback`, or any other structured field that the response already exposes at the top level.

The structured fields (`currentBean`, `profile`, `tastingFeedback`, `dialInSessions`, `bestRecentShot`, `sawPrediction`, `grinderContext`) continue to live exactly once at the top level of the response. The `shotAnalysis` field carries only the prose body the system prompt teaches the AI to read.

`ShotSummarizer::buildShotAnalysisProse(summary)` SHALL be the single source for the prose body. Both `dialing_get_context.shotAnalysis` and the in-app advisor's user-prompt envelope's `shotAnalysis` key SHALL produce byte-identical prose for identical input — the prose comes from the same private renderer in both code paths.

#### Scenario: dialing_get_context.shotAnalysis is a prose string

- **GIVEN** any successful `dialing_get_context` call
- **WHEN** the response is assembled
- **THEN** `result.shotAnalysis` SHALL be a string starting with `## Shot Summary` (or the equivalent prose body header the renderer emits)
- **AND** parsing `result.shotAnalysis` as JSON via `QJsonDocument::fromJson(...)` SHALL fail (the value is prose, not an object)

#### Scenario: dialing_get_context.shotAnalysis carries no structured-field block names

- **GIVEN** any successful `dialing_get_context` call against a shot with populated DYE state
- **WHEN** the response is assembled
- **THEN** `result.shotAnalysis` SHALL NOT contain the substring `"currentBean"` (the JSON key name that the previous nested envelope embedded)
- **AND** `result.shotAnalysis` SHALL NOT contain the substring `"tastingFeedback"`
- **AND** `result.shotAnalysis` SHALL NOT contain `\"profile\":` or any other structured-field block-name token in JSON form

#### Scenario: dialing_get_context.shotAnalysis matches the in-app advisor's shotAnalysis field byte-for-byte

- **GIVEN** a fixed resolved shot
- **WHEN** `dialing_get_context.shotAnalysis` is captured AND the in-app advisor's user-prompt envelope's `shotAnalysis` field is captured for the same shot
- **THEN** the two strings SHALL be `==` (byte-for-byte identical)
- **AND** both SHALL come from `ShotSummarizer::buildShotAnalysisProse(summary)` — no other prose builder may produce either value

### Requirement: dialing_get_context response SHALL NOT double-ship currentBean / profile / tastingFeedback

The `dialing_get_context` response SHALL contain `currentBean`, `profile`, and `tastingFeedback` exactly once each, at the top level of the response. The values previously embedded inside the `shotAnalysis` field's JSON envelope SHALL NOT appear at any nesting level.

#### Scenario: top-level structured blocks remain unchanged

- **GIVEN** any successful `dialing_get_context` call
- **WHEN** the response is assembled
- **THEN** `result.currentBean`, `result.profile`, `result.tastingFeedback` SHALL be emitted at the top level exactly as the existing requirements specify

#### Scenario: no nested copies inside shotAnalysis

- **GIVEN** any successful `dialing_get_context` call
- **WHEN** the response is assembled
- **THEN** `result.shotAnalysis` SHALL NOT contain a JSON-encoded copy of `currentBean`, `profile`, or `tastingFeedback`
- **AND** the LLM SHALL see each of those three blocks exactly once

### Requirement: Dialing block builders SHALL be shared between MCP and in-app advisor surfaces

The block-construction code that produces `dialInSessions`, `bestRecentShot`, `sawPrediction`, and `grinderContext` for `dialing_get_context` SHALL be exported from a shared header (`src/mcp/mcptools_dialing_blocks.h`) so both `mcptools_dialing.cpp` and the in-app advisor's user-prompt enrichment path call the same code. Inline construction of these blocks inside `mcptools_dialing.cpp` SHALL be removed once the helpers are in place.

The shared module SHALL expose, at minimum:

- `QJsonArray buildDialInSessionsBlock(QSqlDatabase& db, const QString& profileKbId, qint64 resolvedShotId, int historyLimit)` — same `kDialInSessionGapSec` threshold, same `groupSessions` + `hoistSessionContext` composition, same per-shot serialization.
- `QJsonObject buildBestRecentShotBlock(QSqlDatabase& db, const QString& profileKbId, qint64 resolvedShotId, const ShotProjection& currentShot)` — same `kBestRecentShotWindowDays = 90` constant, same `enjoyment > 0` filter, same `changeFromBest` diff via `McpDialingHelpers::buildShotChangeDiff`.
- `QJsonObject buildGrinderContextBlock(QSqlDatabase& db, const QString& grinderModel, const QString& beverageType, const QString& beanBrand)` — same bean-scoped → cross-bean fallback semantics, same `allBeansSettings` tagging when bean-scoped is sparse.
- `QJsonObject buildSawPredictionBlock(Settings* settings, ProfileManager* profileManager, const ShotProjection& currentShot)` — main-thread only (touches `settings->calibration()` and `profileManager->baseProfileName()`); same espresso-only, scale + profile, and flow-data-present gates as today.

The response shape of `dialing_get_context` SHALL remain byte-equivalent after the refactor. Existing `tst_mcptools_dialing` tests SHALL pass without modification (the change is a pure refactor at the dialing tool's surface).

#### Scenario: dialing_get_context response is byte-equivalent before and after the refactor

- **GIVEN** a fixed DB state, a fixed resolved shot, and fixed Settings + ProfileManager state
- **WHEN** `dialing_get_context` is invoked before the refactor and after the refactor
- **THEN** the two response JSON strings SHALL be byte-for-byte identical

#### Scenario: Shared helpers are the single source of truth for the four blocks

- **GIVEN** any change to the shape, gating, or content of `dialInSessions`, `bestRecentShot`, `sawPrediction`, or `grinderContext`
- **WHEN** the change is made
- **THEN** it SHALL be made in `src/mcp/mcptools_dialing_blocks.h` (or its `.cpp`)
- **AND** both `dialing_get_context` and the in-app advisor SHALL pick up the change automatically because both call the helpers

