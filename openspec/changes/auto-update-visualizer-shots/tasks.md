## 1. Settings: Add visualizerAutoUpdate property

- [x] 1.1 Add `Q_PROPERTY bool visualizerAutoUpdate` with getter, setter, and `visualizerAutoUpdateChanged()` signal to `src/core/settings_visualizer.h`
- [x] 1.2 Implement getter/setter in `src/core/settings_visualizer.cpp` using QSettings key `"visualizer/autoUpdate"` with default `true`
- [x] 1.3 Register the new property in `src/core/settingsserializer.cpp` (serialize/deserialize alongside existing visualizer settings)
- [x] 1.4 Expose `visualizerAutoUpdate` via `src/mcp/mcptools_settings.cpp` (read + write, same pattern as `visualizerAutoUpload`)

## 2. Settings UI: Auto-Update Shots toggle

- [x] 2.1 Add the **Auto-Update Shots** `ToggleCard` to `qml/pages/settings/SettingsVisualizerTab.qml` directly below the Auto-Upload toggle, binding to `Settings.visualizer.visualizerAutoUpdate`
- [x] 2.2 Set the toggle's `enabled` property to `Settings.visualizer.visualizerAutoUpload` so it is non-interactive when Auto-Upload is off
- [x] 2.3 Add translation keys `"settings.visualizer.autoUpdate"` (fallback: "Auto-Update Shots") and `"settings.visualizer.autoUpdateDesc"` (fallback: "Automatically sync shot edits back to Visualizer") to the English translation resource

## 3. PostShotReviewPage: Auto-update on close

- [x] 3.1 Add a `property bool pendingVisualizerUpdate: false` to `PostShotReviewPage.qml`
- [x] 3.2 Set `pendingVisualizerUpdate = true` at every point where a metadata edit is saved locally (anywhere `saveEditedShot()` or an individual field autosave is called)
- [x] 3.3 Clear `pendingVisualizerUpdate` in the `onUploadSuccess` / `onUpdateSuccess` handler so a successful manual upload prevents a duplicate auto-PATCH on close
- [x] 3.4 Add a `Page.onStatusChanged` (or `Component.onDestruction`) handler that, when `pendingVisualizerUpdate` is true and `Settings.visualizer.visualizerAutoUpdate` is true, calls:
  - `MainController.visualizer.updateShotOnVisualizerWithOverrides(editShotData.visualizerId, editShotData, overridesMap)` if `editShotData.visualizerId` is non-empty
  - `MainController.visualizer.uploadShotFromHistoryWithOverrides(editShotData, overridesMap)` if `editShotData.visualizerId` is empty AND `Settings.visualizer.visualizerAutoUpload` is true
- [x] 3.5 Build the `overridesMap` from the current edited field values the same way the existing manual upload button does

## 4. MCP: Auto-update after shot metadata write

- [x] 4.1 In the MCP shot-update tool handler (in `src/mcp/`), after a successful DB write, read back the shot's `visualizer_id` from the result
- [x] 4.2 If `visualizer_id` is non-empty and `m_settings->visualizer()->visualizerAutoUpdate()` is true, call `m_visualizer->updateShotOnVisualizerWithOverrides(visualizerId, shotProjection, overrides)` with the fields that were written
- [x] 4.3 Ensure the MCP tool response includes a `visualizerUpdateTriggered` boolean field indicating whether a PATCH was fired

## 5. Review and PR

- [ ] 5.1 Verify the toggle is disabled when Auto-Upload is off and interactive when it is on
- [ ] 5.2 Manually verify that closing PostShotReviewPage after an edit triggers a PATCH (check network log or visualizer.coffee)
- [ ] 5.3 Manually verify that closing without edits does not trigger a PATCH
- [ ] 5.4 Verify MCP shot edit on a shot with a `visualizer_id` fires a PATCH
- [ ] 5.5 Open PR, run `/pr-review-toolkit:review-pr`, merge
