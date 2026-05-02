# advisor-conversation-history — Delta

## ADDED Requirements

### Requirement: `AIConversation` SHALL persist a per-turn `shotId`

`AIConversation` SHALL extend each turn entry in `m_messages` to optionally carry a `shotId` (qint64) recording which shot the advisor was asked about for that turn. The field SHALL be a soft-schema extension:

- New entries created by `AIConversation::addUserMessage` / `addAssistantMessage` SHALL carry `shotId` only when explicitly set via `setShotIdForCurrentTurn(qint64)` (or equivalent) — typically called from `AIManager::analyzeShotWithMetadata` once the resolved shot is known.
- A turn without `shotId` (e.g., a free-form "general question" follow-up that does not target a specific shot) SHALL persist without the key. Omission is the documented null state — there SHALL NOT be a placeholder `shotId: 0`.
- Loading older conversations (saved before this change) — entries without `shotId` — SHALL succeed without error, with `shotId` reading as absent / `0`.

A reader `qint64 shotIdForTurn(qsizetype index) const` SHALL return the stored value or `0` for a turn without a recorded shot.

A reader `QList<HistoricalAssistantTurn> recentAssistantTurns(qsizetype max) const` SHALL return up to `max` assistant turns (most-recent first), each carrying `(shotId, content, structuredNext)`. Turns without `structuredNext` or with `shotId == 0` SHALL be SKIPPED, not returned with empty fields.

#### Scenario: shotId round-trips across save / load

- **GIVEN** a fresh `AIConversation`
- **AND** `addUserMessage("Why is this bitter?")` followed by `setShotIdForCurrentTurn(8473)`
- **AND** `addAssistantMessage("...try grind 4.75...", structuredNext)`
- **WHEN** the conversation is saved and a new `AIConversation` loads from the same storage key
- **THEN** `shotIdForTurn(0)` SHALL return `8473` for the user turn
- **AND** `shotIdForTurn(1)` SHALL return `8473` for the assistant turn (linkage is per turn pair)

#### Scenario: Older conversation loads without shotId

- **GIVEN** a conversation persisted before this change (no `shotId` keys on any entry)
- **WHEN** the conversation is loaded
- **THEN** loading SHALL succeed
- **AND** `shotIdForTurn(i)` for every turn SHALL return `0`

#### Scenario: recentAssistantTurns skips entries without structuredNext or shotId

- **GIVEN** a conversation with three assistant turns: turn 0 has structuredNext + shotId=10, turn 1 has structuredNext + shotId=0 (legacy), turn 2 has no structuredNext + shotId=20
- **WHEN** `recentAssistantTurns(5)` runs
- **THEN** the returned list SHALL contain exactly one entry — turn 0
- **AND** SHALL NOT contain turn 1 (no shotId) or turn 2 (no structuredNext)

### Requirement: `setShotIdForCurrentTurn` SHALL bind the shot id to the current user/assistant turn pair

When `AIManager` resolves a shot and is about to ask the advisor about it, it SHALL call `setShotIdForCurrentTurn(shotId)` BEFORE the assistant response is appended. The implementation SHALL apply the id to the most recent user turn and to the assistant turn appended next (so a user/assistant pair share the same `shotId`).

If `setShotIdForCurrentTurn` is called after the assistant message has already been appended, it SHALL apply the id to that latest pair. Calling it twice for the same pair SHALL overwrite the prior id (last-write-wins).

#### Scenario: User and assistant of the same turn pair share shotId

- **GIVEN** a conversation that has one prior user-then-assistant pair already recorded
- **WHEN** the next user message is added, `setShotIdForCurrentTurn(99)` is called, and the next assistant message is appended
- **THEN** the new user turn and new assistant turn SHALL both carry `shotId == 99`
- **AND** the prior pair's `shotId` SHALL NOT change
