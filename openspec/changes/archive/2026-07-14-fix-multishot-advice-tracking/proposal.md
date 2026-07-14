## Why

Issue #639 ("Improve multi-shot conversation context") reported that the AI dialing advisor's multi-shot conversations don't track whether the user actually followed prior advice, and don't know when to stop declaring success. We validated this two ways: (1) reviewing real exported multi-shot conversations via the new `ai_conversations_list`/`ai_conversation_get` MCP tools, and (2) a blind-judged A/B test replaying real conversation turns through both Claude and OpenAI models with candidate system-prompt fixes.

The root cause for the "ignored advice" failure is **not a missing prompt rule — it's a spec-compliance bug**. `advisor-conversation-history` already requires `AIManager` to call `AIConversation::setShotIdForCurrentTurn()` before an assistant turn is appended, and `advisor-user-prompt` already requires the in-app advisor's `requestRecentShotContext` path to build a `recentAdvice` block byte-equal to the one `ai_advisor_invoke` (MCP) produces. Neither is actually true: `setShotIdForCurrentTurn` is never called from anywhere in `src/` or `qml/`, and `requestRecentShotContext`/`emitRecentShotContext` never builds a `recentAdvice` section. The in-app conversation overlay — the primary UI 95%+ of users go through — has never had this closed-loop tracking; only the MCP `ai_advisor_invoke` path does.

The A/B test confirmed fixing just this plumbing (feeding the same `recentAdvice` data into an unmodified system prompt) makes the model explicitly call out ignored/contradicted advice — matching the effect of adding new prompt rules, but at zero added system-prompt tokens (the prompt is already ~10.2K tokens; the existing `recentAdvice` teaching section is already unconditionally present and currently unused by the in-app path).

A second, smaller gap the same test surfaced: when tasting feedback has been missing for 2+ consecutive shots, the advisor declares shots "successful"/"optimal"/"perfect" from curve data alone rather than asking for a score first. A narrow prompt addition alone did not reliably fix this (it lost out to existing "acknowledge success" guidance elsewhere in the prompt); a more emphatic, specific rule did, with no regression on two control scenarios (still detects real multi-shot trends; doesn't re-ask for a score already given).

**Cross-model confirmation.** The ignored-advice failure (the bug this change fixes) was re-run against real Claude Sonnet 5, GPT-5.4-mini, and full GPT-5.4 — all three failed it identically with the unmodified baseline prompt. That's the confirmation this is a data-availability bug, not a model-reasoning gap: no model, regardless of capability, can call out a divergence it was never given the data to see. The same cross-model pass surfaced an unrelated, out-of-scope finding: GPT-5.4-mini specifically (not GPT-5.4 generally) failed to detect a real 4-shot pressure trend that both Claude and full GPT-5.4 caught from the same data, using the *existing*, unmodified `multiShotSystemPrompt()` trend-tracking instruction. That is a model-capability limitation, not something this change addresses — noted as a known limitation, not a task, since fixing it would mean recommending against a specific provider's model tier, not a code change.

## What Changes

- Wire `AIConversation::setShotIdForCurrentTurn()` into the in-app conversation flow (`ConversationOverlay.qml`'s `sendFollowUp()`, mirroring the "ask()/followUp()" call sites) so in-app turns actually carry `shotId`, satisfying the existing `advisor-conversation-history` requirement end-to-end.
- Extend `AIManager::requestRecentShotContext` / `emitRecentShotContext` to build and include a `recentAdvice` section (reusing `DialingBlocks::buildRecentAdviceBlock` and `AIConversation::recentAssistantTurns`, rendered to match the in-app prose format) so the in-app advisor satisfies the existing `advisor-user-prompt` parity requirement with `ai_advisor_invoke`.
- Strengthen the shared espresso system prompt's tasting-feedback guidance with a specific rule: after 2+ consecutive shots with no score/notes, the advisor SHALL ask for taste feedback before using success/quality language, rather than declaring a shot "optimal" from curves alone.
- No change to `multiShotSystemPrompt()`'s existing 3-sentence "track progress" text — the A/B test showed no evidence that adding trend-length, escalation, or completion-criterion rules improves outcomes over what's already covered by fixing the `recentAdvice` plumbing; those remain out of scope pending real failure evidence.

## Capabilities

### New Capabilities
(none)

### Modified Capabilities
- `advisor-conversation-history`: the in-app conversation flow now actually invokes `setShotIdForCurrentTurn`, so the already-specified per-turn `shotId` linkage is reachable end-to-end instead of only from paths that never call it in practice.
- `advisor-user-prompt`: the in-app advisor's `requestRecentShotContext` path now builds and includes `recentAdvice`, satisfying the existing in-app/MCP parity requirement. A new requirement is added: the system prompt SHALL instruct the model to request taste feedback before declaring dial-in success when the last 2+ shots have none.

## Impact

- `src/ai/aiconversation.h/.cpp` — no interface change; `setShotIdForCurrentTurn` gains a real caller.
- `src/ai/aimanager.h/.cpp` — `requestRecentShotContext`/`emitRecentShotContext` gains a `recentAdvice` section; needs the current conversation's `AIConversation*` and the resolved shot id at build time (both already available at the call site).
- `qml/components/ConversationOverlay.qml` — `sendFollowUp()` (desktop) and its mobile-dialog counterpart call `conversation.setShotIdForCurrentTurn(overlay.shotId)` before `ask()`/`followUp()`.
- `src/ai/shotsummarizer.cpp` or wherever the shared espresso system prompt text lives — one new paragraph in the tasting-feedback guidance.
- No DB schema change, no new MCP tools, no breaking change to existing `ai_advisor_invoke` behavior (MCP path already does this correctly and is unaffected).
