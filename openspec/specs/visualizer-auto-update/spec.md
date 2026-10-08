# visualizer-auto-update Specification

## Purpose
Governs the `visualizerAutoUpdate` setting that automatically re-PATCHes an already-uploaded shot's metadata (notes, rating, TDS, etc.) to visualizer.coffee whenever it changes — either from `PostShotReviewPage` closing after an edit or an MCP metadata write — without requiring a manual re-upload action.

## Requirements

### Requirement: Auto-Update setting controls automatic PATCH of edited shots to visualizer.coffee

The application SHALL expose the shared "Auto-update shots" setting (`SettingsUpload::autoUpdate`, QSettings key `"visualizer/autoUpdate"`, default `true`; MCP name `updateAutomatically`). When it is `true`, Visualizer is switched on and connected, and an edited shot has a non-empty `visualizer_id`, the application SHALL PATCH the shot on visualizer.coffee without a manual action, through the shared upload path (shot-uploads). The same setting governs the Decent account.

#### Scenario: Auto-Update defaults to enabled on first install

- **WHEN** the setting has never been written
- **THEN** it SHALL return `true`

#### Scenario: Auto-Update is independently persisted

- **WHEN** the user switches Auto-update off and restarts the app
- **THEN** it SHALL still be off

#### Scenario: Edit from any editor

- **GIVEN** shot N has `visualizer_id == "xyz"` and Auto-update is on
- **WHEN** shot N's metadata is changed from ShotServer, MCP, the AI advisor or the change-beans dialog
- **THEN** Visualizer receives one PATCH for shot N, built from the saved row

### Requirement: MCP shot metadata write triggers auto-update when the shot has a visualizer_id

When `shots_update` successfully writes metadata to a shot, Auto-update is on, and the shot's `visualizer_id` is non-empty, the application SHALL PATCH the shot on visualizer.coffee through the shared upload path. The MCP path SHALL NOT trigger a first upload for shots with no `visualizer_id`. The response SHALL list, as `autoUpdateTo`, the destinations an edit is sent to.

#### Scenario: MCP notes update on an uploaded shot fires a PATCH

- **GIVEN** local shot row N has `visualizer_id == "xyz"` and Auto-update is on
- **WHEN** `shots_update` writes new notes to shot N
- **THEN** Visualizer receives one PATCH for shot N carrying the new notes

#### Scenario: MCP edit on a non-uploaded shot does not fire any request

- **GIVEN** local shot row N has an empty `visualizer_id`
- **WHEN** `shots_update` writes a rating to shot N
- **THEN** no Visualizer upload or PATCH request SHALL be made

#### Scenario: Auto-Update off — MCP edit does not fire a PATCH

- **GIVEN** Auto-update is off
- **AND** shot N has `visualizer_id == "xyz"`
- **WHEN** `shots_update` updates the shot's TDS
- **THEN** no PATCH request SHALL be made

### Requirement: Automatic update sends only the fields edited locally
The application SHALL record, per shot, which Visualizer fields an edit changed. A field re-saved with the same value, or an empty value over an unset one, is not a change. An automatic update SHALL send only those fields, while the Upload button SHALL send every field. A field cleared locally SHALL be sent as JSON null.

#### Scenario: Unrelated Visualizer edit survives
- **GIVEN** the user set a shot's grind in Visualizer's Journal
- **WHEN** the user changes only the notes in Decenza and the automatic update is sent
- **THEN** the PATCH carries only the notes, and the grind on Visualizer is unchanged

#### Scenario: Edit during a send
- **WHEN** the user edits the rating while an update of that shot is in flight
- **THEN** the rating is still marked unsent after the update succeeds, and goes out with the next update

### Requirement: A successful send clears only what it carried
A successful automatic send SHALL clear only the fields it carried, and only if no edit landed while it was in flight. A shot with no unsent edit SHALL NOT be PATCHed by an automatic update.

#### Scenario: Shot with no unsent edit is not patched

- **WHEN** an automatic update runs on a shot with no unsent edit
- **THEN** no PATCH is sent
