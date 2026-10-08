# advisor-user-prompt Specification

## Purpose
The single source of truth for the JSON-shaped user prompt `ShotSummarizer` and `AIManager` send the AI Advisor: the `currentBean`/`currentProfile`/`tastingFeedback`/`shotAnalysis` envelope, the DB-scoped enrichment blocks (`dialInSessions`, `bestRecentShot`, `sawPrediction`, `grinderContext`, `recentAdvice`, `grinderCalibration`) shared with `dialing_get_context`, and the byte-stability and Anthropic prompt-caching rules that make repeated calls cache-hit identically across the in-app advisor and `ai_advisor_invoke`.

## Requirements

### Requirement: AI advisor user prompt SHALL be JSON-shaped

`ShotSummarizer::buildUserPrompt(summary)` SHALL return indented JSON with deterministic field ordering carrying `currentBean`, `currentProfile`, `tastingFeedback` and `shotAnalysis`, mirroring `dialing_get_context`. `buildUserPromptObject(summary, mode)` SHALL return the unwrapped `QJsonObject` that DB-scoped callers enrich before serializing. Callers without DB scope SHALL omit the four enrichment keys, not use `null`.

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

#### Scenario: Envelope carries the required field set

- **WHEN** `buildUserPrompt(summary)` runs for a populated `ShotSummary`
- **THEN** `currentBean` SHALL include `brand`, `type`, `roastLevel`, `grinderBrand`, `grinderModel`, `grinderBurrs`, `grinderSetting` and `doseWeightG`
- **AND** `currentBean.beanFreshness` SHALL be present with `roastDate`, `freshnessKnown: false` and the storage-mode `instruction` whenever the DYE roastDate is non-empty
- **AND** `currentProfile` SHALL include `filename`, `title`, `intent`, `recipe`, `targetWeightG`, `targetTemperatureC`, and `recommendedDoseG` when set
- **AND** `tastingFeedback` SHALL include `hasEnjoymentScore`, `hasNotes` and `hasRefractometer`, plus a `recommendation` string when any of the three is missing

### Requirement: User prompt output SHALL be byte-stable for identical inputs

`buildUserPrompt(summary)` SHALL produce byte-identical output for identical `ShotSummary` inputs, because Anthropic's `cache_control` lookup compares request bytes. The JSON SHALL be serialized with `QJsonDocument(payload).toJson(QJsonDocument::Indented)`. The payload SHALL carry no wall-clock value, request id or per-call counter, and number encoding SHALL NOT depend on locale.

#### Scenario: Two calls with identical ShotSummary produce identical bytes

- **GIVEN** a `ShotSummary` populated with deterministic values (no NaN, no Inf)
- **WHEN** `buildUserPrompt(summary)` is called twice in succession
- **THEN** the two returned `QString`s SHALL be `==` (byte-for-byte identical)

#### Scenario: User prompt carries no wall-clock value

- **GIVEN** a `ShotSummary`
- **WHEN** `buildUserPrompt(summary)` runs
- **THEN** the returned JSON SHALL NOT contain `currentDateTime`, `requestId`, `nowMs`, or any other key whose value varies with wall-clock or per-call state

#### Scenario: Float precision matches the prose path

- **GIVEN** a `ShotSummary` with a dose, a yield and a ratio
- **WHEN** `buildUserPrompt(summary)` runs
- **THEN** grams SHALL be formatted with 1 decimal and ratios with 2 decimals, matching the prose path

### Requirement: User prompt SHALL be cacheable in multi-turn Anthropic conversations

When a conversation extends beyond the first turn, `AnthropicProvider::sendAnalysisRequest` SHALL set `cache_control: {"type": "ephemeral"}` on the first user message, which carries the JSON shot payload. Follow-up user messages SHALL NOT carry `cache_control`. The single-shot `ai_advisor_invoke` path MAY omit it, based on the caller's expect-follow-ups signal.

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

The user-prompt envelope SHALL include an optional top-level `recentAdvice` array derived from the active `AIConversation` and shot history. It SHALL hold up to 3 entries, most recent first, from prior turns with a non-zero `shotId` and non-null `structuredNext` whose shot is on the current shot's `profile_kb_id` and which have a later saved shot on that profile. When none qualify, the key SHALL be absent, never `[]`.

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

### Requirement: Recent advice entries SHALL carry turn, recommendation and outcome fields

Each entry SHALL carry `turnsAgo` (1-indexed over qualifying turns), `recommendation` (verbatim `structuredNext.reasoning`, or a synthesized one-line summary when absent), the verbatim `structuredNext`, and `userResponse` with `actualNextShotId`, `grinderSetting`, `doseG`, `adherence` and `outcomeInPredictedRange`. `outcomeRating0to100` and `outcomeNotes` SHALL be omitted when the rating is `<= 0` or the notes are empty.

#### Scenario: Skipped turns do not consume a slot

- **GIVEN** three qualifying prior turns with one non-qualifying turn between the first and second
- **WHEN** the envelope is built
- **THEN** the entries SHALL carry `turnsAgo` 1, 2 and 3 in order
- **AND** the non-qualifying turn SHALL NOT consume a `turnsAgo` value

#### Scenario: outcomeInPredictedRange carries pressure only when recorded

- **GIVEN** a qualifying entry whose prior turn did not record `expectedPeakPressureBar`
- **WHEN** the envelope is built
- **THEN** `userResponse.outcomeInPredictedRange` SHALL carry `duration` and `flow` booleans
- **AND** SHALL NOT carry a `pressure` key

### Requirement: Recent advice adherence SHALL be scored against recommended fields

`userResponse.adherence` SHALL be `"followed"` when every recommended field in `structuredNext` (`grinderSetting`, `rpm`, `doseG`, `profileTitle`) matches the actual shot within tolerance, `"partial"` when some match, and `"ignored"` when none do. Grind matches as an equal string, equal compound notation ignoring spacing, or within 0.25 of the leading dial number. rpm within ±25, doseG within ±0.3 g, profileTitle exactly.

#### Scenario: Dose within tolerance counts as a match

- **GIVEN** a prior turn recommending `doseG = 19` and a follow-up shot with `doseG = 19.2`
- **AND** the recommended grinder setting and profile also match the follow-up shot
- **WHEN** the follow-up shot is attributed
- **THEN** `adherence` SHALL be `"followed"`

### Requirement: Unscoreable recommendations SHALL yield adherence unclear

`adherence` SHALL be `"unclear"`, taking precedence over the other values, when a recommended field is unscoreable: wrong JSON type, a non-positive numeric value, or prose instead of a dial value. A `structuredNext` with no parameter recommendations SHALL be `"ignored"` only when the actual shot differs beyond tolerance, otherwise `"followed"`. A field SHALL be compared only when both shots record it.

#### Scenario: A numeric doseG emitted as a string yields unclear

- **GIVEN** a prior turn whose `structuredNext.doseG` is the JSON string `"19"`
- **WHEN** the follow-up shot is attributed
- **THEN** `adherence` SHALL be `"unclear"`
- **AND** SHALL NOT be `"ignored"` even if the follow-up dose differs

### Requirement: System prompt SHALL teach the LLM to read `recentAdvice` and weight it

The espresso `shotAnalysisSystemPrompt` SHALL teach the LLM to read `recentAdvice` in its "How to read structured fields" section. The teaching SHALL cover how to react to each `adherence` value, how to treat an omitted `outcomeRating0to100` (do not assume good or bad), and that `recentAdvice` holds the model's own prior recommendations and observed outcomes, so it can self-correct from them.

#### Scenario: System prompt contains recentAdvice teaching

- **GIVEN** the espresso `shotAnalysisSystemPrompt` output
- **WHEN** the prompt is rendered
- **THEN** it SHALL contain a section discussing `recentAdvice`
- **AND** SHALL describe the four `adherence` values and how to react to each
- **AND** SHALL describe the omitted-rating fallback

#### Scenario: Reactions to each adherence value are taught

- **GIVEN** the espresso `shotAnalysisSystemPrompt` output
- **WHEN** the prompt is rendered
- **THEN** `"followed"` with a worse outcome SHALL be taught as a reason to revise direction
- **AND** `"ignored"` SHALL be taught as a reason to stay the course
- **AND** `"partial"` SHALL be taught as a reason to ask before revising
- **AND** `"unclear"` SHALL be taught as "named nothing checkable": treat it like `"ignored"`, do not assume the experiment ran, and restate the recommendation as a concrete value
- **AND** an omitted rating SHALL be taught as a reason to fall back to `outcomeInPredictedRange` or to ask about taste

### Requirement: User-prompt envelope SHALL remain byte-stable for identical inputs after `recentAdvice` is added

For an identical `(AIConversation snapshot, current shot id, DB state)` triple, the serialized `recentAdvice` bytes SHALL be identical across calls. The block SHALL carry no wall-clock value, monotonic counter or per-call unique id. Numeric fields (`durationSec`, `doseG`, numeric `grinderSetting`) SHALL use the envelope's fixed-precision rules.

#### Scenario: Two consecutive builds with identical inputs produce identical recentAdvice bytes

- **GIVEN** an `AIConversation` snapshot, a frozen DB state, and a fixed current shot id
- **WHEN** the envelope is built twice in succession
- **THEN** the serialized `recentAdvice` bytes SHALL be `==` (byte-for-byte identical)

### Requirement: Advisor user prompt SHALL carry dialInSessions / bestRecentShot / sawPrediction / grinderContext when DB scope is available

When the in-app advisor or `ai_advisor_invoke` has DB scope, it SHALL add top-level `dialInSessions`, `bestRecentShot`, `sawPrediction` and `grinderContext` in the shape `dialing_get_context` produces. All four SHALL come from the shared block-builder helpers in `src/mcp/mcptools_dialing_blocks.h`. A field without data SHALL be omitted, never `null`.

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

#### Scenario: Each block is omitted when its data is absent

- **GIVEN** a resolved shot that is not espresso, a resolved shot with no grinder model, and a shot with no rated shot in the 90-day window
- **WHEN** the user prompt is enriched
- **THEN** the envelope SHALL NOT contain `sawPrediction`, `grinderContext` or `bestRecentShot`
- **AND** SHALL NOT contain any of them as a `null` value

#### Scenario: Both surfaces call the same block builders

- **WHEN** `dialing_get_context` and the in-app advisor each build the four blocks for the same shot
- **THEN** both SHALL call the helpers in `src/mcp/mcptools_dialing_blocks.h`
- **AND** the block contents SHALL be identical between the two

### Requirement: Enriched user prompt SHALL be byte-equivalent across in-app and MCP surfaces

The user prompt assembled by the in-app advisor (`AIManager::requestRecentShotContext`) and the user prompt echoed by `ai_advisor_invoke` (MCP, via `AIManager::enrichUserPromptObject`) SHALL be byte-for-byte identical for the same resolved `ShotProjection` + DB state + Settings state. Both surfaces SHALL call the same block-builder helpers and the same `ShotSummarizer::buildUserPromptObject` envelope builder.

#### Scenario: In-app advisor and ai_advisor_invoke produce identical user prompts

- **GIVEN** a fixed `ShotProjection`, DB state, and Settings state
- **WHEN** the in-app advisor's enrichment closure runs and `ai_advisor_invoke`'s enrichment closure runs against the same inputs
- **THEN** the two resulting user prompt strings SHALL be `==` (byte-for-byte identical)

### Requirement: Enriched user prompt SHALL preserve cache stability

The enriched user prompt SHALL NOT carry any per-call value. `currentDateTime` SHALL NOT appear, because it would bust the prompt cache on every call. `daysSinceShot` inside `bestRecentShot` is acceptable, since it changes only on day boundaries. Field encodings SHALL match `dialing_get_context` exactly, including float precision and Qt's alphabetical key order.

#### Scenario: Enriched user prompt has no currentDateTime

- **GIVEN** any enriched user prompt produced by the in-app advisor or `ai_advisor_invoke`
- **WHEN** the prompt is parsed as JSON
- **THEN** the parsed object SHALL NOT contain a `currentDateTime` key

#### Scenario: Two consecutive enrichments with identical state produce identical bytes

- **GIVEN** a fixed resolved shot, fixed DB state, fixed Settings state, and a wall-clock that does not cross a day boundary between calls
- **WHEN** the user prompt is enriched twice in succession
- **THEN** the two resulting strings SHALL be `==`

### Requirement: Rendered calibration section SHALL constrain how the model uses UGS

The rendered `grinderCalibration` section SHALL state that UGS is a relative coarseness ordering, not a click count, and that numbers hold only within `calibratedUgsRange`. It SHALL NOT give a grinder number for a directional profile or a switch when `confidence` is `directional`. The model SHALL NOT scale a UGS distance itself; only the system `conversionKey` within range is sanctioned. The section SHALL repeat `usageConstraint` verbatim.

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

### Requirement: Directional calibration guidance SHALL use grind-size terms only

For a `"directional"` profile the model SHALL give only a finer or coarser direction and SHALL recommend pulling a reference shot on the target profile. It SHALL NOT translate direction into a dial-number change, because that needs the grinder's numeric convention. When the current profile is not UGS-placed, the section SHALL say the two profiles cannot be ordered.

#### Scenario: No direction field states the profiles cannot be ordered

- **GIVEN** a `grinderCalibration` block where the current profile is not UGS-placed and a target profile entry has no `direction` field
- **WHEN** the calibration section is rendered
- **THEN** the section SHALL state that the model cannot order the two profiles
- **AND** SHALL NOT state a direction for them

### Requirement: System prompt SHALL require taste feedback before declaring dial-in success across repeated untasted shots

When tasting feedback (score or notes) has been absent for the last two or more shots in the conversation, the shared espresso system prompt SHALL instruct the model to ask for a taste score before using success or quality language (such as "dialed in") about those shots. Curve-based observations MAY still be described, framed as preliminary pending taste feedback. This extends, and does not replace, the single-shot `tastingFeedback` rule.

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

The in-app advisor's historical context SHALL include a prior shot only when its equipment package matches the current shot's, in addition to the existing bean, profile and time-window match. "No package recorded" SHALL count as a package value of its own, so a user with no packages sees no change. A different basket or grinder is a different package and SHALL be excluded even when bean, profile and setting match.

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

The in-app advisor and `ai_advisor_invoke` SHALL send the same user-prompt format, assembled by the same code: the structured payload whose field paths the shared system prompt names. Neither surface SHALL carry a second renderer of the same data. The user's question SHALL travel as its own field, not concatenated into the payload, so displaying it is a field read.

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

The advisor payload SHALL name the equipment set shared by the history's shots: the grinder (brand, model, burrs), the basket (brand, model) and the puck-prep technique set. A component with no recorded value SHALL be omitted rather than emitted empty. The set SHALL come from a single shared definition, so the session context and the no-history block cannot describe the same package differently.

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

When no prior shot matches the current shot's bean, profile, time window and equipment package, the in-app advisor's historical context SHALL state that no prior shots matched, name the equipment set matched on, and give the reason equipment-mismatched shots were excluded. It SHALL NOT emit an empty historical context in this case.

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

Change detection SHALL compare the current shot with the previous shot in the same thread, and a thread SHALL be keyed by equipment package, so both shots share one. Change detection SHALL continue to report dose, yield, duration and grind setting. It SHALL NOT report an equipment change, because a thread never spans two packages.

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

The shared espresso system prompt SHALL state that a numeric grind setting is comparable only among shots on the same equipment set, since a change of grinder, burrs or basket makes settings incommensurable even on the same dial. When a setting does not fit the ordering the history implies, the model SHALL consider an equipment difference before theorising about the grinder's mechanism.

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
