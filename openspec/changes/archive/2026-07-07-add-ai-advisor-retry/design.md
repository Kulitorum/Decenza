## Context

The in-app AI advisor runs through `AIConversation` (`src/ai/aiconversation.{h,cpp}`), which dispatches requests via `AIManager::analyzeConversation()` → `AIProvider` (HTTP). The conversation is a `QJsonArray m_messages` of `{role, content, [shotId], [structuredNext]}` turns. The QML chat UI is `qml/components/ConversationOverlay.qml`.

Today's failure path (`aiconversation.cpp:416-433`):

```cpp
void AIConversation::onAnalysisFailed(const QString& error)
{
    if (!m_busy) return;
    m_busy = false;
    m_errorMessage = error;
    if (!m_messages.isEmpty())
        m_messages.removeLast();   // <-- discards the user's typed turn
    emit busyChanged();
    emit historyChanged();
    emit errorOccurred(error);
}
```

The QML send handler (`ConversationOverlay.qml:528-576`) clears the text field synchronously on a successful submit (`text = ""`). So once a request is in flight, the only copy of the user's message lives in `m_messages` — and `onAnalysisFailed` then deletes it. The user sees the error box (`ConversationOverlay.qml:402-420`) but the message is unrecoverable.

`AIProvider` already auto-retries transient HTTP statuses (429/502/503/504) with backoff, but **not** `QNetworkReply::TimeoutError` — timeouts flow straight to `onAnalysisFailed`. `hasHistory()` is `!m_messages.isEmpty()`, so keeping a failed turn flips `hasHistory` true and routes the next send through `followUp()` rather than `ask()`.

**Confirmed real-world failure** (DE1 tablet debug log, session 2026-06-21 11:53):

```
[322.486] AIConversation: Sending request with 5 messages
[323.608] WARN "Google Gemini" HTTP 503 - retry 1 in 1000 ms  ("...high demand... Please try again later.")
[384.157] WARN QIODevice::read (QNetworkReplyHttpImpl): device not open
[384.211] AIConversation: Request failed: "Request timed out. The AI service may be slow — please try again."
```

The trigger was a Gemini **HTTP 503** (model overloaded), not a raw network timeout. The provider's built-in auto-retry *did* fire (`retry 1`), but the model stayed overloaded and the request hit the 60s analysis timeout, surfacing the generic "Request timed out" message. The auto-retry could not save it — which is exactly why a **user-initiated** retry (wait, then try again, as the message advises) is the right fix. The failed turn was a follow-up (5 messages), so `onAnalysisFailed` → `removeLast()` deleted the typed question; the conversation's last persisted state was the prior 4-message success, so the lost turn was never saved.

## Goals / Non-Goals

**Goals:**
- Never silently lose a typed advisor message on a failed request.
- Let the user re-send the failed message with one tap, no retyping.
- Keep the provider message stream role-alternating (no two consecutive user turns), which Anthropic's API requires.

**Non-Goals:**
- Automatic/background retry on timeout (explicit user action only).
- Editing the failed message before retry (re-send verbatim; typing a new message is the "edit" path).
- Restoring the cleared text back into the input field (the message lives in history; Retry re-sends from there).
- A Retry affordance in `AIConversationPanel.qml` (out of scope unless trivial).

## Decisions

**1. Stop discarding the failed turn.** Remove the `m_messages.removeLast()` from `onAnalysisFailed()`. The failed user turn stays as the trailing entry; the message bubble remains visible with the error beneath it. Because conversations are saved only in `onAnalysisComplete()` (success), a failed turn is never persisted — reload behavior is unchanged.

**2. Add `Q_INVOKABLE void retry()`.** It re-dispatches the pending turn:

```cpp
void AIConversation::retry()
{
    if (m_busy) return;
    if (m_messages.isEmpty()) return;
    if (m_messages.last().toObject().value("role").toString() != "user") return; // nothing pending
    m_errorMessage.clear();
    emit errorOccurred(m_errorMessage); // notify so errorMessage binding refreshes
    sendRequest();                       // sets busy, trims, re-sends existing m_messages
}
```

`retry()` deliberately does **not** call `followUp()` — that would append a duplicate user turn and re-run the rating/bean-correction capture hooks (`followUp` lines 110-133), which already fired on the first submit. It reuses the stored `m_systemPrompt` and `m_messages` as-is.

**3. Drop the orphan user turn in `followUp()`.** At the top of `followUp()` (before the rating hooks read the *prior* assistant message), if the last entry is a user turn, the previous request failed and was preserved — remove it so the new message doesn't create two consecutive user turns:

```cpp
// A preserved failed turn (last entry is a user message with no assistant reply)
// is superseded by this new message — drop it to keep roles alternating.
if (!m_messages.isEmpty() &&
    m_messages.last().toObject().value("role").toString() == "user") {
    m_messages.removeLast();
}
```

This runs only when the last turn is unanswered; a normal follow-up (last entry = assistant) is untouched. The rating-capture loop already scans backwards for the most-recent *assistant* turn, so removing a trailing *user* turn first does not disturb it.

**4. Expose retry-availability to QML.** Add a read-only `Q_PROPERTY(bool canRetry ...)` (NOTIFY on the existing `historyChanged`/`busyChanged` signals) returning `!m_busy && !m_messages.isEmpty() && lastRole == "user"`. The error `Rectangle` in `ConversationOverlay.qml` already keys off `errorMessage.length > 0 && !busy`; the Retry button binds `visible: conversation.canRetry`. Wired to `conversation.retry()`, internationalized (reuse a `common.button.retry` key if present, else add one), with the standard `Accessible.*` attributes for an interactive control. Lay it out inside the existing error row beside the message text.

**5. Timeouts stay non-auto-retried.** No change to `AIProvider`'s retry policy. Manual retry is sufficient and keeps the user in control; auto-retrying a 60s timeout could double the wait.

## Risks / Trade-offs

- **Stale shotId on the preserved turn.** A failed turn keeps whatever `shotId` was bound to it; `retry()` re-sends the same turn, so the binding stays correct. No special handling needed.
- **`hasHistory` flips true after a first-question failure.** The next QML send then routes through `followUp()` instead of `ask()`. `followUp()` requires a non-empty `m_systemPrompt`, which `ask()` set before failing — so it works, and the orphan-drop (Decision 3) keeps the stream clean. Verified against `ConversationOverlay.qml:552`.
- **Double-send guard.** `retry()` and `sendRequest()` both early-return when `m_busy`; the Retry button is hidden while busy (`canRetry` is false), so rapid taps cannot stack requests.
- **Minimal surface.** Changes are confined to `AIConversation` plus one QML button; no DB, settings, BLE, or provider changes, keeping regression risk low.
