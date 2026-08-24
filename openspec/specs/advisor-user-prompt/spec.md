# advisor-user-prompt Specification

## Purpose
The single source of truth for the JSON-shaped user prompt `ShotSummarizer` and `AIManager` send the AI Advisor: the `currentBean`/`currentProfile`/`tastingFeedback`/`shotAnalysis` envelope, the DB-scoped enrichment blocks (`dialInSessions`, `bestRecentShot`, `sawPrediction`, `grinderContext`, `recentAdvice`, `grinderCalibration`) shared with `dialing_get_context`, and the byte-stability and Anthropic prompt-caching rules that make repeated calls cache-hit identically across the in-app advisor and `ai_advisor_invoke`.

## Requirements

### Requirement: AI advisor user prompt SHALL be JSON-shaped

`ShotSummarizer::buildUserPrompt(summary)` SHALL return a JSON-encoded string (indented, deterministic field ordering) carrying the structured fields the shot-analysis system prompt references. The shape mirrors `dialing_get_context`'s response for the fields available without DB / MainController scope:

- `currentBean` — DYE-resolved bean and grinder identity. SHALL include `brand`, `type`, `roastLevel`, `grinderBrand`, `grinderModel`, `grinderBurrs`, `grinderSetting`, `doseWeightG`. SHALL include `beanFreshness` (with `roastDate`, `freshnessKnown: false`, and the storage-mode `instruction`) when DYE roastDate is non-empty. SHALL include `inferredFromShotId` and `inferredFields[]` when grinder/dose fields fell back to the resolved shot's values.
- `currentProfile` — `filename`, `title`, `intent`, `recipe`, `targetWeightG`, `targetTemperatureC`, `recommendedDoseG` (when set).
- `tastingFeedback` — `hasEnjoymentScore`, `hasNotes`, `hasRefractometer`, plus a `recommendation` string when any of the three is missing.
- `shotAnalysis` — the existing prose markdown (Shot Summary + Phase Data + Detector Observations) preserved verbatim as a string field.

`ShotSummarizer` SHALL also expose `buildUserPromptObject(summary, mode)` returning the unwrapped `QJsonObject` so DB-scoped callers (the in-app advisor's background-thread closure, `ai_advisor_invoke`'s background-thread closure) can append the four enrichment blocks (`dialInSessions`, `bestRecentShot`, `sawPrediction`, `grinderContext`) before serializing. The serialized output of `buildUserPrompt(summary, mode)` SHALL be `QJsonDocument(buildUserPromptObject(summary, mode)).toJson(QJsonDocument::Indented)`.

The four DB-scoped fields (`dialInSessions`, `bestRecentShot`, `sawPrediction`, `grinderContext`) SHALL be added to the user prompt by callers with DB scope (the in-app advisor and `ai_advisor_invoke`), via the shared block-builder helpers. Synchronous callers without DB scope (e.g. the plain prose / history-block path) SHALL continue to ship the four-key envelope from `buildUserPrompt` without enrichment, and SHALL NOT use `null` placeholders for the absent fields.

#### Scenario: User prompt carries currentBean with inferred fields

- **GIVEN** a `ShotSummary` whose DYE grinder fields are blank but whose resolved shot has populated grinder fields
- **WHEN** `buildUserPrompt(summary)` runs
- **THEN** the returned JSON SHALL contain `currentBean.grinderBrand`, `currentBean.grinderModel`, `currentBean.grinderBurrs`, `currentBean.grinderSetting` populated from the shot's values
- **AND** SHALL contain `currentBean.inferredFromShotId` set to the shot id
- **AND** SHALL contain `currentBean.inferredFields[]` listing exactly the field names that fell back

#### Scenario: User prompt carries currentBean.beanFreshness when DYE roastDate is set

- **GIVEN** a `ShotSummary` whose DYE roastDate is `"2026-04-15"`
- **WHEN** `buildUserPrompt(summary)` runs
- **THEN** the returned JSON SHALL contain `currentBean.beanFreshness.roastDate: "2026-04-15"`
- **AND** SHALL contain `currentBean.beanFreshness.freshnessKnown: false`
- **AND** SHALL contain `currentBean.beanFreshness.instruction` carrying the imperative storage-ask text

#### Scenario: User prompt carries currentProfile with intent and recipe

- **GIVEN** a `ShotSummary` whose `profileTitle`, `profileIntent`, `profileRecipe`, `targetWeight`, `targetTemperature` are populated
- **WHEN** `buildUserPrompt(summary)` runs
- **THEN** the returned JSON SHALL contain `currentProfile.title`, `currentProfile.intent`, `currentProfile.recipe`, `currentProfile.targetWeightG`, `currentProfile.targetTemperatureC`

#### Scenario: User prompt carries tastingFeedback with explicit absence flags

- **GIVEN** a `ShotSummary` with no enjoyment score, no notes, and no refractometer reading
- **WHEN** `buildUserPrompt(summary)` runs
- **THEN** the returned JSON SHALL contain `tastingFeedback.hasEnjoymentScore: false`, `tastingFeedback.hasNotes: false`, `tastingFeedback.hasRefractometer: false`
- **AND** SHALL contain `tastingFeedback.recommendation` instructing the AI to ask the user for feedback before suggesting changes

#### Scenario: User prompt preserves shotAnalysis prose verbatim

- **GIVEN** any `ShotSummary` for which the prior prose path produced a non-empty `## Shot Summary` block
- **WHEN** `buildUserPrompt(summary)` runs
- **THEN** the returned JSON SHALL contain a `shotAnalysis` string field whose value matches the prose section (Shot Summary + Phase Data + Tasting Feedback prose + Detector Observations) the prior path produced for the same input

#### Scenario: Synchronous callers without DB scope omit enrichment fields

- **GIVEN** a synchronous caller (e.g. the plain prose / history-block path) that does not have DB / Settings access
- **WHEN** `buildUserPrompt(summary)` runs
- **THEN** the returned JSON SHALL NOT contain a `dialInSessions` key
- **AND** SHALL NOT contain a `bestRecentShot` key
- **AND** SHALL NOT contain a `sawPrediction` key
- **AND** SHALL NOT contain a `grinderContext` key
- **AND** SHALL NOT use `null` placeholders for any of these field names

### Requirement: User prompt output SHALL be byte-stable for identical inputs

`buildUserPrompt(summary)` SHALL produce byte-for-byte identical output across calls for identical `ShotSummary` inputs. This is the load-bearing precondition for prompt caching: Anthropic's `cache_control` cache lookup compares the cached prefix to the incoming request bytes, so any drift busts the cache. Specifically:

- JSON SHALL be serialized via `QJsonDocument(payload).toJson(QJsonDocument::Indented)` (Qt's QJsonObject is alphabetically ordered, satisfying the determinism requirement).
- The payload SHALL NOT carry any wall-clock value, request id, monotonic counter, or anything else that varies across calls for the same shot. `currentDateTime` and similar dialing-context-only fields SHALL NOT appear.
- All string-formatted floats SHALL use fixed precision (matching the existing prose path: 1 decimal for grams, 2 decimals for ratios).
- Field encoding SHALL not depend on locale (no `QLocale::toString` for numbers in the payload — use `QString::number(d, 'f', n)` or `QJsonValue(d)` directly).

#### Scenario: Two calls with identical ShotSummary produce identical bytes

- **GIVEN** a `ShotSummary` populated with deterministic values (no NaN, no Inf)
- **WHEN** `buildUserPrompt(summary)` is called twice in succession
- **THEN** the two returned `QString`s SHALL be `==` (byte-for-byte identical)

#### Scenario: User prompt carries no wall-clock value

- **GIVEN** a `ShotSummary`
- **WHEN** `buildUserPrompt(summary)` runs
- **THEN** the returned JSON SHALL NOT contain `currentDateTime`, `requestId`, `nowMs`, or any other key whose value varies with wall-clock or per-call state

### Requirement: User prompt SHALL be cacheable in multi-turn Anthropic conversations

When the user's conversation with the AI advisor extends beyond the first turn (the in-app conversation overlay's follow-up flow), `AnthropicProvider::sendAnalysisRequest` SHALL apply `cache_control: {"type": "ephemeral"}` to the first user message (carrying the JSON shot payload). Subsequent follow-up messages SHALL NOT carry `cache_control` (they are the variable portion).

The single-shot `ai_advisor_invoke` MCP path (no follow-up expected) MAY skip the user-message cache_control to avoid the cache-write surcharge — implementation chooses based on a "expect follow-ups" signal from the caller.

#### Scenario: Multi-turn conversation reuses cached per-shot context

- **GIVEN** a multi-turn conversation: turn 1 = full shot context + user question, turn 2 = follow-up question only
- **WHEN** turn 2 is sent within the 5-minute cache TTL
- **THEN** the request body's first user message SHALL carry `cache_control: {"type": "ephemeral"}` matching turn 1 exactly
- **AND** the second user message (the follow-up question) SHALL NOT carry `cache_control`
- **AND** the Anthropic API response SHALL report a cache hit on the first user message (verifiable via the `cache_read_input_tokens` field in the usage payload)

#### Scenario: System prompt caching is preserved (no regression)

- **GIVEN** any call to the AI advisor
- **WHEN** `AnthropicProvider::buildCachedSystemPrompt` runs
- **THEN** the system content SHALL continue to be wrapped in a single text block with `cache_control: {"type": "ephemeral"}` exactly as before this change

### Requirement: User-prompt envelope SHALL carry an optional `recentAdvice` block

The JSON envelope produced by `ShotSummarizer::buildUserPromptObject` and enriched by the advisor's DB-scoped background-thread path (`AIManager::enrichUserPromptObject` for `ai_advisor_invoke`; `requestRecentShotContext`/`emitRecentShotContext` for the in-app advisor) SHALL include an optional top-level `recentAdvice` array (or, for the in-app advisor's prose-rendered `historicalContext`, an equivalent `## Recent Advice Tracking` section carrying the same data). The same block SHALL appear under `userPromptUsed` in `ai_advisor_invoke`'s tool result envelope (parity contract from #1041).

The block SHALL be derived from the active `AIConversation` (matched by storage key — bean+profile hash) and from the user's shot history.

`recentAdvice` SHALL be an array of up to 3 entries, ordered most-recent-first, each derived from a prior advisor turn satisfying ALL of:

- The prior turn has a non-zero `shotId` recorded in the conversation.
- The prior turn has a non-null `structuredNext` (per #1054). Question-only turns SHALL NOT enter `recentAdvice`.
- A *later* shot exists in the user's saved history that postdates the prior turn's shot, on the same `profile_kb_id` as the current shot.
- The prior turn's `shotId` is on the same `profile_kb_id` as the current shot. Cross-profile advice SHALL NOT enter the block.

When zero entries qualify, the `recentAdvice` key SHALL be ABSENT from the envelope. There SHALL NOT be `recentAdvice: []` placeholders.

Each entry in the array SHALL carry:

- `turnsAgo` (number, 1-indexed) — the entry's position in the qualifying-turn sequence (1 = most recent qualifying assistant turn, etc.). Skipped non-qualifying turns SHALL NOT consume a `turnsAgo` slot.
- `recommendation` (string) — short summary, sourced verbatim from the prior turn's `structuredNext.reasoning` when present. When `reasoning` is absent, the field SHALL be a synthesized one-line summary derived from the recommended fields (e.g., `"Try grinder 4.75; expect 32-38s, 1.0-1.5 ml/s"`).
- `structuredNext` (object) — the verbatim `structuredNext` block from the prior turn. The LLM uses this to re-read its own predicted ranges.
- `userResponse` (object) — the follow-up shot attribution computed by the app, with these fields:
  - `actualNextShotId` (number) — the immediate next shot in the user's history postdating the prior turn's shot, on the same profile.
  - `grinderSetting` (string) — actual grinder setting on that shot.
  - `doseG` (number) — actual dose on that shot.
  - `adherence` (`"followed" | "partial" | "ignored" | "unclear"`):
    - `"followed"` — every recommended field present in `structuredNext` (grinderSetting, rpm, doseG, profileTitle) matches the actual within tolerance: grinderSetting equal as string, equal as compound notation ignoring spacing, OR within 0.25 of a numeric step (comparing the leading dial number, so a recorded annotation such as `"23.5 1400rpm"` matches a recommended `"23.5"`); rpm within ±25; doseG within ±0.3g; profileTitle equal.
    - `"partial"` — at least one but not all recommended fields match.
    - `"ignored"` — none of the recommended fields match.
    - `"unclear"` — at least one recommended field was present but **unscoreable**, so adherence cannot be determined. A field is unscoreable when its JSON type is wrong for the schema (e.g. `grinderSetting` emitted as a bare number rather than a string, `rpm` as a string rather than a number), when a numeric field is present but non-positive, or when `grinderSetting` is prose rather than a dial value (e.g. `"a touch coarser than 9"`). `"unclear"` SHALL take precedence over the other three values whenever any field is unscoreable.
    - When `structuredNext` had no parameter recommendations (only ranges/successCondition), `adherence` SHALL be `"ignored"` only when the actual shot is on different parameters from the prior turn's shot; otherwise `"followed"`. The comparison SHALL use the same tolerances as scoring (grinderSetting equal as string, as compound notation ignoring spacing, or within 0.25 of a step on the leading dial number; rpm within ±25; doseG within ±0.3g; profileTitle equal), so measurement noise does not read as a deliberate change. A field SHALL be compared only when BOTH shots record it — a blank grinder setting or an unrecorded dose is missing data, and SHALL NOT be reported as a change. An unscoreable field is NOT "no parameter recommendation" — it SHALL yield `"unclear"`, never fall through to this rule.
  - `outcomeRating0to100` (number, 0-100) — `enjoyment0to100` from the actual shot. OMITTED when the actual shot's enjoyment is `<= 0`.
  - `outcomeNotes` (string) — `espressoNotes` from the actual shot. OMITTED when empty.
  - `outcomeInPredictedRange` (object) — booleans for each range that was on the prior turn's `structuredNext`:
    - `duration` (bool) — REQUIRED.
    - `flow` (bool) — REQUIRED.
    - `pressure` (bool) — REQUIRED iff `expectedPeakPressureBar` was on the prior turn; otherwise omitted.

The block SHALL be stable across calls for identical inputs (same conversation, same `(currentShotId, profile_kb_id)`).

#### Scenario: Single qualifying prior turn renders with adherence=followed and outcome in range

- **GIVEN** a conversation with one prior assistant turn whose `shotId = 100`, `structuredNext.grinderSetting = "4.75"`, `expectedDurationSec = [32, 38]`, `expectedFlowMlPerSec = [1.0, 1.5]`
- **AND** the user's history has shot 105 (the next shot after 100 on the same profile) with `grinderSetting = "4.75"`, `durationSec = 35`, `mainFlowMlPerSec = 1.2`, `enjoyment0to100 = 75`, `espressoNotes = "balanced and sweet"`
- **AND** the current shot being asked about is on the same profile
- **WHEN** the envelope is built
- **THEN** `recentAdvice` SHALL have exactly one entry with `turnsAgo: 1`
- **AND** `userResponse.adherence` SHALL be `"followed"`
- **AND** `userResponse.outcomeRating0to100` SHALL be `75`
- **AND** `userResponse.outcomeInPredictedRange.duration` SHALL be `true`
- **AND** `userResponse.outcomeInPredictedRange.flow` SHALL be `true`

#### Scenario: Outcome rating is omitted when actual shot is unrated

- **GIVEN** the same prior turn as above
- **AND** the actual follow-up shot has `enjoyment0to100 = 0` (unrated)
- **WHEN** the envelope is built
- **THEN** `userResponse.outcomeRating0to100` SHALL be ABSENT from the entry
- **AND** `outcomeInPredictedRange` SHALL still be present (curve-based signal, not rating-based)

#### Scenario: Cross-profile prior turn is filtered out

- **GIVEN** a conversation with one prior assistant turn on profile `A`
- **AND** the current shot is on profile `B`
- **WHEN** the envelope is built for the current shot
- **THEN** `recentAdvice` SHALL be ABSENT (no entries qualify)

#### Scenario: User ignored the recommendation

- **GIVEN** a prior turn recommending `grinderSetting = "4.75"` and `doseG = 19` (different from the prior shot's setup)
- **AND** the actual follow-up shot has `grinderSetting = "5.0"` (the prior shot's setting) and `doseG = 18` (also unchanged)
- **WHEN** the envelope is built
- **THEN** `userResponse.adherence` SHALL be `"ignored"`

#### Scenario: Prose in `grinderSetting` yields `"unclear"`

- **GIVEN** a prior turn whose `structuredNext.grinderSetting` is prose rather than a dial value
- **WHEN** the follow-up shot is attributed
- **THEN** `adherence` SHALL be `"unclear"`
- **AND** it SHALL be `"unclear"` whether or not the user changed the grinder, because the recommendation named no setting to compare against

#### Scenario: A malformed `rpm` does not read as compliance

- **GIVEN** a prior turn whose `structuredNext.rpm` is a JSON string, or is present and `<= 0`
- **WHEN** the follow-up shot is attributed
- **THEN** `adherence` SHALL be `"unclear"`
- **AND** SHALL NOT be `"followed"`

#### Scenario: A ranges-only turn repeated on the same setup is followed

- **GIVEN** a prior turn whose `structuredNext` recommends no parameter changes, only ranges
- **AND** the follow-up shot is on the same grinder setting, dose and profile as the prior shot
- **WHEN** the follow-up shot is attributed
- **THEN** `adherence` SHALL be `"followed"` — the predicted repeat happened

#### Scenario: A ranges-only turn whose setup changed is ignored

- **GIVEN** a prior turn whose `structuredNext` recommends no parameter changes, only ranges
- **AND** the follow-up shot changed the grinder setting, dose or profile beyond tolerance
- **WHEN** the follow-up shot is attributed
- **THEN** `adherence` SHALL be `"ignored"`
- **AND** SHALL NOT be `"followed"` — the prediction was made about a shot that did not happen, so the model SHALL NOT be told the experiment ran

#### Scenario: Missing setup data on a ranges-only turn does not read as a change

- **GIVEN** a prior turn whose `structuredNext` recommends no parameter changes, only ranges
- **AND** either the prior or the follow-up shot has no recorded grinder setting
- **WHEN** the follow-up shot is attributed
- **THEN** `adherence` SHALL be `"followed"` — absence of evidence is not evidence of a change

#### Scenario: Equivalent notations of the same setting count as followed

- **GIVEN** a prior turn recommending `"1 + 4"` and a follow-up shot recorded as `"1+4"`, or a turn recommending `"23.5"` and a shot recorded as `"23.5 1400rpm"`
- **WHEN** the follow-up shot is attributed
- **THEN** `adherence` SHALL be `"followed"` — spacing and recorded annotations SHALL NOT decide adherence

#### Scenario: Empty conversation omits the block

- **GIVEN** an `AIConversation` with no prior assistant turns (first call)
- **WHEN** the envelope is built
- **THEN** `recentAdvice` SHALL NOT appear as a key in the envelope
- **AND** the envelope SHALL NOT contain `recentAdvice: []`

#### Scenario: Parity between in-app advisor and ai_advisor_invoke

- **GIVEN** the same `AIConversation` storage key, the same DB state, and the same current shot
- **WHEN** the in-app advisor builds its user-prompt envelope
- **AND** `ai_advisor_invoke` independently builds its `userPromptUsed` echo for the same inputs
- **THEN** the `recentAdvice` block in both surfaces SHALL be byte-equal under `==`

#### Scenario: In-app advisor's requestRecentShotContext builds the Recent Advice Tracking section

- **GIVEN** the in-app `AIConversation` for the current bean+profile has a qualifying prior turn (non-zero `shotId`, non-null `structuredNext`, a later shot exists on the same profile)
- **WHEN** `AIManager::requestRecentShotContext` runs and `emitRecentShotContext` renders the result
- **THEN** the emitted `historicalContext` string SHALL contain a `## Recent Advice Tracking` section
- **AND** that section SHALL carry the same `turnsAgo` / `recommendation` / `structuredNext` / `userResponse` data `DialingBlocks::buildRecentAdviceBlock` would produce for the same inputs
- **AND** when zero entries qualify, `historicalContext` SHALL NOT contain a `## Recent Advice Tracking` section at all

### Requirement: System prompt SHALL teach the LLM to read `recentAdvice` and weight it

The espresso `shotAnalysisSystemPrompt` SHALL include teaching for the `recentAdvice` block in its "How to read structured fields" section. The teaching SHALL cover:

- How to interpret `adherence`: `"followed"` + worse outcome ⇒ revise direction; `"ignored"` ⇒ stay the course; `"partial"` ⇒ ask before revising; `"unclear"` ⇒ the prior recommendation named nothing checkable, so treat it like `"ignored"`, do not assume the experiment ran, and restate the recommendation as a concrete value.
- How to interpret omitted `outcomeRating0to100`: do not assume good or bad — fall back to `outcomeInPredictedRange` for a curve-shape signal, or ask the user about taste.
- That `recentAdvice` is the LLM's own prior recommendations + observed outcomes — it can self-correct based on it.

#### Scenario: System prompt contains recentAdvice teaching

- **GIVEN** the espresso `shotAnalysisSystemPrompt` output
- **WHEN** the prompt is rendered
- **THEN** it SHALL contain a section discussing `recentAdvice`
- **AND** SHALL describe the four `adherence` values and how to react to each
- **AND** SHALL describe the omitted-rating fallback

### Requirement: User-prompt envelope SHALL remain byte-stable for identical inputs after `recentAdvice` is added

The byte-stability requirement on `buildUserPromptObject`'s output SHALL extend to cover `recentAdvice`. Specifically:

- For an identical `(AIConversation snapshot, current shot id, DB state)` triple, the serialized `recentAdvice` bytes SHALL be identical across calls.
- The block SHALL NOT carry any wall-clock value, monotonic counter, or per-call unique id. `actualNextShotId` is a stable database id.
- Numeric formatting (`durationSec`, `doseG`, `grinderSetting` when numeric) SHALL match the existing fixed-precision rules used elsewhere in the envelope.

#### Scenario: Two consecutive builds with identical inputs produce identical recentAdvice bytes

- **GIVEN** an `AIConversation` snapshot, a frozen DB state, and a fixed current shot id
- **WHEN** the envelope is built twice in succession
- **THEN** the serialized `recentAdvice` bytes SHALL be `==` (byte-for-byte identical)

### Requirement: Advisor user prompt SHALL carry dialInSessions / bestRecentShot / sawPrediction / grinderContext when DB scope is available

When the in-app advisor (via `AIManager::requestRecentShotContext`) and the MCP `ai_advisor_invoke` tool (via `AIManager::enrichUserPromptObject`) assemble the user prompt, they SHALL enrich the JSON envelope with up to four additional top-level fields, matching `dialing_get_context`'s shape exactly:

- `dialInSessions` — runs of consecutive shots on the same profile within ~60 minutes of each other, with hoisted session-level `context` and per-shot `changeFromPrev` diffs. Same shape `dialing_get_context` produces.
- `bestRecentShot` — the highest-rated shot on the same profile within the last 90 days (excluding the current shot), with a `changeFromBest` diff against the current shot. Omitted entirely (no key, no `null`) when no rated shot exists in that window.
- `sawPrediction` — predicted post-cut drip in grams from the SAW learner, with `sourceTier` reporting the active model. Omitted (no key) when the resolved shot is not espresso, when no scale is configured, when no profile is configured, or when the shot lacks usable flow samples in the last 2 seconds.
- `grinderContext` — observed settings range and step size for the resolved shot's grinder model. Omitted (no key) when the resolved shot has no grinder model OR when both the bean-scoped and cross-bean queries return no rows.

These four fields SHALL be produced by shared block-builder helpers exported from `src/mcp/mcptools_dialing_blocks.h`. Both `dialing_get_context` and the in-app advisor / `ai_advisor_invoke` SHALL call the same helpers, so divergence between the two surfaces is impossible by construction.

#### Scenario: User prompt carries dialInSessions when shots exist on the resolved shot's profile

- **GIVEN** a resolved shot whose `profileKbId` matches 4 prior shots in two distinct sessions
- **WHEN** the advisor's DB-scoped path enriches the user prompt
- **THEN** the JSON envelope SHALL contain a `dialInSessions` array with two session objects
- **AND** each session SHALL carry the hoisted `context` and per-shot `shots[].changeFromPrev` diffs the same way `dialing_get_context` does

#### Scenario: User prompt carries bestRecentShot when a rated shot exists in the 90-day window

- **GIVEN** a resolved shot on a profile that has one prior rated shot 14 days ago and several unrated shots
- **WHEN** the user prompt is enriched
- **THEN** the JSON envelope SHALL contain `bestRecentShot.id`, `.timestamp`, `.enjoyment0to100`, `.doseG`, `.yieldG`, `.durationSec`, `.grinderSetting`, `.beanBrand`, `.beanType`, `.daysSinceShot`
- **AND** SHALL contain `bestRecentShot.changeFromBest` showing the diff between the best shot and the current shot

#### Scenario: User prompt omits bestRecentShot when only stale rated shots exist

- **GIVEN** a resolved shot whose only rated prior shots are 100+ days old
- **WHEN** the user prompt is enriched
- **THEN** the JSON envelope SHALL NOT contain a `bestRecentShot` key
- **AND** SHALL NOT use a `null` placeholder for the field

#### Scenario: User prompt carries sawPrediction when scale + profile + flow data are present

- **GIVEN** a resolved espresso shot with a configured `Settings::scaleType()`, a `ProfileManager::baseProfileName()`, and flow samples > 0 in the last 2 seconds of the pour
- **WHEN** the user prompt is enriched
- **THEN** the JSON envelope SHALL contain `sawPrediction.predictedDripG`, `.flowAtCutoffMlPerSec`, `.learnedLagSec`, `.sampleCount`, `.sourceTier`, `.profileFilename`, `.scaleType`
- **AND** SHALL contain `sawPrediction.recommendation` when `predictedDripG >= 0.2`, otherwise the recommendation field SHALL be absent

#### Scenario: User prompt carries grinderContext when grinder model has history

- **GIVEN** a resolved shot whose `grinderModel` is non-empty AND has at least one prior shot in history
- **WHEN** the user prompt is enriched
- **THEN** the JSON envelope SHALL contain `grinderContext.model`, `.beverageType`, `.settingsObserved`, `.isNumeric`
- **AND** when the bean-scoped query has < 2 distinct settings AND the cross-bean fallback has data, SHALL also contain `grinderContext.allBeansSettings` tagged as cross-bean

### Requirement: Enriched user prompt SHALL be byte-equivalent across in-app and MCP surfaces

The user prompt assembled by the in-app advisor (`AIManager::requestRecentShotContext`) and the user prompt echoed by `ai_advisor_invoke` (MCP, via `AIManager::enrichUserPromptObject`) SHALL be byte-for-byte identical for the same resolved `ShotProjection` + DB state + Settings state. Both surfaces SHALL call the same block-builder helpers and the same `ShotSummarizer::buildUserPromptObject` envelope builder.

#### Scenario: In-app advisor and ai_advisor_invoke produce identical user prompts

- **GIVEN** a fixed `ShotProjection`, DB state, and Settings state
- **WHEN** the in-app advisor's enrichment closure runs and `ai_advisor_invoke`'s enrichment closure runs against the same inputs
- **THEN** the two resulting user prompt strings SHALL be `==` (byte-for-byte identical)

### Requirement: Enriched user prompt SHALL preserve cache stability

The enriched user prompt SHALL NOT introduce any per-call wall-clock value, request id, monotonic counter, or anything else that varies across calls for the same resolved shot. Specifically:

- `currentDateTime` (a top-level field on `dialing_get_context`'s response) SHALL NOT appear in the user prompt — the AI advisor doesn't need it and including it would bust the prompt cache on every call.
- `daysSinceShot` (inside `bestRecentShot`) is acceptable — it changes on day boundaries, not per call, and is already shipped by `dialing_get_context`.
- All field encodings SHALL match the existing `dialing_get_context` shape exactly (same float precisions, same JSON key ordering via Qt's alphabetical default).

#### Scenario: Enriched user prompt has no currentDateTime

- **GIVEN** any enriched user prompt produced by the in-app advisor or `ai_advisor_invoke`
- **WHEN** the prompt is parsed as JSON
- **THEN** the parsed object SHALL NOT contain a `currentDateTime` key

#### Scenario: Two consecutive enrichments with identical state produce identical bytes

- **GIVEN** a fixed resolved shot, fixed DB state, fixed Settings state, and a wall-clock that does not cross a day boundary between calls
- **WHEN** the user prompt is enriched twice in succession
- **THEN** the two resulting strings SHALL be `==`

### Requirement: Rendered calibration section SHALL constrain how the model uses UGS

When the enriched user prompt includes a `grinderCalibration` block, the rendered calibration section SHALL carry explicit usage constraints that prevent the model from using UGS in ways it was not intended. The constraints SHALL be stated as directives, not background prose, and SHALL be byte-stable and present on both the in-app advisor and `dialing_get_context` surfaces.

The rendered section SHALL state, at minimum:

- UGS is a **relative ordering** of profiles by grind coarseness, not a grinder click count or an absolute dial position.
- Numeric grinder settings are valid **only within the stated `calibratedUgsRange`**. The model SHALL NOT compute, infer, or quote a grinder number for any profile reported with `source: "directional"`.
- For a `"directional"` profile the model SHALL give only relative direction (finer/coarser) and SHALL recommend pulling a reference shot on the target profile to establish a number.
- The model SHALL NOT multiply a UGS distance by any factor of its own to produce a setting; the only sanctioned arithmetic is the system-provided `conversionKey` applied within the validated range.
- When `confidence` is `"directional"`, the model SHALL NOT present any grinder number for a profile switch and SHALL say a number cannot be given without more dial-in data on the current coffee.
- Directional guidance SHALL be expressed only as a grind-size term (finer/coarser). The model SHALL NOT translate it into a dial-number change ("go up N", "turn coarser by 2") — that needs the grinder's numeric convention and reintroduces the #1223 sign risk; the `direction` field is anchor-free and already correct as finer/coarser.
- When a directional entry has no `direction` field (the current profile is not UGS-placed), the model SHALL state it cannot order the two profiles rather than guess.

The section SHALL repeat the block's `usageConstraint` string verbatim so a single directive governs every provider (Claude, Gemini, GPT, OpenRouter, Ollama) identically.

#### Scenario: Out-of-range profile renders as directional with no number

- **GIVEN** a `grinderCalibration` block whose `calibratedUgsRange` is `[0.0, 1.5]` and a `profiles` entry for "TurboTurbo" with `source: "directional"`, `direction: "coarser"`
- **WHEN** the calibration section is rendered into the user prompt
- **THEN** the section SHALL present "TurboTurbo" as "coarser, pull a reference shot" with no grinder number
- **AND** the section SHALL state that numbers are valid only within UGS 0.0–1.5
- **AND** the `usageConstraint` string SHALL appear verbatim

#### Scenario: Directional confidence suppresses all numeric switch advice

- **GIVEN** a `grinderCalibration` block with `confidence: "directional"` (no `conversionKey`)
- **WHEN** the calibration section is rendered
- **THEN** the section SHALL instruct the model to give only finer/coarser direction for any profile switch
- **AND** the section SHALL state that a specific grinder number cannot be given without more dial-in data on the current coffee
- **AND** the rendered section SHALL contain no numeric grinder settings

#### Scenario: No-anchor directional guidance is correct grind-size language

- **GIVEN** a `grinderCalibration` block with `confidence: "directional"`, no `conversionKey`, no `coffeeAnchor`, current profile "D-Flow / Q", and a "TurboTurbo" entry `direction: "coarser"`
- **WHEN** the calibration section is rendered
- **THEN** the section SHALL tell the model TurboTurbo is coarser than the current profile and to pull a reference shot
- **AND** the section SHALL contain no dial-number delta and no grinder setting
- **AND** the guidance SHALL be correct without reference to the grinder's finer-direction convention

#### Scenario: Constraint wording is byte-stable across surfaces

- **GIVEN** the same `grinderCalibration` block
- **WHEN** rendered via the in-app advisor enrichment path and via `dialing_get_context`
- **THEN** the calibration section text including the usage constraints SHALL be byte-identical between the two surfaces

### Requirement: System prompt SHALL require taste feedback before declaring dial-in success across repeated untasted shots

The shared espresso system prompt SHALL instruct the model: when tasting feedback (score or notes) has been absent for the last 2 or more shots in the current conversation, the model SHALL ask the user for a taste score before using success/quality language (e.g. "successful", "optimal", "excellent", "dialed in") to characterize those shots from pressure/flow curve data alone. Curve-based observations MAY still be described, but SHALL be framed as preliminary pending taste feedback rather than as a conclusion.

This extends (does not replace) the existing `tastingFeedback`-driven rule that asks for taste feedback when ALL of `hasEnjoymentScore`/`hasNotes`/`hasRefractometer` are false for the CURRENT shot — this new rule additionally triggers on a run of consecutive shots each missing feedback, even if earlier shots in the conversation did have a score.

#### Scenario: Two consecutive untasted shots gates success language

- **GIVEN** a multi-shot conversation where the two most recent shots have no tasting score or notes
- **AND** both shots' pressure/flow curves land inside the profile's intended target band
- **WHEN** the model responds to the latest shot
- **THEN** the response SHALL ask the user for a taste score before or in place of declaring the shots successful, optimal, or excellent
- **AND** SHALL NOT use unqualified success/quality language about those shots based on curve data alone

#### Scenario: A single untasted shot after a rated shot does not trigger the stricter gate

- **GIVEN** a multi-shot conversation whose most recent shot has no tasting score
- **AND** the shot immediately before it DID have a tasting score
- **WHEN** the model responds to the latest shot
- **THEN** the existing single-shot `tastingFeedback` guidance applies (ask about taste, but the stricter "2+ in a row" success-language gate is not required)

### Requirement: In-app advisor shot history SHALL be scoped to the shot's equipment package

The in-app advisor's historical context SHALL include a prior shot only when that shot's
equipment package matches the current shot's, in addition to the bean, profile and time-window
match it already applies. "No package recorded" SHALL be treated as a package value in its own
right, so shots with no equipment package match each other and nothing else — a user who has
never created a package SHALL see no change in which shots qualify.

An equipment package identifies grinder, basket and puck prep together, and changing any one of
them yields a different package. The same numeric grind setting on a different basket does not
describe the same extraction, so a prior shot on different equipment SHALL be excluded however
closely its bean, profile and setting match.

#### Scenario: History excludes shots pulled on a different basket

- **GIVEN** two equipment packages sharing one grinder, differing only in basket
- **AND** a history of shots on both, all on the same bean and profile
- **WHEN** the in-app advisor builds historical context for a shot on the second package
- **THEN** the `## Previous Shots with This Bean & Profile` section SHALL contain only shots
  from the second package
- **AND** SHALL NOT contain a shot from the first

#### Scenario: A user with no equipment packages sees an unchanged history

- **GIVEN** a user whose shots all have no equipment package recorded
- **WHEN** the in-app advisor builds historical context for any of their shots
- **THEN** the qualifying shots SHALL be exactly those that qualified before this requirement
- **AND** no shot SHALL be excluded on equipment grounds

### Requirement: Both advisor surfaces SHALL send one payload in one format

The in-app advisor and `ai_advisor_invoke` build their system prompt from one function and send
it to the same model. They SHALL therefore send the same user-prompt format, assembled by the
same code: the structured payload whose field paths that shared system prompt names. Neither
surface SHALL carry a second renderer of the same data.

A system prompt that instructs the model to read `dialInSessions[].context` and a payload that
delivers markdown are a contract and a breach of it. Two renderers of one dataset also drift by
construction — the identity fields are defined once in `ShotIdentity::fields()`, and a hand-
written second copy is what left the basket and puck prep out of one surface while the other
picked them up from a single table row.

The user's question SHALL travel as its own field rather than concatenated into the payload, so
that recovering it for display is a field read and not a parse of prose.

#### Scenario: The in-app advisor sends the structured blocks its system prompt names

- **WHEN** the in-app advisor sends a shot with historical context
- **THEN** the user prompt SHALL be the structured payload
- **AND** SHALL carry the blocks the shared system prompt references, on the same field paths
  `ai_advisor_invoke` uses

#### Scenario: One renderer defines the payload

- **WHEN** an equipment component is added to `ShotIdentity::fields()`
- **THEN** it SHALL appear on both surfaces without a further edit to either
- **AND** no surface SHALL hand-render an identity field it could read from that table

#### Scenario: The displayed conversation reads the question from a field

- **WHEN** the conversation view renders a user turn that carried shot context
- **THEN** the question SHALL be read from the turn's own field
- **AND** SHALL NOT be recovered by searching the payload text for delimiters

### Requirement: The advisor payload SHALL name the equipment set its shots were pulled on

The advisor payload SHALL name the equipment set shared by the history's shots: the grinder
(brand, model, burrs), the basket (brand, model), and the puck-prep technique set. Components
with no recorded value SHALL be omitted rather than emitted empty.

A filter the model cannot see is a silent one: without the equipment named in the payload, the
model can neither attribute the history to the gear it came from nor recognise that a user has
changed baskets. The equipment set SHALL come from a single shared definition so the session
context and the no-history block below cannot describe the same package differently.

#### Scenario: The payload names grinder, basket and puck prep

- **GIVEN** a history whose shots were pulled on a Niche Zero with 63mm Mazzer Kony conical
  burrs, a Graph Coffee "Stepped 58→46mm" basket, and puck prep of shaker + puck screen + RDT
- **WHEN** the advisor assembles the payload
- **THEN** the hoisted session context SHALL name the grinder, the basket and the puck-prep
  techniques
- **AND** SHALL continue to name the bean, roast level and roast date as before

#### Scenario: A package with no basket recorded omits the basket fields

- **GIVEN** a history whose equipment package has no basket recorded
- **WHEN** the advisor assembles the payload
- **THEN** the session context SHALL name the grinder and bean as before
- **AND** SHALL NOT carry an empty or placeholder basket field

### Requirement: In-app advisor SHALL state an empty history rather than omitting it

When no prior shot matches the current shot's bean, profile, time window and equipment package,
the in-app advisor's historical context SHALL emit a block that states no prior shots matched
and names the equipment set that was matched on, together with the reason equipment-mismatched
shots were excluded. It SHALL NOT emit an empty historical context in this case.

An absent history block is indistinguishable from "this user has no history at all", and a model
given no anchor in context is a model that supplies one: the reported failure cited a "70/100
shot" that appears nowhere in its context and then reasoned from it. A stated absence is a fact
the model can use in place of an invented one.

#### Scenario: First shot on a new equipment package states the empty history

- **GIVEN** a user with an extensive history on one equipment package
- **WHEN** they pull the first shot on a newly created package and open the advisor
- **THEN** the historical context SHALL state that no prior shots match this equipment set
- **AND** SHALL name the equipment set
- **AND** SHALL instruct the model to judge the shot on its own data rather than referring to
  shots it cannot see

#### Scenario: A populated history does not carry the empty-history block

- **GIVEN** at least one qualifying prior shot
- **WHEN** the in-app advisor renders the historical context
- **THEN** the rendered context SHALL contain the per-shot history blocks
- **AND** SHALL NOT contain the empty-history statement

### Requirement: Shot-to-shot change detection SHALL compare only within one equipment package

Change detection compares the current shot with the previous shot IN THE SAME THREAD. A thread is
identified by its equipment package, so both shots necessarily share one, and an equipment change
cannot appear in that comparison — a basket switch opens a different thread instead. Change
detection SHALL continue to report dose, yield, duration and grind setting, and SHALL NOT report
an equipment change: the arm would compare a value with itself and could never emit.

This requirement is recorded rather than dropped because an earlier draft of this change asked
for the opposite, and the reasoning is not obvious from the code. It was written when a thread
was keyed on bean and profile alone, where a swap genuinely could land two packages in one
transcript and narrating it was the mitigation on the table. Keying on the package removed the
condition instead of describing it.

#### Scenario: Switching basket does not appear as a change within a thread

- **GIVEN** a thread on equipment package A
- **WHEN** the user pulls a shot on package B with the same bean and profile
- **THEN** a separate thread SHALL be used
- **AND** the package B shot's changes line SHALL NOT reference package A's shots

#### Scenario: A grind change on the same package is still reported

- **GIVEN** consecutive shots in one thread at grinder settings 9.5 and 9.25
- **WHEN** the advisor assembles the second shot's message
- **THEN** the changes line SHALL report the grind change

### Requirement: System prompt SHALL scope grind-setting comparability to one equipment set

The shared espresso system prompt SHALL instruct the model that a numeric grind setting is
comparable only among shots pulled on the same equipment set, and that a change of grinder,
burrs or basket makes two settings incommensurable even on the same dial. When the model
observes a setting that does not fit the ordering the rest of the history implies, it SHALL
consider an equipment difference before concluding anything about the grinder's mechanism.

This complements the existing rule that settings are never comparable across grinder models.

#### Scenario: An out-of-order setting prompts an equipment question, not a mechanism theory

- **GIVEN** a history where coarser settings produced lower peak pressure
- **AND** a current shot at a numerically much coarser setting that produced higher peak
  pressure and a longer shot
- **WHEN** the model explains the discrepancy
- **THEN** it SHALL consider a basket or grinder difference as a candidate explanation
- **AND** SHALL NOT assert a change in the grinder's own calibration or burr alignment as
  established fact

### Requirement: System prompt SHALL forbid citing shots, scores or taste notes absent from context

The shared system prompt SHALL instruct the model that it may cite only shots, enjoyment scores,
taste notes and measurements that appear in the context it was given, and that inventing any of
them is hallucination — the same discipline the prompt already imposes on the setpoints of
profiles other than the current one. When the model wants an anchor the context does not
contain, it SHALL say the data is not available rather than supplying a value.

#### Scenario: The model does not invent a rated shot to anchor a recommendation

- **GIVEN** a context whose shots carry no enjoyment score except the current shot's
- **WHEN** the model recommends a change and wants to refer to a previously well-rated shot
- **THEN** it SHALL NOT cite a score, taste description or shot that is not in the context
- **AND** SHALL state that no rated prior shot is available

#### Scenario: Scores present in context remain citable

- **GIVEN** a context containing a prior shot with an enjoyment score and taste notes
- **WHEN** the model refers to that shot
- **THEN** it MAY cite that shot's score and notes as recorded
