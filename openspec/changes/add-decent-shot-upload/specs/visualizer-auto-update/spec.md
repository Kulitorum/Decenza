## MODIFIED Requirements

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

## REMOVED Requirements

### Requirement: PostShotReviewPage close triggers auto-update when changes were made

**Reason**: Replaced by shot-uploads "Review-page edits are sent once, on close", which covers both destinations. Two of its scenarios also described behaviour the app never had: closing the page did not upload a shot with no `visualizer_id`, and it called override functions that no longer exist.
**Migration**: None; the page's close sends its edits through `ShotUploads::releaseUpdates`.
