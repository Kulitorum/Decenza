# visualizer-auto-update Specification

## Purpose
TBD - created by archiving change auto-update-visualizer-shots. Update Purpose after archive.
## Requirements
### Requirement: Auto-Update setting controls automatic PATCH of edited shots to visualizer.coffee

The application SHALL expose a `visualizerAutoUpdate` boolean setting (QSettings key `"visualizer/autoUpdate"`, default `true`) on `SettingsVisualizer`. When this setting is `true` and the shot being edited has a non-empty `visualizer_id`, the application SHALL automatically PATCH the shot on visualizer.coffee without requiring a manual user action.

#### Scenario: Auto-Update defaults to enabled on first install

- **WHEN** the setting has never been written
- **THEN** `visualizerAutoUpdate` SHALL return `true`

#### Scenario: Auto-Update is independently persisted

- **WHEN** the user sets `visualizerAutoUpdate` to `false` and restarts the app
- **THEN** `visualizerAutoUpdate` SHALL still be `false`

### Requirement: PostShotReviewPage close triggers auto-update when changes were made

When the user closes `PostShotReviewPage` (navigates away or the StackView deactivates the page), and at least one metadata field was changed since the page opened, and `visualizerAutoUpdate` is `true`, the application SHALL:

- Call `VisualizerUploader::updateShotOnVisualizerWithOverrides()` if the shot has a non-empty `visualizer_id`.
- Call `VisualizerUploader::uploadShotFromHistoryWithOverrides()` if the shot has no `visualizer_id` AND `visualizerAutoUpload` is also `true` (first upload path).

The auto-triggered request SHALL NOT fire if no metadata fields were changed.

#### Scenario: Close after editing notes fires a PATCH

- **GIVEN** a shot with `visualizer_id == "abc"` is open in PostShotReviewPage
- **AND** `visualizerAutoUpdate` is `true`
- **WHEN** the user edits the notes field and then navigates away from the page
- **THEN** `VisualizerUploader::updateShotOnVisualizerWithOverrides("abc", ...)` SHALL be called exactly once

#### Scenario: Close without any changes does not fire a PATCH

- **GIVEN** a shot with `visualizer_id == "abc"` is open in PostShotReviewPage
- **AND** the user makes no edits
- **WHEN** the user navigates away from the page
- **THEN** no PATCH or upload request SHALL be made for this action

#### Scenario: Manual upload clears the pending-update flag

- **GIVEN** the user edits a field (setting `pendingVisualizerUpdate = true`)
- **AND** then taps the manual upload button (which fires its own PATCH and succeeds)
- **WHEN** the user navigates away from the page
- **THEN** a second PATCH SHALL NOT be made (the flag was cleared on manual upload success)

#### Scenario: Auto-Update off — close after edits does not fire a PATCH

- **GIVEN** `visualizerAutoUpdate` is `false`
- **AND** the user edits the rating and closes PostShotReviewPage
- **THEN** no automatic PATCH SHALL be made

#### Scenario: Shot has no visualizer_id and auto-upload is off — no request is fired

- **GIVEN** a shot with an empty `visualizer_id`
- **AND** `visualizerAutoUpload` is `false`
- **AND** `visualizerAutoUpdate` is `true`
- **WHEN** the user edits the notes and closes PostShotReviewPage
- **THEN** no upload or PATCH request SHALL be made

#### Scenario: Shot has no visualizer_id but auto-upload is on — first upload is triggered

- **GIVEN** a shot with an empty `visualizer_id`
- **AND** both `visualizerAutoUpload` and `visualizerAutoUpdate` are `true`
- **WHEN** the user edits a field and closes PostShotReviewPage
- **THEN** `VisualizerUploader::uploadShotFromHistoryWithOverrides()` SHALL be called

### Requirement: MCP shot metadata write triggers auto-update when the shot has a visualizer_id

When an MCP tool successfully writes one or more metadata fields (notes, rating, TDS, dose, beans, etc.) to a local shot row, and `visualizerAutoUpdate` is `true`, and the shot's `visualizer_id` is non-empty, the application SHALL automatically PATCH the shot on visualizer.coffee.

The MCP path SHALL NOT trigger a first upload for shots with no `visualizer_id`.

#### Scenario: MCP notes update on an uploaded shot fires a PATCH

- **GIVEN** local shot row N has `visualizer_id == "xyz"`
- **AND** `visualizerAutoUpdate` is `true`
- **WHEN** an MCP tool writes new notes to shot N
- **THEN** `VisualizerUploader::updateShotOnVisualizerWithOverrides("xyz", ...)` SHALL be called with the updated fields

#### Scenario: MCP edit on a non-uploaded shot does not fire any request

- **GIVEN** local shot row N has an empty `visualizer_id`
- **WHEN** an MCP tool writes a rating to shot N
- **THEN** no upload or PATCH request SHALL be made

#### Scenario: Auto-Update off — MCP edit does not fire a PATCH

- **GIVEN** `visualizerAutoUpdate` is `false`
- **AND** shot N has `visualizer_id == "xyz"`
- **WHEN** an MCP tool updates the shot's TDS
- **THEN** no PATCH request SHALL be made

