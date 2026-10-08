# advisor-retry-on-failure Specification

## Purpose
Governs recovery from a failed AI Advisor request: the failed user turn is preserved rather than discarded, `AIConversation::retry()` re-sends it without duplicating history or re-running per-turn hooks, a new message after a failure replaces the stale pending turn to keep role alternation valid, and `ConversationOverlay.qml` surfaces a Retry control alongside the error.

## Requirements
### Requirement: A failed advisor request SHALL preserve the user's pending turn

When an in-flight advisor request fails (any error reaching
`AIConversation::onAnalysisFailed`), the conversation SHALL retain the user
message being sent, keep it as the last entry in `m_messages`, expose the error
text via `errorMessage`, and clear `busy`. A failed request SHALL NOT be
persisted; only successful turns are saved, so a preserved failed turn never
reappears on reload.

#### Scenario: Timeout keeps the user's message

- **GIVEN** an advisor conversation with a prior user/assistant pair
- **AND** the user sends a follow-up "Why is this sour?"
- **WHEN** the request fails with a timeout error
- **THEN** "Why is this sour?" SHALL remain as the last user turn in the conversation
- **AND** `errorMessage` SHALL be the failure text
- **AND** `busy` SHALL be `false`

#### Scenario: First-question failure keeps the message

- **GIVEN** an empty advisor conversation
- **WHEN** the user's first `ask(...)` request fails
- **THEN** the user's message SHALL remain in the conversation
- **AND** the conversation SHALL report `hasHistory == true`

### Requirement: `AIConversation` SHALL expose an explicit `retry()` that re-sends the pending turn

`AIConversation` SHALL provide `Q_INVOKABLE void retry()`, which re-dispatches
the pending turn. It SHALL be valid only when the conversation is not busy and
the last entry in `m_messages` is a user turn. When valid, it SHALL clear
`errorMessage`, set `busy`, and re-send the existing messages and system prompt
without modifying history or requiring the user to retype. It SHALL re-use the
stored message and SHALL NOT append a new user turn.

#### Scenario: Retry re-sends the same message

- **GIVEN** a conversation whose last turn failed and is preserved as the pending user turn
- **WHEN** `retry()` is called
- **THEN** the same message SHALL be re-sent to the AI provider
- **AND** `errorMessage` SHALL be cleared and `busy` SHALL become `true`
- **AND** no additional user turn SHALL be appended to the history

#### Scenario: Retry succeeds on the second attempt

- **GIVEN** a preserved failed user turn
- **WHEN** `retry()` is called and the request succeeds
- **THEN** the assistant response SHALL be appended after the preserved user turn
- **AND** the conversation SHALL be saved to storage

#### Scenario: Retry is a no-op when not applicable

- **GIVEN** a conversation that is busy, OR whose last turn is an assistant turn (no pending failure)
- **WHEN** `retry()` is called
- **THEN** no request SHALL be sent and the history SHALL be unchanged

### Requirement: Retry does not re-run per-turn hooks

`retry()` SHALL NOT re-run the per-follow-up rating and metadata-capture hooks,
which already ran when the turn was first submitted.

#### Scenario: Retry skips the submit hooks

- **WHEN** `retry()` re-sends a preserved pending turn
- **THEN** the rating and metadata-capture hooks are not invoked again

### Requirement: Timeouts never retry automatically

The conversation SHALL NOT retry automatically on timeout; retry is user-
initiated only. The network-layer auto-retry for transient HTTP 429, 502, 503
and 504 responses is unchanged and independent.

#### Scenario: A timeout does not re-send by itself

- **WHEN** an advisor request fails with a timeout
- **THEN** no request is re-sent until the user activates Retry

### Requirement: Sending a new message after a failure SHALL preserve role alternation

If, after a failure, the user submits a *new* message instead of retrying — via any append-then-send entry point (`followUp()` or `addShotContext()`) — the conversation SHALL drop the stale unanswered user turn before appending the new user message, so the provider never receives two consecutive user-role messages. This cleanup SHALL apply only when the last entry is a user turn; a normal follow-up after a successful turn (last entry is an assistant turn) SHALL NOT remove anything.

#### Scenario: New message replaces the failed pending turn

- **GIVEN** a conversation whose last turn is a preserved failed user message "Why is this sour?"
- **WHEN** the user submits a new follow-up "Actually, why is it bitter?"
- **THEN** the stale "Why is this sour?" user turn SHALL be removed
- **AND** "Actually, why is it bitter?" SHALL be the only trailing user turn sent to the provider

#### Scenario: Normal follow-up is unaffected

- **GIVEN** a conversation whose last turn is an assistant response
- **WHEN** the user submits a follow-up
- **THEN** no prior turn SHALL be removed
- **AND** the new user message SHALL be appended after the assistant turn

### Requirement: The advisor error UI SHALL offer a Retry action

`ConversationOverlay.qml` SHALL present a Retry control together with the error message when a request has failed and a pending user turn exists. Activating it SHALL call `conversation.retry()`. The control SHALL be internationalized via the project's translation mechanism and SHALL carry the accessibility attributes required of interactive elements (`Accessible.role`, `Accessible.name`, `Accessible.focusable`, `Accessible.onPressAction`).

#### Scenario: Retry button appears with the error and re-sends on tap

- **GIVEN** the advisor overlay is showing a timeout error with a preserved pending turn
- **THEN** a Retry control SHALL be visible alongside the error text
- **WHEN** the user activates Retry
- **THEN** `conversation.retry()` SHALL be invoked and the request SHALL be re-sent

#### Scenario: Retry control hidden while busy and when no failure is pending

- **GIVEN** the conversation is busy (request in flight) OR there is no error/pending failed turn
- **THEN** the Retry control SHALL NOT be shown

