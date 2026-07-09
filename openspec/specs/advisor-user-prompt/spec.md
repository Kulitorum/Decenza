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

The JSON envelope produced by `ShotSummarizer::buildUserPromptObject` and enriched by the advisor's DB-scoped background-thread path (`AIManager::enrichUserPromptObject` for `ai_advisor_invoke`; `requestRecentShotContext` for the in-app advisor) SHALL include an optional top-level `recentAdvice` array. The same block SHALL appear under `userPromptUsed` in `ai_advisor_invoke`'s tool result envelope (parity contract from #1041).

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
  - `adherence` (`"followed" | "partial" | "ignored"`):
    - `"followed"` — every recommended field present in `structuredNext` (grinderSetting, doseG, profileTitle) matches the actual within tolerance: grinderSetting equal as string OR within 0.25 of a numeric step; doseG within ±0.3g; profileTitle equal.
    - `"partial"` — at least one but not all recommended fields match.
    - `"ignored"` — none of the recommended fields match.
    - When `structuredNext` had no parameter recommendations (only ranges/successCondition), `adherence` SHALL be `"ignored"` only when the actual shot is on different parameters from the prior turn's shot; otherwise `"followed"`.
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

### Requirement: System prompt SHALL teach the LLM to read `recentAdvice` and weight it

The espresso `shotAnalysisSystemPrompt` SHALL include teaching for the `recentAdvice` block in its "How to read structured fields" section. The teaching SHALL cover:

- How to interpret `adherence`: `"followed"` + worse outcome ⇒ revise direction; `"ignored"` ⇒ stay the course; `"partial"` ⇒ ask before revising.
- How to interpret omitted `outcomeRating0to100`: do not assume good or bad — fall back to `outcomeInPredictedRange` for a curve-shape signal, or ask the user about taste.
- That `recentAdvice` is the LLM's own prior recommendations + observed outcomes — it can self-correct based on it.

#### Scenario: System prompt contains recentAdvice teaching

- **GIVEN** the espresso `shotAnalysisSystemPrompt` output
- **WHEN** the prompt is rendered
- **THEN** it SHALL contain a section discussing `recentAdvice`
- **AND** SHALL describe the three `adherence` values and how to react to each
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

