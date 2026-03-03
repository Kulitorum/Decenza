## Visualizer Integration

### DYE (Describe Your Espresso) Metadata
- **Location**: `qml/pages/PostShotReviewPage.qml` and `qml/pages/BeanInfoPage.qml`
- **Settings**: `src/core/settings.h` - dye* properties (sticky between shots)
- **Feature toggle**: Settings → Visualizer → "Extended metadata"
- **Auto-show**: Settings → Visualizer → "Edit after shot"
- **Access**: Shot Info button on IdlePage (between Espresso and Steam)

Supported metadata fields:
- `bean_brand`, `bean_type`, `roast_date`, `roast_level`
- `grinder_model`, `grinder_setting`
- `drink_tds`, `drink_ey`, `espresso_enjoyment`
- `espresso_notes`, `barista`

### Profile Import (VisualizerImporter)
- **Location**: `src/network/visualizerimporter.cpp/.h`
- **QML Page**: `qml/pages/VisualizerBrowserPage.qml`
- **Browser**: Uses QtWebEngineQuick (full Chromium) - NOT QtWebView (native WebView looks bad on old Android)
- **API**: `GET https://visualizer.coffee/api/shots/{id}/profile.json`

### Import Flow
1. User browses visualizer.coffee in embedded WebEngineView
2. User navigates to a shot page and clicks "Import Profile"
3. JavaScript searches DOM for `profile.json` link
4. VisualizerImporter fetches profile JSON and converts to native format
5. If duplicate exists, shows overwrite/save-as-new dialog

### Key Implementation Notes
- `QtWebEngineQuick::initialize()` must be called before QApplication in main.cpp
- WebEngineView respects `visible` property (unlike native WebView which needs size=0 hack)
- WebEngineView properly tracks SPA navigation URL changes
- Duplicate handling: `saveOverwrite()`, `saveAsNew()`, `saveWithNewName(newTitle)`
- Keyboard handling for Android: FocusScope + keyboardOffset pattern for text input

### Visualizer Profile Format
- Visualizer and de1app use the same JSON format with string-encoded numbers (Tcl huddle serialization)
- The unified `jsonToDouble()` helper and `ProfileFrame::fromJson()` handle string-to-double conversion and nested-to-flat field mapping transparently
- The Visualizer uploader (`buildVisualizerProfileJson()`) string-encodes numbers to match de1app convention

### Profile Import Architecture (ProfileSaveHelper)

Both `ProfileImporter` (file-system import from DE1 tablet) and `VisualizerImporter` (network import from visualizer.coffee) delegate save/compare/deduplicate logic to `ProfileSaveHelper` (`src/profile/profilesavehelper.h/.cpp`). Key methods:

- **`compareProfiles()`** — 6 profile-level fields + all frame fields (temperature, sensor, pump, transition, pressure, flow, seconds, volume, exit conditions, weight exit, limiter, popup)
- **`checkProfileStatus()`** — Checks ProfileStorage, downloaded folder, and built-in profiles
- **`saveProfile()`** — Save with duplicate detection (downloaded + built-in). Returns: 1=saved, 0=duplicate (emits `duplicateFound`), -1=failed. Callers must emit `importSuccess`/`importFailed` themselves.
- **`saveOverwrite()`/`saveAsNew()`/`saveWithNewName()`** — Duplicate resolution (emit signals directly)
- **`titleToFilename()`** — Delegates to `MainController::titleToFilename()`
- **`downloadedProfilesPath()`** — Static helper: `{AppDataLocation}/profiles/downloaded/`

### Filename Generation: Decenza vs de1app

**de1app** (`profile.tcl` `filename_from_title`): Preserves case and Unicode, replaces spaces→`_`, `/`→`__`, removes shell-unsafe special chars, truncates to 60 chars. Example: `"Café Leche"` → `"Café_Leche"`.

**Decenza** (`MainController::titleToFilename`): Lowercases, replaces accented chars with ASCII equivalents (é→e, ñ→n, etc.), replaces all non-alphanumeric→`_`, collapses double underscores, strips leading/trailing underscores. Example: `"Café Leche"` → `"cafe_leche"`.

This is an intentional divergence for cross-platform filesystem compatibility. Both approaches are internally consistent. de1app's approach can produce filenames with Unicode characters that may cause issues on some platforms.

### Profile Import: Decenza vs de1app

| Aspect | de1app | Decenza |
|--------|--------|---------|
| Duplicate detection | Filename existence only | Filename + content comparison |
| Duplicate resolution | Append `_YYYYMMDD_HHMMSS` timestamp | User dialog: overwrite/save-as-new/rename |
| `saveAsNew` naming | N/A (uses timestamp) | Smart: author → step count → numbered suffix |
| Visualizer category | Auto-prefixes `"Visualizer/"` to title | No category prefix |
| Comparison fields | DYE viewer: 5 textual lines per step | 6 profile fields + all frame fields |
| Profile comparison for imports | None (file existence only) | Full frame-by-frame comparison |
