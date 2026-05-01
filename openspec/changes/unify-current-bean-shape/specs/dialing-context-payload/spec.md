# dialing-context-payload — `currentBean` source unification

## ADDED Requirements

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

## MODIFIED Requirements

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
