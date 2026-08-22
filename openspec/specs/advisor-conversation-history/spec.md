# advisor-conversation-history Specification

## Purpose
Defines how `AIConversation` binds each user/assistant turn pair to the shot it discussed via an optional per-turn `shotId`, and exposes readers (`shotIdForTurn`, `recentAssistantTurns`) that let later code find prior advisor turns tied to a specific shot. This is the linkage that closes the loop between an AI Advisor recommendation and the shot it was about.

## Requirements

### Requirement: `AIConversation` SHALL persist a per-turn `shotId`

`AIConversation` SHALL extend each turn entry in `m_messages` to optionally carry a `shotId` (qint64) recording which shot the advisor was asked about for that turn. The field SHALL be a soft-schema extension:

- New entries created by `AIConversation::addUserMessage` / `addAssistantMessage` SHALL carry `shotId` only when explicitly set via `setShotIdForCurrentTurn(qint64)` (or equivalent) — set once the resolved shot for the turn is known.
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

### Requirement: A turn's `shotId` SHALL survive an import that renumbers shots

`shotId` binds a conversation turn to the shot it discussed. Importing a conversation alongside a shot history SHALL rewrite each turn's `shotId` to the destination id of the same shot, so the binding continues to name the shot the turn is actually about. Where the import preserved the shot's id the rewrite is an identity and the turn is unchanged; the guarantee is about the binding remaining correct, not about the id changing.

A turn whose source shot is not present in the destination after the import SHALL have its `shotId` removed, leaving the turn in the documented null state — the same state as a free-form turn that never targeted a shot. It SHALL NOT retain the source id.

This applies wherever conversations are imported: backup restore, device-to-device migration, and the backup endpoint.

#### Scenario: Imported turns point at the destination's shots

- **GIVEN** a conversation whose turns carry shot ids from a source database
- **AND** those shots are imported and assigned different ids in the destination
- **WHEN** the conversation is imported by the same operation
- **THEN** each turn's `shotId` SHALL be the destination id of the shot it discussed
- **AND** `shotIdForTurn` SHALL resolve to a shot that exists

#### Scenario: A turn whose shot did not come across reads as absent

- **GIVEN** a conversation turn carrying a source shot id
- **AND** that shot is not present in the destination after the import
- **WHEN** the conversation is imported
- **THEN** the turn SHALL persist without a `shotId` key
- **AND** `shotIdForTurn` for that turn SHALL return `0`
- **AND** `recentAssistantTurns` SHALL skip it, as it skips any turn without a shot id

#### Scenario: An unresolvable stored id is not acted upon

- **GIVEN** a conversation carrying a `shotId` that matches no shot in the database
- **WHEN** a later turn would write back to the shot that id names
- **THEN** the write SHALL NOT be attempted against that id
- **AND** the condition SHALL be reported rather than absorbed

#### Scenario: Conversations already imported before this change are repaired on load

- **GIVEN** a conversation imported by an earlier version, whose turns hold shot ids that do not resolve
- **WHEN** the conversation is loaded
- **THEN** loading SHALL succeed
- **AND** each unresolvable `shotId` SHALL read as absent
- **AND** no write SHALL be addressed to an unresolvable id, including after the database's assigned ids grow past it
