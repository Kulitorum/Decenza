## Why

When an AI advisor request fails (e.g. the cloud API times out — Decenza shows "Request timed out. The AI service may be slow — please try again."), `AIConversation::onAnalysisFailed()` deletes the user's message from the conversation, and the QML input field was already cleared on send. The user's typed question is gone with no way to recover it — they must retype the whole thing. There is no retry affordance, so a single transient timeout forces a full re-type.

## What Changes

- On a failed advisor request, **keep** the user's failed turn in the conversation instead of discarding it. The message bubble stays visible and the error is shown beneath it.
- Add a `retry()` method on `AIConversation` that re-sends the already-stored pending turn (same message, same system prompt) without requiring the user to retype. It clears the prior error, sets busy, and dispatches the request again.
- Add a **Retry** button to the advisor error UI (`ConversationOverlay.qml`) shown alongside the timeout/error message, wired to `conversation.retry()`.
- Preserve the role-alternation invariant: if the user instead types a *new* message after a failure (routing through `followUp()`), the stale unanswered user turn is dropped before the new turn is added, so the provider never receives two consecutive user-role messages.
- No automatic retry on timeout (the network-layer auto-retry for 429/502/503/504 is unchanged); retry is an explicit user action.

## Capabilities

### New Capabilities
- `advisor-retry-on-failure`: When an AI advisor request fails, the conversation preserves the user's pending turn, exposes an explicit retry that re-sends it, and maintains the message-role-alternation invariant for subsequent sends.

### Modified Capabilities
<!-- None: advisor-conversation-history covers turn persistence/shotId binding and is not changing its requirements. Failure/retry handling is new behavior. -->

## Impact

- `src/ai/aiconversation.h` / `src/ai/aiconversation.cpp`: stop discarding the failed user turn in `onAnalysisFailed()`; add `Q_INVOKABLE void retry()`; add orphan-user-turn cleanup at the top of `followUp()`. May add a `canRetry`/`hasPendingFailedTurn` read property for the QML to bind the button's visibility/enabled state.
- `qml/components/ConversationOverlay.qml`: add a Retry button to the existing error `Rectangle` (lines ~402-420), internationalized via `TranslationManager`/`Tr`, with proper `Accessible.*` attributes.
- No DB schema, BLE, or settings changes. Conversations are only persisted on success, so no failed/orphan turn is ever written to storage — load behavior is unaffected.
- Optional: `qml/components/AIConversationPanel.qml` surfaces `errorMessage` too; a retry affordance there is out of scope unless trivial.
