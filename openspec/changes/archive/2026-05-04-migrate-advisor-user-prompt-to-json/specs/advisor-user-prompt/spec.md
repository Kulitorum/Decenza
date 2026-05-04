# advisor-user-prompt — Delta

## ADDED Requirements

### Requirement: AI advisor user prompt SHALL be JSON-shaped

`ShotSummarizer::buildUserPrompt(summary)` SHALL return a JSON-encoded string (indented, deterministic field ordering) carrying the structured fields the shot-analysis system prompt references. The shape mirrors `dialing_get_context`'s response for the fields available without DB / MainController scope:

- `currentBean` — DYE-resolved bean and grinder identity. SHALL include `brand`, `type`, `roastLevel`, `grinderBrand`, `grinderModel`, `grinderBurrs`, `grinderSetting`, `doseWeightG`. SHALL include `beanFreshness` (with `roastDate`, `freshnessKnown: false`, and the storage-mode `instruction`) when DYE roastDate is non-empty. SHALL include `inferredFromShotId` and `inferredFields[]` when grinder/dose fields fell back to the resolved shot's values.
- `currentProfile` — `filename`, `title`, `intent`, `recipe`, `targetWeightG`, `targetTemperatureC`, `recommendedDoseG` (when set).
- `tastingFeedback` — `hasEnjoymentScore`, `hasNotes`, `hasRefractometer`, plus a `recommendation` string when any of the three is missing.
- `shotAnalysis` — the existing prose markdown (Shot Summary + Phase Data + Detector Observations) preserved verbatim as a string field.

Fields requiring DB/MainController scope (`dialInSessions`, `bestRecentShot`, `sawPrediction`, `grinderContext`) SHALL be omitted from the in-app advisor's user prompt. Their absence SHALL NOT be encoded as empty placeholders (no `dialInSessions: []`, no `bestRecentShot: null`); they are simply not keys in the payload. Omission is the documented contract — a follow-up change may route the in-app advisor through `dialing_get_context`'s code path to ship them.

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

#### Scenario: Out-of-scope fields are omitted, not nulled

- **GIVEN** any `ShotSummary` (the in-app advisor's call site has no DB scope)
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
