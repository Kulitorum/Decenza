## Context

Two related AI advisor surfaces exist for multi-turn dial-in conversations:

1. **In-app**: `ConversationOverlay.qml` drives a live `AIConversation` (owned by `AIManager`). Its single `sendFollowUp()` function (used by desktop inline input, the send button, and the mobile fullscreen dialog) builds the outgoing message from `overlay.pendingShotSummary` + `overlay.historicalContext`, then calls `conversation.ask(...)` (first turn) or `conversation.followUp(...)` (subsequent turns). `overlay.historicalContext` is populated asynchronously by `AIManager::requestRecentShotContext()` → `emitRecentShotContext()`, which queries prior shots on the same bean+profile and renders `## Previous Shots...`, `## Grinder Context`, and `## Grinder Calibration` markdown sections.
2. **MCP**: `ai_advisor_invoke` (in `mcptools_ai.cpp`) builds a JSON envelope per call via `ShotSummarizer::buildUserPromptObject` + `AIManager::enrichUserPromptObject`, which includes a `recentAdvice` JSON array built by `DialingBlocks::buildRecentAdviceBlock`, fed by `AIConversation::loadRecentAssistantTurnsForKey()` (a static helper that reads QSettings directly — designed for exactly this kind of caller that has no live `AIConversation` instance).

`advisor-conversation-history` and `advisor-user-prompt` already specify that (a) `AIManager` SHALL call `setShotIdForCurrentTurn` before any assistant turn is appended, and (b) the in-app path SHALL produce a `recentAdvice`-equivalent block byte-parity with the MCP path. Neither is implemented: `setShotIdForCurrentTurn` has zero callers in `src/`/`qml/`, and `emitRecentShotContext` has no `recentAdvice` section. A blind-judged A/B test (see proposal) confirmed that fixing this data gap — with no system prompt changes — makes the model correctly call out ignored/contradicted prior advice, at zero added token cost (the system prompt's `recentAdvice` teaching section is already unconditionally present and currently dead weight for the in-app path).

## Goals / Non-Goals

**Goals:**
- In-app conversation turns carry `shotId`, matching what MCP-driven turns already do.
- The in-app advisor's `historicalContext` includes the same advice/outcome tracking data the MCP path already computes, reusing the existing `DialingBlocks::buildRecentAdviceBlock` + `AIConversation::loadRecentAssistantTurnsForKey` machinery rather than inventing a parallel implementation.
- Strengthen the taste-feedback-before-success-language rule in the shared system prompt, using the wording validated by the A/B test.

**Non-Goals:**
- No changes to `multiShotSystemPrompt()`'s existing text, and no new rules for escalation, trend-length, completion-criteria, or bean-change handling — the A/B test did not produce evidence these help, and adding unvalidated rules to an already-large (~10.2K token) system prompt is exactly the risk this investigation was meant to avoid.
- No changes to the MCP `ai_advisor_invoke` path — it already implements both requirements correctly and is the reference implementation this change makes the in-app path match.
- No new MCP tools, DB schema changes, or settings.

## Decisions

### Reuse `loadRecentAssistantTurnsForKey` + `buildRecentAdviceBlock` verbatim, on the same background thread `requestRecentShotContext` already uses

`requestRecentShotContext` already spawns a `QThread` that does DB work via `withTempDb`, then hops back to the main thread via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` to render and emit. The `recentAdvice` build slots into the same background-thread block:

```cpp
// Inside the existing withTempDb(dbPath, "ai_grinder_ctx", ...) lambda, or a
// sibling withTempDb call — both already run on the background QThread:
const QString convKey = AIManager::conversationKey(beanBrand, beanType, profileName);
const auto turns = AIConversation::loadRecentAssistantTurnsForKey(convKey, 3);
QJsonArray recentAdvice;
if (!turns.isEmpty()) {
    DialingBlocks::RecentAdviceInputs in;
    in.turns = turns;
    in.currentProfileKbId = /* resolved profile_kb_id for excludeShotId's profile */;
    in.currentShotId = excludeShotId;
    recentAdvice = DialingBlocks::buildRecentAdviceBlock(db, in);
}
```

This is the exact pattern `ai_advisor_invoke` already uses (see `mcptools_ai.cpp`) — `loadRecentAssistantTurnsForKey` is explicitly documented as a static, DB-free, thread-safe helper "for surfaces that want the recent-turn view but don't have an instantiated AIConversation," which is precisely this background thread's situation. No new thread-safety pattern is introduced.

**Alternative considered**: pass the live `AIConversation*` into the background thread and call its instance method `recentAssistantTurns()`. Rejected — `AIConversation` is a `QObject` owned by the main thread; touching it from a background thread would violate Qt's thread-affinity rules. The static QSettings-based helper exists specifically to avoid this.

`profile_kb_id` for the current shot isn't already loaded in `requestRecentShotContext`'s background thread — it needs a small addition to the existing grinder-context query (which already does `SELECT ... FROM shots s WHERE s.id = ?` for `excludeShotId`) to also select `s.profile_kb_id`.

### Render `recentAdvice` to a `## Recent Advice Tracking` markdown section, not raw JSON, in `emitRecentShotContext`

The in-app `historicalContext` is prose (consistent with `## Previous Shots`, `## Grinder Context`, `## Grinder Calibration` — all hand-rendered markdown, not JSON blobs), unlike the MCP envelope which is a JSON object. Render each `recentAdvice` entry as a short markdown block: turns-ago label, the recommendation sentence, the recommended `structuredNext` fields, and the `userResponse` outcome (adherence, actual grind/dose, rating if present, in-predicted-range flags). The system prompt's existing `recentAdvice` teaching section already describes the semantics (`adherence` values, omitted-rating fallback) in prose terms that apply equally whether the model receives JSON or an equivalent markdown rendering — no system prompt change is needed for this piece.

**Alternative considered**: embed the raw `recentAdvice` JSON verbatim inside the markdown context (fenced code block). Rejected for the in-app path — every other section of `historicalContext` is prose, and mixing in a raw JSON block would be inconsistent and likely harder for the model to weight against surrounding markdown; the MCP JSON envelope already has parity via the app-side data (`buildRecentAdviceBlock`), the requirement is behavioral parity, not byte-identical serialization across the two different prompt shapes (only *within* the JSON path is byte-equality specified — see the `advisor-user-prompt` spec's existing byte-stability requirement, which is scoped to the JSON envelope callers, both of which remain JSON).

### Call `setShotIdForCurrentTurn` from `sendFollowUp()`, guarded on `overlay.shotId > 0`

Single call site (`ConversationOverlay.qml`'s `conversationInput.sendFollowUp()`, used by all three entry points: desktop inline field, send button, mobile fullscreen dialog). Add `conversation.setShotIdForCurrentTurn(overlay.shotId)` immediately before the existing `conversation.ask(...)` / `conversation.followUp(...)` calls, only when `overlay.shotId > 0` (a free-form follow-up with no associated shot — e.g. a general question after the shot data was already sent — should not stamp a stale/wrong id).

### Strengthen the taste-feedback-gating rule using the A/B-validated wording

Add the new paragraph to wherever the shared espresso system prompt's tasting-feedback guidance lives (`ShotSummarizer::shotAnalysisSystemPrompt` or equivalent), using wording close to the "Candidate B" variant validated in the A/B test (the narrower first draft did not reliably override existing "acknowledge success" guidance elsewhere in the prompt).

## Risks / Trade-offs

- **[Risk]** The background-thread `recentAdvice` build adds one more DB round-trip to `requestRecentShotContext`, on the critical path for opening the conversation overlay. → **Mitigation**: `loadRecentAssistantTurnsForKey` reads QSettings (no DB), and `buildRecentAdviceBlock`'s DB queries are narrowly scoped (single shot lookups by id), matching the cost profile of the grinder-context query already running in the same thread.
- **[Risk]** `overlay.shotId` being 0 for some pre-existing call sites (e.g. a free-form question with no shot context) means those turns still won't carry `shotId`, so they won't contribute to future `recentAdvice`. → **Mitigation**: this matches the existing spec's own definition of "a free-form 'general question' follow-up that does not target a specific shot" persisting without `shotId` — expected, not a regression.
- **[Risk]** Strengthening the taste-feedback rule could cause the advisor to ask for a score more often than users want. → **Mitigation**: the A/B test's two control scenarios (a conversation with a score already given; a conversation with a genuine multi-shot trend to report) showed no regression — the rule only engages when the last 2+ shots are both missing feedback.
- **[Risk, now checked]** The strengthened taste-feedback wording was validated (Candidate B beat the narrower draft) only via Claude subagent replay, not against a real provider. Separately, real cross-provider calls (Claude Sonnet 5, GPT-5.4-mini, full GPT-5.4) confirmed the *bug this change fixes* — the ignored-advice case — fails identically on the unmodified baseline prompt across all three, which is the load-bearing validation for the `recentAdvice` plumbing fix. Task 5.5 closed the remaining gap: a live call against GPT-5.4-mini (the weakest performer at baseline — it never asked for feedback at all) with the shipped wording now explicitly asks for taste feedback before recommending a change, a real improvement. It is not a clean pass on the strictest reading — mini still uses some conclusory framing ("right neighborhood") rather than fully "preliminary" — but it never uses the explicitly-banned success words. Judged good enough to ship; not iterating the wording further per the non-goals below.
- **[Known limitation, out of scope]** The same cross-provider pass found GPT-5.4-mini fails to detect a genuine multi-shot pressure trend that Claude and full GPT-5.4 both catch, using the existing unmodified `multiShotSystemPrompt()` instruction. This is a model-capability gap tied to the "mini" tier specifically, not something a prompt or plumbing change in this codebase can fix — noted here so it isn't mistaken for a regression introduced by this change, and isn't accidentally treated as in-scope.

## Migration Plan

No data migration. Existing saved conversations have no `shotId` on their older turns and will simply not contribute to `recentAdvice` until new turns are made — this is the existing "legacy conversation" tolerance already specified (`advisor-conversation-history`'s "Older conversation loads without shotId" scenario). No feature flag; ships as a bug fix.

## Open Questions

- Exact wording/placement of the strengthened taste-feedback rule within the existing system prompt structure — finalize during implementation by adapting the A/B-tested Candidate B paragraph to fit the surrounding "Response Guidelines" / structured-fields sections without duplicating the existing single-shot `tastingFeedback` rule.
- Resolved: task 5.5's live GPT-5.4-mini check found the shipped wording works well enough to ship (asks for feedback, avoids the banned words) but doesn't fully eliminate conclusory curve-language on that specific model. Not pursued further — see the risk above.
