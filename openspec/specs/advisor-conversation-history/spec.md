# advisor-conversation-history Specification

## Purpose
Defines how `AIConversation` binds each user/assistant turn pair to the shot it discussed via an optional per-turn `shotId`, and exposes readers (`shotIdForTurn`, `recentAssistantTurns`) that let later code find prior advisor turns tied to a specific shot. This is the linkage that closes the loop between an AI Advisor recommendation and the shot it was about.

## Requirements

### Requirement: `AIConversation` SHALL persist a per-turn `shotId`
`AIConversation` SHALL extend each turn entry in `m_messages` with an optional `shotId` (qint64) naming the shot the advisor was asked about. A turn SHALL carry `shotId` only when set through `setShotIdForCurrentTurn(qint64)`, and a turn without one SHALL omit the key rather than store `shotId: 0`. Conversations saved before this change SHALL load without error, reading as absent.

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

### Requirement: Shot-id readers
`qint64 shotIdForTurn(qsizetype index) const` SHALL return the stored value or `0` for a turn without a recorded shot. `QList<HistoricalAssistantTurn> recentAssistantTurns(qsizetype max) const` SHALL return up to `max` assistant turns, most recent first, each as `(shotId, content, structuredNext)`, and SHALL skip turns without `structuredNext` or with `shotId == 0`.

#### Scenario: Reader reports zero for an untargeted turn

- **WHEN** `shotIdForTurn` is called for a turn that has no `shotId`
- **THEN** it returns `0`

### Requirement: `setShotIdForCurrentTurn` SHALL bind the shot id to the current user/assistant turn pair
When `AIManager` resolves a shot, it SHALL call `setShotIdForCurrentTurn(shotId)` before the assistant response is appended, so the most recent user turn and the assistant turn appended next share the same `shotId`. If it is called after the assistant message is appended, it SHALL apply the id to that latest pair. A second call on the same pair SHALL overwrite the id (last write wins).

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

### Requirement: Every shot-driven surface stamps shotId
This binding SHALL apply to every surface that drives turns for a resolved shot, not only MCP `ai_advisor_invoke`. The in-app `ConversationOverlay.qml` `sendFollowUp()` on both the desktop inline input and the mobile fullscreen input SHALL stamp `overlay.shotId` before calling `ask()` or `followUp()`.

#### Scenario: MCP invoke stamps the shot

- **WHEN** `ai_advisor_invoke` resolves a shot and asks the advisor about it
- **THEN** the user and assistant turns of that pair share the resolved `shotId`

### Requirement: A turn's `shotId` SHALL survive an import that renumbers shots
Importing conversations alongside a shot history SHALL rewrite each turn's `shotId` to the destination id of the same shot, so the binding still names the shot the turn discussed. A turn whose source shot is absent after the import SHALL have its `shotId` removed, never keeping the source id. This SHALL apply to backup restore, device-to-device migration and the backup endpoint.

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

### Requirement: Advisor conversation threads SHALL be identified by equipment package
An advisor conversation thread SHALL be identified by the equipment package a shot was pulled on, in addition to the bean and profile. Shots on the same bean and profile with different equipment packages SHALL open different threads, and a shot returning to a package that has a thread SHALL resume it. Threads created under this requirement SHALL be retained unchanged and age out under the usual limit.

#### Scenario: First advisor use after upgrading starts a fresh thread

- **GIVEN** a user with saved advisor conversations from before this change
- **WHEN** they open the advisor on a shot after upgrading
- **THEN** a new conversation thread SHALL start with no prior turns
- **AND** the pre-upgrade thread's turns SHALL NOT be sent to the model

#### Scenario: Switching basket opens a separate thread

- **GIVEN** an ongoing advisor conversation about a bean and profile on equipment package A
- **WHEN** the user pulls a shot on package B with the same bean and profile, and opens the
  advisor
- **THEN** a separate thread SHALL be used for the package B shot
- **AND** the package A conversation SHALL NOT contribute turns to it

#### Scenario: Returning to the earlier equipment resumes its thread

- **GIVEN** threads exist for both package A and package B on the same bean and profile
- **AND** neither has aged out under the retention limit
- **WHEN** the user pulls another shot on package A and opens the advisor
- **THEN** the package A thread SHALL resume with its existing turns

### Requirement: Pre-upgrade threads are cleared once
Threads saved before this requirement SHALL be cleared once, at the upgrade. They cannot be resumed, since their key carries no package, and cannot be read, since their prose-format turns have no reader.

#### Scenario: Old thread is removed at upgrade

- **WHEN** the app upgrades while a thread saved before this requirement exists
- **THEN** that thread is cleared and the next advisor use starts a fresh thread

### Requirement: A thread keeps one equipment set for its life
A saved conversation is replayed to the model on every request, so its equipment scope SHALL hold for its whole life. Scoping only the payload would leave older turns describing other equipment inside the transcript.

#### Scenario: Older turns from other equipment stay out of the transcript

- **WHEN** a shot pulled on a different equipment package is discussed in a thread
- **THEN** that shot is answered in its own thread, not appended to the existing transcript
