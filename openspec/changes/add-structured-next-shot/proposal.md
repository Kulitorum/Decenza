# Change: AI advisor emits a structured `nextShot` recommendation alongside prose

## Why

The advisor today returns a paragraph that ends with concrete advice — "try grind 4.75", "expect 32–38s, 1.0–1.5 ml/s" — but the *app* cannot read it. Every downstream feature that wants to validate adherence, score predictions, surface "expected outcome" coachmarks, or compare provider performance has to re-parse prose.

In the May-2026 testing run (post-#1041 / #1037), every response had an actionable recommendation in prose, none in machine-readable form. That gap blocks closed-loop coaching (#1053), proactive coachmarks on the dial-in screen, mechanical multi-provider A/B, and any quality-of-advice scoring.

See issue #1054.

## What Changes

- **The shot-analysis system prompt** SHALL instruct the LLM to append a fenced JSON block named `nextShot` to its response when it makes a concrete recommendation. The block is required when the response recommends a parameter change; it is optional (and SHALL be omitted) when the response only asks the user a clarifying question or reports a finding without a recommendation.
- **`AIManager`** SHALL parse the trailing fenced JSON block out of the assistant's response. The parser SHALL:
  - extract the last fenced `json` block (pattern: ` ```json\n{...}\n``` ` at end-of-message after optional whitespace),
  - tolerate absence (older models, off-task replies, clarifying questions): `structuredNext = null` and the response is shipped unchanged,
  - tolerate parse failure (malformed JSON): logged at warning level, `structuredNext = null`, prose preserved.
- **The parsed `nextShot` block** SHALL be persisted into `AIConversation` storage on the assistant message it was extracted from. Schema-wise, the assistant entry gains an optional `structuredNext` JSON field alongside `role` and `content`. This is the load-bearing precondition for #1053's closed-loop coaching.
- **`ai_advisor_invoke` (MCP)** SHALL surface the parsed block as a top-level `structuredNext` field in its response envelope, separate from `response` (the prose). MCP consumers do not have to re-parse.
- **The `nextShot` schema** carries the recommendation in a fixed shape:
  - `grinderSetting` (string) — REQUIRED when the recommendation moves grind; omitted when grind is unchanged.
  - `doseG` (number) — REQUIRED when the recommendation moves dose; omitted when dose is unchanged.
  - `profileTitle` (string) — REQUIRED when the recommendation switches profile; omitted otherwise.
  - `expectedDurationSec` (`[low, high]` two-element number array) — REQUIRED. The window the LLM expects the next shot's total duration to fall in if its recommendation is followed.
  - `expectedFlowMlPerSec` (`[low, high]`) — REQUIRED.
  - `expectedPeakPressureBar` (`[low, high]`) — OPTIONAL; included when the recommendation specifically targets pressure dynamics.
  - `successCondition` (string) — REQUIRED. A short natural-language predicate the app stores verbatim (and a future change may evaluate). Examples: `"score >= 70 OR (durationSec in [32,38] AND flowMlPerSec in [1.0,1.5])"`, `"channeling_none AND yieldG within 0.5g of target"`.
  - `reasoning` (string) — REQUIRED. One-sentence summary of *why* (so the app can show it on a coachmark without showing the full prose).
- **No new user-facing UI** is introduced in this change. The structured block enables future surfaces (#1053 closed-loop, dial-in coachmarks) without committing to a UI shape now.

## Impact

- **Affected specs:**
  - New spec `advisor-structured-next` — pins the JSON block contract, parser tolerances, persistence requirement, and `ai_advisor_invoke` surface.
- **Affected code:**
  - `src/ai/shotsummarizer.cpp` — extend `shotAnalysisSystemPrompt` (espresso variant) with a "Response Format" section asking for the `nextShot` JSON suffix, with the schema spelled out and an example.
  - `src/ai/aimanager.cpp` — new `parseStructuredNext(QString assistantText) -> std::optional<QJsonObject>` helper; called on every assistant message before it lands in `AIConversation`. The prose returned to the user is unchanged (the JSON block is left in place at the end — stripping it would force a second LLM call for non-conversational re-rendering, and seeing the structured block confirms the system prompt was honored).
  - `src/ai/aiconversation.{h,cpp}` — assistant message persistence gains an optional `structuredNext` field. Storage format: each entry in the `messages` JSON array becomes `{role, content, structuredNext?}`. Older saved conversations (no `structuredNext`) load as `null` — fully backward-compatible.
  - `src/mcp/mcptools_ai.cpp` — `ai_advisor_invoke` parses the structured block from the response and emits it as `structuredNext` in the tool result envelope (alongside `response`, `userPromptUsed`).
  - `tests/aimanager_tests/tst_aimanager.cpp` — new tests:
    - parser pins the contract for a synthetic assistant message containing a valid block,
    - parser returns null on absent block (clarifying-question response),
    - parser returns null on malformed JSON, with a warning log,
    - end-to-end MCP test: feed a fixed prose-with-block reply through `ai_advisor_invoke`, assert `structuredNext.grinderSetting`, `expectedDurationSec`, `successCondition` reach the response envelope.
- **Affected callers (no signature change, behavior change only):**
  - In-app conversation overlay receives prose unchanged; `AIConversation` load now exposes `structuredNext` per assistant turn (read-only for now).
  - `ai_advisor_invoke` MCP envelope adds `structuredNext` (optional). Existing consumers ignoring unknown fields are unaffected.
- **NOT in scope:**
  - Tool-call-shape support (Anthropic / OpenAI native function calling) — the JSON-suffix path works across every provider including Ollama. A future change may add tool-call-shape as an alternative when the provider supports it.
  - Evaluating `successCondition` programmatically. The string is stored verbatim; #1053 reads it as advisory text for the LLM, not as an executable predicate.
  - Surfacing the structured block in any in-app UI (coachmarks, dial-in screen). A separate change will do that once the data exists.
- **Cache stability:** the system prompt's new "Response Format" section is static text and ships in the cached prefix. No per-call drift introduced.
- **Backward compatibility:** absence of `structuredNext` on an assistant message is the documented null state; #1053's recentAdvice block degrades gracefully when prior turns lack the field.
