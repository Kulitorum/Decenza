# Tasks: Closed-loop advisor — `recentAdvice` block + per-turn shot linkage

## Implementation

### Part A — Per-turn shot linkage in AIConversation

- [x] 1. Extend `src/ai/aiconversation.{h,cpp}`:
  - persist `shotId` (qint64) on each turn entry; the `messages` JSON array becomes `{role, content, shotId?, structuredNext?}`,
  - add `void setShotIdForCurrentTurn(qint64 shotId)` — applies the id to the most recent user turn AND the next-appended assistant turn (single-value member that latches until the next user message resets it),
  - add `qint64 shotIdForTurn(qsizetype index) const` — returns 0 when the key is absent,
  - add a small `HistoricalAssistantTurn` struct (`shotId`, `content`, `structuredNext`) and `QList<HistoricalAssistantTurn> recentAssistantTurns(qsizetype max) const` — most-recent-first, SKIPS turns where shotId == 0 OR structuredNext is absent,
  - confirm older conversations (no `shotId` key) load cleanly and `shotIdForTurn` returns 0.
- [x] 2. Wire `AIManager::analyzeShotWithMetadata` (`src/ai/aimanager.cpp`) to call `m_conversation->setShotIdForCurrentTurn(shotId)` after the resolved shot id is known and before the assistant message is appended.
- [x] 3. Persistence test in `tests/aimanager_tests/tst_aimanager.cpp`: round-trip a conversation containing one user/assistant pair with `shotId = 8473`; reload from the same storage key; assert both turns carry `shotId == 8473`. Add a second test where a legacy persisted conversation (manually written `messages` array with no `shotId` keys) loads and `shotIdForTurn` returns 0.

### Part B — recentAdvice block builder

- [x] 4. Add `buildRecentAdviceBlock` to `src/mcp/mcptools_dialing_blocks.{h,cpp}`. Signature uses an adapter struct `RecentAdviceInputs` so both surfaces can drive it:
  ```cpp
  struct RecentAdviceInputs {
      QList<AIConversation::HistoricalAssistantTurn> turns;  // most-recent-first
      QSqlDatabase db;
      qint64 currentProfileKbId;
      qint64 currentShotId;  // excluded from "follow-up" lookup so we don't pair a turn with its own shot
  };
  QJsonArray buildRecentAdviceBlock(const RecentAdviceInputs& in);
  ```
- [x] 5. Helper inside the builder:
  - For each candidate turn (most-recent-first, capped at the first 3 that pass), look up the prior turn's shot in `db` to get its `profile_kb_id`, `timestamp`. SKIP if `profile_kb_id != currentProfileKbId`.
  - Query the user's history for the next shot postdating the prior turn's timestamp on the same profile (excluding `currentShotId`). SKIP the entry if no such shot exists.
  - Compute `adherence` per the spec rules (string-equal grinder OR within 0.25 step; dose ±0.3g; profile filename equality). Default `"ignored"` when `structuredNext` carries no parameter recommendations and the actual shot's params equal the prior turn's shot's params, override to `"followed"`.
  - Compute `outcomeInPredictedRange.duration` / `.flow` / `.pressure` from the next shot's `durationSec`, `mainFlowMlPerSec` (or equivalent — match existing fields), `peakPressureBar`. `pressure` is included only if `expectedPeakPressureBar` was on the prior turn's `structuredNext`.
  - Omit `outcomeRating0to100` when `enjoyment0to100 <= 0`. Omit `outcomeNotes` when empty.
  - Use `structuredNext.reasoning` verbatim for `recommendation`; when absent, synthesize a short summary (e.g., `"Try grinder X; expect Y-Zs, A-B ml/s"`).
- [x] 6. Wire the in-app advisor:
  - in `AIManager::analyzeShotWithMetadata`'s background-thread closure (where `dialInSessions` etc. are built), pull `m_conversation->recentAssistantTurns(3)` once on the main thread before the closure starts, pass into the closure as `RecentAdviceInputs.turns`, call `buildRecentAdviceBlock`, and merge into the envelope alongside the existing four blocks.
  - omit the key entirely when the returned `QJsonArray` is empty.
- [x] 7. Wire `ai_advisor_invoke` (`src/mcp/mcptools_ai.cpp`):
  - the MCP tool already has access to a conversation context (per-profile, per-bean). Pull `recentAssistantTurns(3)` from that conversation, build the block, attach to `userPromptUsed` echo so it matches the in-app advisor byte-for-byte for the same inputs.

### Part C — System prompt teaching

- [x] 8. Extend the espresso `shotAnalysisSystemPrompt` builder in `src/ai/shotsummarizer.cpp` with a new "How to use `recentAdvice`" subsection in "How to read structured fields":
  - explain `adherence` semantics and the three reactions (revise / stay-the-course / ask),
  - explain the `outcomeRating0to100` omission fallback,
  - emphasize that `recentAdvice` is the LLM's own prior recommendations + outcomes, so it can self-correct mid-session.

### Part D — Tests

- [x] 9. `recentAdviceQualifyingTurnRendersWithAdherenceFollowed` in `tests/aimanager_tests/tst_aimanager.cpp`:
  - synthesize an `AIConversation` with one prior assistant turn (`shotId = 100`, `structuredNext` with `grinderSetting "4.75"` and `expectedDurationSec [32, 38]` and `expectedFlowMlPerSec [1.0, 1.5]`),
  - insert a fake DB with shot 100 and shot 105 on the same profile (shot 105 has `grinderSetting "4.75"`, `durationSec 35`, `mainFlowMlPerSec 1.2`, `enjoyment0to100 75`, `espressoNotes "balanced"`),
  - call `buildRecentAdviceBlock`, assert one entry with `adherence == "followed"`, `outcomeRating0to100 == 75`, `outcomeInPredictedRange.duration == true`, `outcomeInPredictedRange.flow == true`.
- [x] 10. `recentAdviceOmitsRatingWhenUnrated`: same setup but shot 105 has `enjoyment0to100 == 0`. Assert `outcomeRating0to100` key is absent on the entry.
- [x] 11. `recentAdviceCrossProfileFiltersOut`: prior turn on profile A, current shot on profile B. Assert `recentAdvice` is empty (block-level absent).
- [x] 12. `recentAdviceIgnoredWhenUserDidntFollow`: prior turn recommended `grinderSetting "4.75"` + `doseG 19`. Actual next shot kept `grinderSetting "5.0"` + `doseG 18`. Assert `adherence == "ignored"`.
- [x] 13. `recentAdviceParityBetweenInAppAndMcp`: same conversation + DB + current shot, drive both the in-app advisor enrichment and `ai_advisor_invoke`'s envelope construction, assert `recentAdvice` JSON is byte-equal.
- [x] 14. `byteStabilityRecentAdvice`: build the envelope twice with frozen inputs; assert serialized `recentAdvice` bytes are identical.

### Part E — Validation + build

- [x] 15. Run `openspec validate add-closed-loop-advisor --strict --no-interactive`; resolve any issues.
- [x] 16. Build via Qt Creator (Decenza-Desktop), run `tst_aimanager`, confirm green.
- [x] 17. Sanity-build `tst_mcptools_dialing` since the new helper lives in the dialing-blocks module.
