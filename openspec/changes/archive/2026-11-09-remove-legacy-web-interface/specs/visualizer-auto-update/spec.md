# Spec Delta

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
- **WHEN** shot N's metadata is changed from the shared app, MCP, the AI advisor or the change-beans dialog
- **THEN** Visualizer receives one PATCH for shot N, built from the saved row

