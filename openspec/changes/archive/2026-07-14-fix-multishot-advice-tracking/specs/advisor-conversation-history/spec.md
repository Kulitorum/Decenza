## MODIFIED Requirements

### Requirement: `setShotIdForCurrentTurn` SHALL bind the shot id to the current user/assistant turn pair

When `AIManager` resolves a shot and is about to ask the advisor about it, it SHALL call `setShotIdForCurrentTurn(shotId)` BEFORE the assistant response is appended. The implementation SHALL apply the id to the most recent user turn and to the assistant turn appended next (so a user/assistant pair share the same `shotId`).

If `setShotIdForCurrentTurn` is called after the assistant message has already been appended, it SHALL apply the id to that latest pair. Calling it twice for the same pair SHALL overwrite the prior id (last-write-wins).

This requirement applies to EVERY surface that drives `AIConversation` turns for a resolved shot, not only the MCP `ai_advisor_invoke` path — including the in-app conversation overlay (`ConversationOverlay.qml`'s `sendFollowUp()` on both the desktop inline input and the mobile fullscreen input dialog), which resolves a `shotId` (`overlay.shotId`) before calling `ask()`/`followUp()`.

#### Scenario: User and assistant of the same turn pair share shotId

- **GIVEN** a conversation that has one prior user-then-assistant pair already recorded
- **WHEN** the next user message is added, `setShotIdForCurrentTurn(99)` is called, and the next assistant message is appended
- **THEN** the new user turn and new assistant turn SHALL both carry `shotId == 99`
- **AND** the prior pair's `shotId` SHALL NOT change

#### Scenario: In-app conversation overlay stamps shotId before sending

- **GIVEN** the user opens the conversation overlay for a specific shot (`overlay.shotId` is a valid, non-zero database id)
- **AND** the user types a follow-up message and sends it
- **WHEN** `sendFollowUp()` calls `conversation.setShotIdForCurrentTurn(overlay.shotId)` and then `conversation.ask(...)` or `conversation.followUp(...)`
- **THEN** the resulting user/assistant turn pair SHALL carry `shotId == overlay.shotId`
- **AND** a subsequent call to `recentAssistantTurns()` SHALL be able to find this turn (given it also carries `structuredNext`)
