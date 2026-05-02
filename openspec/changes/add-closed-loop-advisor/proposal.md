# Change: Closed-loop advisor — `recentAdvice` block + per-turn shot linkage

## Why

Each call to the AI advisor is currently amnesic about its own past advice and whether the user followed it. The dialing-context envelope ships recent shot history and the previous shot's `changeFromPrev` diff, but nothing about what the *advisor* recommended on the prior turn or whether the recommendation worked.

In the May-2026 testing run (post-#1041 / #1037), the advisor said "try grind 4.75" on shot 884 and "0.25 finer" on shot 855. On the next turn, it had no structured way to know whether the user actually moved grind, what the resulting shot tasted like, or whether to revise direction. Each turn re-derives the dial-in trajectory from raw history; the advisor's *own contribution to the trajectory is invisible*.

Today's advisor is a one-shot analyst. Closing the loop turns it into a coach:

- **Adherence-aware:** "Last time I said go finer; you stayed at 4.5 — let's try the recommendation before I revise" vs. "you did go finer and it got worse — direction was wrong, reverse."
- **Self-correcting:** the advisor can notice when its own advice didn't move the outcome and adapt strategy mid-session.
- **Cumulative learning within a session:** by turn 3 the advisor has narrowed the search instead of restarting.

See issue #1053.

## What Changes

This change has two co-dependent parts:

### Part A — Per-turn shot linkage in `AIConversation`

`AIConversation::m_messages` today persists `{role, content}` and now (after #1054) optionally `structuredNext` per assistant turn. There is no field tying a turn to the *shot id* the advisor was analyzing — turns are reconstructable per profile/bean conversation key, but the per-shot mapping is lost.

- `AIConversation` SHALL persist a `shotId` (qint64) on each user/assistant turn pair, recording which shot the advisor was asked about.
- The current `addUserMessage(content)` / `addAssistantMessage(content[, structuredNext])` API SHALL gain a sibling `setShotIdForCurrentTurn(qint64)` (or equivalent) called from `AIManager::analyzeShotWithMetadata` once the resolved shot id is known.
- Storage layout: each turn entry becomes `{role, content, shotId?, structuredNext?}`. A turn without a `shotId` is a "general" question (no specific shot context — same null-state semantics as `structuredNext`).
- Loading older conversations (no `shotId` key) SHALL succeed; `shotId` reads as `0` / absent. No migration step required — it's a soft schema extension.

### Part B — `recentAdvice` block in the user-prompt envelope

A new top-level `recentAdvice` block SHALL be added to the JSON envelope produced by `ShotSummarizer::buildUserPromptObject` and enriched by `AIManager::analyzeShotWithMetadata`'s background-thread path (mirrors the dialing-blocks enrichment pattern from #1041). The same block SHALL appear in `ai_advisor_invoke`'s `userPromptUsed` echo (parity contract with the in-app advisor, established by #1041).

The block SHALL be an array of up to 3 entries, ordered most-recent-first, each derived from a prior advisor turn in the *same conversation* (matched by `AIConversation` storage key — bean+profile hash) where:

- the prior turn has a non-zero `shotId`,
- the prior turn has a non-null `structuredNext` (no recommendation → no entry; a question-only turn doesn't enter recentAdvice),
- a later shot exists in the user's history that postdates the prior turn's shot (otherwise there's no "outcome" yet — the user hasn't pulled a follow-up shot),
- AND the prior turn's shot is on the *same profile* as the current shot (cross-profile advice is not closed-loop comparable).

For each entry, the block SHALL carry:

- `turnsAgo` (number) — 1, 2, or 3 (the position in the assistant-turn history; gaps from omitted turns are skipped).
- `recommendation` (string) — short summary built from `structuredNext.reasoning`. Falls back to a synthesized summary derived from `structuredNext` fields when `reasoning` is missing.
- `structuredNext` (object) — the verbatim block from the advisor's prior turn, so the LLM can re-read its own predictions.
- `userResponse` (object) — the *follow-up shot* attribution computed by the app:
  - `actualNextShotId` (number) — the next shot in the user's history postdating the prior turn's shot, on the same profile.
  - `grinderSetting` (string) — the actual grinder setting on that shot.
  - `doseG` (number).
  - `adherence` (`"followed" | "partial" | "ignored"`) — computed by comparing the recommendation to the actual setting:
    - `followed` — every recommended field (grinder/dose/profile) matches the actual within tolerance (grinder string equality OR within 0.25 step; dose ±0.3g; profile filename equality).
    - `partial` — at least one but not all recommended fields match.
    - `ignored` — none of the recommended fields match.
  - `outcomeRating` (number, 0-100) — `enjoyment0to100` from the actual shot. OMITTED when the actual shot is unrated (`<= 0`); the LLM is taught to read the omission as "rating not captured."
  - `outcomeNotes` (string) — `espressoNotes` from the actual shot. OMITTED when empty.
  - `outcomeInPredictedRange` (object with `duration: bool` and `flow: bool`) — whether the actual shot's totals landed inside the prior turn's `expectedDurationSec` / `expectedFlowMlPerSec` ranges. `pressure: bool` SHALL be present when `expectedPeakPressureBar` was on the prior turn.

### System prompt teaching

`shotAnalysisSystemPrompt` SHALL gain a new "How to use `recentAdvice`" subsection in its "How to read structured fields" area:

- "When `recentAdvice[].userResponse.adherence == 'followed'` AND `outcomeRating` (or `outcomeInPredictedRange`) suggests the shot got worse, REVISE direction; do not repeat the same recommendation."
- "When `adherence == 'ignored'`, the user did not run your previous experiment — STAY THE COURSE before pivoting."
- "When `outcomeRating` is omitted, do not assume good or bad — fall back to `outcomeInPredictedRange` for a curve-shape signal, or ask the user."

### Cache stability

The block SHALL NOT introduce per-call wall-clock or per-request unique values. `turnsAgo` (1/2/3) is structural, not temporal. `actualNextShotId` is a database id — stable across calls for the same input.

### Gating / omission

- `recentAdvice` SHALL be OMITTED entirely (no key, no empty array) when it would be empty.
- Individual entries SHALL be OMITTED (skipping their `turnsAgo` slot) when their preconditions fail. The block does NOT carry placeholder entries.

## Impact

- **Affected specs:**
  - New spec `advisor-conversation-history` — pins per-turn shot linkage, soft-schema extension, and reader API.
  - `advisor-user-prompt` — ADDED requirements for the `recentAdvice` block, system-prompt teaching, and parity between the in-app advisor and `ai_advisor_invoke`.
- **Affected code:**
  - `src/ai/aiconversation.{h,cpp}` — soft-schema extension: persist `shotId` per turn, expose `setShotIdForCurrentTurn`, expose readers for the last N assistant turns with their `(shotId, structuredNext)` tuple.
  - `src/ai/aimanager.cpp` — `analyzeShotWithMetadata` calls `setShotIdForCurrentTurn(resolvedShotId)` after the shot is resolved; the background-thread enrichment path queries `AIConversation` for prior `(shotId, structuredNext)` tuples and `ShotHistoryStorage` for follow-up shot attribution; new helper `buildRecentAdviceBlock(conversation, db, currentProfileKbId, currentShotId) -> QJsonArray`.
  - `src/mcp/mcptools_dialing_blocks.{h,cpp}` — the new `buildRecentAdviceBlock` lives here so it's shared with `ai_advisor_invoke` (parity with the dialing-blocks pattern from #1041). It takes a shared adapter struct so `ai_advisor_invoke` can drive it without depending on `AIConversation` directly when the MCP path doesn't have one.
  - `src/mcp/mcptools_ai.cpp` (`ai_advisor_invoke`) — when the caller has a conversation context (per-profile, per-bean storage key), enrich its echoed `userPromptUsed` with `recentAdvice`. When called fresh (no prior turns for the key), the block is omitted — same null-state semantics as the in-app path.
  - `src/ai/shotsummarizer.cpp` — extend `shotAnalysisSystemPrompt` (espresso variant) with the "How to use `recentAdvice`" subsection.
  - `tests/aimanager_tests/tst_aimanager.cpp`:
    - persistence: turn-pair with non-zero `shotId` round-trips through save/load.
    - block builder: synthesize a fixed `AIConversation` with two prior advisor turns (one with `structuredNext`, one without), a fake `ShotProjection` history with follow-up shots, assert `recentAdvice` is built with the expected `adherence` and `outcomeInPredictedRange` values.
    - parity: feed identical inputs through the in-app advisor's enrichment and `ai_advisor_invoke`, assert `recentAdvice` is byte-equal.
    - omission: empty conversation → block absent (no `recentAdvice: []`).
- **Affected callers (no signature changes; behavior changes only):**
  - In-app conversation overlay: passes `shotId` through from analysis to `AIConversation`. UI does not change.
  - `ai_advisor_invoke` MCP envelope adds `recentAdvice` (optional). Existing consumers ignoring unknown fields are unaffected.
- **Depends on:**
  - **#1054** (structured `nextShot` block) — without it, `recentAdvice` entries have nothing to carry under `structuredNext` and no `expectedDurationSec` / `expectedFlowMlPerSec` to score against.
- **Soft dependency:**
  - **#1055** (rating capture) — without ratings, `outcomeRating` is omitted on most entries. The block still ships `adherence` and `outcomeInPredictedRange`; only the headline taste signal is missing. Spec is written so `outcomeRating` is OPTIONAL and degrades gracefully.
- **NOT in scope:**
  - Any UI surface for `recentAdvice` (e.g., showing previous recommendations in the conversation overlay). The block is for the LLM to read, not the user — same as `dialInSessions`.
  - Cross-profile advice attribution. If the user switches profile between turns, `recentAdvice` for the new profile is empty; the prior profile's advice is not carried over.
  - Editing or deleting prior turns. The block is read-only.
- **Migration:** soft-schema extension. Older conversations (no `shotId` per turn) load with `shotId = 0` on every entry, which excludes them from `recentAdvice` (the block requires non-zero `shotId`). They become eligible only after their owners' next advisor call writes a `shotId`. No data backfill is required.
- **Cache stability (`buildUserPromptObject` byte-stability requirement from #1034):** the new block is structural, derived from stored conversation + database state. For the same `(conversation key, current shot id)` pair, the block bytes SHALL be identical across calls.
