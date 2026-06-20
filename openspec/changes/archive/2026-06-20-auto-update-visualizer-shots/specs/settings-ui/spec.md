## ADDED Requirements

### Requirement: Auto-Update Shots toggle appears in Visualizer settings tab

The Visualizer settings tab SHALL display an **Auto-Update Shots** toggle card positioned immediately below the existing **Auto-Upload Shots** toggle. The toggle SHALL be disabled (non-interactive, visually dimmed) when **Auto-Upload Shots** is off, reflecting that auto-update depends on the upload feature being active.

The toggle SHALL bind to `Settings.visualizer.visualizerAutoUpdate` and use translation keys `"settings.visualizer.autoUpdate"` (label, fallback "Auto-Update Shots") and `"settings.visualizer.autoUpdateDesc"` (description, fallback "Automatically sync shot edits back to Visualizer").

#### Scenario: Auto-Update toggle appears below Auto-Upload

- **WHEN** the user navigates to Settings → Visualizer
- **THEN** an "Auto-Update Shots" toggle card SHALL be visible directly below "Auto-Upload Shots"

#### Scenario: Auto-Update toggle is disabled when Auto-Upload is off

- **GIVEN** the Auto-Upload Shots toggle is off
- **WHEN** the user views the Visualizer settings tab
- **THEN** the Auto-Update Shots toggle SHALL appear disabled and SHALL NOT be interactive

#### Scenario: Auto-Update toggle is enabled when Auto-Upload is on

- **GIVEN** the Auto-Upload Shots toggle is on
- **WHEN** the user views the Visualizer settings tab
- **THEN** the Auto-Update Shots toggle SHALL be interactive and reflect the current `visualizerAutoUpdate` value
