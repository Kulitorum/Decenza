## MODIFIED Requirements

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

### Requirement: currentBean SHALL expose a beanFreshness block instead of precomputed days-since-roast

`currentBean.daysSinceRoast` and `currentBean.daysSinceRoastNote` SHALL NOT be present in the response. They are replaced by `currentBean.beanFreshness`, a structured block built from the resolved shot's snapshotted `roastDate`, `frozenDate`, `defrostDate`, `storageHint`, and `openedDate`:

- `roastDate` — the resolved shot's saved `roastDate` string, surfaced verbatim, when non-empty.
- `frozenDate` / `defrostDate` — surfaced verbatim when set.
- `storageHint` / `openedDate` — surfaced verbatim when set (the non-frozen analogue of `frozenDate`/`defrostDate`; `storageHint` never has a "frozen" value — see `bag-freeze-lifecycle`).
- `freshnessKnown` — boolean. `true` when at least one of `frozenDate`, `defrostDate`, or `openedDate` is set; `false` otherwise (no storage history recorded at all).
- `instruction` — one of two fixed strings selected by `freshnessKnown`:
  - When `false`: an imperative telling the AI calendar age from `roastDate` is NOT freshness (many users freeze/thaw or rotate portions) and to ASK the user about storage before applying any bean-aging guidance.
  - When `true`: an imperative telling the AI storage history is known — do NOT ask about it — and to age the beans from the most recent of `defrostDate`/`openedDate` (whichever is set), not from `roastDate`. This instruction SHALL also teach the reverse-direction case: a *recent* `defrostDate`/`openedDate` can mean the portion is under-rested/gassy (chokes, runs long, over-extracts, may want a coarser grind that settles back over the following days), not merely "less stale" — the AI SHALL NOT treat a recent thaw/open date as unconditionally meaning "fresher is better."

The block SHALL be omitted entirely when `roastDate` is empty AND no lifecycle field (`frozenDate`, `defrostDate`, `storageHint`, `openedDate`) is set. The block SHALL NOT contain a precomputed day count under any field name; the AI MUST do the subtraction itself, in front of the user, to make the assumption visible.

The `shotAnalysis` prose SHALL NOT contain the parenthetical "(N days since roast, not necessarily freshness — ask about storage)" that previously rendered next to the bean name. It is replaced by the lighter "(roasted YYYY-MM-DD; ask user about storage before reasoning about age)" — same caveat, no day count. (This prose clause is unchanged by this delta; it is carried forward verbatim from the baseline requirement for continuity with the later PR-2-scope requirements in this file that govern whether/where that clause still renders.)

#### Scenario: beanFreshness emits with freshnessKnown false and the ask-about-storage instruction
- **GIVEN** a resolved shot with `roastDate = "2026-04-15"` and no `frozenDate`/`defrostDate`/`storageHint`/`openedDate`
- **WHEN** the response is built
- **THEN** `currentBean.beanFreshness.roastDate` SHALL be `"2026-04-15"`
- **AND** `currentBean.beanFreshness.freshnessKnown` SHALL be `false`
- **AND** `currentBean.beanFreshness.instruction` SHALL match the ask-about-storage imperative
- **AND** `currentBean` SHALL NOT contain `daysSinceRoast` or `daysSinceRoastNote` under any spelling
- **AND** `currentBean.beanFreshness` SHALL NOT contain a `daysSinceRoast`, `calendarDaysSinceRoast`, `effectiveAgeDays`, or any other precomputed-day-count field

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

## ADDED Requirements

### Requirement: bestRecentShot SHALL carry its own snapshotted lifecycle state

`bestRecentShot` (a single candidate object, not a session list — no hoisting concept applies) SHALL carry the candidate shot's own snapshotted `frozenDate`/`defrostDate`/`storageHint`/`openedDate` directly, unconditionally when set, the same way it already carries `grinderModel`/`beanBrand`/`beanType` directly.

No additional instruction block, precomputed "different portion" boolean, or day-count field is added on top of this — for `bestRecentShot` or for the `dialInSessions` lifecycle-field hoisting in the requirement above. Exposing the raw dates is the whole fix: it follows the same "shot-variable field, emitted only when it differs from session context" shape the payload already uses for `grinderBrand`/`beanBrand`, which the system prompt already teaches the AI to read as a signal that something meaningful changed between shots. Before this change, historical shots carried no lifecycle data at all, so there was nothing for the AI to compare — that gap, not a missing instruction, was the actual bug behind the #1032 follow-up comment's near-miss. Whether the model reliably acts on the now-exposed dates without further prompting is an open question to check post-ship (see design.md), not something either requirement mandates.

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
