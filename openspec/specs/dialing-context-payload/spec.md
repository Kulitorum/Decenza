# dialing-context-payload Specification

## Purpose
Defines the payload shape of the `dialing_get_context` MCP tool and the equivalent user-prompt enrichment consumed by the in-app AI advisor — both built from the same shared `DialingBlocks` helpers so the two surfaces stay byte-equivalent. Covers session-level shot-identity hoisting and shotAnalysis prose formatting, dedup of currentBean/profile/tastingFeedback across blocks, equipment-package-resolved grinder/basket/puck-prep context, the UGS-based `grinderCalibration` grind-transfer algorithm (within-coffee conversion key, per-coffee anchor, extrapolation cap, directional-only fallback) and its dedicated `dialing_get_grinder_calibration` tool, and correctness constraints on the shipped Profile Knowledge Base (`resources/ai/profile_knowledge.json`) that these blocks read from.

## Requirements

### Requirement: dialInSessions SHALL hoist common shot identity to a session-level context

Each session in `dialInSessions` SHALL carry a `context` object holding the shot-identity fields (grinder, basket, puck prep, bean, roast date and storage dates). A field identical across the session SHALL appear only in `context`; a shot whose value differs SHALL carry it as an override; a field no shot recorded SHALL be omitted. A shot with no storage date where the context has one SHALL carry it as `null`.

#### Scenario: All shots share identity → context has all fields, shots have no overrides

- **GIVEN** a 3-shot session where every shot has the same grinder (Niche Zero, 63mm Kony) and bean (Northbound Single Origin)
- **WHEN** `dialing_get_context` builds the session
- **THEN** `session.context` SHALL contain `grinderBrand: "Niche"`, `grinderModel: "Zero"`, `grinderBurrs: "63mm Kony"`, `beanBrand: "Northbound"`, `beanType: "Single Origin"`
- **AND** none of `shots[0]`, `shots[1]`, `shots[2]` SHALL contain any of those five fields

#### Scenario: Session context names the basket and puck prep the shots were pulled on

- **GIVEN** a 3-shot session on an equipment package with a Graph Coffee "Stepped 58→46mm" basket and puck prep of shaker + puck screen + RDT
- **WHEN** `dialing_get_context` builds the session
- **THEN** `session.context` SHALL contain `basketBrand: "Graph Coffee"` and `basketModel: "Stepped 58→46mm"`
- **AND** `session.context.puckPrep` SHALL carry the recorded technique set
- **AND** none of the per-shot entries SHALL carry those fields

#### Scenario: A package with no basket recorded omits the basket fields

- **GIVEN** a session whose equipment package has no basket recorded
- **WHEN** `dialing_get_context` builds the session
- **THEN** `session.context` SHALL omit `basketBrand` and `basketModel` entirely
- **AND** SHALL NOT emit them as empty strings

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
- **THEN** `session.context` SHALL carry the shot's full identity (whichever of the twelve fields are non-empty)
- **AND** `shots[0]` SHALL NOT carry any of the hoisted fields

#### Scenario: A session spans a thaw event — the differing shot carries its own defrostDate

- **GIVEN** a 3-shot session where shots 1-2 were pulled before a thaw (`defrostDate = "2026-05-01"`) and shot 3 after a new thaw (`defrostDate = "2026-05-13"`)
- **WHEN** `dialing_get_context` builds the session
- **THEN** `session.context.defrostDate` SHALL be `"2026-05-01"` (the value shared by the first shot and the majority)
- **AND** `shots[2]` SHALL carry `defrostDate: "2026-05-13"` directly, using the identical override mechanism `beanBrand`/`grinderBrand` already use

#### Scenario: A shot with no thaw recorded does not inherit the session's thaw date

- **GIVEN** a session whose first shot has `defrostDate = "2026-05-01"` and whose last shot recorded no `defrostDate`
- **WHEN** `dialing_get_context` builds the session
- **THEN** `session.context.defrostDate` SHALL be `"2026-05-01"`
- **AND** the last shot's entry SHALL carry `defrostDate: null`

#### Scenario: Context takes the first recorded value

- **GIVEN** a session whose first shot has no recorded `grinderBurrs` and whose later shots do
- **WHEN** `dialing_get_context` builds the session
- **THEN** `session.context.grinderBurrs` SHALL be the first non-empty value, and only shots that differ from it SHALL carry an override

#### Scenario: Per-shot entries keep the shot-variable fields

- **WHEN** `dialing_get_context` builds a session
- **THEN** the session SHALL keep `shotCount`, `sessionStart`, `sessionEnd` and `shots[]`
- **AND** each shot entry SHALL carry its shot-variable fields (`id`, `timestamp`, `doseG`, `yieldG`, `durationSec`, `grinderSetting`, `notes`, `enjoyment0to100`, `temperatureOverrideC`, `targetWeightG`, `changeFromPrev`)
- **AND** the basket and puck-prep fields SHALL use the same override mechanism, although history scoped to one equipment package shares them by construction

### Requirement: dialing_get_context response SHALL NOT include a separate `shot` block

The response SHALL NOT carry a top-level `result["shot"]` block summarizing the
resolved shot. Its fields are rendered in `shotAnalysis` prose, which is the
canonical surface for shot-summary metadata; shipping both let the two versions
disagree on precision.

#### Scenario: Response has no top-level `shot` field

- **GIVEN** any successful `dialing_get_context` call
- **WHEN** the response is assembled
- **THEN** `result.shot` SHALL be absent
- **AND** the same dose / yield / duration / grinder / bean information SHALL still appear inside `result.shotAnalysis`

### Requirement: Shot-summary fields remain in the shotAnalysis prose

The fields that previously lived under `result["shot"]` (dose, yield, duration,
ratio, grinder and bean) SHALL continue to appear in the `shotAnalysis` prose
body produced by `ShotSummarizer::buildUserPrompt`.

#### Scenario: Summary fields still appear in prose

- **WHEN** the response is assembled
- **THEN** `result.shotAnalysis` contains the dose, yield, duration, grinder and bean values

### Requirement: shotAnalysis prose SHALL NOT carry the static detector-observations legend

The `## Detector Observations` section of the `shotAnalysis` prose SHALL keep
its header and per-line severity tags (`[warning]`, `[caution]`, `[good]`,
`[observation]`), but SHALL NOT carry the seven-line preamble explaining those
tags.

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

### Requirement: The severity-tag legend lives in the system prompt

The system prompt produced by `ShotSummarizer::shotAnalysisSystemPrompt`, in
both its espresso and filter variants, SHALL include the legend text so the AI's
reading of severity tags is unchanged.

#### Scenario: Filter prompt carries the legend

- **WHEN** the filter variant of the system prompt is rendered
- **THEN** its output contains the severity-tag explanation bullets

### Requirement: dialing_get_context response SHALL move static framing strings to the system prompt

The response SHALL NOT include the static framing strings
`currentBean.inferredNote`, `currentBean.daysSinceRoastNote` or
`tastingFeedback.recommendation`; these are taught once in the system prompt.
The structural fields they qualified, `currentBean.beanFreshness` and
`tastingFeedback.hasEnjoymentScore`, `.hasNotes` and `.hasRefractometer`, SHALL
remain.

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

### Requirement: Inferred-field keys are absent from currentBean

The `currentBean.inferredFromShotId` and `currentBean.inferredFields` fields
SHALL NOT appear in the response, because `currentBean` is sourced solely from
the resolved shot.

#### Scenario: Inferred keys are omitted

- **WHEN** a `currentBean` is built while live DYE is blank
- **THEN** neither `inferredFromShotId` nor `inferredFields` is present

### Requirement: The system prompt teaches the structured-field gates

The `shotAnalysisSystemPrompt` SHALL include a "How to read structured fields"
section covering `tastingFeedback` gating (ask about taste when every `has*`
boolean is false), `beanFreshness` gating (never quote calendar age until
`freshnessKnown` is true), and the meaning of empty `currentBean` strings (the
shot did not record that field, so ask before recommending a change to it). It
SHALL NOT describe `inferredFields`.

#### Scenario: Section names the three gates

- **WHEN** the espresso `shotAnalysisSystemPrompt` is rendered
- **THEN** it contains the `tastingFeedback`, `beanFreshness` and empty-string guidance and no `inferredFields` text

### Requirement: currentBean SHALL expose a beanFreshness block instead of precomputed days-since-roast

`currentBean` SHALL carry `beanFreshness` instead of `daysSinceRoast`/`daysSinceRoastNote`: the resolved shot's roast and storage dates (each verbatim when set), `referenceDate` (the shot's local date, today when live), `freshnessKnown` (a freeze or thaw date, or an opened date with a storage hint), `restAgeDays` only when known, and an `instruction` matching what is known. It SHALL be omitted when no roast date or storage field is set.

#### Scenario: beanFreshness emits with freshnessKnown false and the upper-bound instruction
- **GIVEN** a resolved shot with `roastDate = "2026-04-15"` and no `frozenDate`/`defrostDate`/`storageHint`/`openedDate`
- **WHEN** the response is built
- **THEN** `currentBean.beanFreshness.roastDate` SHALL be `"2026-04-15"`
- **AND** `currentBean.beanFreshness.freshnessKnown` SHALL be `false`
- **AND** `currentBean.beanFreshness.instruction` SHALL frame `roastDate` as the staleness upper bound, carve out recent-roast-is-fresh (no storage question), and gate the ASK on an OLD roast
- **AND** `currentBean` SHALL NOT contain `daysSinceRoast` or `daysSinceRoastNote` under any spelling
- **AND** `currentBean.beanFreshness` SHALL NOT contain a `daysSinceRoast`, `calendarDaysSinceRoast`, `effectiveAgeDays`, or any other precomputed-day-count field

#### Scenario: beanFreshness with a storageHint but no date does not re-ask storage
- **GIVEN** a resolved shot with `roastDate` set, `storageHint = "vacuum-sealed"`, and no `frozenDate`/`defrostDate`/`openedDate`
- **WHEN** the response is built
- **THEN** `currentBean.beanFreshness.freshnessKnown` SHALL be `false` (a hint without a date is not a precise aging anchor)
- **AND** `currentBean.beanFreshness.storageHint` SHALL be `"vacuum-sealed"`
- **AND** the instruction SHALL name the known storage type and direct the AI NOT to re-ask how the beans are stored — asking, at most, only for the aging-start date and only when the roast is old

#### Scenario: beanFreshness emits with freshnessKnown true from frozen/defrost dates
- **GIVEN** a resolved shot with `frozenDate` and `defrostDate` set
- **WHEN** the response is built
- **THEN** `currentBean.beanFreshness.freshnessKnown` SHALL be `true`
- **AND** the instruction SHALL count the days before freezing plus the days since `defrostDate`
- **AND** the instruction SHALL include the under-rested/gassy reverse-direction guidance

#### Scenario: An opened date alone does not make storage known
- **GIVEN** a resolved shot with `roastDate` and `openedDate` set and no `storageHint`, `frozenDate` or `defrostDate`
- **WHEN** the response is built
- **THEN** `currentBean.beanFreshness.freshnessKnown` SHALL be `false`, the instruction SHALL gate an ASK on an old roast, and no `restAgeDays` SHALL be present

#### Scenario: beanFreshness emits with freshnessKnown true from a never-frozen bag's openedDate
- **GIVEN** a resolved shot with `storageHint = "airtight"` and `openedDate` set, no `frozenDate`/`defrostDate`
- **WHEN** the response is built
- **THEN** `currentBean.beanFreshness.freshnessKnown` SHALL be `true`
- **AND** `currentBean.beanFreshness.storageHint` SHALL be `"airtight"`
- **AND** the instruction SHALL direct aging from `roastDate` and say `openedDate` does not reset it

#### Scenario: Empty shot roastDate and no lifecycle fields omits the block entirely
- **GIVEN** a resolved shot with no `roastDate` and no lifecycle fields set
- **WHEN** the response is built
- **THEN** `currentBean.beanFreshness` SHALL be absent
- **AND** the block SHALL be absent regardless of whether live DYE has a `dyeRoastDate` value

#### Scenario: shotAnalysis prose mirrors the no-day-count contract

- **GIVEN** a shot whose bean has a populated `roastDate`
- **WHEN** `shotAnalysis` prose is rendered
- **THEN** the prose SHALL contain `"roasted YYYY-MM-DD"` for the date itself
- **AND** SHALL contain a phrase pointing at storage uncertainty (e.g., `"ask user about storage"`)
- **AND** SHALL NOT contain any phrase of the form `"N days since roast"` or `"N days post-roast"` or any standalone integer adjacent to a roast-date string

#### Scenario: Rest age is sent only when storage is known

- **GIVEN** a shot with `roastDate` `2026-09-01`, `frozenDate` `2026-09-03`, `defrostDate` `2026-10-07` and `referenceDate` `2026-10-08`
- **WHEN** the response is built
- **THEN** `restAgeDays` SHALL be `3` (roast to freeze, plus thaw to reference), computed once in C++
- **AND** a block whose storage is not known SHALL carry no day count under any name
- **AND** the known-storage instruction SHALL say to quote `restAgeDays`, or, when none could be computed, that no age is available

#### Scenario: A legacy roast date goes as text

- **WHEN** the stored roast date is not exactly `yyyy-MM-dd`
- **THEN** it SHALL be sent as `roastDateText`, not `roastDate`, and no age SHALL be computed from it

#### Scenario: An opened date from a previous portion is dropped

- **WHEN** `openedDate` is earlier than `defrostDate`
- **THEN** `openedDate` SHALL be omitted, since it belongs to the portion before the latest thaw

### Requirement: The freshness instruction treats the roast date as an upper bound

When `freshnessKnown` is false and no `storageHint` is set,
`beanFreshness.instruction` SHALL teach that `roastDate` is the upper bound on
staleness, because freezing and airtight storage only pause staling. A recent
roast SHALL be treated as fresh without asking about storage, and storage SHALL
be asked about only for an old roast. The AI judges recent versus old itself.

#### Scenario: Recent roast needs no storage question

- **WHEN** `freshnessKnown` is false, no storage hint is set and the roast is recent
- **THEN** the instruction treats the beans as fresh and does not ask about storage

### Requirement: A known storage type is not asked again

When `freshnessKnown` is false but a `storageHint` is set, the instruction SHALL
keep the upper-bound teaching and SHALL name the known storage type. The AI
SHALL NOT re-ask how the beans are stored; for an old roast it MAY ask only for
the aging-start date.

#### Scenario: Known storage type is not re-asked

- **WHEN** a shot has `storageHint = "vacuum-sealed"` and no lifecycle dates
- **THEN** the instruction names vacuum-sealed storage and forbids asking how the beans are stored

### Requirement: A known storage history ages from the latest lifecycle date

When `freshnessKnown` is true, the instruction SHALL say storage history is known and SHALL NOT ask about it. It SHALL tell the AI to quote `restAgeDays` (roast to freeze plus thaw to reference date for a frozen bag; roast to reference date otherwise, since opening does not reset it), or say no age is available when none was computed. It SHALL teach that a recent thaw can mean an under-rested, gassy portion.

#### Scenario: Recent thaw is not treated as fresher
- **WHEN** `freshnessKnown` is true and the `defrostDate` is recent
- **THEN** the instruction includes the under-rested guidance and does not call the recent date fresher

### Requirement: beanFreshness is omitted only when no date is known

The `beanFreshness` block SHALL be omitted entirely when `roastDate` is empty
and no lifecycle field (`frozenDate`, `defrostDate`, `storageHint` or
`openedDate`) is set.

#### Scenario: A storage hint alone keeps the block

- **WHEN** `roastDate` is empty and only a `storageHint` is set
- **THEN** `currentBean.beanFreshness` is present

### Requirement: shotAnalysis prose uses the no-day-count phrasing

The `shotAnalysis` prose SHALL NOT contain "(N days since roast, not necessarily
freshness — ask about storage)". Beside the bean name it SHALL use "(roasted
YYYY-MM-DD; ask user about storage before reasoning about age)".

#### Scenario: Old parenthetical is gone

- **WHEN** the prose is rendered for a bean with a roast date
- **THEN** no "days since roast" parenthetical appears

### Requirement: bestRecentShot SHALL carry its own snapshotted lifecycle state

`bestRecentShot` SHALL carry the candidate shot's own snapshotted `frozenDate`, `defrostDate`, `storageHint`, `openedDate` and `roastDate` when set, its `restAgeDays` at the time it was pulled when its storage was known, and `sameBagAsCurrent` when both shots record a bag, so the AI can tell whether the anchor's beans were comparable. History shots in `dialInSessions` SHALL carry their own `restAgeDays` the same way.

#### Scenario: A best-recent-shot anchor predates the current portion's thaw
- **GIVEN** the resolved shot has `defrostDate = "2026-05-13"` and the `bestRecentShot` candidate has `defrostDate = "2026-05-01"` (a different, longer-rested portion)
- **WHEN** `dialing_get_context` builds the response
- **THEN** `bestRecentShot` SHALL carry its own `defrostDate = "2026-05-01"`, distinct from the resolved shot's `2026-05-13`
- **AND** the AI has, for the first time, the raw data needed to notice the mismatch, with no forced instruction dictating what it does with it

#### Scenario: No lifecycle data recorded — behavior unchanged
- **GIVEN** a `bestRecentShot` candidate with no `frozenDate`/`defrostDate`/`storageHint`/`openedDate` recorded (a legacy shot predating this feature)
- **WHEN** `dialing_get_context` builds the response
- **THEN** no lifecycle fields SHALL appear on that entry
- **AND** the AI receives no cross-portion signal for it, same as today

#### Scenario: The anchor came from an earlier roast of the same coffee
- **GIVEN** a `bestRecentShot` from a finished bag of the same coffee, roasted `2026-07-22`, while the current bag was roasted `2026-09-01`
- **WHEN** the response is built
- **THEN** `bestRecentShot.roastDate` SHALL be `"2026-07-22"` and `bestRecentShot.sameBagAsCurrent` SHALL be `false`

### Requirement: Lifecycle fields add no instruction or day count

No instruction block or "different portion" boolean SHALL be added for `bestRecentShot` or for the `dialInSessions` lifecycle hoisting. The only day count SHALL be `restAgeDays`, sent where storage is known, and the only added boolean SHALL be `bestRecentShot.sameBagAsCurrent`.

#### Scenario: Raw dates only
- **WHEN** the response is built with lifecycle dates present
- **THEN** no instruction accompanies those dates, and the only day count is `restAgeDays` where storage is known

### Requirement: grinderContext.settingsObserved SHALL be scoped to the current bean

`grinderContext.settingsObserved` SHALL be filtered to shots whose `bean_brand`
matches the resolved shot's `beanBrand`. When the bean-scoped query returns at
least 2 distinct settings, the response SHALL emit `settingsObserved` with the
bean-scoped list and SHALL NOT emit `allBeansSettings`.

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

### Requirement: Sparse bean history adds allBeansSettings

When the bean-scoped query returns fewer than 2 distinct settings, the response
SHALL also emit `allBeansSettings` carrying the cross-bean list, explicitly
tagged so it is not read as bean-specific. The bean-scoped `settingsObserved`
SHALL still be emitted alongside it.

#### Scenario: Single bean-scoped setting adds the cross-bean list

- **WHEN** the bean-scoped query returns one distinct setting
- **THEN** both the bean-scoped `settingsObserved` and `allBeansSettings` are present

### Requirement: An empty bean brand falls back to the cross-bean list

When the resolved shot's `beanBrand` is empty, the response SHALL emit
`settingsObserved` with the cross-bean list and SHALL NOT emit
`allBeansSettings`.

#### Scenario: No bean brand yields the legacy list

- **WHEN** the resolved shot has no `beanBrand`
- **THEN** `settingsObserved` holds the cross-bean settings and `allBeansSettings` is absent

### Requirement: Profile metadata SHALL appear in exactly one structured block [PR 2 scope]

The response SHALL carry a top-level `result.profile` block with `filename`,
`title`, `intent`, `steps`, `targetWeightG`, `targetTemperatureC`, and
`recommendedDoseG` (omitted when the profile has no recommended dose). It is the
single canonical source for profile metadata.

#### Scenario: Profile metadata lives only in result.profile

- **GIVEN** any successful `dialing_get_context` response
- **WHEN** the response is inspected
- **THEN** `result.profile` SHALL contain `filename`, `title`, `intent`, `steps`, `targetWeightG`, `targetTemperatureC` (all populated when the source profile has them)
- **AND** `result.currentProfile` SHALL NOT be present
- **AND** `result.shotAnalysis` SHALL NOT contain the substring `"Profile:"` (as a section/field label) or `"Profile intent:"` or `"## Profile Steps"`

#### Scenario: History rendering hoists profile constants to one section header

- **GIVEN** the in-app advisor renders 4 historical shots on the same profile via `AIManager::buildRecentShotContext`
- **WHEN** the rendered prose is inspected
- **THEN** `Profile intent:` SHALL appear at most once in the prose
- **AND** `## Profile Steps` SHALL appear at most once in the prose
- **AND** the per-shot blocks under `### Shot (date)` SHALL NOT carry those fields individually

### Requirement: The legacy currentProfile block is not emitted

The legacy `result.currentProfile` block SHALL NOT be emitted; its fields are
subsumed by `result.profile`.

#### Scenario: currentProfile is absent

- **WHEN** any `dialing_get_context` call succeeds
- **THEN** `result.currentProfile` is absent

### Requirement: Shot prose carries no profile lines

The `shotAnalysis` prose body SHALL NOT contain a `Profile:` line, a `Profile
intent:` line, or a `## Profile Steps` section. The system prompt SHALL teach
the AI to read profile metadata from `result.profile.*`.

#### Scenario: Prose omits profile fields

- **WHEN** the `shotAnalysis` prose is rendered
- **THEN** it contains no `Profile:` label and no `## Profile Steps` heading

### Requirement: History renders one profile header

When `AIManager::buildRecentShotContext` and
`ShotSummarizer::buildHistoryContext` render several historical shots, they
SHALL emit one profile-level header for the history section, covering `Profile`,
`Profile intent` and `Profile Steps`, and SHALL NOT repeat those fields in per-
shot blocks.

#### Scenario: Per-shot blocks carry no profile fields

- **WHEN** the history section of several shots on one profile is rendered
- **THEN** the per-shot blocks under `### Shot (date)` carry no profile fields

### Requirement: shotAnalysis prose SHALL carry only shot-variable fields in its summary block [PR 2 scope]

The `## Shot Summary` block at the top of the `shotAnalysis` prose SHALL contain
only shot-variable fields: `Dose`, `Yield` (with delta-from-target when
present), `Ratio`, `Duration`, `Extraction` (TDS / EY) and `Overall shot peaks`
(pressure / flow with timing). Per-shot `grinderSetting` MAY appear when
present.

#### Scenario: Shot Summary prose carries dose/yield/ratio/duration but no identity

- **GIVEN** a resolved shot whose `shotAnalysis` prose is rendered
- **WHEN** the `## Shot Summary` block is inspected
- **THEN** the block SHALL contain `Dose`, `Yield`, `Duration`, `ratio` (or equivalent ratio expression)
- **AND** the block SHALL NOT contain `Coffee:`, `Beans:`, or any bean brand/type string
- **AND** the block SHALL NOT contain a `Grinder:` line carrying the model or burrs identifier; only `grinderSetting` values are permitted in the prose
- **AND** the block SHALL NOT contain the literal substring `"roasted "` followed by a date
- **AND** the prose body MAY still carry per-shot `grinderSetting` inline (e.g., as part of a comparison line) since `grinderSetting` is a shot-variable

### Requirement: Shot Summary carries no shot-invariant identity

The block SHALL NOT contain shot-invariant identity: no `Coffee:` line (bean
brand, type, roast level or roast-date string), no `Grinder:` line carrying
brand, model or burrs, and no `Profile:` or `Profile intent:` line.

#### Scenario: Summary shows no bean or grinder identity

- **WHEN** the `## Shot Summary` block is inspected
- **THEN** it contains no `Coffee:` line and no grinder model or burr identifier

### Requirement: The system prompt names where identity lives

The system prompt SHALL teach the AI: "Shot-invariant identity (bean, grinder
model+burrs, profile, roast date) lives in structured JSON blocks
(`currentBean`, `result.profile`, `dialInSessions[].context`). The
`shotAnalysis` prose carries shot-variable data only."

#### Scenario: Prompt states the identity split

- **WHEN** the espresso system prompt is rendered
- **THEN** it contains that identity-split sentence

### Requirement: dialing_get_context response SHALL contain a single canonical surface for the user's roast date

Roast-date keys SHALL appear only at `currentBean.beanFreshness.roastDate` (or `roastDateText`), `dialInSessions[].context.roastDate` with its per-shot override, and `bestRecentShot.roastDate`; these identify which roast a shot was. Bean age SHALL reach the AI only as `restAgeDays`, where storage is known. The `shotAnalysis` prose SHALL NOT contain a day count next to a roast date ("N days since roast", "N-day-old").

#### Scenario: Single canonical roast key in JSON

- **GIVEN** any `dialing_get_context` response
- **WHEN** the response JSON is recursively walked for keys containing the substring `"roast"` (case-insensitive)
- **THEN** every matching key path SHALL be one of the paths listed above
- **AND** `currentBean.daysSinceRoast` and `currentBean.daysSinceRoastNote` SHALL be absent

#### Scenario: Empirical anchor against the Northbound 80's Espresso conversation

- **GIVEN** a 4-shot iteration session on `80's Espresso` profile + `Niche Zero` grinder + `Northbound Coffee Roasters Spring Tour 2026 #2` bean (the conversation captured 2026-04-30)
- **WHEN** `dialing_get_context` is called and the response is fully assembled
- **THEN** `dialInSessions[0].context` SHALL carry `grinderBrand: "Niche"`, `grinderModel: "Zero"`, `grinderBurrs: "63mm Mazzer Kony conical"`, `beanBrand: "Northbound Coffee Roasters"`, `beanType: "Spring Tour 2026 #2"`
- **AND** none of the four entries in `dialInSessions[0].shots[]` SHALL carry any of those five fields
- **AND** the response (JSON keys + `shotAnalysis` prose content) SHALL contain zero occurrences of the substring `"days since roast"` and zero occurrences of `"days post-roast"`
- **AND** the roast date `"2026-03-30"` SHALL NOT appear anywhere in the `shotAnalysis` prose body
- **AND** the `shotAnalysis` prose SHALL NOT contain `"## Profile Steps"` (it lives in `result.profile.steps`)
- **AND** the `shotAnalysis` prose SHALL NOT contain a `"Coffee:"`, `"Beans:"`, or `"Grinder:"` line for the resolved shot (these live in `currentBean` and `dialInSessions[].context`)

#### Scenario: Prose carries no roasted date once PR 2 lands

- **GIVEN** PR 2's prose Coffee/Grinder removal is in effect
- **WHEN** `shotAnalysis` is rendered
- **THEN** it SHALL NOT contain `"roasted YYYY-MM-DD"`; until then the Coffee line MAY carry `, roasted YYYY-MM-DD (ask user about storage before reasoning about age)`
- **AND** with no roast date entered and `beanFreshness` omitted, the requirement is satisfied trivially

### Requirement: Prose carries no roast-age phrasing

The `shotAnalysis` prose SHALL NOT contain any phrase of the form "N days since
roast", "N days post-roast" or "N-day-old", or any standalone integer
immediately adjacent to a roast date string.

#### Scenario: No day-count phrasing in prose

- **WHEN** the prose is rendered for a bean with a roast date
- **THEN** none of those day-count phrases appears

### Requirement: dialing-context payload SHALL include grinderCalibration block

The system SHALL compute a `grinderCalibration` block and include it in the
payload of both `dialing_get_context` and the in-app advisor's user-prompt
enrichment. Both SHALL call `DialingBlocks::buildGrinderCalibrationBlock` and
produce byte-equivalent JSON. The block SHALL NEVER emit a numeric grinder
setting for a profile whose UGS is outside the validated range.

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

### Requirement: The grind model is baseline plus UGS times conversion key

The block SHALL model grind as `grind(profile, coffee) ≈ coffeeBaseline(coffee)
+ UGS·conversionKey`. The conversion key SHALL be coffee-independent, and the
per-coffee baseline SHALL come from a recent dialed-in shot on that coffee.

#### Scenario: Coffee baseline anchors the model

- **WHEN** a dialed-in shot exists on the current coffee
- **THEN** the profile settings are derived from that shot plus the UGS difference times the conversion key

### Requirement: ProfileKnowledge SHALL expose UGS as a parsed numeric field

The `ShotSummarizer::ProfileKnowledge` struct SHALL carry a `double ugs` field (default `NaN` — not present) and a `bool ugsInferred` field (default `false`). `loadProfileKnowledge()` SHALL populate these from the entry's `ugs.value`/`ugs.inferred` fields in the structured JSON knowledge base (see the `profile-knowledge-base` capability for the authoring schema and build-time validation). Entries with no `ugs` field (cross-profile reference material) SHALL have `ugs = NaN`.

#### Scenario: Canonical UGS value parsed correctly

- **GIVEN** an entry with `"ugs": {"value": 0.5, "inferred": false}`
- **WHEN** `loadProfileKnowledge()` parses the entry
- **THEN** `pk.ugs` SHALL be `0.5` and `pk.ugsInferred` SHALL be `false`

#### Scenario: Inferred UGS value parsed with flag

- **GIVEN** an entry with `"ugs": {"value": 0.25, "inferred": true, "note": "low-temp regime requires finer grind"}`
- **WHEN** `loadProfileKnowledge()` parses the entry
- **THEN** `pk.ugs` SHALL be `0.25` and `pk.ugsInferred` SHALL be `true`

### Requirement: currentBean SHALL describe the resolved shot's setup, not live DYE

`currentBean` SHALL be built from the resolved shot's saved bean, grinder, dose
and roastDate metadata on every surface that emits it. It SHALL contain `brand`,
`type`, `roastLevel`, `grinderBrand`, `grinderModel`, `grinderBurrs`,
`grinderSetting` and `doseWeightG`, read directly from the shot's saved fields.
Both surfaces SHALL produce byte-equivalent JSON for the same shot.

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

### Requirement: currentBean never reads live DYE

The block SHALL NOT read from `Settings::dye()` or any other live machine state,
and SHALL NOT fall back to a different shot or to live DYE. Empty shot strings
SHALL be empty strings; `doseWeightG` SHALL be the saved numeric value, which
may be `0`.

#### Scenario: Live DYE never fills a blank shot field

- **WHEN** a shot has a blank grinder field and live DYE holds a grinder value
- **THEN** the `currentBean` grinder field is the empty string

### Requirement: The prompt describes currentBean as the resolved setup

The system prompt's "How to read structured fields" section SHALL describe
`currentBean` as "the setup that produced the resolved shot" and SHALL NOT teach
an `inferredFields` reading.

#### Scenario: Prompt wording for currentBean

- **WHEN** the espresso system prompt is rendered
- **THEN** it contains "the setup that produced the resolved shot" and no `inferredFields` text

### Requirement: dialing_get_context.shotAnalysis SHALL be prose, not a JSON envelope

`result.shotAnalysis` SHALL be a prose markdown string carrying the `## Shot
Summary`, `## Phase Data` and `## Detector Observations` blocks, the same
content the in-app advisor's `shotAnalysis` carries. It SHALL NOT carry a JSON-
encoded object.

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

### Requirement: Structured fields live once at the top level

The structured fields (`currentBean`, `profile`, `tastingFeedback`,
`dialInSessions`, `bestRecentShot`, `sawPrediction` and `grinderContext`) SHALL
continue to live exactly once at the top level. `shotAnalysis` SHALL carry only
the prose body.

#### Scenario: No structured block is embedded in prose

- **WHEN** the response is assembled
- **THEN** `result.shotAnalysis` holds no JSON copy of those blocks

### Requirement: One renderer produces the shotAnalysis prose

`ShotSummarizer::buildShotAnalysisProse(summary)` SHALL be the single source for
the prose body. Both `dialing_get_context.shotAnalysis` and the in-app advisor's
`shotAnalysis` key SHALL produce byte-identical prose for identical input.

#### Scenario: Only one builder produces the prose

- **WHEN** either surface builds its `shotAnalysis`
- **THEN** the prose is produced by `buildShotAnalysisProse`

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

The block-construction code that produces `dialInSessions`, `bestRecentShot`,
`sawPrediction` and `grinderContext` SHALL live in a shared header
(`src/mcp/mcptools_dialing_blocks.h`) called by both `mcptools_dialing.cpp` and
the in-app advisor's enrichment. Inline construction of these blocks in
`mcptools_dialing.cpp` SHALL be removed.

#### Scenario: dialing_get_context response is byte-equivalent before and after the refactor

- **GIVEN** a fixed DB state, a fixed resolved shot, and fixed Settings + ProfileManager state
- **WHEN** `dialing_get_context` is invoked before the refactor and after the refactor
- **THEN** the two response JSON strings SHALL be byte-for-byte identical

#### Scenario: Shared helpers are the single source of truth for the four blocks

- **GIVEN** any change to the shape, gating, or content of `dialInSessions`, `bestRecentShot`, `sawPrediction`, or `grinderContext`
- **WHEN** the change is made
- **THEN** it SHALL be made in `src/mcp/mcptools_dialing_blocks.h` (or its `.cpp`)
- **AND** both `dialing_get_context` and the in-app advisor SHALL pick up the change automatically because both call the helpers

### Requirement: Block builder signatures

The shared module SHALL expose at least `buildDialInSessionsBlock(db,
profileKbId, resolvedShotId, historyLimit)`, `buildBestRecentShotBlock(db,
profileKbId, resolvedShotId, currentShot)`, `buildGrinderContextBlock(db,
grinderModel, beverageType, beanBrand)` and `buildSawPredictionBlock(settings,
profileManager, currentShot)`. The sawPrediction builder SHALL be main-thread
only.

#### Scenario: Both surfaces call the same builders

- **WHEN** either surface builds these blocks
- **THEN** it calls the shared functions above

### Requirement: Builders keep their constants and gates

Each builder SHALL keep its existing constants and gates: `kDialInSessionGapSec`
with `groupSessions` and `hoistSessionContext`; `kBestRecentShotWindowDays = 90`
with the `enjoyment > 0` filter and the `McpDialingHelpers::buildShotChangeDiff`
diff; the bean-scoped to cross-bean fallback; and the espresso-only, scale and
flow-data gates on sawPrediction.

#### Scenario: Gates are unchanged by the refactor

- **WHEN** the builders run on a fixed database
- **THEN** the same shots, diffs and gates apply as before the refactor

### Requirement: The refactor leaves the response unchanged

The `dialing_get_context` response SHALL remain byte-equivalent after the
refactor, and the existing `tst_mcptools_dialing` tests SHALL pass without
modification.

#### Scenario: Existing tests pass unchanged

- **WHEN** `tst_mcptools_dialing` runs after the refactor
- **THEN** it passes without edits to the test file

### Requirement: The Profile Knowledge Base SHALL assign distinct UGS positions to pressure-target-distinct profile variants

The Profile Knowledge Base (`resources/ai/profile_knowledge.json`) SHALL NOT
encode one UGS value for profile variants whose pressure targets differ
materially. Such variants SHALL be authored as distinct entries with distinct
`ugs` values and canonical `id`s, so cross-profile grind transfer gives a
directional adjustment rather than treating them as grind-equivalent.

#### Scenario: Base D-Flow keeps the chart-authoritative canonical UGS

- **GIVEN** the shipped `profile_knowledge.json` parsed by `loadProfileKnowledge()`
- **WHEN** `computeProfileKbId("D-Flow / default", "dflow")` is resolved and `ugsForKbId` / `ugsInferredForKbId` are read for the result
- **THEN** `ugsForKbId(kbId)` SHALL be `0.5`
- **AND** `ugsInferredForKbId(kbId)` SHALL be `false`

#### Scenario: D-Flow/Q resolves strictly coarser than base D-Flow and is inferred

- **GIVEN** the shipped `profile_knowledge.json` parsed by `loadProfileKnowledge()`
- **WHEN** `kbBase = computeProfileKbId("D-Flow / default", "dflow")` and `kbQ = computeProfileKbId("D-Flow / Q", "dflow")` are resolved
- **THEN** `ugsForKbId(kbQ)` SHALL be strictly greater than `ugsForKbId(kbBase)`
- **AND** `ugsInferredForKbId(kbQ)` SHALL be `true`
- **AND** `canonicalNameForKbId(kbQ)` SHALL NOT equal `canonicalNameForKbId(kbBase)`

#### Scenario: "Damian's Q" resolves to the same coarser inferred position as D-Flow/Q

- **GIVEN** the shipped `profile_knowledge.json` parsed by `loadProfileKnowledge()`
- **WHEN** `kbQ = computeProfileKbId("D-Flow / Q", "dflow")` and `kbDamianQ = computeProfileKbId("Damian's Q", "dflow")` are resolved
- **THEN** `canonicalNameForKbId(kbDamianQ)` SHALL equal `canonicalNameForKbId(kbQ)`
- **AND** `ugsForKbId(kbDamianQ)` SHALL equal `ugsForKbId(kbQ)`

#### Scenario: Damian's LRv3 resolves to the canonical Londinium/LRv3 position

- **GIVEN** the shipped `profile_knowledge.json` parsed by `loadProfileKnowledge()`
- **WHEN** `kbLrv3 = computeProfileKbId("Damian's LRv3", "dflow")` is resolved
- **THEN** `ugsForKbId(kbLrv3)` SHALL be `0`
- **AND** `ugsForKbId(kbLrv3)` SHALL be strictly less than `ugsForKbId(computeProfileKbId("D-Flow / default", "dflow"))`

#### Scenario: Shared behavioral suppression is preserved for every D-Flow variant

- **GIVEN** the shipped `profile_knowledge.json` parsed by `loadProfileKnowledge()`
- **WHEN** analysis flags are read for the kbIds of "D-Flow / default", "D-Flow / Q", and "Damian's LRv2"
- **THEN** `getAnalysisFlags(kbId)` SHALL contain `flow_trend_ok` for each of the three variants

### Requirement: D-Flow positions follow the chart and the Damian entries

The base D-Flow position SHALL remain the chart-authoritative canonical `0.5`.
D-Flow/Q (alias "Damian's Q") SHALL resolve to a strictly coarser UGS than base
D-Flow and SHALL be marked inferred. Damian's LRv3 SHALL resolve to canonical
UGS `0`.

#### Scenario: Base D-Flow stays at the chart position

- **WHEN** the base D-Flow entry is resolved
- **THEN** its UGS is `0.5` and it is not marked inferred

### Requirement: D-Flow variants keep the shared behavioral suppression

The shared behavioral false-positive suppression (`AnalysisFlags: flow_trend_ok`
and the "DO NOT flag declining pressure / pressurized soak" guidance) SHALL
remain in effect for every D-Flow variant after the split.

#### Scenario: Every D-Flow variant keeps flow_trend_ok

- **WHEN** analysis flags are read for each D-Flow variant
- **THEN** each contains `flow_trend_ok`

### Requirement: The shipped Profile Knowledge Base SHALL describe D-Flow/A-Flow as editor types, not profiles

The D-Flow and A-Flow entries of `resources/ai/profile_knowledge.json`, injected
into the in-app advisor prompt and `dialing_get_context`, SHALL describe D-Flow
and A-Flow as profile editor *types*, with the profile being the name past the
`/`. They SHALL NOT use "variant", "family" or "base D-Flow" phrasing that
implies either is itself a profile.

#### Scenario: D-Flow/A-Flow sections teach the editor model without renaming headers

- **WHEN** the D-Flow/A-Flow entries of `resources/ai/profile_knowledge.json` are rendered into the advisor prompt and `dialing_get_context`
- **THEN** the prose SHALL state D-Flow/A-Flow are editor types and the profile is the name past the `/`
- **AND** it SHALL NOT contain profile-implying "D-Flow variant/family/base D-Flow" phrasing
- **AND** every entry `id` and every `alsoMatches` alias SHALL be unchanged from before this change (drift-check)

### Requirement: Editor-level behaviour is not a profile trait

A shared-behavior grouping SHALL be expressed as "profiles built with the D-Flow
editor". The lever-decline shape and the per-profile pressure-limit clamp SHALL
be described as editor-level behavior, not as a profile trait.

#### Scenario: Clamp is described at editor level

- **WHEN** the D-Flow entries are rendered
- **THEN** the lever-decline shape and pressure-limit clamp are described as editor behavior

### Requirement: Entry ids and aliases stay stable

Entry `id`s (`d-flow`, `d-flow-q-variant`, `damians-lr-v2-v3`, `a-flow`,
`londinium`) and their `alsoMatches` arrays SHALL remain unchanged. Only `prose`
and in-entry profile-name references change.

#### Scenario: Drift check passes

- **WHEN** the entry ids and aliases are compared with the previous version
- **THEN** every id and alias is identical

### Requirement: The shipped Profile Knowledge Base SHALL reference only real built-in profile names

D-Flow and A-Flow profile names in `resources/ai/profile_knowledge.json` SHALL
correspond to shipped built-in titles in `resources/profiles/`. The stale names
`A-Flow / medium`, `A-Flow / dark`, `A-Flow / very dark` and `A-Flow / like
D-Flow` SHALL be replaced with `A-Flow / default-light`, `A-Flow / default-
medium`, `A-Flow / default-dark`, `A-Flow / default-very-dark` and `A-Flow /
default-like-dflow`.

#### Scenario: Stale A-Flow names are corrected to shipped built-ins

- **WHEN** the shipped KB is parsed/rendered
- **THEN** it SHALL NOT contain `A-Flow / medium`, `A-Flow / dark`, `A-Flow / very dark`, or `A-Flow / like D-Flow`
- **AND** it SHALL reference the actual A-Flow built-in titles as shipped in `resources/profiles/a_flow_*.json`

#### Scenario: A regression guard prevents reintroduction

- **WHEN** the test suite runs
- **THEN** a guard SHALL fail if `resources/ai/profile_knowledge.json` contains any of the stale A-Flow names
- **AND** the guard SHALL fail if a referenced D-Flow/A-Flow profile name has no corresponding `resources/profiles/*.json` title

### Requirement: Unbacked profile names are never presented

No profile name not backed by a `resources/profiles/*.json` `title` SHALL be
presented to the AI as an existing profile.

#### Scenario: Unbacked name fails the guard

- **WHEN** a knowledge-base profile name has no matching built-in title
- **THEN** the regression guard fails

### Requirement: D-Flow / La Pavoni SHALL resolve to its own KB entry, not the base D-Flow entry

The Profile Knowledge Base SHALL parse `D-Flow / La Pavoni` into its own entry
with canonical `id` `d-flow-la-pavoni-variant`, distinct from `d-flow`. It SHALL
NOT be an `alsoMatches` alias of `d-flow`; resolution SHALL be via its own
`alsoMatches: ["D-Flow / La Pavoni"]`.

#### Scenario: D-Flow / La Pavoni resolves to its own canonical name, distinct from default

- **GIVEN** the shipped `profile_knowledge.json` parsed by `loadProfileKnowledge()`
- **WHEN** `kbBase = computeProfileKbId("D-Flow / default", "dflow")` and `kbLP = computeProfileKbId("D-Flow / La Pavoni", "dflow")` are resolved
- **THEN** `canonicalNameForKbId(kbLP)` SHALL NOT equal `canonicalNameForKbId(kbBase)`

#### Scenario: D-Flow / La Pavoni resolves strictly coarser than base D-Flow and is inferred

- **GIVEN** the shipped `profile_knowledge.json` parsed by `loadProfileKnowledge()`
- **WHEN** `kbBase = computeProfileKbId("D-Flow / default", "dflow")` and `kbLP = computeProfileKbId("D-Flow / La Pavoni", "dflow")` are resolved
- **THEN** `ugsForKbId(kbLP)` SHALL be strictly greater than `ugsForKbId(kbBase)`
- **AND** `ugsInferredForKbId(kbLP)` SHALL be `true`

#### Scenario: Shared behavioral suppression is preserved for D-Flow / La Pavoni

- **GIVEN** the shipped `profile_knowledge.json` parsed by `loadProfileKnowledge()`
- **WHEN** `kbLP = computeProfileKbId("D-Flow / La Pavoni", "dflow")` is resolved
- **THEN** `getAnalysisFlags(kbLP)` SHALL contain `flow_trend_ok`

#### Scenario: The split introduces exactly one entry and no id collision

- **GIVEN** the shipped `profile_knowledge.json` before and after this change
- **WHEN** the entry count is compared and every built-in profile title is resolved
- **THEN** the entry count after SHALL be exactly the count before plus one
- **AND** every built-in profile title SHALL resolve to exactly one entry (no `id` collides with the base `d-flow` entry)

### Requirement: La Pavoni resolves coarser and inferred

`D-Flow / La Pavoni` SHALL resolve to a strictly coarser UGS than `D-Flow /
default` and SHALL be marked inferred, by the same lower-pressure-target and
84°C-fill mechanism the Q variant documents.

#### Scenario: La Pavoni is coarser and inferred

- **WHEN** `D-Flow / La Pavoni` is resolved
- **THEN** its UGS is greater than the base entry and it is marked inferred

### Requirement: La Pavoni keeps the shared suppression

The shared behavioral false-positive suppression (`AnalysisFlags: flow_trend_ok`
and the "DO NOT flag" guidance) SHALL remain in effect for `D-Flow / La Pavoni`.

#### Scenario: La Pavoni keeps flow_trend_ok

- **WHEN** analysis flags are read for `D-Flow / La Pavoni`
- **THEN** they contain `flow_trend_ok`

### Requirement: Dialing grinder context SHALL resolve via the equipment package
The grinder identity in dialing surfaces (`grinderContext`, `currentBean` setup, the grinder-calibration inputs) SHALL be resolved through the resolved shot's `equipment_id` rather than from grinder identity columns on the shot row. Inputs to the grinder-calibration block (grinder model + burrs) SHALL come from the resolved package's grinder item.

#### Scenario: grinderContext sourced from the package
- **WHEN** `dialing_get_context` builds `grinderContext` for the resolved shot
- **THEN** the grinder brand/model/burrs SHALL be resolved by following `equipment_id` to the package's grinder item

#### Scenario: Shot with no linked equipment
- **WHEN** the resolved shot has a null `equipment_id`
- **THEN** the grinder context SHALL be omitted or empty rather than fabricated

### Requirement: Dialing context SHALL expose the rpm dial-in
The dialing context SHALL include the shot's `rpm` dial-in value (when present) alongside the grind setting, and SHALL indicate whether the grinder is `rpmCapable`.

#### Scenario: rpm present on a capable grinder
- **WHEN** the resolved shot used an rpm-capable grinder and recorded an rpm
- **THEN** the dialing context SHALL include the `rpm` value

#### Scenario: rpm absent
- **WHEN** the resolved shot recorded no rpm
- **THEN** the `rpm` field SHALL be omitted

### Requirement: Dialing context SHALL expose the basket via the equipment package

The `currentBean` block SHALL include a `basket` sub-object resolved through the
package's basket item via `equipment_id`; there is no shot-level basket column.
The sub-object SHALL carry `brand`, `model`, and the registry-derived
`wallProfile`, `relativeFlow`, `precision` and `doseRangeG`. Absent fields SHALL
be omitted rather than fabricated, including for a custom basket with unknown
specs.

#### Scenario: Basket sub-object present
- **WHEN** the resolved shot's package has a registry basket
- **THEN** `currentBean.basket` SHALL include brand, model, wallProfile, relativeFlow, precision, and doseRangeG

#### Scenario: No basket on the package
- **WHEN** the resolved shot's package has no basket item
- **THEN** the `basket` sub-object SHALL be omitted

#### Scenario: Custom basket with unknown specs
- **WHEN** the resolved basket does not match the registry
- **THEN** `currentBean.basket` SHALL include brand/model and omit the unknown derived spec fields

### Requirement: Relative flow class SHALL be expressed as a directional string
The basket's `relativeFlow` SHALL be a human-readable string
(`"restrictive"` / `"standard"` / `"open"`), conveying the direction of a
cross-basket grind change, not a magnitude. The payload SHALL NOT present it as an
ordered numeric scale.

#### Scenario: Flow class string
- **WHEN** the basket sub-object is emitted
- **THEN** `relativeFlow` SHALL be one of the directional strings, not a numeric code

### Requirement: Dose range SHALL be available as an advisory sanity signal
The basket sub-object SHALL expose the basket's recommended dose range
(`doseRangeG: { min, max }`) so the advisor can flag a dose that falls outside the
basket's rated range. This is advisory only and SHALL NOT change dose ownership
(dose remains bean/recipe-scoped).

#### Scenario: Dose outside the rated range is detectable
- **WHEN** the shot's dose is above the basket's `doseRangeG.max`
- **THEN** the payload SHALL carry the range such that the advisor can detect the mismatch

### Requirement: Dialing context SHALL expose puck prep via the equipment package
The dialing context SHALL include a `puckPrep` sub-object in the `currentBean` block
(and in the shot snapshot it resolves), populated through the package's puck-prep
item via `equipment_id` — there SHALL be no separate shot-level puck-prep column.
The sub-object SHALL carry the set boolean flags and the derived `distribution`
rollup. When the resolved package has no puck-prep item, the `puckPrep` sub-object
SHALL be omitted rather than fabricated.

#### Scenario: Puck-prep sub-object present
- **WHEN** the resolved shot's package has a puck-prep item
- **THEN** `currentBean.puckPrep` SHALL include the set flags and `distribution`

#### Scenario: No puck prep on the package
- **WHEN** the resolved shot's package has no puck-prep item
- **THEN** the `puckPrep` sub-object SHALL be omitted

### Requirement: The distribution rollup SHALL be a human-readable directional string
The `distribution` rollup SHALL be one of the human-readable strings
`"none"` / `"light"` / `"thorough"`, conveying the amount of distribution effort so
the advisor can branch its channeling guidance. It SHALL NOT be a numeric code.

#### Scenario: Distribution string emitted
- **WHEN** the puck-prep sub-object is emitted
- **THEN** `distribution` SHALL be one of `"none"`, `"light"`, or `"thorough"`

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

### Requirement: grinderContext SHALL report the grinder's smallest commonly-repeated step

`grinderContext` SHALL carry a `stepSize` field giving the grinder's effective
dial step, the smallest increment the user makes repeatedly between observed
settings. It SHALL be present only when at least two distinct numeric settings
are available. One shared helper, `deriveGrindStep`, SHALL serve the
`dialing_get_context` payload, the in-app AI enrichment and the Grind quick-
select widget.

#### Scenario: Step from clean history

- **GIVEN** the grinder's observed numeric settings are 7.5, 8, 8.5, 8.75, 9
- **WHEN** `grinderContext` is built
- **THEN** `grinderContext.stepSize` SHALL be `0.25`
- **AND** the payload SHALL NOT carry a `smallestStep` field

#### Scenario: The finest repeated step wins over a more common coarse one

- **GIVEN** the grinder's observed settings are 5, 5.5, 6, 7, 7.5, 8, 8.5, 8.75, 9, 10, 12 (five 0.5 gaps, two 0.25 gaps)
- **WHEN** `grinderContext.stepSize` is derived
- **THEN** it SHALL be `0.25` (the finest repeated step), not `0.5` (the most common gap)

#### Scenario: A lone outlier does not collapse the step

- **GIVEN** the grinder's observed numeric settings are 7.5, 8, 8.5, 8.75, 9 plus a single `8.1`
- **WHEN** `grinderContext.stepSize` is derived
- **THEN** it SHALL remain `0.25`, not `0.1`

#### Scenario: Insufficient data omits the step

- **GIVEN** the grinder has fewer than two distinct numeric settings in history
- **WHEN** `grinderContext` is built
- **THEN** `grinderContext.stepSize` SHALL be omitted

### Requirement: The step estimator takes the smallest repeated gap

The estimator SHALL operate on the sorted, de-duplicated numeric settings and
return the smallest gap that occurs at least twice between consecutive values,
rounding gaps to absorb floating-point noise. When no gap repeats it SHALL fall
back to the smallest gap, clamped to a sane floor.

#### Scenario: A single mistyped setting is skipped

- **WHEN** one setting produces a gap that occurs only once
- **THEN** that gap is ignored when choosing the step

### Requirement: stepSize is grinder-wide and per-bean fields stay scoped

The step SHALL be computed grinder-model-wide across all beans and beverages, so
the widget and the AI payload cannot diverge. `settingsObserved`, min and max
SHALL remain bean-scoped as per-bean context.

#### Scenario: Step uses settings from every bean

- **WHEN** two beans together show a repeated gap that neither shows alone
- **THEN** `stepSize` reflects that gap while `settingsObserved` stays bean-scoped

### Requirement: Dial-in blocks SHALL pair RPM with the grind setting

Every dial-in surface that carries `grinderSetting` SHALL also carry the sibling
`rpm` when one is recorded (`rpm > 0`), emitted sparsely so legacy and non-RPM
shots are unchanged. RPM is shot-variable: in `dialInSessions` it SHALL appear
on the per-shot entry, never the hoisted session `context`.

#### Scenario: Per-shot RPM in dialInSessions

- **GIVEN** a session whose shots were pulled at grind `2.4` and RPM `1400`
- **WHEN** `dialInSessions` is built
- **THEN** each shot entry SHALL carry `grinderSetting: "2.4"` AND `rpm: 1400`
- **AND** `rpm` SHALL NOT appear in the session `context` hoist (it is shot-variable)

#### Scenario: RPM change appears in the diff

- **GIVEN** two consecutive shots identical except RPM moved 1400 → 1350
- **WHEN** the `changeFromPrev` diff is built
- **THEN** it SHALL report the RPM change

#### Scenario: Sparse — no RPM noise on non-RPM shots

- **GIVEN** a shot with no recorded RPM (`rpm` is 0)
- **WHEN** any dial-in block carrying its grind setting is built
- **THEN** no `rpm` field SHALL be emitted for that shot

### Requirement: RPM reaches each diff and surface

This applies to the `dialInSessions[].shots[]` entries, `bestRecentShot`, the
`changeFromPrev` and `changeFromBest` diffs (which SHALL report an RPM change
when it differs), and the prose shot-summary grind line, which SHALL render the
RPM alongside the setting.

#### Scenario: Prose grind line shows RPM

- **WHEN** the shot-summary grind line is rendered for a shot with an RPM
- **THEN** the RPM appears beside the grind setting

### Requirement: grinderContext summarizes observed RPMs

When the grinder is RPM-capable, `grinderContext` SHALL summarize the user's
observed RPMs: the observed values, their range, and a noise-filtered RPM step
from the same estimator.

#### Scenario: RPM summary on a capable grinder

- **WHEN** the grinder is RPM-capable and history contains RPM values
- **THEN** `grinderContext` carries the observed RPMs, their range and the RPM step

### Requirement: The advisor SHALL be able to recommend and score an RPM change

The `nextShot` schema SHALL include an optional integer `rpm`, which the advisor
emits only when it recommends a motor-RPM change, and only for variable-RPM
grinders. It is independent of `grinderSetting`: either, both or neither may be
recommended. Adherence SHALL score `rpm` with the same "matched within tolerance
AND the user actually moved" discipline used for grind.

#### Scenario: RPM recommendation is tracked

- **GIVEN** an advisor turn whose `nextShot` recommends `rpm: 1350` (no grind change)
- **AND** the user's next shot is pulled at 1350 RPM (moved from a prior 1400)
- **WHEN** the `recentAdvice` block is built
- **THEN** the recommendation SHALL list the predicted RPM
- **AND** adherence SHALL count the RPM as followed

#### Scenario: RPM only offered for variable-RPM grinders

- **GIVEN** a fixed-RPM grinder
- **THEN** the schema guidance SHALL instruct the model to omit `rpm`

### Requirement: Outcome renderers show RPM beside grind

The recommendation and outcome renderers SHALL surface the RPM alongside the
grind.

#### Scenario: Outcome line shows RPM

- **WHEN** a recommendation that includes an `rpm` is rendered
- **THEN** the RPM appears with the grind value

### Requirement: dialInSessions SHALL be scoped to the resolved shot's equipment package

The shot history that `dialInSessions` is built from SHALL include only shots
whose equipment package matches the resolved shot's. "No package recorded" SHALL
be a package value in its own right, matching other shots with no package
recorded and no others.

#### Scenario: Shots on a second basket are excluded from the session history

- **GIVEN** a user with two equipment packages sharing one grinder — package A with a
  straight-wall 18 g basket, package B with a stepped 58→46 mm basket
- **AND** a history containing shots on both packages, same bean and same profile
- **WHEN** `dialing_get_context` resolves a shot recorded on package B
- **THEN** `dialInSessions` SHALL contain only shots recorded on package B
- **AND** SHALL NOT contain any shot recorded on package A

#### Scenario: A user with no equipment package sees an unchanged history

- **GIVEN** a user who has never created an equipment package, so every shot has no package
  recorded
- **WHEN** `dialing_get_context` resolves any of their shots
- **THEN** `dialInSessions` SHALL contain the same shots it contained before this requirement —
  every bean/profile/window match
- **AND** no shot SHALL be excluded on equipment grounds

#### Scenario: A package fork empties the history until the new set accumulates shots

- **GIVEN** a user whose entire history is on one equipment package
- **WHEN** they change a component of that package — grinder burrs, basket, or puck prep — and
  pull a shot on the resulting new package
- **THEN** `dialInSessions` for that shot SHALL be empty
- **AND** subsequent shots on the new package SHALL accumulate into it normally

### Requirement: A different package excludes a shot

An equipment package identifies the grinder (brand, model, burrs), the basket
and the puck-prep technique set together. A different package SHALL exclude a
shot from the history regardless of how similar its bean, profile or grind
setting is.

#### Scenario: Same grind on another basket is excluded

- **WHEN** a shot has the same numeric grind setting but a different basket
- **THEN** it is excluded from `dialInSessions`

### Requirement: Bean, profile and window scoping are unchanged

The bean, profile-knowledge-id and time-window scoping of this history SHALL be
unchanged. Sessions SHALL continue to be grouped by time gaps within the matched
set.

#### Scenario: Time-gap grouping still applies

- **WHEN** matched shots are separated by a gap longer than the session threshold
- **THEN** they form separate sessions

### Requirement: bestRecentShot SHALL be selected from the resolved shot's equipment package

The `bestRecentShot` anchor SHALL be selected only from shots whose equipment
package matches the resolved shot's, in addition to the profile, rating and
time-window criteria. "No package recorded" SHALL match other shots with no
package recorded and nothing else. When no rated shot on the matching equipment
exists in the window, the block SHALL be omitted rather than falling back to
other equipment.

#### Scenario: A highly rated shot on other equipment is not offered as the anchor

- **GIVEN** a user whose highest-rated recent shot on this profile was pulled on equipment
  package A
- **WHEN** `dialing_get_context` resolves a shot on package B, and package B has its own rated
  shots in the window
- **THEN** `bestRecentShot` SHALL be the highest-rated shot from package B
- **AND** SHALL NOT be the package A shot, however much higher its rating

#### Scenario: No rated shot on this equipment omits the block

- **GIVEN** a user with rated shots on package A only
- **WHEN** `dialing_get_context` resolves a shot on package B
- **THEN** `bestRecentShot` SHALL be absent from the response
- **AND** SHALL NOT fall back to the package A shot

### Requirement: grinderContext observed settings SHALL be scoped to the equipment package

The observed-settings list, explored range and typical step in `grinderContext`
SHALL be drawn only from shots on the resolved shot's equipment package, in
addition to the grinder, beverage-type and bean scoping. "No package recorded"
SHALL match other shots with no package recorded and nothing else.

#### Scenario: Settings from another basket are not in the observed range

- **GIVEN** a user whose shots on package A used settings 7.5–10 and whose shots on package B
  used settings 16–17, all on one grinder
- **WHEN** `dialing_get_context` resolves a shot on package A
- **THEN** `grinderContext.settingsObserved` SHALL contain only the package A settings
- **AND** the reported explored range SHALL NOT extend to 17

#### Scenario: The cross-bean fallback stays within the package

- **GIVEN** a resolved shot on package A whose bean has fewer than two recorded settings
- **WHEN** `grinderContext` widens to the user's other beans
- **THEN** the widened result SHALL contain only shots from package A

### Requirement: The cross-bean fallback stays within the package

The existing cross-bean fallback, which widens to all beans when the bean-scoped
result is too sparse, SHALL remain, but SHALL stay within the equipment package
when it widens.

#### Scenario: Widening keeps the package

- **WHEN** a resolved shot on one package has too few bean-scoped settings
- **THEN** the widened result contains only shots from that package

### Requirement: grinderCalibration SHALL mine its pairs and anchor from one equipment package

The within-batch paired slopes that produce the UGS conversion key, and the
current-batch anchor shot, SHALL be drawn only from shots on the resolved shot's
equipment package. The block SHALL NOT pool shots from other packages that share
the grinder model and burrs.

#### Scenario: A cross-basket pair does not contribute to the conversion key

- **GIVEN** two shots of the same roast batch on the same grinder, one on package A and one on
  package B
- **WHEN** `grinderCalibration` mines within-batch pairs for a shot on package A
- **THEN** that pair SHALL NOT contribute to the conversion key
- **AND** only pairs whose members share package A SHALL contribute

#### Scenario: Insufficient package-scoped signal degrades to directional

- **GIVEN** a resolved shot whose equipment package has too few qualifying same-batch shots
- **AND** the user's other packages on the same grinder have plenty
- **WHEN** `grinderCalibration` is built
- **THEN** it SHALL report directional guidance only
- **AND** SHALL NOT emit a numeric setting derived from the other packages

### Requirement: Insufficient package signal degrades to directional

When the package-scoped pool has too little signal to qualify, the block SHALL
degrade to directional guidance (finer or coarser, and pull a reference shot).
It SHALL NOT widen to other packages to recover a numeric setting.

#### Scenario: No cross-package recovery of a number

- **WHEN** the package has too few qualifying same-batch shots and other packages have many
- **THEN** the block reports directional guidance only
