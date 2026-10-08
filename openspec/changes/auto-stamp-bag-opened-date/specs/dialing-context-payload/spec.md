## MODIFIED Requirements

### Requirement: dialInSessions SHALL hoist common shot identity to a session-level context

Each session in `dialInSessions` SHALL carry a `context` object holding the identity fields shared by every shot in the session: `grinderBrand`, `grinderModel`, `grinderBurrs`, `basketBrand`, `basketModel`, `puckPrep`, `beanBrand`, `beanType`, `frozenDate`, `defrostDate`, `storageHint`, `openedDate`. When a field's value is identical across all shots in the session, it SHALL appear in `context` only — not on the per-shot entries. When a field's value differs on a particular shot in the session, that shot's entry SHALL carry the field directly, overriding the session context for that shot.

The basket and puck-prep fields SHALL be present so the model can name the equipment the session's shots were pulled on. Because the history is scoped to one equipment package, they are shared across every shot in a session by construction and hoist to `context` in practice; the per-shot override mechanism SHALL still apply to them, so no reader depends on that being true.

The first shot of every session SHALL be the reference for the `context` object's values when at least one shot in the session has a non-empty value for the field. When no shot in the session has a non-empty value, the field SHALL be omitted from `context` entirely.

An empty storage date (`frozenDate`, `defrostDate`, `openedDate`) on a shot is a recorded fact, not a gap: when the context carries a value for one, a shot that recorded none SHALL carry that field as an explicit `null` rather than inheriting the context's date. Other fields keep inheriting (a legacy shot with no recorded burrs reads as the session's).

The session's `shotCount`, `sessionStart`, `sessionEnd`, and `shots[]` array SHALL remain. Per-shot entries SHALL continue to carry shot-variable fields (`id`, `timestamp`, `doseG`, `yieldG`, `durationSec`, `grinderSetting`, `notes`, `enjoyment0to100`, `temperatureOverrideC`, `targetWeightG`, `changeFromPrev`).

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

### Requirement: currentBean SHALL expose a beanFreshness block instead of precomputed days-since-roast

`currentBean.daysSinceRoast` and `currentBean.daysSinceRoastNote` SHALL NOT be present in the response. They are replaced by `currentBean.beanFreshness`, a structured block built from the resolved shot's snapshotted `roastDate`, `frozenDate`, `defrostDate`, `storageHint`, and `openedDate`:

- `roastDate` — the resolved shot's saved `roastDate` string, surfaced verbatim, when non-empty.
- `frozenDate` / `defrostDate` — surfaced verbatim when set.
- `storageHint` / `openedDate` — surfaced verbatim when set (`storageHint` never has a "frozen" value — see `bag-freeze-lifecycle`).
- `referenceDate` — the local date the shot was pulled (today for a live snapshot), the date every age is measured to.
- `freshnessKnown` — boolean. `true` when `frozenDate` or `defrostDate` is set, or `openedDate` is set together with a `storageHint`; `false` otherwise. An `openedDate` alone does not count: every bag gets one from its first shot.
- `restAgeDays` — present only when `freshnessKnown` is `true` and the dates allow it: the beans' age at `referenceDate` with frozen time removed (`roastDate`→`frozenDate` plus `defrostDate`→`referenceDate`), computed once in C++ (`DialingHelpers::restAgeDays`).
- `roastDateText` — instead of `roastDate` when the stored roast date is not `yyyy-MM-dd` (legacy free text), so it is never computed with.
- `instruction` — selected by `freshnessKnown` and the presence of a `storageHint`, in three states:
  - When `false` and no `storageHint`: an imperative teaching the freshness ASYMMETRY — `roastDate` is the UPPER BOUND on staleness because freezing/airtight/vacuum storage only pauses staling, so beans are never older than their calendar age since roast, only fresher. Therefore the AI SHALL treat a *recent* roast as fresh WITHOUT asking about storage (nothing storage could reveal makes recently-roasted beans stale), and SHALL ask about storage ONLY when the roast date is old (the sole case where freshness is genuinely ambiguous: frozen-since-roast-and-fresh vs left-out-and-stale). The AI judges "recent vs old" itself — the block ships no day count.
  - When `false` and a `storageHint` IS set: the same upper-bound imperative PLUS a clause stating the storage TYPE is already known (naming the hint). The AI SHALL NOT re-ask how the beans are stored; at most — and only when the roast is old — it SHALL ask solely for the aging-start date. `freshnessKnown` stays `false` because a hint without a date is not a precise aging anchor.
  - When `true`: an imperative telling the AI storage history is known — do NOT ask about it — and that only freezing pauses aging. With `frozenDate`, age = days from `roastDate` to `frozenDate` plus days from `defrostDate` to `referenceDate`; with no `defrostDate` the beans are ground straight from the freezer and age = `roastDate` to `frozenDate`. Without `frozenDate`, age runs from `roastDate` to `referenceDate`. `openedDate` SHALL be described as air exposure for this portion, NOT a reset of age. This instruction SHALL also teach that low age (about a week or less) means under-rested/gassy beans (choke, run long, over-extract, may want a coarser grind that settles back over the following days) — including a portion frozen soon after roast and just thawed — so the AI SHALL NOT treat a recent date as unconditionally meaning "fresher is better."

The block SHALL be omitted entirely when `roastDate` is empty AND no lifecycle field (`frozenDate`, `defrostDate`, `storageHint`, `openedDate`) is set. When storage is NOT known the block SHALL NOT contain a day count under any field name: without storage history a calendar age misleads. An `openedDate` earlier than `defrostDate` belongs to the previous portion and SHALL be omitted.

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

### Requirement: dialing_get_context response SHALL contain a single canonical surface for the user's roast date

Roast-date keys SHALL appear only at `currentBean.beanFreshness.roastDate` (or `roastDateText`), `dialInSessions[].context.roastDate` with its per-shot override, and `bestRecentShot.roastDate`. The history and anchor copies identify WHICH roast a shot was (two roasts of one coffee are different beans); bean age reaches the AI only as `restAgeDays`, and only where storage is known.

This requirement is verifiable by structural inspection — independent of which parts of the payload are populated for any given call. When `currentBean.beanFreshness` is omitted (no roast date entered), the requirement is satisfied trivially.

The `shotAnalysis` prose body is also subject to this requirement at the *content* level: the prose SHALL NOT contain any phrase of the form `"N days since roast"`, `"N days post-roast"`, `"N-day-old"`, or any standalone integer immediately adjacent to a roast date string. **[PR 1, in effect now]**

**[PR 2 scope]** Per the canonical-source separation requirement above, the prose SHALL NOT contain the literal `"roasted YYYY-MM-DD"` string either — the date lives exclusively in `currentBean.beanFreshness.roastDate`. PR 1 still carries `, roasted YYYY-MM-DD (ask user about storage before reasoning about age)` in the prose Coffee line; this strict-strip rule activates with PR 2's prose Coffee/Grinder removal. The system prompt teaches the AI that bean-age reasoning starts from `currentBean.beanFreshness`, never from the prose.

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
- **AND** the `shotAnalysis` prose SHALL NOT contain `"## Profile Recipe"` (it lives in `result.profile.recipe`)
- **AND** the `shotAnalysis` prose SHALL NOT contain a `"Coffee:"`, `"Beans:"`, or `"Grinder:"` line for the resolved shot (these live in `currentBean` and `dialInSessions[].context`)

### Requirement: bestRecentShot SHALL carry its own snapshotted lifecycle state

`bestRecentShot` (a single candidate object, not a session list — no hoisting concept applies) SHALL carry the candidate shot's own snapshotted `frozenDate`/`defrostDate`/`storageHint`/`openedDate` directly, unconditionally when set, the same way it already carries `grinderModel`/`beanBrand`/`beanType` directly.

It SHALL also carry its `roastDate`, its `restAgeDays` at the time it was pulled when its storage was known (as `beanFreshness`), and `sameBagAsCurrent` when both shots record a bag, so the AI can tell whether the anchor's beans were comparable. History shots in `dialInSessions` SHALL carry their own `restAgeDays` the same way.

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

## REMOVED Requirements

### Requirement: dialInSessions[].shots[] SHALL NOT include roastDate
**Reason**: Its rationale no longer holds. Bean rotation is not carried in `changeFromPrev` (the dial-in comparison drops the bean group), so a restock of the same coffee was invisible; and history shots now carry their storage dates and `restAgeDays`, so a roast date identifies the roast without inviting calendar-age guesses.
**Migration**: `roastDate` is hoisted to `dialInSessions[].context` like the other shot-identity fields, with a per-shot override when it differs.
