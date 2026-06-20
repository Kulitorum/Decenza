## Why

When a user edits shot metadata (notes, rating, TDS, etc.) in the post-shot review page or via the MCP API, those changes are saved locally but are not automatically reflected on visualizer.coffee. The user must manually tap the upload button to sync edits, which is easy to forget and creates drift between local and cloud state.

## What Changes

- Add a `visualizerAutoUpdate` boolean setting (default `true`) to `SettingsVisualizer`, stored as `"visualizer/autoUpdate"`.
- Add an **Auto-Update Shots** toggle to `SettingsVisualizerTab.qml`, positioned directly below the existing Auto-Upload toggle, enabled only when Auto-Upload is also on.
- When the toggle is on and the shot already has a `visualizer_id`:
  - Automatically PATCH the shot on visualizer.coffee when `PostShotReviewPage` is closed and local changes were made.
  - Automatically PATCH the shot when an MCP tool writes any metadata field (notes, rating, TDS, beans, etc.) to a shot that has a `visualizer_id`.
- If a shot has no `visualizer_id` but auto-upload is also on, the existing auto-upload path already handles first-upload at shot end; no additional first-upload is triggered from these two paths.

## Capabilities

### New Capabilities

- `visualizer-auto-update`: Automatic sync of edited shot metadata back to visualizer.coffee when the shot already has a `visualizer_id`, triggered on PostShotReviewPage close-with-changes and on MCP metadata writes.

### Modified Capabilities

- `settings-ui`: New toggle added to the Visualizer settings tab.

## Impact

- **`src/core/settings_visualizer.h/.cpp`** — new `visualizerAutoUpdate` property.
- **`src/core/settingsserializer.cpp`** — serialize/deserialize the new setting.
- **`src/mcp/mcptools_settings.cpp`** — expose new setting via MCP.
- **`qml/pages/settings/SettingsVisualizerTab.qml`** — new toggle UI.
- **`qml/pages/PostShotReviewPage.qml`** — call `updateShotOnVisualizerWithOverrides()` / `uploadShotFromHistoryWithOverrides()` on page close when `hasUnsavedChanges` was true and `visualizerAutoUpdate` is on.
- **`src/mcp/mcptools_shots.cpp`** (or equivalent shot-edit MCP tool) — after a successful metadata write, trigger a visualizer update if the shot has a `visualizer_id` and `visualizerAutoUpdate` is on.
- No new network endpoints; uses the existing `VisualizerUploader::updateShotOnVisualizerWithOverrides()` and `uploadShotFromHistoryWithOverrides()` methods.
- No BLE impact.
