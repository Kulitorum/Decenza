## 1. Preserve the failed turn and add retry (C++)

- [x] 1.1 In `src/ai/aiconversation.cpp` `onAnalysisFailed()`, remove the `m_messages.removeLast()` block so the failed user turn is kept; leave `errorMessage`/`busy`/signal emission intact.
- [x] 1.2 Add `Q_INVOKABLE void retry()` (declare in `aiconversation.h`, implement in `.cpp`): early-return when `m_busy`, when `m_messages` is empty, or when the last turn's role is not `"user"`; otherwise clear `m_errorMessage`, emit the error-changed notify, and call `sendRequest()`. Do NOT call `followUp()` or re-run the rating/bean-correction hooks.
- [x] 1.3 In `followUp()`, before the rating-capture logic, drop a trailing orphan user turn: if the last entry's role is `"user"`, `removeLast()` it (preserves role alternation when the user types a new message instead of retrying).
- [x] 1.4 Add `Q_PROPERTY(bool canRetry READ canRetry NOTIFY ...)` (notify via a dedicated `canRetryChanged`) returning `!m_busy && !m_messages.isEmpty() && lastRole == "user"`; implement `bool canRetry() const`.

## 2. Retry affordance in the chat UI (QML)

- [x] 2.1 In `qml/components/ConversationOverlay.qml`, add a Retry button to the error `Rectangle` (~lines 402-420), beside the error text; bind `visible`/`enabled` to `conversation.canRetry`.
- [x] 2.2 Wire the button's activation to `MainController.aiManager.conversation.retry()`.
- [x] 2.3 Internationalize the label (new `conversation.retry` / `conversation.retry.accessible` keys) and add `Accessible.role`/`Accessible.name`/`Accessible.focusable`/`Accessible.onPressAction`.

## 3. Verify

- [ ] 3.1 Build via Qt Creator (no warnings) and confirm the advisor compiles/links.
- [ ] 3.2 Manually reproduce a failure (e.g. force a timeout / disconnect network) and confirm: the message stays, the Retry button appears, tapping it re-sends, and a successful retry appends the assistant reply.
- [ ] 3.3 Confirm typing a new message after a failure does not produce two consecutive user turns (check the request payload / no Anthropic role-alternation error).
- [ ] 3.4 Confirm a first-question (`ask`) failure is also retryable and that the next normal follow-up after a success is unaffected.
