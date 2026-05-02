# advisor-user-prompt — Delta (closed-loop coaching)

## ADDED Requirements

### Requirement: User-prompt envelope SHALL carry an optional `recentAdvice` block

The JSON envelope produced by `ShotSummarizer::buildUserPromptObject` and enriched by `AIManager::analyzeShotWithMetadata`'s background-thread path SHALL include an optional top-level `recentAdvice` array. The same block SHALL appear under `userPromptUsed` in `ai_advisor_invoke`'s tool result envelope (parity contract from #1041).

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
