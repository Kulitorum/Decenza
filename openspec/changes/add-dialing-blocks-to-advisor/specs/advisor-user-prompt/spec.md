# advisor-user-prompt — Delta

## ADDED Requirements

### Requirement: Advisor user prompt SHALL carry dialInSessions / bestRecentShot / sawPrediction / grinderContext when DB scope is available

When the in-app advisor (`AIManager::analyzeShotWithMetadata`) and the MCP `ai_advisor_invoke` tool assemble the user prompt, they SHALL enrich the JSON envelope with up to four additional top-level fields, matching `dialing_get_context`'s shape exactly:

- `dialInSessions` — runs of consecutive shots on the same profile within ~60 minutes of each other, with hoisted session-level `context` and per-shot `changeFromPrev` diffs. Same shape `dialing_get_context` produces.
- `bestRecentShot` — the highest-rated shot on the same profile within the last 90 days (excluding the current shot), with a `changeFromBest` diff against the current shot. Omitted entirely (no key, no `null`) when no rated shot exists in that window.
- `sawPrediction` — predicted post-cut drip in grams from the SAW learner, with `sourceTier` reporting the active model. Omitted (no key) when the resolved shot is not espresso, when no scale is configured, when no profile is configured, or when the shot lacks usable flow samples in the last 2 seconds.
- `grinderContext` — observed settings range and step size for the resolved shot's grinder model. Omitted (no key) when the resolved shot has no grinder model OR when both the bean-scoped and cross-bean queries return no rows.

These four fields SHALL be produced by shared block-builder helpers exported from `src/mcp/mcptools_dialing_blocks.h`. Both `dialing_get_context` and the in-app advisor / `ai_advisor_invoke` SHALL call the same helpers, so divergence between the two surfaces is impossible by construction.

#### Scenario: User prompt carries dialInSessions when shots exist on the resolved shot's profile

- **GIVEN** a resolved shot whose `profileKbId` matches 4 prior shots in two distinct sessions
- **WHEN** `AIManager::analyzeShotWithMetadata` enriches the user prompt
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

The user prompt produced by `AIManager::analyzeShotWithMetadata` (in-app advisor) and the user prompt echoed by `ai_advisor_invoke` (MCP) SHALL be byte-for-byte identical for the same resolved `ShotProjection` + DB state + Settings state. Both surfaces SHALL call the same block-builder helpers and the same `ShotSummarizer::buildUserPromptObject` envelope builder.

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

## MODIFIED Requirements

### Requirement: AI advisor user prompt SHALL be JSON-shaped

`ShotSummarizer::buildUserPrompt(summary)` SHALL return a JSON-encoded string (indented, deterministic field ordering) carrying the structured fields the shot-analysis system prompt references. The shape mirrors `dialing_get_context`'s response for the fields available without DB / MainController scope:

- `currentBean` — DYE-resolved bean and grinder identity. SHALL include `brand`, `type`, `roastLevel`, `grinderBrand`, `grinderModel`, `grinderBurrs`, `grinderSetting`, `doseWeightG`. SHALL include `beanFreshness` (with `roastDate`, `freshnessKnown: false`, and the storage-mode `instruction`) when DYE roastDate is non-empty. SHALL include `inferredFromShotId` and `inferredFields[]` when grinder/dose fields fell back to the resolved shot's values.
- `currentProfile` — `filename`, `title`, `intent`, `recipe`, `targetWeightG`, `targetTemperatureC`, `recommendedDoseG` (when set).
- `tastingFeedback` — `hasEnjoymentScore`, `hasNotes`, `hasRefractometer`, plus a `recommendation` string when any of the three is missing.
- `shotAnalysis` — the existing prose markdown (Shot Summary + Phase Data + Detector Observations) preserved verbatim as a string field.

`ShotSummarizer` SHALL also expose `buildUserPromptObject(summary, mode)` returning the unwrapped `QJsonObject` so DB-scoped callers (the in-app advisor's background-thread closure, `ai_advisor_invoke`'s background-thread closure) can append the four enrichment blocks (`dialInSessions`, `bestRecentShot`, `sawPrediction`, `grinderContext`) before serializing. The serialized output of `buildUserPrompt(summary, mode)` SHALL be `QJsonDocument(buildUserPromptObject(summary, mode)).toJson(QJsonDocument::Indented)`.

The four DB-scoped fields (`dialInSessions`, `bestRecentShot`, `sawPrediction`, `grinderContext`) SHALL be added to the user prompt by callers with DB scope (the in-app advisor and `ai_advisor_invoke`), via the shared block-builder helpers. Synchronous callers without DB scope (`generateEmailPrompt`, `generateShotSummary`, the history-block path) SHALL continue to ship the four-key envelope from `buildUserPrompt` without enrichment, and SHALL NOT use `null` placeholders for the absent fields.

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

- **GIVEN** a synchronous caller (e.g. `generateEmailPrompt` or the history-block path) that does not have DB / Settings access
- **WHEN** `buildUserPrompt(summary)` runs
- **THEN** the returned JSON SHALL NOT contain a `dialInSessions` key
- **AND** SHALL NOT contain a `bestRecentShot` key
- **AND** SHALL NOT contain a `sawPrediction` key
- **AND** SHALL NOT contain a `grinderContext` key
- **AND** SHALL NOT use `null` placeholders for any of these field names
