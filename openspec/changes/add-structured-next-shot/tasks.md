# Tasks: AI advisor emits structured `nextShot` recommendation

## Implementation

- [x] 1. Extend `ShotSummarizer::shotAnalysisSystemPrompt` (espresso variant) in `src/ai/shotsummarizer.cpp` with a "Response Format" section that:
  - asks for a fenced ` ```json ` block at end of message named `nextShot`,
  - documents the schema (`grinderSetting`, `doseG`, `profileFilename`, `expectedDurationSec`, `expectedFlowMlPerSec`, `expectedPeakPressureBar`, `successCondition`, `reasoning`) with required-vs-optional rules,
  - includes one example block,
  - tells the LLM to OMIT the block entirely when the response is a clarifying question or has no concrete parameter recommendation.
- [x] 2. Add `AIManager::parseStructuredNext(const QString& assistantText) -> std::optional<QJsonObject>` in `src/ai/aimanager.cpp`. Behavior:
  - locate the trailing fenced ` ```json ... ``` ` block via regex anchored to end-of-message (allow trailing whitespace),
  - parse via `QJsonDocument::fromJson`,
  - on absent block, return `std::nullopt` silently,
  - on parse error, log `qWarning() << "structuredNext parse failed:" << err.errorString()` and return `std::nullopt`,
  - do NOT strip the block from the prose returned to the conversation — the user's overlay shows the full message and seeing the JSON confirms the model honored the format.
- [x] 3. Call `parseStructuredNext` on every assistant message landing in `AIManager::onAnalysisComplete` (and the conversation-response path). Pass the resulting optional into `AIConversation::addAssistantMessage(content, structuredNext)`.
- [x] 4. Extend `AIConversation` (`src/ai/aiconversation.{h,cpp}`):
  - signature: `void addAssistantMessage(const QString& content, const std::optional<QJsonObject>& structuredNext = std::nullopt)`,
  - storage: each entry in `m_messages` JSON array becomes `{role, content, structuredNext?}`. Omit the key when `structuredNext` is absent (no `null` placeholders),
  - reader: `std::optional<QJsonObject> AIConversation::structuredNextForLastAssistantTurn() const` and a sibling for an arbitrary index — used by #1053's recentAdvice builder,
  - migration: when loading older conversations, missing `structuredNext` keys are simply absent (no schema bump needed; `QJsonObject` is open-shape).
- [x] 5. Extend `ai_advisor_invoke` in `src/mcp/mcptools_ai.cpp`:
  - after the analyze-call resolves, parse `structuredNext` from the assistant prose using the same `parseStructuredNext` helper,
  - add it to the tool result envelope as a top-level optional field `structuredNext` (omitted when absent — no null),
  - update the tool description to document the new field and its omission semantics.
- [x] 6. Tests in `tests/aimanager_tests/tst_aimanager.cpp`:
  - `parsesValidStructuredNext` — feeds a synthetic prose-with-block string, asserts every required key is present with the expected types and array lengths.
  - `parsesAbsentBlockAsNullopt` — pure prose, no fenced block → returns `nullopt`, no warning.
  - `parsesMalformedBlockAsNullopt` — fenced block with broken JSON → returns `nullopt`, warning log.
  - `parserIgnoresMidMessageJsonBlocks` — message with a `json` block in the middle (e.g., quoted user query) and no trailing block → returns `nullopt`. (Defends against false-positive extraction.)
  - `aiAdvisorInvokeSurfacesStructuredNext` — feed a known assistant reply through the MCP path with a stub provider, assert the tool envelope's `structuredNext` matches.
  - `conversationPersistsStructuredNext` — call `addAssistantMessage` with a structured block, save and reload the conversation, assert `structuredNextForLastAssistantTurn` returns the same object.
- [x] 7. Update documentation:
  - `docs/CLAUDE_MD/AI_ADVISOR.md` — new section "Structured nextShot output" documenting the schema and the gating ("only emitted when the response makes a concrete recommendation").
- [x] 8. Run `openspec validate add-structured-next-shot --strict --no-interactive` and resolve any issues.
- [x] 9. Build via Qt Creator (Decenza-Desktop), run `tst_aimanager`, confirm green.
