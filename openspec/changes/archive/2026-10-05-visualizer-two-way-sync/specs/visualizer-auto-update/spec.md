## ADDED Requirements

### Requirement: Automatic update sends only the fields edited locally

The application SHALL record, per shot, which Visualizer fields an edit changed (a field re-saved with the same value, or an empty value over an unset one, is not a change). An automatic update SHALL send only those fields, and a successful send SHALL clear only the fields it carried and only if no edit landed while it was in flight. The Upload button SHALL send every field. A field that is sent and was cleared locally SHALL be sent as JSON null. A shot with no unsent edit SHALL NOT be PATCHed by an automatic update.

#### Scenario: Unrelated Visualizer edit survives
- **GIVEN** the user set a shot's grind in Visualizer's Journal
- **WHEN** the user changes only the notes in Decenza and the automatic update is sent
- **THEN** the PATCH carries only the notes, and the grind on Visualizer is unchanged

#### Scenario: Edit during a send
- **WHEN** the user edits the rating while an update of that shot is in flight
- **THEN** the rating is still marked unsent after the update succeeds, and goes out with the next update
