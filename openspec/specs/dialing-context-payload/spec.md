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

The response SHALL NOT include the static framing strings `currentBean.inferredNote`, `currentBean.daysSinceRoastNote`, or `tastingFeedback.recommendation`. These are taught once in the system prompt rather than shipped on every call. The structural fields they previously qualified — `currentBean.inferredFromShotId`, `currentBean.inferredFields`, `currentBean.beanFreshness`, `tastingFeedback.hasEnjoymentScore` / `.hasNotes` / `.hasRefractometer` — SHALL remain.

The `shotAnalysisSystemPrompt` SHALL include a "How to read structured fields" section covering: (a) `inferredFields` semantics (fields inferred from the most recent shot, confirm before recommending), (b) `tastingFeedback` gating (when all `has*` booleans are false, ASK the user about taste before suggesting changes), and (c) `beanFreshness` gating (never quote calendar age until `freshnessKnown == true`).

The `currentBean.daysSinceRoastNote` field is replaced by the `beanFreshness.instruction` field defined in the next requirement, NOT by a system-prompt entry — the freshness instruction is bean-state-specific and benefits from sitting next to the `roastDate` field.

#### Scenario: Inferred fields ship without a per-call note

- **GIVEN** a `dialing_get_context` call where the resolved shot's grinder fields back-fill blank DYE
- **WHEN** the response is built
- **THEN** `currentBean.inferredFromShotId` and `currentBean.inferredFields` SHALL be present
- **AND** `currentBean.inferredNote` SHALL NOT be present

#### Scenario: Tasting feedback ships booleans only

- **GIVEN** a shot with no enjoyment score, no notes, and no TDS
- **WHEN** the response is built
- **THEN** `tastingFeedback.hasEnjoymentScore`, `.hasNotes`, `.hasRefractometer` SHALL all be `false`
- **AND** `tastingFeedback.recommendation` SHALL NOT be present

#### Scenario: System prompt teaches the field semantics

- **GIVEN** the espresso `shotAnalysisSystemPrompt` output
- **WHEN** rendered
- **THEN** the output SHALL contain guidance for `inferredFields` (the AI must confirm before acting on inferred values)
- **AND** SHALL contain guidance for `tastingFeedback` (when all `has*` are false, ask first)
- **AND** SHALL contain guidance for `beanFreshness` (never quote age until `freshnessKnown == true`)

### Requirement: dialInSessions[].shots[] SHALL NOT include roastDate

Per-shot entries in `dialInSessions[].shots[]` SHALL NOT carry a `roastDate` field. Aging-trend reasoning across an iteration session would require subtracting `roastDate` from each shot's `timestamp` — a calculation that is misleading when storage history is unknown (frozen vs counter), and within-session bean rotation is already encoded in `changeFromPrev.beanBrand`.

The single canonical surface for `roastDate` in the response is `currentBean.beanFreshness.roastDate` (defined in the next requirement). The AI can quote that one field; it cannot silently derive aging trends from a sequence of historical shots.

#### Scenario: No per-shot roastDate in dialInSessions

- **GIVEN** any `dialing_get_context` response with a populated `dialInSessions`
- **WHEN** the response is inspected
- **THEN** for every session and every shot within `shots[]`, the `roastDate` field SHALL be absent

### Requirement: currentBean SHALL expose a beanFreshness block instead of precomputed days-since-roast

`currentBean.daysSinceRoast` and `currentBean.daysSinceRoastNote` SHALL NOT be present in the response. They are replaced by `currentBean.beanFreshness`, a structured block with three fields:

- `roastDate` — the user-entered date string, surfaced verbatim.
- `freshnessKnown` — boolean. Until a separate change introduces storage-mode tracking, this SHALL always be `false`.
- `instruction` — a fixed imperative string: `"Calendar age from roastDate is NOT freshness — many users freeze and thaw weekly. ASK the user about storage before applying any bean-aging guidance."`

The block SHALL be omitted entirely when `roastDate` is empty (no user-entered date). The block SHALL NOT contain a precomputed day count under any field name; the AI MUST do the subtraction itself, in front of the user, to make the assumption visible.

The `shotAnalysis` prose SHALL NOT contain the parenthetical "(N days since roast, not necessarily freshness — ask about storage)" that previously rendered next to the bean name. It is replaced by the lighter "(roasted YYYY-MM-DD; ask user about storage before reasoning about age)" — same caveat, no day count.

#### Scenario: beanFreshness emits with freshnessKnown false and the imperative instruction

- **GIVEN** a `currentBean` with `roastDate = "2026-04-15"` and no storage-mode information
- **WHEN** the response is built
- **THEN** `currentBean.beanFreshness.roastDate` SHALL be `"2026-04-15"`
- **AND** `currentBean.beanFreshness.freshnessKnown` SHALL be `false`
- **AND** `currentBean.beanFreshness.instruction` SHALL match the fixed imperative string verbatim
- **AND** `currentBean` SHALL NOT contain `daysSinceRoast` or `daysSinceRoastNote` under any spelling
- **AND** `currentBean.beanFreshness` SHALL NOT contain a `daysSinceRoast`, `calendarDaysSinceRoast`, `effectiveAgeDays`, or any other precomputed-day-count field

#### Scenario: Empty roastDate omits the block entirely

- **GIVEN** a `currentBean` with no `roastDate` (DYE field blank, no shot fallback)
- **WHEN** the response is built
- **THEN** `currentBean.beanFreshness` SHALL be absent

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

