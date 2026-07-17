# dialing-context-payload Specification

## Purpose
Defines the payload shape of the `dialing_get_context` MCP tool and the equivalent user-prompt enrichment consumed by the in-app AI advisor — both built from the same shared `DialingBlocks` helpers so the two surfaces stay byte-equivalent. Covers session-level shot-identity hoisting and shotAnalysis prose formatting, dedup of currentBean/profile/tastingFeedback across blocks, equipment-package-resolved grinder/basket/puck-prep context, the UGS-based `grinderCalibration` grind-transfer algorithm (within-coffee conversion key, per-coffee anchor, extrapolation cap, directional-only fallback) and its dedicated `dialing_get_grinder_calibration` tool, and correctness constraints on the shipped Profile Knowledge Base (`resources/ai/profile_knowledge.json`) that these blocks read from.
## Requirements
### Requirement: dialInSessions SHALL hoist common shot identity to a session-level context

Each session in `dialInSessions` SHALL carry a `context` object holding the identity fields shared by every shot in the session: `grinderBrand`, `grinderModel`, `grinderBurrs`, `beanBrand`, `beanType`, `frozenDate`, `defrostDate`, `storageHint`, `openedDate`. When a field's value is identical across all shots in the session, it SHALL appear in `context` only — not on the per-shot entries. When a field's value differs on a particular shot in the session, that shot's entry SHALL carry the field directly, overriding the session context for that shot.

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
- **THEN** `session.context` SHALL carry the shot's full identity (whichever of the nine fields are non-empty)
- **AND** `shots[0]` SHALL NOT carry any of the hoisted fields

#### Scenario: A session spans a thaw event — the differing shot carries its own defrostDate

- **GIVEN** a 3-shot session where shots 1-2 were pulled before a thaw (`defrostDate = "2026-05-01"`) and shot 3 after a new thaw (`defrostDate = "2026-05-13"`)
- **WHEN** `dialing_get_context` builds the session
- **THEN** `session.context.defrostDate` SHALL be `"2026-05-01"` (the value shared by the first shot and the majority)
- **AND** `shots[2]` SHALL carry `defrostDate: "2026-05-13"` directly, using the identical override mechanism `beanBrand`/`grinderBrand` already use

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

`currentBean.daysSinceRoast` and `currentBean.daysSinceRoastNote` SHALL NOT be present in the response. They are replaced by `currentBean.beanFreshness`, a structured block built from the resolved shot's snapshotted `roastDate`, `frozenDate`, `defrostDate`, `storageHint`, and `openedDate`:

- `roastDate` — the resolved shot's saved `roastDate` string, surfaced verbatim, when non-empty.
- `frozenDate` / `defrostDate` — surfaced verbatim when set.
- `storageHint` / `openedDate` — surfaced verbatim when set (the non-frozen analogue of `frozenDate`/`defrostDate`; `storageHint` never has a "frozen" value — see `bag-freeze-lifecycle`).
- `freshnessKnown` — boolean. `true` when at least one of `frozenDate`, `defrostDate`, or `openedDate` is set; `false` otherwise (no storage history recorded at all).
- `instruction` — selected by `freshnessKnown` and the presence of a `storageHint`, in three states:
  - When `false` and no `storageHint`: an imperative teaching the freshness ASYMMETRY — `roastDate` is the UPPER BOUND on staleness because freezing/airtight/vacuum storage only pauses staling, so beans are never older than their calendar age since roast, only fresher. Therefore the AI SHALL treat a *recent* roast as fresh WITHOUT asking about storage (nothing storage could reveal makes recently-roasted beans stale), and SHALL ask about storage ONLY when the roast date is old (the sole case where freshness is genuinely ambiguous: frozen-since-roast-and-fresh vs left-out-and-stale). The AI judges "recent vs old" itself — the block ships no day count.
  - When `false` and a `storageHint` IS set: the same upper-bound imperative PLUS a clause stating the storage TYPE is already known (naming the hint). The AI SHALL NOT re-ask how the beans are stored; at most — and only when the roast is old — it SHALL ask solely for the aging-start date. `freshnessKnown` stays `false` because a hint without a date is not a precise aging anchor.
  - When `true`: an imperative telling the AI storage history is known — do NOT ask about it — and to age the beans from the most recent of `defrostDate`/`openedDate` (whichever is set), not from `roastDate`. This instruction SHALL also teach the reverse-direction case: a *recent* `defrostDate`/`openedDate` can mean the portion is under-rested/gassy (chokes, runs long, over-extracts, may want a coarser grind that settles back over the following days), not merely "less stale" — the AI SHALL NOT treat a recent thaw/open date as unconditionally meaning "fresher is better."

The block SHALL be omitted entirely when `roastDate` is empty AND no lifecycle field (`frozenDate`, `defrostDate`, `storageHint`, `openedDate`) is set. The block SHALL NOT contain a precomputed day count under any field name; the AI MUST do the subtraction itself, in front of the user, to make the assumption visible.

The `shotAnalysis` prose SHALL NOT contain the parenthetical "(N days since roast, not necessarily freshness — ask about storage)" that previously rendered next to the bean name. It is replaced by the lighter "(roasted YYYY-MM-DD; ask user about storage before reasoning about age)" — same caveat, no day count.

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
- **AND** the instruction SHALL direct aging from `defrostDate`, not `roastDate`
- **AND** the instruction SHALL include the under-rested/gassy reverse-direction guidance

#### Scenario: beanFreshness emits with freshnessKnown true from a never-frozen bag's openedDate
- **GIVEN** a resolved shot with `storageHint = "airtight"` and `openedDate` set, no `frozenDate`/`defrostDate`
- **WHEN** the response is built
- **THEN** `currentBean.beanFreshness.freshnessKnown` SHALL be `true`
- **AND** `currentBean.beanFreshness.storageHint` SHALL be `"airtight"`
- **AND** the instruction SHALL direct aging from `openedDate`

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

### Requirement: bestRecentShot SHALL carry its own snapshotted lifecycle state

`bestRecentShot` (a single candidate object, not a session list — no hoisting concept applies) SHALL carry the candidate shot's own snapshotted `frozenDate`/`defrostDate`/`storageHint`/`openedDate` directly, unconditionally when set, the same way it already carries `grinderModel`/`beanBrand`/`beanType` directly.

No additional instruction block, precomputed "different portion" boolean, or day-count field is added on top of this — for `bestRecentShot` or for the `dialInSessions` lifecycle-field hoisting in the requirement above. Exposing the raw dates is the whole fix: it follows the same "shot-variable field, emitted only when it differs from session context" shape the payload already uses for `grinderBrand`/`beanBrand`, which the system prompt already teaches the AI to read as a signal that something meaningful changed between shots. Before this change, historical shots carried no lifecycle data at all, so there was nothing for the AI to compare — that gap, not a missing instruction, was the actual bug behind the #1032 follow-up comment's near-miss.

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

### Requirement: The Profile Knowledge Base SHALL assign distinct UGS positions to pressure-target-distinct profile variants

The Profile Knowledge Base (`resources/ai/profile_knowledge.json`) SHALL NOT encode a single UGS value for a group of profile variants whose pressure targets differ materially. Variants that share behavioral guidance (the family abstraction) but differ in pressure target or fill temperature SHALL be authored as distinct entries with distinct `ugs` values and distinct canonical `id`s, so that cross-profile grind transfer produces a directional grinder adjustment rather than treating them as grind-equivalent.

This requirement constrains the KB content `loadProfileKnowledge()` consumes; it does not change the parser, the relative-grinder-setting anchor algorithm, or the canonical/inferred `ugs.inferred` semantics. The base D-Flow position SHALL remain the chart-authoritative canonical `0.5`. D-Flow/Q (alias "Damian's Q") SHALL resolve to a strictly coarser (numerically greater) UGS than base D-Flow and SHALL be marked inferred. Damian's LRv3 SHALL resolve to canonical UGS `0` (the chart's "Londinium / LRv3" position). The shared behavioral false-positive suppression (`AnalysisFlags: flow_trend_ok` and the "DO NOT flag declining pressure / pressurized soak" guidance) SHALL remain in effect for every D-Flow variant after the split.

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

### Requirement: The shipped Profile Knowledge Base SHALL describe D-Flow/A-Flow as editor types, not profiles

The D-Flow and A-Flow content in `resources/ai/profile_knowledge.json` (the KB injected verbatim into both the in-app advisor system prompt and `dialing_get_context`) SHALL describe `D-Flow` and `A-Flow` as Recipe Editor *types*, with the profile being the name past the `/`. It SHALL NOT use "variant", "family", or "base D-Flow" phrasing in a way that implies D-Flow or A-Flow is itself a profile; a shared-behavior grouping SHALL be expressed as "profiles built with the D-Flow editor" (or equivalent). The lever-decline shape and the per-profile pressure-limit clamp SHALL be described as editor-level behavior, not as a profile trait.

Entry `id`s (`d-flow`, `d-flow-q-variant`, `damians-lr-v2-v3`, `a-flow`, `londinium`) and their `alsoMatches` alias arrays SHALL remain stable — only `prose` and in-entry profile-name references are changed.

#### Scenario: D-Flow/A-Flow sections teach the editor model without renaming headers

- **WHEN** the D-Flow/A-Flow entries of `resources/ai/profile_knowledge.json` are rendered into the advisor prompt and `dialing_get_context`
- **THEN** the prose SHALL state D-Flow/A-Flow are editor types and the profile is the name past the `/`
- **AND** it SHALL NOT contain profile-implying "D-Flow variant/family/base D-Flow" phrasing
- **AND** every entry `id` and every `alsoMatches` alias SHALL be unchanged from before this change (drift-check)

### Requirement: The shipped Profile Knowledge Base SHALL reference only real built-in profile names

D-Flow/A-Flow profile names written in `resources/ai/profile_knowledge.json` SHALL correspond to actual shipped built-in profile titles in `resources/profiles/`. Specifically, the stale A-Flow names `A-Flow / medium`, `A-Flow / dark`, `A-Flow / very dark`, `A-Flow / like D-Flow` SHALL be replaced with the real built-ins `A-Flow / default-light`, `A-Flow / default-medium`, `A-Flow / default-dark`, `A-Flow / default-very-dark`, `A-Flow / default-like-dflow`. No profile name not backed by a `resources/profiles/*.json` `title` SHALL be presented to the AI as an existing profile.

#### Scenario: Stale A-Flow names are corrected to shipped built-ins

- **WHEN** the shipped KB is parsed/rendered
- **THEN** it SHALL NOT contain `A-Flow / medium`, `A-Flow / dark`, `A-Flow / very dark`, or `A-Flow / like D-Flow`
- **AND** it SHALL reference the actual A-Flow built-in titles as shipped in `resources/profiles/a_flow_*.json`

#### Scenario: A regression guard prevents reintroduction

- **WHEN** the test suite runs
- **THEN** a guard SHALL fail if `resources/ai/profile_knowledge.json` contains any of the stale A-Flow names
- **AND** the guard SHALL fail if a referenced D-Flow/A-Flow profile name has no corresponding `resources/profiles/*.json` title

### Requirement: D-Flow / La Pavoni SHALL resolve to its own KB entry, not the base D-Flow entry

The Profile Knowledge Base (`resources/ai/profile_knowledge.json`) SHALL parse `D-Flow / La Pavoni` into its own entry with its own canonical `id` (`d-flow-la-pavoni-variant`), distinct from `D-Flow / default`'s (`d-flow`). `D-Flow / La Pavoni` SHALL NOT be an `alsoMatches` alias of the base `d-flow` entry — resolution SHALL be via its own `alsoMatches: ["D-Flow / La Pavoni"]`, the same construction the shipped `d-flow-q-variant` entry uses.

This requirement constrains KB content `loadProfileKnowledge()` consumes; it does not change the parser, the relative-grinder-setting anchor algorithm, or the canonical/inferred `ugs.inferred` semantics. `D-Flow / La Pavoni` SHALL resolve to a strictly coarser (numerically greater) UGS than base `D-Flow / default` and SHALL be marked inferred (the same lower-pressure-target + 84°C-fill mechanism the shipped Q variant documents). The shared behavioral false-positive suppression (`AnalysisFlags: flow_trend_ok` and the "DO NOT flag declining pressure / pressurized soak / setpoint-vs-actual temperature gap" guidance) SHALL remain in effect for `D-Flow / La Pavoni` after the split.

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
The dialing context SHALL include a `basket` sub-object in the `currentBean` block
(and in the shot snapshot it resolves), populated through the package's basket item
via `equipment_id` — there SHALL be no separate shot-level basket column. The
sub-object SHALL carry `brand`, `model`, and the registry-derived `wallProfile`,
`relativeFlow`, `precision`, and `doseRangeG`. When the resolved package has no
basket, or the basket is custom with unknown specs, the absent fields SHALL be
omitted rather than fabricated.

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

