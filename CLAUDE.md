<!-- OPENSPEC:START -->
# OpenSpec Instructions

Instructions for AI coding assistants using OpenSpec for spec-driven development.

## TL;DR Quick Checklist

- Search existing work: `openspec spec list --long`, `openspec list` (use `rg` only for full-text search)
- Decide scope: new capability vs modify existing capability
- Pick a unique `change-id`: kebab-case, verb-led (`add-`, `update-`, `remove-`, `refactor-`)
- Scaffold: `proposal.md`, `tasks.md`, `design.md` (only if needed), and delta specs per affected capability
- Write deltas: use `## ADDED|MODIFIED|REMOVED|RENAMED Requirements`; include at least one `#### Scenario:` per requirement
- Validate: `openspec validate [change-id] --strict --no-interactive` and fix issues
- Request approval: Do not start implementation until proposal is approved

## When to Create a Proposal

**Create a proposal for:**
- New features or capabilities
- Breaking changes (API, schema)
- Architecture or pattern changes
- Security or significant performance changes

**Skip proposal for:**
- Bug fixes (restoring intended behavior)
- Typos, formatting, comments
- Dependency updates (non-breaking)
- Tests for existing behavior

## Three-Stage Workflow

### Stage 1: Creating Changes

1. Review `openspec/project.md`, `openspec list`, and `openspec list --specs`
2. Choose a unique verb-led `change-id`, scaffold `proposal.md`, `tasks.md`, optional `design.md`, and spec deltas under `openspec/changes/<id>/`
3. Write spec deltas using `## ADDED|MODIFIED|REMOVED Requirements` with at least one `#### Scenario:` per requirement
4. Run `openspec validate <id> --strict --no-interactive` and resolve issues

### Stage 2: Implementing Changes

1. Read `proposal.md` — understand what's being built
2. Read `design.md` (if exists) — review technical decisions
3. Read `tasks.md` — get implementation checklist
4. Implement tasks sequentially; confirm every item is done before updating statuses
5. Set every task to `- [x]` after all work is complete
6. **Do not start implementation until the proposal is reviewed and approved**

### Stage 3: Archiving Changes

After deployment, create a separate PR to:
- Move `changes/[name]/` → `changes/archive/YYYY-MM-DD-[name]/`
- Update `specs/` if capabilities changed
- Use `openspec archive <change-id> --skip-specs --yes` for tooling-only changes
- Run `openspec validate --strict --no-interactive` to confirm archived change passes

## Directory Structure

```
openspec/
├── project.md              # Project conventions
├── specs/                  # Current truth — what IS built
│   └── [capability]/
│       ├── spec.md         # Requirements and scenarios
│       └── design.md       # Technical patterns
├── changes/                # Proposals — what SHOULD change
│   ├── [change-name]/
│   │   ├── proposal.md     # Why, what, impact
│   │   ├── tasks.md        # Implementation checklist
│   │   ├── design.md       # Technical decisions (optional)
│   │   └── specs/[capability]/spec.md  # ADDED/MODIFIED/REMOVED deltas
│   └── archive/            # Completed changes
```

## Proposal Structure

**proposal.md:**
```markdown
# Change: [Brief description]
## Why
[1-2 sentences on problem/opportunity]
## What Changes
- [Bullet list; mark breaking changes with **BREAKING**]
## Impact
- Affected specs: [capabilities]
- Affected code: [key files/systems]
```

**spec delta format:**
```markdown
## ADDED Requirements
### Requirement: New Feature
The system SHALL provide...

#### Scenario: Success case
- **WHEN** user performs action
- **THEN** expected result

## MODIFIED Requirements
### Requirement: Existing Feature
[Full updated requirement — paste entire block from existing spec and edit]
```

**Critical**: Every requirement needs `#### Scenario:` (4 hashtags). MODIFIED must include the full existing requirement text, not just the changed parts.

**design.md** — create only when: cross-cutting change, new external dependency, significant data model change, security/migration complexity, or ambiguity requiring technical decisions before coding.

## Key CLI Commands

```bash
openspec list                    # Active changes
openspec list --specs            # Existing capabilities
openspec show [item]             # View change or spec details
openspec validate [item] --strict --no-interactive  # Validate
openspec archive <change-id> --yes  # Archive after deployment
```

## Delta Operations

- `## ADDED Requirements` — new capabilities
- `## MODIFIED Requirements` — changed behavior (always include full requirement text)
- `## REMOVED Requirements` — deprecated features (include reason + migration)
- `## RENAMED Requirements` — name-only changes; use with MODIFIED if behavior also changes

<!-- OPENSPEC:END -->

# Decenza

Qt/C++ cross-platform controller for the Decent Espresso DE1 machine with BLE connectivity.

## Reference Documents

Detailed documentation lives in `docs/CLAUDE_MD/`. Read these when working in the relevant domain:

| Document | When to read |
|----------|-------------|
| `CI_CD.md` | Release process, GitHub Actions workflows, version bumping |
| `RECIPE_PROFILES.md` | Recipe Editor, D-Flow/A-Flow/Pressure/Flow types, frame generation |
| `TESTING.md` | Test framework, mock strategy, adding new tests |
| `BLE_PROTOCOL.md` | BLE UUIDs, retry mechanism, shot debug logging, battery/steam control |
| `VISUALIZER.md` | DYE metadata, profile import/export, ProfileSaveHelper, filename generation |
| `DATA_MIGRATION.md` | Device-to-device transfer architecture and REST endpoints |
| `PLATFORM_BUILD.md` | Windows installer, Android signing, Decent tablet quirks |
| `STEAM_CALIBRATION.md` | Postmortem on the removed steam calibration feature — why it didn't work, what held up, future directions |
| `CUP_FILL_VIEW.md` | CupFillView layer stack, GPU shaders, updating cup images |
| `EMOJI_SYSTEM.md` | Twemoji SVG rendering, adding/switching emoji sets |
| `ACCESSIBILITY.md` | TalkBack/VoiceOver rules, focus order, anti-patterns, implementation plan |
| `AUTO_FLOW_CALIBRATION.md` | Auto flow calibration algorithm, batched median updates, windowing, convergence |

Also in `docs/`:
- `MCP_SERVER.md` — full MCP tool list, access levels, architecture
- `AI_ADVISOR.md` — AI dialing assistant design

## Development Environment

- **ADB path**: `/c/Users/Micro/AppData/Local/Android/Sdk/platform-tools/adb.exe`
- **Uninstall app**: `adb uninstall io.github.kulitorum.decenza_de1`
- **WiFi debugging**: `192.168.1.208:5555` (reconnect: `adb connect 192.168.1.208:5555`)
- **Qt version**: 6.10.3
- **Qt path**: `C:/Qt/6.10.3/msvc2022_64`
- **C++ standard**: C++17
- **de1app source**: `C:\code\de1app` (Windows) or `/Users/jeffreyh/Development/GitHub/de1app` (macOS) — original Tcl/Tk DE1 app for reference
- **IMPORTANT**: Use relative paths (e.g., `src/main.cpp`) instead of absolute paths (e.g., `C:\CODE\de1-qt\src\main.cpp`) to avoid "Error: UNKNOWN: unknown error, open" when editing files

## Command Line Build (for Claude sessions)

**IMPORTANT**: Don't build automatically - let the user build in Qt Creator, which is ~50x faster than command-line builds. Only use these commands if the user explicitly asks for a CLI build.

MSVC environment variables (INCLUDE, LIB) are set permanently. Use Visual Studio generator (Ninja not in PATH).

**Configure Release:**
```bash
rm -rf build/Release && mkdir -p build/Release && cd build/Release && cmake ../.. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.10.3/msvc2022_64"
```

**Build Release (parallel):**
```bash
cd build/Release && unset CMAKE_BUILD_PARALLEL_LEVEL && MSYS_NO_PATHCONV=1 cmake --build . --config Release -- /m
```

**Configure Debug:**
```bash
rm -rf build/Debug && mkdir -p build/Debug && cd build/Debug && cmake ../.. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.10.3/msvc2022_64" -DCMAKE_BUILD_TYPE=Debug
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
rm -rf build/Qt_6_10_3_for_iOS && mkdir -p build/Qt_6_10_3_for_iOS && cd build/Qt_6_10_3_for_iOS && /Users/mic/Qt/6.10.3/ios/bin/qt-cmake ../.. -G Xcode
```

**Configure macOS (generates Xcode project):**
```bash
rm -rf build/Qt_6_10_3_for_macOS && mkdir -p build/Qt_6_10_3_for_macOS && cd build/Qt_6_10_3_for_macOS && /Users/mic/Qt/6.10.3/macos/bin/qt-cmake ../.. -G Xcode
```

**Open in Xcode:**
```bash
open build/Qt_6_10_3_for_iOS/Decenza.xcodeproj
# or
open build/Qt_6_10_3_for_macOS/Decenza.xcodeproj
```

Then in Xcode: Product → Archive for App Store submission.

## CI/CD (GitHub Actions)

See `docs/CLAUDE_MD/CI_CD.md` for workflows, secrets, quick commands, and platform notes.

## Project Structure

```
src/
├── ai/                     # AI assistant integration
│   ├── aimanager.*         # Provider config, test connection, conversation lifecycle
│   ├── aiprovider.*        # HTTP calls to OpenAI/Anthropic/Ollama/OpenRouter
│   ├── aiconversation.*    # Multi-turn conversation state + history persistence
│   ├── shotanalysis.*      # Shot analysis prompts and structured response parsing
│   └── shotsummarizer.*    # Summarise shot data into AI-readable text
├── ble/                    # Bluetooth LE layer
│   ├── de1device.*         # DE1 machine protocol
│   ├── blemanager.*        # Device discovery
│   ├── scaledevice.*       # Abstract scale interface
│   ├── protocol/           # BLE UUIDs, binary codec
│   ├── scales/             # Scale implementations (13 types)
│   └── transport/          # BLE transport abstraction
├── controllers/
│   ├── maincontroller.*    # App logic, profiles, shot processing
│   ├── directcontroller.*  # Direct frame control mode
│   ├── profilemanager.*    # Profile CRUD, activation, built-in management
│   └── shottimingcontroller.* # Shot timing, tare management, weight processing
├── core/
│   ├── settings.*          # QSettings persistence
│   ├── batterymanager.*    # Smart charging control
│   ├── accessibilitymanager.* # TTS announcements, tick sounds, a11y settings
│   ├── asynclogger.*       # Background-thread file logger
│   ├── autowakemanager.*   # Scheduled DE1 auto-wake (time-based)
│   ├── crashhandler.*      # Signal handler → writes crash log on crash
│   ├── databasebackupmanager.* # Daily automatic backup of shots/settings/profiles
│   ├── datamigrationclient.*   # Device-to-device data transfer (REST client)
│   ├── dbutils.h           # withTempDb() helper for background-thread DB connections
│   ├── documentformatter.* # Formats shot/profile data as Markdown for AI context
│   ├── grinderaliases.h    # Grinder brand/model normalisation table
│   ├── memorymonitor.*     # RSS/heap tracking, low-memory warnings
│   ├── profilestorage.*    # Low-level profile file I/O (JSON read/write, enumeration)
│   ├── settingsserializer.* # Export/import settings as JSON
│   ├── translationmanager.* # Runtime i18n — loads locale JSON, exposes translate()
│   ├── updatechecker.*     # GitHub Releases polling for app updates
│   └── widgetlibrary.*     # Local library for layout items, zones, layouts, themes
├── history/                # Shot history storage and queries
│   ├── shothistorystorage.* # SQLite CRUD, background-thread query helpers
│   ├── shotimporter.*      # Import shots from JSON files / migration
│   ├── shotdebuglogger.*   # Per-shot BLE frame debug log writer
│   └── shotfileparser.*    # Parse legacy shot file formats
├── machine/
│   ├── machinestate.*      # Phase tracking, stop-at-weight, stop-at-volume
│   ├── steamcalibrator.*   # Steam flow calibration procedure
│   ├── steamhealthtracker.* # Tracks steam health metrics over time
│   └── weightprocessor.*   # Centralised weight filtering and smoothing
├── mcp/                    # Model Context Protocol server (AI agent tools)
│   ├── mcpserver.*         # WebSocket server, session management
│   ├── mcpsession.h        # Per-connection session state
│   ├── mcptoolregistry.h   # Tool registration and dispatch
│   ├── mcpresourceregistry.h # Resource registration
│   ├── mcptools_shots.*    # Shot history tools
│   ├── mcptools_profiles.* # Profile read/write tools
│   ├── mcptools_machine.*  # Machine state tools
│   ├── mcptools_control.*  # Shot control tools
│   ├── mcptools_devices.*  # BLE device tools
│   ├── mcptools_scale.*    # Scale tools
│   ├── mcptools_settings.* # Settings tools
│   ├── mcptools_dialing.*  # Dialing assistant tools
│   ├── mcptools_write.*    # Write/update tools
│   └── mcptools_agent.*    # Agent coordination tools
├── models/
│   ├── shotdatamodel.*     # Shot data for graphing (live + history)
│   ├── shotcomparisonmodel.* # Side-by-side shot comparison data
│   ├── flowcalibrationmodel.* # Flow calibration data model
│   └── steamdatamodel.*    # Steam session data for graphing
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
│   ├── visualizerimporter.*    # Import profiles from visualizer.coffee
│   ├── librarysharing.*    # Community profile library (browse/download/upload)
│   ├── mqttclient.*        # MQTT connectivity for remote monitoring
│   ├── relayclient.*       # WebSocket relay for remote DE1 control
│   ├── crashreporter.*     # Crash report submission to backend
│   ├── shotreporter.*      # Automatic shot reporting / webhooks
│   ├── locationprovider.*  # City + coordinates for shot metadata
│   ├── screencaptureservice.* # Screenshot capture for sharing
│   └── webdebuglogger.*    # Web-accessible debug log endpoint
├── profile/
│   ├── profile.*           # Profile container, JSON/TCL formats
│   ├── profileframe.*      # Single extraction step
│   ├── profilesavehelper.* # Shared save/compare/deduplicate logic for importers
│   ├── profileconverter.*  # Convert between profile formats
│   ├── profileimporter.*   # Import profiles from files / visualizer
│   ├── recipeanalyzer.*    # Extract RecipeParams from frame-based profiles
│   ├── recipegenerator.*   # Generate frame profiles from RecipeParams
│   └── recipeparams.*      # Typed recipe parameter container
├── rendering/              # Custom rendering (shot graphs, etc.)
├── screensaver/            # Screensaver implementation
├── simulator/              # DE1 machine simulator
├── usb/                    # USB scale connectivity (Decent USB scale, serial)
├── weather/                # Weather data for shot metadata
└── main.cpp                # Entry point, object wiring

qml/
├── pages/                  # Full-screen pages (EspressoPage, ShotDetailPage, etc.)
│   └── settings/           # Settings tab pages
├── components/             # Reusable components (ShotGraph, StatusBar, etc.)
├── simulator/              # Simulator UI (GHCSimulatorWindow)
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
                          → ShotTimingController (timing, tare, weight)
ScaleDevice     → signals → MachineState (stop-at-weight)
                          → MainController (graph data)
MachineState    → signals → MainController → QML page navigation
AIManager       → signals → QML AI panels (conversation responses)
MqttClient      → signals → MainController (remote monitoring)
RelayClient     ←→ DE1Device (remote control over WebSocket relay)
MCPServer       → tool calls → MainController / ProfileManager / ShotHistoryStorage
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

### AI & MCP
- **AIManager**: Manages provider config and conversation lifecycle; exposes `conversation` to QML
- **AIConversation**: Multi-turn conversation with history; used by AI panels across the app
- **MCPServer**: WebSocket server exposing machine control and data as MCP tools for external AI agents
- **ShotAnalysis / ShotSummarizer**: Format shot data as text context for AI prompts

### Profile Pipeline
```
TCL/JSON file → ProfileImporter → ProfileConverter → Profile (in memory)
RecipeParams  → RecipeGenerator → Profile frames → DE1 upload
Profile frames ← RecipeAnalyzer ← existing frame-based profile (reverse)
ProfileManager: CRUD, activation, built-in management, ProfileStorage I/O
```

## Conventions

### Design Principles
- **Never use timers as guards/workarounds.** Timers are fragile heuristics that break on slow devices and hide the real problem. Use event-based flags and conditions instead. For example, "suppress X until Y has happened" should be a boolean cleared by the Y event, not a timer. Only use timers for genuinely periodic tasks (polling, animation, heartbeats) and **UI auto-dismiss** (toasts/banners that hide after N seconds). Everything else — including debounce — should use event-based flags.
- **Never run database or disk I/O on the main thread.** Use `QThread::create()` with a `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` callback to run queries on a background thread and deliver results back to the main thread. See `ShotHistoryStorage::requestShot()` for the canonical pattern. For database connections inside background threads, always use the `withTempDb()` helper from `src/core/dbutils.h` — it handles unique connection naming, `busy_timeout`, `foreign_keys` pragmas, and cleanup. Never manually call `QSqlDatabase::addDatabase()`/`removeDatabase()` when `withTempDb` can be used instead.

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
- **Use `??` not `||` for numeric defaults where 0 is valid.** JavaScript `||` treats `0` as falsy, so `value || 0.6` returns `0.6` when `value` is `0`. Use `value ?? 0.6` (nullish coalescing) which only falls back for `null`/`undefined`. Only use `||` when `0` genuinely means "no data" (e.g., unrated enjoyment, unmeasured TDS).

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

**IME last-word drop on mobile**: On Android/iOS virtual keyboards, the last typed word is held in a composing/pre-edit state and is NOT reflected in `TextField.text` until committed. When a button's `onClicked` reads a text field's `.text` directly (to send, save, or pass to a C++ method), always call `Qt.inputMethod.commit()` first — otherwise the last word is silently dropped. This is a no-op on desktop so it is safe to always include.
```qml
// BAD - last word may be missing on mobile
onClicked: {
    doSomething(myField.text)
}

// GOOD - commit pending IME composition first
onClicked: {
    Qt.inputMethod.commit()
    doSomething(myField.text)
}
```
This applies to every button/action that reads and immediately uses text input — save dialogs, send buttons, preset name dialogs, TOTP code fields, search/import fields, etc. For `doSave()` helper functions called from both buttons and `Keys.onReturnPressed`, put the commit at the top of the function.

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

**Accessibility on interactive elements**: See `docs/CLAUDE_MD/ACCESSIBILITY.md` for the full rules, component table, common mistakes checklist, focus-order requirements, and anti-patterns. The short version: every interactive element needs `Accessible.role`, `Accessible.name`, `Accessible.focusable: true`, and `Accessible.onPressAction`. Prefer `AccessibleButton` or `AccessibleMouseArea` over raw `Rectangle+MouseArea`. (`activeFocusOnTab: true` is keyboard-only and low priority for this tablet app — see ACCESSIBILITY.md.)

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

### MCP Tool Responses (src/mcp/)

MCP tool responses are consumed by LLMs which cannot reliably interpret raw numbers. Follow these conventions:

- **Never return Unix timestamps.** Use ISO 8601 with timezone: `dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODate)` → `"2026-03-21T11:20:41-06:00"`
- **Include units in field names.** `doseG` (grams), `pressureBar`, `temperatureC`, `flowMlPerSec`, `durationSec`, `weightG`, `targetVolumeMl`. An AI seeing `"pressure": 9.0` doesn't know bar vs PSI vs kPa.
- **Include scale in field names for bounded values.** `enjoyment0to100` instead of `enjoyment`.
- **Use human-readable strings for enums.** Machine phases, editor types, and states as strings (`"idle"`, `"pouring"`), not numeric codes.

See `docs/MCP_SERVER.md` for the full data conventions section.

## Emoji System

See `docs/CLAUDE_MD/EMOJI_SYSTEM.md`. Key rule: always use `Image { source: Theme.emojiToImage(value) }` — never `Text` for emojis.

## Cup Fill View

See `docs/CLAUDE_MD/CUP_FILL_VIEW.md` for layer stack, GPU shaders, and how to update cup images.

## Profile System

See `docs/CLAUDE_MD/RECIPE_PROFILES.md` for the Recipe Editor, D-Flow/A-Flow/Pressure/Flow editor types, frame generation details, and recipe parameters.

- **FrameBased mode**: Upload to machine, executes autonomously
- **DirectControl mode**: App sends setpoints frame-by-frame
- Formats: JSON (unified with de1app v2), TCL (de1app import)
- Tare happens when frame 0 starts (after machine preheat)
- **Stop limits**: `target_weight` (SAW) and `target_volume` (SAV) are checked independently — whichever triggers first stops the shot. A value of 0 means disabled. Volume bucketing uses **DE1 substate** splitting (matching de1app): flow during Preinfusion substate → preinfusion volume, flow during Pouring substate → pour volume. Other substates (heating, stabilising) are excluded. SAV uses a raw `pourVolume >= target` comparison with no lag compensation (matching de1app). SAW ignores the first 5 seconds of extraction and only fires after the current frame reaches `number_of_preinfuse_frames` (matching de1app). For **basic profiles** (`settings_2a`/`settings_2b`) with a BLE scale *configured* (not just connected), SAV is skipped (matching de1app's `skip_sav_check` / `expecting_present`). The DE1 firmware also has a `TargetEspressoVol` safety limit (200 ml, matching de1app's `espresso_typical_volume`) sent via `setShotSettings`.
- **Profile comparison/sync**: Use the `profile_sync` C++ tool (built with the main project, no extra flags). `profile_sync <de1app_profiles_dir> <builtin_profiles_dir>` compares TCL sources against built-in JSONs. Add `--sync` to overwrite stale JSONs and create missing ones (**modifies `resources/profiles/` in-place** — review changes before committing).
- **Profile import test**: Run `ctest -R tst_tclimport` (requires `-DBUILD_TESTS=ON`). The `compareWithBuiltin` test loads all TCL files from `tests/data/de1app_profiles/` through the C++ parser and verifies they match their built-in JSON counterparts field-by-field.

### JSON Format (unified with de1app)

Decenza and de1app share the same JSON profile format. The writer (`toJson()`) outputs de1app v2 format with JSON numbers (not strings): nested `exit`/`limiter` objects, `version`, `legacy_profile_type`, `notes`, `number_of_preinfuse_frames`. The reader (`fromJson()`) accepts both de1app nested and legacy Decenza flat fields (for old profiles in shot history), with `jsonToDouble()` handling de1app's string-encoded numbers.

- **Writer keys**: `notes` (not `profile_notes`), `legacy_profile_type` (not `profile_type`), `number_of_preinfuse_frames` (not `preinfuse_frame_count`), nested `exit`/`limiter`/`weight` (no flat exit fields)
- **Reader fallbacks**: Accepts old flat fields (`exit_if`, `exit_type`, `exit_pressure_over`, `max_flow_or_pressure`, `profile_notes`, `profile_type`, `preinfuse_frame_count`) for backward compat with shot history snapshots
- **Decenza extensions**: `recipe`, `mode`, `has_recommended_dose`, `temperature_presets`, simple profile params — de1app ignores these. (`is_recipe_mode` was removed; editor type is now derived at runtime from title + `legacy_profile_type`)
- **No separate reader**: There is no `loadFromDE1AppJson()` — `fromJson()` handles all variants

## Data Migration (Device-to-Device Transfer)

See `docs/CLAUDE_MD/DATA_MIGRATION.md` for architecture, REST endpoints, and import options.

## Visualizer Integration

See `docs/CLAUDE_MD/VISUALIZER.md` for DYE metadata, profile import, visualizer format, ProfileSaveHelper, and filename generation.

## Unit Testing

See `docs/CLAUDE_MD/TESTING.md` for the test framework, architecture, mock strategy, and how to add new tests. Tests use Qt Test (QTest) with `friend class` access behind `#ifdef DECENZA_TESTING`. Build with `-DBUILD_TESTS=ON`.

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
- **Completion flow**: When operations end, show 1.5-second completion overlay, then navigate to IdlePage

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

See `docs/CLAUDE_MD/ACCESSIBILITY.md` for the full reference: component rules, focus-order requirements, anti-patterns, common mistakes checklist, and the page-by-page implementation plan for [Kulitorum/Decenza#736](https://github.com/Kulitorum/Decenza/issues/736).

**Key rule for modifying existing components**: Fix pre-existing violations in any file you touch — do not dismiss them as "pre-existing". Issues compound over time and each change is an opportunity to fix them.
