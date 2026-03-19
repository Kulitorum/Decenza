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

# Decenza

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
- Release: `build/Release/Release/Decenza.exe`
- Debug: `build/Debug/Debug/Decenza.exe`

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
rm -rf build/Qt_6_10_2_for_iOS && mkdir -p build/Qt_6_10_2_for_iOS && cd build/Qt_6_10_2_for_iOS && /Users/mic/Qt/6.10.2/ios/bin/qt-cmake ../.. -G Xcode
```

**Configure macOS (generates Xcode project):**
```bash
rm -rf build/Qt_6_10_2_for_macOS && mkdir -p build/Qt_6_10_2_for_macOS && cd build/Qt_6_10_2_for_macOS && /Users/mic/Qt/6.10.2/macos/bin/qt-cmake ../.. -G Xcode
```

**Open in Xcode:**
```bash
open build/Qt_6_10_2_for_iOS/Decenza.xcodeproj
# or
open build/Qt_6_10_2_for_macOS/Decenza.xcodeproj
```

Then in Xcode: Product → Archive for App Store submission.

## CI/CD (GitHub Actions)

See `docs/CLAUDE_MD/CI_CD.md` for workflows, secrets, quick commands, and platform notes.

## Project Structure

```
src/
├── ai/                     # AI assistant integration
├── ble/                    # Bluetooth LE layer
│   ├── de1device.*         # DE1 machine protocol
│   ├── blemanager.*        # Device discovery
│   ├── scaledevice.*       # Abstract scale interface
│   ├── protocol/           # BLE UUIDs, binary codec
│   ├── scales/             # Scale implementations (13 types)
│   └── transport/          # BLE transport abstraction
├── controllers/
│   ├── maincontroller.*    # App logic, profiles, shot processing
│   └── directcontroller.*  # Direct frame control mode
├── core/
│   ├── settings.*          # QSettings persistence
│   └── batterymanager.*    # Smart charging control
├── history/                # Shot history storage and queries
├── machine/
│   └── machinestate.*      # Phase tracking, stop-at-weight
├── models/
│   └── shotdatamodel.*     # Shot data for graphing
├── network/
│   ├── shotserver.cpp      # HTTP server core + route dispatch
│   ├── shotserver_backup.cpp   # Backup/restore endpoints
│   ├── shotserver_layout.cpp   # Layout editor web UI
│   ├── shotserver_shots.cpp    # Shot history endpoints
│   ├── shotserver_settings.cpp # Settings endpoints
│   ├── shotserver_ai.cpp       # AI assistant endpoints
│   ├── shotserver_auth.cpp     # Authentication
│   ├── shotserver_theme.cpp    # Theme endpoints
│   ├── shotserver_upload.cpp   # File upload handling
│   ├── visualizeruploader.*    # Upload shots to visualizer.coffee
│   └── visualizerimporter.*    # Import profiles from visualizer.coffee
├── profile/
│   ├── profile.*           # Profile container, JSON/TCL formats
│   ├── profileframe.*      # Single extraction step
│   └── profilesavehelper.* # Shared save/compare/deduplicate logic for importers
├── rendering/              # Custom rendering (shot graphs, etc.)
├── screensaver/            # Screensaver implementation
├── simulator/              # DE1 machine simulator
├── usb/                    # USB scale connectivity
├── weather/                # Weather data for shot metadata
└── main.cpp                # Entry point, object wiring

qml/
├── pages/                  # EspressoPage, SettingsPage, etc.
├── components/             # ShotGraph, StatusBar, etc.
└── Theme.qml               # Singleton styling (+ emojiToImage())

resources/
├── CoffeeCup/              # 3D-rendered cup images for CupFillView
│   ├── BackGround.png      # Cup back, interior, handle (701x432)
│   ├── Mask.png            # Black = coffee area, white = no coffee
│   ├── Overlay.png         # Rim, front highlights (lighten blend)
│   └── FullRes.7z          # Source PSD archive
├── emoji/                  # ~750 Twemoji SVGs (CC-BY 4.0)
├── emoji.qrc               # Qt resource file for emoji SVGs
└── resources.qrc           # Icons, fonts, other assets

shaders/
├── crt.frag                # CRT/retro display effect
├── cup_mask.frag           # Masks coffee to cup interior (inverts Mask.png)
└── cup_lighten.frag        # Lighten (MAX) blend + brightness-to-alpha

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
Also: Steaming, HotWater, Flushing, Refill, Descaling, Cleaning
```

## Conventions

### Design Principles
- **Never use timers as guards/workarounds.** Timers are fragile heuristics that break on slow devices and hide the real problem. Use event-based flags and conditions instead. For example, "suppress X until Y has happened" should be a boolean cleared by the Y event, not a timer. Only use timers for genuinely periodic tasks (polling, animation, heartbeats) and **UI auto-dismiss** (toasts/banners that hide after N seconds). Everything else — including debounce — should use event-based flags.
- **Never run database or disk I/O on the main thread.** Use `QThread::create()` with a `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` callback to run queries on a background thread and deliver results back to the main thread. See `ShotHistoryStorage::requestShot()` for the canonical pattern.

### C++
- Classes: `PascalCase`
- Methods/variables: `camelCase`
- Members: `m_` prefix
- Slots: `onEventName()`
- Use `Q_PROPERTY` with `NOTIFY` for bindable properties
- Use `qsizetype` (not `int`) for container sizes — `QVector::size()`, `QList::size()`, `QString::size()` etc. return `qsizetype` (64-bit on iOS/macOS). Assigning to `int` causes `-Wshorten-64-to-32` warnings.

### QML
- Files: `PascalCase.qml` — new QML files **must** be added to `CMakeLists.txt` (in the `qt_add_qml_module` file list) to be included in the Qt resource system. Without this, the file won't be found at runtime.
- **New layout widgets** require registration in 4 places: (1) `CMakeLists.txt` file list, (2) `LayoutItemDelegate.qml` switch, (3) `LayoutEditorZone.qml` widget palette + chip label map, (4) `shotserver_layout.cpp` web editor widget list. Optionally add to `LayoutCenterZone.qml` if the widget should be allowed in center/idle zones.
- IDs/properties: `camelCase`
- Use `Theme.qml` singleton for all styling — never hardcode colors, font sizes, spacing, or radii. Use `Theme.textColor`, `Theme.bodyFont`, `Theme.subtitleFont`, `Theme.spacingMedium`, `Theme.cardRadius`, etc.
- All user-visible text must be internationalized. Use `TranslationManager.translate("section.key", "Fallback text")` for property bindings and inline expressions. Use the `Tr` component for standalone visible text (`Tr { key: "section.name"; fallback: "English text" }`). For text used in properties via `Tr`, use a hidden instance: `Tr { id: trMyLabel; key: "my.key"; fallback: "Label"; visible: false }` then `text: trMyLabel.text`. Reuse existing keys like `common.button.ok` and `common.accessibility.dismissDialog` where applicable.
- Use `StyledTextField` instead of `TextField` to avoid Material floating label
- `ActionButton` dims icon (50% opacity) and text (secondary color) when disabled
- `native` is a reserved JavaScript keyword - use `nativeName` instead
- **Never use Unicode symbols as icons in text** (e.g., `"\u270E"`, `"\u2717"`, `"\u2630"`). These render as tofu squares on devices without the right font glyphs. Use SVG icons from `qrc:/icons/` with `Image` instead. For buttons/menu items, use a `Row { Image {} Text {} }` contentItem. Safe Unicode characters (°, ·, —, →, ×) that are in standard fonts are OK.
- **Never override FINAL properties on Qt types.** Qt 6.10+ marks some `Popup`/`Dialog` properties as FINAL (e.g., `message`, `title`). Declaring `property string message` on a Dialog will prevent the component from loading. Use a different name (e.g., `resultMessage`), or use the inherited property directly if it already exists on the base type.

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

**Reserved property names in JS model data**: `name` is a reserved QML property (`QObject::objectName`). When a JS array of objects is used as a Repeater model, `modelData.name` resolves to the QML object name (empty string), not the JS property. Use a different key like `label`.
```qml
// BAD - modelData.name resolves to empty string
readonly property var items: [{ name: "Foo" }]
Repeater {
    model: items
    delegate: Text { text: modelData.name }  // Shows nothing!
}

// GOOD - use a non-reserved key
readonly property var items: [{ label: "Foo" }]
Repeater {
    model: items
    delegate: Text { text: modelData.label }  // Works correctly
}
```

Other reserved names to avoid in model data: `parent`, `children`, `data`, `state`, `enabled`, `visible`, `width`, `height`, `x`, `y`, `z`, `focus`, `clip`.

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
| Button (Rectangle+MouseArea) | `AccessibleButton` or `AccessibleMouseArea` | `Accessible.role: Accessible.Button` + `name` + `focusable` + `onPressAction` |
| Text input | `StyledTextField` | `Accessible.role: Accessible.EditableText` + `name` + `description: text` + `focusable` |
| Autocomplete field | `SuggestionField` | (same as text input) |
| Checkbox | Qt `CheckBox` | `Accessible.name` + `Accessible.checked: checked` + `focusable` |
| Dropdown | `StyledComboBox` | `Accessible.role: Accessible.ComboBox` + `name` (use label, not displayText) + `focusable` |
| List delegate | — | `Accessible.role: Accessible.Button` + `name` (summarize row content) + `focusable` + `onPressAction` |

Common mistakes:
- **Multi-action element missing `Accessible.description` hint**: Any interactive element with secondary actions (long-press, double-tap) **must** set `Accessible.description` (or `accessibleDescription` on `AccessibleTapHandler`) to announce those actions. TalkBack/VoiceOver reads this as a hint after the element name. Without it, blind users cannot discover secondary actions. Format: `"Double-tap or long-press to <action>."` For `AccessibleTapHandler` use the `accessibleDescription` property; for `ActionButton` and raw `Rectangle` use `Accessible.description` directly.
- **Rectangle+MouseArea without accessibility**: TalkBack cannot see it. Use `AccessibleButton`, `AccessibleMouseArea`, or add all four properties (`role`, `name`, `focusable`, `onPressAction`).
- **Accessibility on raw MouseArea instead of Rectangle**: Never put `Accessible.role`/`name`/`focusable` on a raw `MouseArea` child — put them on the parent Rectangle. MouseArea should only have an `id` so the Rectangle's `Accessible.onPressAction` can route to it. This does **not** apply to `AccessibleMouseArea`, which is a project component designed to handle accessibility on behalf of the parent via `accessibleItem`.
- **Missing `Accessible.onPressAction`**: Every raw Rectangle+MouseArea button **must** have `Accessible.onPressAction: mouseAreaId.clicked(null)` (or `.tapped()` for TapHandler). Without it, TalkBack/VoiceOver double-tap does nothing. This applies even when the other three properties (`role`, `name`, `focusable`) are present. Not needed when using `AccessibleMouseArea` or `AccessibleButton`.
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

// GOOD - use AccessibleButton (preferred for standard buttons)
AccessibleButton {
    text: "Save"
    accessibleName: "Save changes"
    onClicked: doSomething()
}

// GOOD - use AccessibleMouseArea (for custom-styled buttons, provides announce-first TalkBack behavior)
Rectangle {
    id: myButton
    color: Theme.primaryColor
    Accessible.ignored: true
    Text { text: "Save"; Accessible.ignored: true }
    AccessibleMouseArea {
        anchors.fill: parent
        accessibleName: "Save changes"
        accessibleItem: myButton
        onAccessibleClicked: doSomething()
    }
}

// OK - add accessibility to Rectangle manually (last resort, loses announce-first behavior)
Rectangle {
    Accessible.role: Accessible.Button
    Accessible.name: "Save changes"
    Accessible.focusable: true
    Accessible.onPressAction: area.clicked(null)
    MouseArea { id: area; onClicked: doSomething() }
}
```

### ShotServer Web UI (split across shotserver_*.cpp files)

The ShotServer is split into multiple files: `shotserver.cpp` (core + routing), `shotserver_layout.cpp` (layout editor), `shotserver_shots.cpp`, `shotserver_backup.cpp`, `shotserver_settings.cpp`, `shotserver_ai.cpp`, `shotserver_auth.cpp`, `shotserver_theme.cpp`, `shotserver_upload.cpp`. The layout editor web UI is served as inline HTML/JS from `shotserver_layout.cpp`. Follow these conventions:

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

## Cup Fill View

The espresso extraction cup visualization (`qml/components/CupFillView.qml`) uses a hybrid image+procedural approach:

### Layer Stack
```
1. BackGround.png (Image)     — cup back, interior, handle
2. Coffee Canvas (masked)     — liquid fill, crema, waves, ripples
3. Effects Canvas (unmasked)  — stream, steam wisps, completion glow
4. Overlay.png (lighten blend) — rim, front wall highlights
5. Weight text overlay
```

### GPU Shaders (require Qt6 ShaderTools)
- **cup_mask.frag**: Masks coffee to cup interior using Mask.png (black = coffee visible, inverted in shader)
- **cup_lighten.frag**: MAX blend per channel with brightness-to-alpha (black areas become transparent)
- Compiled via `qt_add_shaders()` in CMakeLists.txt to `.qsb` format

### Updating Cup Images
1. Edit `resources/CoffeeCup/FullRes.7z` (contains source PSD)
2. Export three layers as 701x432 RGBA PNGs:
   - `BackGround.png` — everything behind the coffee (on transparent/black)
   - `Mask.png` — black silhouette of cup interior, white elsewhere
   - `Overlay.png` — everything in front of coffee (on black, for lighten blend)
3. Rebuild (images are in `resources.qrc`)

### Coffee Geometry
The procedural coffee rendering uses proportional coordinates relative to the cup image dimensions. Key geometry is defined in `cupGeometry()`: cup center at 44% width, rim at 6% height, bottom at 92% height. Adjust these if the cup shape changes.

## Profile System

See `docs/CLAUDE_MD/RECIPE_PROFILES.md` for the Recipe Editor, D-Flow/A-Flow/Pressure/Flow editor types, frame generation details, and recipe parameters.

- **FrameBased mode**: Upload to machine, executes autonomously
- **DirectControl mode**: App sends setpoints frame-by-frame
- Formats: JSON (unified with de1app v2), TCL (de1app import)
- Tare happens when frame 0 starts (after machine preheat)
- **Stop limits**: Both `target_weight` and `target_volume` are checked independently during espresso — whichever target is reached first stops the shot (matching de1app). A value of 0 means that limit is disabled. There is no `stop_at_type` selector.
- **Profile comparison**: Run `python scripts/compare_profiles.py [de1app_profiles_dir]` to compare built-in profiles against de1app TCL sources. Checks frame data, exit conditions, and classifies differences by severity.
- **Profile sync**: Run `python scripts/sync_profiles.py [de1app_profiles_dir]` to update built-in profile JSON files to match de1app TCL sources. **Modifies `resources/profiles/` in-place** — review changes before committing.
- **Profile import test**: Run `python scripts/test_profile_import.py [de1app_profiles_dir]` to build a C++ test app that loads all profiles through `Profile::fromJson()` and `Profile::loadFromTclString()` and verifies parsed weight/volume values match de1app.

### JSON Format (unified with de1app)

Decenza and de1app share the same JSON profile format. The writer (`toJson()`) outputs de1app v2 format with JSON numbers (not strings): nested `exit`/`limiter` objects, `version`, `legacy_profile_type`, `notes`, `number_of_preinfuse_frames`. The reader (`fromJson()`) accepts both de1app nested and legacy Decenza flat fields (for old profiles in shot history), with `jsonToDouble()` handling de1app's string-encoded numbers.

- **Writer keys**: `notes` (not `profile_notes`), `legacy_profile_type` (not `profile_type`), `number_of_preinfuse_frames` (not `preinfuse_frame_count`), nested `exit`/`limiter`/`weight` (no flat exit fields)
- **Reader fallbacks**: Accepts old flat fields (`exit_if`, `exit_type`, `exit_pressure_over`, `max_flow_or_pressure`, `profile_notes`, `profile_type`, `preinfuse_frame_count`) for backward compat with shot history snapshots
- **Decenza extensions**: `is_recipe_mode`, `recipe`, `mode`, `has_recommended_dose`, `temperature_presets`, simple profile params — de1app ignores these
- **No separate reader**: There is no `loadFromDE1AppJson()` — `fromJson()` handles all variants

## Data Migration (Device-to-Device Transfer)

See `docs/CLAUDE_MD/DATA_MIGRATION.md` for architecture, REST endpoints, and import options.

## Visualizer Integration

See `docs/CLAUDE_MD/VISUALIZER.md` for DYE metadata, profile import, visualizer format, ProfileSaveHelper, and filename generation.

## BLE Protocol Notes

See `docs/CLAUDE_MD/BLE_PROTOCOL.md` for UUIDs, retry mechanism, shot debug logging, battery management, and steam heater control.

## Platforms

- Desktop: Windows, macOS, Linux
- Mobile: Android (API 28+), iOS (17.0+)
- Android needs Location permission for BLE scanning

## Windows Installer / Android Build / Tablet Troubleshooting

See `docs/CLAUDE_MD/PLATFORM_BUILD.md` for Windows Inno Setup, Android signing, and Decent tablet quirks.

## Publishing Releases

See `docs/CLAUDE_MD/CI_CD.md` for the full release process, updating pre-releases, auto-update system, and release notes guidelines.

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

- **Display version** (versionName): Set in `CMakeLists.txt` line 2: `project(Decenza VERSION x.y.z)`
- **Version code** (versionCode): Stored in `versioncode.txt`. Does **not** auto-increment during local builds. CI workflows bump it on tag push, and the Android workflow commits the new value back to `main`.
- **version.h**: Auto-generated from `src/version.h.in` with VERSION_STRING macro
- **AndroidManifest.xml**: Auto-generated from `android/AndroidManifest.xml.in` by CMake at build time (gitignored). Both `versionCode` and `versionName` come from `versioncode.txt` and `CMakeLists.txt` respectively.
- **installer/version.iss**: Auto-generated from `installer/version.iss.in` by CMake at build time (gitignored).
- To release a new version: Update VERSION in CMakeLists.txt, commit, then follow the "Publishing Releases" process (create release first, then push tag)

## Git Workflow

- **Version codes are managed by CI** — local builds use `versioncode.txt` as-is (no auto-increment). All 6 CI workflows bump the code identically on tag push. The Android workflow commits the bumped value back to `main`.
- You do **not** need to manually commit version code files — only `versioncode.txt` is tracked. `android/AndroidManifest.xml` and `installer/version.iss` are generated from `.in` templates by CMake at build time and are gitignored.

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
- `qml/components/AccessibleMouseArea.qml` - Alternative touch handler with announce-first TalkBack behavior
- `qml/components/AccessibleButton.qml` - Button with required accessibleName
- `qml/components/AccessibleLabel.qml` - Tap-to-announce text

### Accessibility Anti-patterns (Do NOT Use)

1. **Parent-ignored / child-accessible**: Never set `Accessible.ignored: true` on a parent and put `Accessible.role` on a child occupying the same bounds. TalkBack can't reliably route activation to the child. Put accessibility properties on the interactive element itself. **Exception**: `AccessibleMouseArea` with `accessibleItem` is designed for this pattern — the parent Rectangle has `Accessible.ignored: true` and `AccessibleMouseArea` carries the accessibility properties. This is the established pattern for custom-styled buttons throughout the codebase.
2. **Popup for selection lists**: Never use `Popup` for lists users must navigate. TalkBack can't trap focus inside Qt `Popup` elements. Use `Dialog { modal: true }` with `AccessibleButton` delegates instead.
3. **Overlapping accessible elements**: Never position accessible buttons inside another accessible element's bounds (e.g., buttons inside a TextField's padding area). TalkBack will only discover one element. Use conditional layout to show buttons in separate bounds when accessibility is enabled.

### Rules for New Components

1. Every interactive element must have `Accessible.role`, `Accessible.name`, `Accessible.focusable`, `Accessible.onPressAction` **on itself** (not on a child). Exception: `AccessibleMouseArea` with `accessibleItem` carries accessibility for its parent — see anti-pattern #1 exception above.
2. Every interactive element with secondary actions (long-press, double-tap) **must** also set `Accessible.description` (or `accessibleDescription` on `AccessibleTapHandler`) describing those actions. Format: `"Double-tap or long-press to <action>."` This is how TalkBack/VoiceOver announces hints — without it, blind users cannot discover secondary workflows.
3. Never use `Popup` for selection lists — use `Dialog` with `AccessibleButton` delegates
4. Never overlap accessible elements — separate bounds or use conditional layout (`_accessibilityMode` pattern)
5. Test with TalkBack: double-tap to activate, swipe to navigate

### Rules for Modifying Existing Components

When touching existing code, **fix pre-existing accessibility violations on the lines you modify** — do not dismiss them as "pre-existing". If you add properties to a Text element inside an `Accessible.name`-bearing parent and that Text is missing `Accessible.ignored: true`, add it. Accessibility issues compound over time and each modification is an opportunity to fix them.

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
- `qml/pages/RecipeEditorPage.qml`
- `qml/pages/ProfileEditorPage.qml`

**Testing:** Enable TalkBack on Android, then:
1. Swipe right repeatedly - should move through all controls in logical order
2. No elements should be skipped
3. Focus should not jump unexpectedly
4. First element on each page should receive focus when page opens
