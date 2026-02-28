<!-- OPENSPEC:START -->
# OpenSpec Instructions

These instructions are for AI assistants working in this project.

Always open `@/openspec/AGENTS.md` when the request:
- Mentions planning or proposals (words like proposal, spec, change, plan)
- Introduces new capabilities, breaking changes, architecture shifts, or big performance/security work
- Sounds ambiguous and you need the authoritative spec before coding

Use `@/openspec/AGENTS.md` to learn:
- How to create and apply change proposals
- Spec format and conventions
- Project structure and guidelines

Keep this managed block so 'openspec update' can refresh the instructions.

<!-- OPENSPEC:END -->

# Decenza DE1

Qt/C++ cross-platform controller for the Decent Espresso DE1 machine with BLE connectivity.

## Development Environment

- **ADB path**: `/c/Users/Micro/AppData/Local/Android/Sdk/platform-tools/adb.exe`
- **Uninstall app**: `adb uninstall io.github.kulitorum.decenza_de1`
- **WiFi debugging**: `192.168.1.208:5555` (reconnect: `adb connect 192.168.1.208:5555`)
- **Qt version**: 6.10.2
- **Qt path**: `C:/Qt/6.10.2/msvc2022_64`
- **C++ standard**: C++17
- **de1app source**: `C:\code\de1app` (original Tcl/Tk DE1 app for reference)
- **IMPORTANT**: Use relative paths (e.g., `src/main.cpp`) instead of absolute paths (e.g., `C:\CODE\de1-qt\src\main.cpp`) to avoid "Error: UNKNOWN: unknown error, open" when editing files

## Command Line Build (for Claude sessions)

**IMPORTANT**: Don't build automatically - let the user build in Qt Creator, which is ~50x faster than command-line builds. Only use these commands if the user explicitly asks for a CLI build.

MSVC environment variables (INCLUDE, LIB) are set permanently. Use Visual Studio generator (Ninja not in PATH).

**Configure Release:**
```bash
rm -rf build/Release && mkdir -p build/Release && cd build/Release && cmake ../.. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.10.2/msvc2022_64"
```

**Build Release (parallel):**
```bash
cd build/Release && unset CMAKE_BUILD_PARALLEL_LEVEL && MSYS_NO_PATHCONV=1 cmake --build . --config Release -- /m
```

**Configure Debug:**
```bash
rm -rf build/Debug && mkdir -p build/Debug && cd build/Debug && cmake ../.. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.10.2/msvc2022_64" -DCMAKE_BUILD_TYPE=Debug
```

**Build Debug (parallel):**
```bash
cd build/Debug && unset CMAKE_BUILD_PARALLEL_LEVEL && MSYS_NO_PATHCONV=1 cmake --build . --config Debug -- /m
```

Note: `unset CMAKE_BUILD_PARALLEL_LEVEL` avoids conflicts with `/m`. `MSYS_NO_PATHCONV=1` prevents bash from converting `/m` to `M:/`. The `/m` flag enables MSBuild parallel compilation.

**Output locations:**
- Release: `build/Release/Release/Decenza_DE1.exe`
- Debug: `build/Debug/Debug/Decenza_DE1.exe`

## macOS/iOS Build (on Mac)

Use Qt's `qt-cmake` wrapper which handles cross-compilation correctly.

**Finding Qt paths:** Qt is installed at `~/Qt/`. Discover paths dynamically:
```bash
# Find qt-cmake for macOS
find ~/Qt -name "qt-cmake" -path "*/macos/*"
# Find Ninja (bundled with Qt)
find ~/Qt/Tools -name "ninja"
```

**Configure iOS (generates Xcode project):**
```bash
rm -rf build/Qt_6_10_1_for_iOS && mkdir -p build/Qt_6_10_1_for_iOS && cd build/Qt_6_10_1_for_iOS && /Users/mic/Qt/6.10.2/ios/bin/qt-cmake ../.. -G Xcode
```

**Configure macOS (generates Xcode project):**
```bash
rm -rf build/Qt_6_10_1_for_macOS && mkdir -p build/Qt_6_10_1_for_macOS && cd build/Qt_6_10_1_for_macOS && /Users/mic/Qt/6.10.2/macos/bin/qt-cmake ../.. -G Xcode
```

**Open in Xcode:**
```bash
open build/Qt_6_10_1_for_iOS/Decenza_DE1.xcodeproj
# or
open build/Qt_6_10_1_for_macOS/Decenza_DE1.xcodeproj
```

Then in Xcode: Product → Archive for App Store submission.

## CI/CD (GitHub Actions)

All platforms build automatically when a `v*` tag is pushed. Each workflow can also be triggered manually via `workflow_dispatch` for **test builds only** (no version bump, no uploads by default).

All workflows have concurrency controls — if the same workflow triggers twice for the same ref, the older run is cancelled. Artifacts use 1-day retention with overwrite, so only the latest artifact per platform exists at any time. Dependabot (`.github/dependabot.yml`) checks weekly for Actions dependency updates.

### Workflows

| Platform | Workflow | Runner | Output |
|----------|----------|--------|--------|
| Android | `android-release.yml` | ubuntu-24.04 | Signed APK |
| iOS | `ios-release.yml` | macos-15 | IPA → App Store |
| macOS | `macos-release.yml` | macos-15 | Signed + notarized DMG |
| Windows | `windows-release.yml` | windows-latest | Inno Setup installer |
| Linux | `linux-release.yml` | ubuntu-24.04 | AppImage |
| Linux ARM64 | `linux-arm64-release.yml` | ubuntu-24.04-arm | AppImage (aarch64) |

On tag push: all workflows bump version code, build, upload to GitHub Release, and (for iOS) upload to App Store Connect. On `workflow_dispatch`: build only, no version bump, no upload (unless explicitly opted in).

### Quick commands
```bash
# Trigger individual TEST builds (no upload, no version bump)
gh workflow run android-release.yml --repo Kulitorum/Decenza -f upload_to_release=false
gh workflow run ios-release.yml --repo Kulitorum/Decenza -f upload_to_appstore=false
gh workflow run windows-release.yml --repo Kulitorum/Decenza -f upload_to_release=false
gh workflow run macos-release.yml --repo Kulitorum/Decenza -f upload_to_release=false
gh workflow run linux-release.yml --repo Kulitorum/Decenza -f upload_to_release=false
gh workflow run linux-arm64-release.yml --repo Kulitorum/Decenza -f upload_to_release=false

# Check build status
gh run list --repo Kulitorum/Decenza --limit 5

# Watch live logs
gh run watch --repo Kulitorum/Decenza

# View failed logs
gh run view --repo Kulitorum/Decenza --log-failed
```

### Release all platforms at once
See "Publishing Releases" section below for the full process. In short:
```bash
# 1. Create the GitHub Release FIRST (so CI finds it)
gh release create vX.Y.Z --title "Decenza DE1 vX.Y.Z" --prerelease --notes "..."
# 2. Then push the tag to trigger all 6 builds
git tag vX.Y.Z
git push origin vX.Y.Z
```

### GitHub Secrets

**Android:**
- `ANDROID_KEYSTORE_BASE64` — Base64-encoded `.jks` keystore
- `ANDROID_KEYSTORE_PASSWORD` — Keystore password

**iOS:**
- `P12_CERTIFICATE_BASE64`, `P12_PASSWORD` — iPhone Distribution certificate
- `PROVISIONING_PROFILE_BASE64`, `PROVISIONING_PROFILE_NAME` — App Store profile
- `KEYCHAIN_PASSWORD` — Temporary keychain password
- `APPLE_TEAM_ID` — Apple Developer Team ID
- `APP_STORE_CONNECT_API_KEY_ID`, `APP_STORE_CONNECT_API_ISSUER_ID`, `APP_STORE_CONNECT_API_KEY_BASE64` — App Store upload

**macOS:**
- `MACOS_DEVELOPER_ID_P12_BASE64`, `MACOS_DEVELOPER_ID_P12_PASSWORD` — Developer ID cert
- `APPLE_ID`, `APPLE_ID_APP_PASSWORD` — For notarization

### Platform notes
- iOS bundle ID: `io.github.kulitorum.decenza` (differs from Android: `io.github.kulitorum.decenza_de1`)
- iOS signing credentials expire yearly — see `docs/IOS_CI_FOR_CLAUDE.md` for renewal
- iOS tag-push builds upload to App Store Connect automatically (available in TestFlight). Manual `workflow_dispatch` builds default to `upload_to_appstore=false` (test only). App Store submission remains a manual step in App Store Connect. See `docs/IOS_TESTFLIGHT_SETUP.md` for setup instructions.
- Android keystore path is configurable via `ANDROID_KEYSTORE_PATH` env var (falls back to local path)
- Android build uses `build.gradle` post-build hook for signing and versioned APK naming

## Project Structure

```
src/
├── ble/                    # Bluetooth LE layer
│   ├── de1device.*         # DE1 machine protocol
│   ├── blemanager.*        # Device discovery
│   ├── scaledevice.*       # Abstract scale interface
│   ├── protocol/           # BLE UUIDs, binary codec
│   └── scales/             # Scale implementations (14+ types)
├── controllers/
│   ├── maincontroller.*    # App logic, profiles, shot processing
│   └── directcontroller.*  # Direct frame control mode
├── machine/
│   └── machinestate.*      # Phase tracking, stop-at-weight
├── profile/
│   ├── profile.*           # Profile container, JSON/TCL formats
│   └── profileframe.*      # Single extraction step
├── models/
│   └── shotdatamodel.*     # Shot data for graphing
├── core/
│   ├── settings.*          # QSettings persistence
│   └── batterymanager.*    # Smart charging control
├── network/
│   ├── visualizeruploader.*  # Upload shots to visualizer.coffee
│   └── visualizerimporter.*  # Import profiles from visualizer.coffee
└── main.cpp                # Entry point, object wiring

qml/
├── pages/                  # EspressoPage, SettingsPage, etc.
├── components/             # ShotGraph, StatusBar, etc.
└── Theme.qml               # Singleton styling (+ emojiToImage())

resources/
├── emoji/                  # ~750 Twemoji SVGs (CC-BY 4.0)
├── emoji.qrc               # Qt resource file for emoji SVGs
└── resources.qrc           # Icons, fonts, other assets

scripts/
└── download_emoji.py       # Download emoji SVGs from various sources
```

## Key Architecture

### Signal/Slot Flow
```
DE1Device (BLE) → signals → MainController → ShotDataModel → QML graphs
ScaleDevice     → signals → MachineState (stop-at-weight)
                          → MainController (graph data)
```

### Scale System
- **ScaleDevice**: Abstract base class
- **FlowScale**: Virtual scale from DE1 flow data (fallback when no physical scale)
- **Physical scales**: DecentScale, AcaiaScale, FelicitaScale, etc. (factory pattern)

### Machine Phases
```
Disconnected → Sleep → Idle → Heating → Ready
Ready → EspressoPreheating → Preinfusion → Pouring → Ending
Also: Steaming, HotWater, Flushing
```

## Conventions

### Design Principles
- **Never use timers as guards/workarounds.** Timers are fragile heuristics that break on slow devices and hide the real problem. Use event-based flags and conditions instead. For example, "suppress X until Y has happened" should be a boolean cleared by the Y event, not a timer. Only use timers for genuinely periodic tasks (polling, animation, heartbeats).
- **Never run database or disk I/O on the main thread.** Use `QThread::create()` with a `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` callback to run queries on a background thread and deliver results back to the main thread. See `ShotHistoryStorage::requestShot()` for the canonical pattern.

### C++
- Classes: `PascalCase`
- Methods/variables: `camelCase`
- Members: `m_` prefix
- Slots: `onEventName()`
- Use `Q_PROPERTY` with `NOTIFY` for bindable properties

### QML
- Files: `PascalCase.qml`
- IDs/properties: `camelCase`
- Use `Theme.qml` singleton for all styling
- Use `StyledTextField` instead of `TextField` to avoid Material floating label
- `ActionButton` dims icon (50% opacity) and text (secondary color) when disabled
- `native` is a reserved JavaScript keyword - use `nativeName` instead

### QML Gotchas

**Font property conflict**: Cannot use `font: Theme.bodyFont` and then override sub-properties like `font.bold: true`. QML treats this as assigning the property twice.
```qml
// BAD - causes "Property has already been assigned a value" error
Text {
    font: Theme.bodyFont
    font.bold: true  // Error!
}

// GOOD - use individual properties
Text {
    font.family: Theme.bodyFont.family
    font.pixelSize: Theme.bodyFont.pixelSize
    font.bold: true
}
```

**Keyboard handling for text inputs**: Always wrap pages with text input fields in `KeyboardAwareContainer` to shift content above the keyboard on mobile:
```qml
KeyboardAwareContainer {
    id: keyboardContainer
    anchors.fill: parent
    textFields: [myTextField1, myTextField2]  // Register all text inputs

    // Your page content here
    ColumnLayout {
        StyledTextField { id: myTextField1 }
        StyledTextField { id: myTextField2 }
    }
}
```

**Accessibility on interactive elements**: Every interactive element must have `Accessible.role`, `Accessible.name`, and `Accessible.focusable: true`. Without these, TalkBack/VoiceOver cannot discover or activate the element. Use the table below:

| Element | Use instead | If raw, must set |
|---------|-------------|------------------|
| Button (Rectangle+MouseArea) | `AccessibleButton` | `Accessible.role: Accessible.Button` + `name` + `focusable` + `onPressAction` |
| Text input | `StyledTextField` | `Accessible.role: Accessible.EditableText` + `name` + `description: text` + `focusable` |
| Autocomplete field | `SuggestionField` | (same as text input) |
| Checkbox | Qt `CheckBox` | `Accessible.name` + `Accessible.checked: checked` + `focusable` |
| Dropdown | `StyledComboBox` | `Accessible.role: Accessible.ComboBox` + `name` (use label, not displayText) + `focusable` |
| List delegate | — | `Accessible.role: Accessible.Button` + `name` (summarize row content) + `focusable` + `onPressAction` |

Common mistakes:
- **Rectangle+MouseArea without accessibility**: TalkBack cannot see it. Use `AccessibleButton` or add all four properties (`role`, `name`, `focusable`, `onPressAction`).
- **Accessibility on MouseArea instead of Rectangle**: Never put `Accessible.role`/`name`/`focusable` on the MouseArea child — put them on the parent Rectangle. MouseArea should only have an `id` so the Rectangle's `Accessible.onPressAction` can route to it. This is a recurring bug that is easy to miss in review.
- **Missing `Accessible.onPressAction`**: Every Rectangle+MouseArea button **must** have `Accessible.onPressAction: mouseAreaId.clicked(null)` (or `.tapped()` for TapHandler). Without it, TalkBack/VoiceOver double-tap does nothing. This applies even when the other three properties (`role`, `name`, `focusable`) are present.
- **Child Text inside accessible button missing `Accessible.ignored: true`**: When a Rectangle has `Accessible.name`, all child Text elements must set `Accessible.ignored: true`. Otherwise TalkBack announces the button name AND the text content, doubling the announcement.
- **Text input missing `Accessible.description: text`**: Field sounds "Empty" even when it contains text. `StyledTextField` and `SuggestionField` set this automatically. Note: `Accessible.value` does not exist in Qt QML — use `Accessible.description` instead.
- **ComboBox `Accessible.name` set to `displayText`**: Announces the selected value instead of the field label. Override with the label text.
- **List row with no accessibility**: Only child elements (e.g. CheckBox) are discoverable; the row itself and its primary action are invisible.
- **Decorative text without `Accessible.ignored: true`**: When a list delegate summarizes its content in `Accessible.name`, all child Text elements must set `Accessible.ignored: true`. Otherwise TalkBack announces the summary AND each text line individually, doubling every piece of information. Same applies to icon/label text inside buttons that already have `Accessible.name`.

```qml
// BAD - TalkBack can't see this button
Rectangle {
    MouseArea { onClicked: doSomething() }
}

// GOOD - use AccessibleButton
AccessibleButton {
    text: "Save"
    accessibleName: "Save changes"
    onClicked: doSomething()
}

// GOOD - or add accessibility to Rectangle manually
Rectangle {
    Accessible.role: Accessible.Button
    Accessible.name: "Save changes"
    Accessible.focusable: true
    Accessible.onPressAction: area.clicked(null)
    MouseArea { id: area; onClicked: doSomething() }
}
```

### ShotServer Web UI (shotserver_layout.cpp)

The layout editor web UI is served as inline HTML/JS from `shotserver_layout.cpp`. Follow these conventions:

**Async community endpoints (signal-based):**
- Community API calls (`browse`, `download`, `upload`, `delete`) are async — they connect to `LibrarySharing` signals and wait for a callback. Use the established pattern: `QPointer<QTcpSocket>` + `std::shared_ptr<bool>` fired guard + `QTimer` timeout + `PendingLibraryRequest` tracking.
- **Always verify the operation was accepted** after calling `LibrarySharing` methods. Methods like `browseCommunity()` and `downloadEntry()` silently return if already busy (`m_browsing`/`m_downloading`). Check `isBrowsing()`/`isDownloading()` after the call and send an immediate error response if rejected — otherwise the request hangs until the 60s timeout.
- **Always log timeout and cleanup events.** Use `qWarning()` when a timeout fires, `qDebug()` when a response is dropped (socket disconnected) or when a duplicate callback is blocked by the fired guard.
- **Only one request per type** is allowed at a time (`hasInFlightLibraryRequest`), because `LibrarySharing` is a singleton that emits one signal consumed by whichever handler is connected.

**JavaScript `fetch()` calls:**
- **Every `fetch()` must have a `.catch()` handler.** Never leave a fetch chain without error handling — silent failures leave the UI in a broken state (spinner stuck, editor blank, no feedback).
- **Check `r.ok` before `r.json()`** in fetch chains. Non-2xx responses with non-JSON bodies will throw on `.json()` and produce a misleading "Network error" instead of "Server error (500)".
- **Use `AbortController` with a timeout** for community API calls (client-side 45s, server-side 60s safety net).
- **Don't mutate state before async success.** For example, increment a page counter only after the fetch succeeds, not before — otherwise failed requests skip pages permanently.

## Emoji System

Emojis are rendered as pre-rendered SVG images (Twemoji), not via a color font. This avoids D3D12/GPU crashes caused by CBDT/CBLC bitmap fonts (NotoColorEmoji.ttf) being incompatible with Qt's scene graph glyph cache across all platforms.

### How It Works
- Emoji characters are stored as Unicode strings in settings/layout data (e.g., `"☕"`, `"😀"`)
- Decenza SVG icons are stored as `qrc:/icons/...` paths (e.g., `"qrc:/icons/espresso.svg"`)
- At display time, `Theme.emojiToImage(emoji)` converts to an image path:
  - `qrc:/icons/*` paths pass through unchanged
  - Unicode emoji → codepoints → `qrc:/emoji/<hex>.svg` (e.g., `"☕"` → `"qrc:/emoji/2615.svg"`)
- All QML components use `Image { source: Theme.emojiToImage(value) }` — never Text for emojis

### Switching Emoji Sets
```bash
# Download from a different source (twemoji, openmoji, noto, fluentui)
python scripts/download_emoji.py openmoji
# Regenerates resources/emoji/ and resources/emoji.qrc
```

### Adding New Emojis
1. Add the emoji character to the relevant category in `qml/components/layout/EmojiData.js`
2. Re-run `python scripts/download_emoji.py twemoji` to download the new SVG
3. Rebuild (the script regenerates `emoji.qrc`)

### Attribution
Twemoji by Twitter/X (CC-BY 4.0): https://github.com/twitter/twemoji

## Profile System

- **FrameBased mode**: Upload to machine, executes autonomously
- **DirectControl mode**: App sends setpoints frame-by-frame
- Formats: JSON (native), TCL (de1app import)
- Tare happens when frame 0 starts (after machine preheat)

### Exit Conditions

There are two types of exit conditions:

1. **Machine-side exits** (pressure/flow): Controlled by `exit_if` flag and `exit_type`
   - Encoded in BLE frame flags (DoCompare, DC_GT, DC_CompF)
   - Machine autonomously checks and advances frames
   - Types: `pressure_over`, `pressure_under`, `flow_over`, `flow_under`

2. **App-side exits** (weight): Controlled by `exit_weight` field INDEPENDENTLY
   - App monitors scale weight and sends `SkipToNext` (0x0E) command
   - **CRITICAL**: Weight exit is independent of `exit_if` flag!
   - A frame can have `exit_if: false` (no machine exit) with `exit_weight: 3.6` (app exit)
   - Both can coexist: machine checks pressure/flow, app checks weight

### Weight Exit Implementation

```cpp
// CORRECT - weight is independent of exitIf
if (frame.exitWeight > 0) {
    if (weight >= frame.exitWeight) {
        m_device->skipToNextFrame();
    }
}

// WRONG - don't require exitIf for weight!
if (frame.exitIf && frame.exitType == "weight" ...) // BUG!
```

## Data Migration (Device-to-Device Transfer)

Transfer profiles, shots, settings, and media between devices over WiFi.

### Architecture
- **Server**: `ShotServer` (same as Remote Access) exposes `/backup/*` REST endpoints
- **Client**: `DataMigrationClient` discovers servers and imports data
- **Discovery**: UDP broadcast on port 8889 for automatic device finding
- **UI**: Settings → Data tab

### Key Files
- `src/core/datamigrationclient.cpp/.h` - Client-side discovery and import
- `src/network/shotserver.cpp` - Server-side backup endpoints (lines 4400+)
- `qml/pages/settings/SettingsDataTab.qml` - UI

### How It Works
1. **Source device**: Enable "Remote Access" in Settings → Shot History tab
2. **Target device**: Go to Settings → Data tab, tap "Search"
3. Client broadcasts UDP discovery packet to port 8889
4. Servers respond with device info (name, URL, platform)
5. Client filters out own device, shows discovered servers
6. User selects device and imports desired data types

### REST Endpoints (on ShotServer)
- `GET /backup/manifest` - List available data (counts, sizes)
- `GET /backup/settings` - Export settings JSON
- `GET /backup/profiles` - List profile filenames
- `GET /backup/profiles/{name}` - Download single profile
- `GET /backup/shots` - List shot IDs
- `GET /backup/shots/{id}` - Download single shot
- `GET /backup/media` - List media files
- `GET /backup/media/{name}` - Download media file

### Import Options
- **Import All**: Settings, profiles, shots, media
- **Individual**: Import only specific data types (Settings, Profiles, Shots, Media buttons)

## Visualizer Integration

### DYE (Describe Your Espresso) Metadata
- **Location**: `qml/pages/ShotMetadataPage.qml`
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

### Visualizer Profile Format Conversion
- Visualizer stores values as strings (we convert to doubles)
- Exit conditions: `{type, value, condition}` → `exitType`, `exitPressureOver`, etc.
- Limiter: `{value, range}` → `maxFlowOrPressure`, `maxFlowOrPressureRange`

## BLE Protocol Notes

- DE1 Service: `0000A000-...`
- Command queue prevents BLE overflow (50ms between writes)
- Shot samples at ~5Hz during extraction
- Profile upload: header (5 bytes) + frames (8 bytes each)
- USB charger control: MMR address `0x803854` (1=on, 0=off)
- DE1 has 10-minute timeout that auto-enables charger; must resend command every 60s

### BLE Write Retry & Timeout (like de1app)

BLE writes can fail or hang. The implementation includes retry logic similar to de1app:

**Mechanism:**
- Each write starts a 5-second timeout timer
- On error or timeout: retry up to 3 times with 100ms delay
- After max retries: log failure and move to next command
- Queue is cleared when any flowing operation starts (espresso, steam, hot water, flush)

**Error Logging (captured in shot debug log):**
```
DE1Device: BLE write TIMEOUT after 5000 ms - uuid: 0000a00f data: 0102...
DE1Device: Retrying after timeout (1/3)
DE1Device: Write FAILED (timeout) after 3 retries - uuid: 0000a00f data: 0102...
```

**Key UUIDs:**
- `0000a002` = RequestedState
- `0000a00d` = ShotSettings
- `0000a00e` = StateInfo
- `0000a00f` = HeaderWrite (profile header)
- `0000a010` = FrameWrite (profile frames)

**Comparison to de1app:**
- de1app uses soft 1-second fallback timer (just retries queue)
- de1app has `vital` flag for commands that must retry
- Our implementation: hard 5-second timeout, all commands can retry up to 3 times

### Shot Debug Logging

`ShotDebugLogger` captures all `qDebug()`/`qWarning()` messages during shots:
- Installs Qt message handler when shot starts
- Stores captured log in `debug_log` column of shot history
- Users can view/export via shot history web interface
- BLE errors are automatically captured (use `qWarning()` for errors)

## Battery Management

### Smart Charging (BatteryManager)
- **Off**: Charger always on (no control)
- **On** (default): Maintains 55-65% charge
- **Night**: Maintains 90-95% charge
- Commands sent every 60 seconds with `force=true` to overcome DE1 timeout

## Steam Heater Control

### Settings
- **`keepSteamHeaterOn`**: When true, keeps steam heater warm during Idle for faster steaming
- **`steamDisabled`**: Completely disables steam (sends 0°C)
- **`steamTemperature`**: Target steam temperature (typically 140-160°C)

### Key Functions (MainController)
- **`applySteamSettings()`**: Smart function that checks phase and settings:
  - If `steamDisabled` → sends 0°C
  - If phase is Ready → always sends steam temp (machine heating, steam should be available)
  - If `keepSteamHeaterOn=false` → sends 0°C (turn off in Idle)
  - Otherwise → sends configured steam temp
- **`startSteamHeating()`**: Always sends steam temp (ignores `keepSteamHeaterOn`) - use when user wants to steam
- **`turnOffSteamHeater()`**: Sends 0°C to turn off heater

### Behavior by Phase
| Phase | keepSteamHeaterOn=true | keepSteamHeaterOn=false |
|-------|------------------------|-------------------------|
| Startup/Idle | Sends steam temp, periodic refresh | Sends 0°C |
| Ready | Sends steam temp | Sends steam temp (for GHC) |
| Steaming | Sends steam temp | Sends steam temp |
| After Steaming | Keeps heater warm | Turns off heater |

### SteamPage Flow
1. **Page opens**: Calls `startSteamHeating()` to force heater on
2. **Heating indicator**: Shows progress bar when current temp < target - 5°C
3. **During steaming**: Calls `startSteamHeating()` for any setting changes
4. **After steaming**: If `keepSteamHeaterOn=false`, calls `turnOffSteamHeater()`
5. **Back button**: Turns off heater if `keepSteamHeaterOn=false`

### Comparison with de1app
- de1app sends `TargetSteamTemp=0` when `steam_disabled=1` or `steam_temperature < 135°C`
- We send 0°C when `steamDisabled=true` or `keepSteamHeaterOn=false` (in Idle)
- Both approaches explicitly turn off the heater rather than relying on machine timeout

## Platforms

- Desktop: Windows, macOS, Linux
- Mobile: Android (API 28+), iOS (14.0+)
- Android needs Location permission for BLE scanning

## Decent Tablet Troubleshooting

The tablets shipped with Decent espresso machines have some quirks:

### GPS Not Working (Shot Map Location)
The GPS provider is **disabled by default** on these tablets. To enable:

**Via ADB:**
```bash
adb shell settings put secure location_providers_allowed +gps
```

**Via Android Settings:**
1. Settings → Location → Turn ON
2. Set Mode to "High accuracy" (not "Battery saving")

**Note:** These tablets don't have Google Play Services, so network-based location (WiFi/cell triangulation) won't work. GPS requires clear sky view (outdoors or near window) for first fix. The app supports manual city entry as a fallback.

### No Google Play Services
The tablet lacks Google certification, so:
- Network location unavailable (requires Play Services)
- Some Google apps may prompt to install Play Services
- GPS-only location works once enabled (see above)

## Windows Installer

The Windows installer is built with Inno Setup (`installer/setup.iss`). It uses a local config file `installer/setupvars.iss` (gitignored) to define machine-specific paths.

### Local setupvars.iss
Create `installer/setupvars.iss` with paths for your machine:
```iss
#define SourceDir "C:\CODE\de1-qt"
#define AppBuildDir "C:\CODE\de1-qt\build\Release"
#define AppDeployDir "C:\CODE\de1-qt\installer\deploy"
#define QtDir "C:\Qt\6.10.2\msvc2022_64"
#define VcRedistDir "C:\CODE\de1-qt\vcredist"
#define VcRedistFile "vc_redist.x64.exe"
#define OpenSslDir "C:\Program Files\OpenSSL-Win64\bin"
```

### OpenSSL Dependency
The app links OpenSSL directly (for TLS certificate generation in Remote Access). The installer must bundle `libssl-3-x64.dll` and `libcrypto-3-x64.dll`. If `OpenSslDir` is defined in `setupvars.iss`, the installer copies them automatically. Install OpenSSL for Windows from https://slproweb.com/products/Win32OpenSSL.html if you don't have it.

## Android Build & Signing

### Build Process
- **Local**: Qt Creator runs `androiddeployqt` with `--sign` flag
- **CI**: GitHub Actions workflow (`android-release.yml`) on ubuntu-24.04
- **Keystore**: `C:/CODE/Android APK keystore.jks` locally, `ANDROID_KEYSTORE_BASE64` secret on CI
- **Key alias**: `de1-key`
- **Keystore path**: `build.gradle` reads `ANDROID_KEYSTORE_PATH` env var, falls back to local path

### How Signing & Renaming Works
1. Build creates unsigned APK (`android-build-Decenza_DE1-release-unsigned.apk`)
2. `gradle.buildFinished` hook in `android/build.gradle` triggers:
   - Finds unsigned APK
   - Signs it with `apksigner` from Android SDK build-tools (cross-platform: `.bat` on Windows, no extension on Linux)
   - Outputs as `Decenza_DE1_<version>.apk`
3. For AAB: same hook copies (via `java.nio.file.Files.copy`) and signs with `jarsigner` to `Decenza_DE1-<version>.aab`
4. On CI, a fallback step signs the APK manually if the gradle hook fails

### Output Files
- **APK output**: `build/.../android-build-Decenza_DE1/build/outputs/apk/release/`
  - `Decenza_DE1_X.Y.Z.apk` (versioned, signed)
- **AAB output**: `build/.../android-build-Decenza_DE1/build/outputs/bundle/release/`
  - `Decenza_DE1-X.Y.Z.aab` (versioned, for Play Store)

## Publishing Releases

### Prerequisites
- GitHub CLI (`gh`) installed: `winget install GitHub.cli`
- Authenticated: `gh auth login`

### Release Process

**IMPORTANT: Always use tag pushes to build releases.** Never use `workflow_dispatch` for release builds — it skips version code bumps and causes duplicate upload errors (especially iOS App Store). The `workflow_dispatch` trigger is only for test builds that don't upload anywhere.

**IMPORTANT**: Release notes should only include **user-experience changes** (new features, UI changes, bug fixes users would notice). Skip internal changes like code refactoring, developer tools, translation system improvements, or debug logging changes.

#### Step 1: Review changes since last release
```bash
gh release list --limit 5
git log <previous-tag>..HEAD --oneline
```

#### Step 2: Create the GitHub Release FIRST (before pushing the tag)
**You must create the release before pushing the tag.** If CI runs and no release exists, it creates one as a draft without `--prerelease`, which is wrong. Creating it first ensures your release notes and prerelease flag are preserved.

The `Build: XXXX` line is injected automatically by CI after the Android build completes. Do NOT add it manually.

For beta/prerelease builds, add `--prerelease` flag. Users with "Beta updates" enabled in Settings will get these. Omit `--prerelease` for stable releases.

```bash
gh release create vX.Y.Z \
  --title "Decenza DE1 vX.Y.Z" \
  --prerelease \
  --notes "$(cat <<'EOF'
## Changes

### New Features
- Feature 1 (from commit messages)
- Feature 2

### Bug Fixes
- Fix 1
- Fix 2

## Installation

**Direct APK download:** https://github.com/Kulitorum/Decenza/releases/download/vX.Y.Z/Decenza_DE1_X.Y.Z.apk

Install on your Android device (allow unknown sources).
EOF
)"
```

#### Step 3: Push the tag to trigger builds
```bash
git tag vX.Y.Z
git push origin vX.Y.Z
```

This triggers all 6 platform builds simultaneously. Each workflow will:
- Bump the version code
- Build the binary
- Upload the artifact to the existing GitHub Release
- Android workflow commits the bumped version code back to main
- Android workflow injects `Build: XXXX` into the release notes
- iOS workflow uploads to App Store Connect

#### Updating an existing pre-release
To rebuild an existing pre-release at the current HEAD:
```bash
# Delete old tag and recreate at HEAD
git tag -d vX.Y.Z
git push origin :refs/tags/vX.Y.Z
git tag vX.Y.Z
git push origin vX.Y.Z
```
**Note:** Do NOT delete the GitHub Release — only the tag. The release persists and CI will upload new artifacts to it. If the release was accidentally deleted or created as a draft, fix it with:
```bash
gh release edit vX.Y.Z --draft=false --prerelease
```

### Updating Release Notes
```bash
gh release edit vX.Y.Z --notes "$(cat <<'EOF'
Updated notes here...
EOF
)"
```

### Auto-Update System
- **Check interval**: Every 60 minutes (configurable in Settings → Updates)
- **Version detection**: Compares display version (`X.Y.Z`), then falls back to build number if versions are equal
- **Build number source**: Parsed from release notes using pattern `Build: XXXX` (or `Build XXXX`)
- **Beta channel**: Users opt-in via Settings → Updates → "Beta updates". Prereleases are only shown to opted-in users.
- **Platforms**: Android auto-downloads APK; iOS directs to App Store; desktop shows release page

### Notes
- **Always use tag pushes** — never `workflow_dispatch` — for release builds
- **Always review `git log <prev-release>..HEAD`** to include all changes in release notes
- `Build: XXXX` is injected automatically by CI — do not add manually
- Always include direct APK link in release notes (old browsers can't see Assets section)
- Remove `--prerelease` flag for stable releases
- APK files are for direct distribution (sideloading)
- AAB files are only for Google Play Store uploads
- Users cannot install AAB files directly

## QML Navigation System

### Page Navigation (main.qml)
- **StackView**: `pageStack` manages page navigation
- **Auto-navigation**: `MachineState.phaseChanged` signal triggers automatic page transitions
- **Operation pages**: SteamPage, HotWaterPage, FlushPage, EspressoPage
- **Completion flow**: When operations end, show 3-second completion overlay, then navigate to IdlePage

### Phase Change Handler Pattern
```qml
// In main.qml onPhaseChanged handler:
// 1. Check pageStack.busy ONLY for navigation calls, not completion handling
// 2. Navigation TO operation pages: check !pageStack.busy before replace()
// 3. Completion handling (Idle/Ready): NEVER skip - always show completion overlay
```

### Operation Page Structure
Each operation page (Steam, HotWater, Flush) has:
- `objectName`: Must be set for navigation detection (e.g., `objectName: "steamPage"`)
- `isOperating` property: Binds to `MachineState.phase === <phase>`
- **Live view**: Shown during operation (timer, progress, stop button)
- **Settings view**: Shown when idle (presets, configuration)
- **Stop button**: Only visible on headless machines (`DE1Device.isHeadless`)

### Common Bug Pattern
**Problem**: Early `return` in `onPhaseChanged` can skip completion handling
**Solution**: Only check `pageStack.busy` before `replace()` calls, not at handler start

## Versioning

- **Display version** (versionName): Set in `CMakeLists.txt` line 2: `project(Decenza_DE1 VERSION x.y.z)`
- **Version code** (versionCode): Stored in `versioncode.txt`. Does **not** auto-increment during local builds. CI workflows bump it on tag push, and the Android workflow commits the new value back to `main`.
- **version.h**: Auto-generated from `src/version.h.in` with VERSION_STRING macro
- To release a new version: Update VERSION in CMakeLists.txt, commit, then follow the "Publishing Releases" process (create release first, then push tag)

## Git Workflow

- **Version codes are managed by CI** — local builds use `versioncode.txt` as-is (no auto-increment). All 6 CI workflows bump the code identically on tag push. The Android workflow commits the bumped value back to `main`.
- You do **not** need to manually commit version code files (`versioncode.txt`, `android/AndroidManifest.xml`, `installer/version.iss`) — CI handles this automatically.

## Accessibility (TalkBack/VoiceOver)

### Current Implementation
The app has accessibility support via `AccessibilityManager` (C++) with:
- Text-to-speech announcements via `AccessibilityManager.announce()`
- Tick sounds for frame changes
- `AccessibleTapHandler` and `AccessibleMouseArea` for touch handling
- Extraction announcements (phase changes, weight milestones, periodic updates)
- User-configurable settings in Settings → Accessibility

### Key Components
- `src/core/accessibilitymanager.h/cpp` - TTS, tick sounds, settings persistence
- `qml/components/AccessibleTapHandler.qml` - Touch handler that works with TalkBack
- `qml/components/AccessibleMouseArea.qml` - Simpler version of above
- `qml/components/AccessibleButton.qml` - Button with required accessibleName
- `qml/components/AccessibleLabel.qml` - Tap-to-announce text

### Accessibility Anti-patterns (Do NOT Use)

1. **Parent-ignored / child-accessible**: Never set `Accessible.ignored: true` on a parent and put `Accessible.role` on a child occupying the same bounds. TalkBack can't reliably route activation to the child. Put accessibility properties on the interactive element itself.
2. **Popup for selection lists**: Never use `Popup` for lists users must navigate. TalkBack can't trap focus inside Qt `Popup` elements. Use `Dialog { modal: true }` with `AccessibleButton` delegates instead.
3. **Overlapping accessible elements**: Never position accessible buttons inside another accessible element's bounds (e.g., buttons inside a TextField's padding area). TalkBack will only discover one element. Use conditional layout to show buttons in separate bounds when accessibility is enabled.

### Rules for New Components

1. Every interactive element must have `Accessible.role`, `Accessible.name`, `Accessible.focusable`, `Accessible.onPressAction` **on itself** (not on a child)
2. Never use `Popup` for selection lists — use `Dialog` with `AccessibleButton` delegates
3. Never overlap accessible elements — separate bounds or use conditional layout (`_accessibilityMode` pattern)
4. Test with TalkBack: double-tap to activate, swipe to navigate

### TODO: Focus Order Improvements

**Problem:** Screen reader users report unpredictable navigation order when swiping through elements. Focus jumps unexpectedly and some elements are skipped.

**Root Cause:** Many interactive elements lack proper focus configuration:
1. Missing `Accessible.focusable: true` on interactive items
2. No logical `KeyNavigation.tab` / `KeyNavigation.backtab` chains
3. Missing `FocusScope` wrappers for grouped controls
4. No `focus: true` on first focusable element in pages

**Fix Required:** Go through each page and ensure:
```qml
// 1. All interactive elements are focusable
Rectangle {
    Accessible.role: Accessible.Button
    Accessible.name: "My Button"
    Accessible.focusable: true  // ADD THIS
}

// 2. Logical tab order with KeyNavigation
Item {
    id: firstControl
    KeyNavigation.tab: secondControl
    KeyNavigation.backtab: lastControl
}

// 3. Group related controls
FocusScope {
    // Controls inside share focus context
}

// 4. Set initial focus
Page {
    Component.onCompleted: firstControl.forceActiveFocus()
}
```

**Pages to Review:**
- `qml/pages/IdlePage.qml`
- `qml/pages/EspressoPage.qml`
- `qml/pages/SteamPage.qml`
- `qml/pages/HotWaterPage.qml`
- `qml/pages/FlushPage.qml`
- `qml/pages/SettingsPage.qml` (and all settings tabs)
- `qml/pages/RecipesPage.qml`
- `qml/pages/ProfileEditorPage.qml`

**Testing:** Enable TalkBack on Android, then:
1. Swipe right repeatedly - should move through all controls in logical order
2. No elements should be skipped
3. Focus should not jump unexpectedly
4. First element on each page should receive focus when page opens
