## MODIFIED Requirements

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
- **AND** the `shotAnalysis` prose SHALL NOT contain `"## Profile Recipe"` (it lives in `result.profile.recipe`)
- **AND** the `shotAnalysis` prose SHALL NOT contain a `"Coffee:"`, `"Beans:"`, or `"Grinder:"` line for the resolved shot (these live in `currentBean` and `dialInSessions[].context`)

#### Scenario: Prose carries no roasted date once PR 2 lands

- **GIVEN** PR 2's prose Coffee/Grinder removal is in effect
- **WHEN** `shotAnalysis` is rendered
- **THEN** it SHALL NOT contain `"roasted YYYY-MM-DD"`; until then the Coffee line MAY carry `, roasted YYYY-MM-DD (ask user about storage before reasoning about age)`
- **AND** with no roast date entered and `beanFreshness` omitted, the requirement is satisfied trivially

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

### Requirement: A known storage history ages from the latest lifecycle date

When `freshnessKnown` is true, the instruction SHALL say storage history is known and SHALL NOT ask about it. It SHALL tell the AI to quote `restAgeDays` (roast to freeze plus thaw to reference date for a frozen bag; roast to reference date otherwise, since opening does not reset it), or say no age is available when none was computed. It SHALL teach that a recent thaw can mean an under-rested, gassy portion.

#### Scenario: Recent thaw is not treated as fresher
- **WHEN** `freshnessKnown` is true and the `defrostDate` is recent
- **THEN** the instruction includes the under-rested guidance and does not call the recent date fresher

### Requirement: Lifecycle fields add no instruction or day count

No instruction block or "different portion" boolean SHALL be added for `bestRecentShot` or for the `dialInSessions` lifecycle hoisting. The only day count SHALL be `restAgeDays`, sent where storage is known, and the only added boolean SHALL be `bestRecentShot.sameBagAsCurrent`.

#### Scenario: Raw dates only
- **WHEN** the response is built with lifecycle dates present
- **THEN** no instruction accompanies those dates, and the only day count is `restAgeDays` where storage is known

## REMOVED Requirements

### Requirement: dialInSessions[].shots[] SHALL NOT include roastDate
**Reason**: Its rationale no longer holds. Bean rotation is not carried in `changeFromPrev` (the dial-in comparison drops the bean group), so a restock of the same coffee was invisible; and history shots now carry their storage dates and `restAgeDays`, so a roast date identifies the roast without inviting calendar-age guesses.
**Migration**: `roastDate` is hoisted to `dialInSessions[].context` like the other shot-identity fields, with a per-shot override when it differs.

### Requirement: A differing shot carries its own identity field
**Reason**: Merged back into "dialInSessions SHALL hoist common shot identity to a session-level context", which now states the override rule.
**Migration**: None; the override behaviour is unchanged.

### Requirement: Session context takes its value from the first shot
**Reason**: Superseded: the context takes the first recorded value, not the first shot's, and a shot missing a storage date carries `null`. Both rules live in the hoist requirement.
**Migration**: None.

### Requirement: Sessions keep shot-variable fields on each shot
**Reason**: Merged back into the hoist requirement's "Per-shot entries keep the shot-variable fields" scenario.
**Migration**: None.

### Requirement: beanFreshness carries the snapshotted dates verbatim
**Reason**: Superseded by the rewritten beanFreshness requirement: an opened date alone no longer makes storage known, an opened date older than the thaw is dropped, and a non-ISO roast date goes as `roastDateText`.
**Migration**: None; readers use the fields that are present.

### Requirement: The roast date leaves the prose under PR 2
**Reason**: Merged back into the single-surface requirement's "Prose carries no roasted date once PR 2 lands" scenario; history shots now legitimately carry their roast date.
**Migration**: None.
