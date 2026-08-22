## ADDED Requirements

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
