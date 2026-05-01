# Project Structure

Top-level layout of the Decenza source tree. Use this as a map when locating subsystems; the actual file list is authoritative — run `ls`/`tree` if you suspect drift.

## Top-level

```
src/        # C++ sources (see breakdown below)
qml/        # QML UI (pages/, components/, simulator/, Theme.qml)
resources/  # CoffeeCup/, emoji/, .qrc files, icons, fonts
shaders/    # crt.frag, cup_mask.frag, cup_lighten.frag
scripts/    # download_emoji.py and other helpers
tests/      # Qt Test sources (gated behind -DBUILD_TESTS=ON)
docs/       # Documentation (this directory: docs/CLAUDE_MD/)
openspec/   # OpenSpec change proposals and capability specs
android/    # AndroidManifest.xml.in, build.gradle
installer/  # Inno Setup script + setupvars template
```

## src/ subsystems

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
│   ├── settings.*          # QSettings persistence (façade — see SETTINGS.md)
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
│   ├── shotfileparser.*    # Parse legacy shot file formats
│   └── shotprojection.*    # Projected/derived shot fields
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
```

## qml/

```
qml/
├── pages/                  # Full-screen pages (EspressoPage, ShotDetailPage, etc.)
│   └── settings/           # Settings tab pages
├── components/             # Reusable components (ShotGraph, StatusBar, etc.)
├── simulator/              # Simulator UI (GHCSimulatorWindow)
└── Theme.qml               # Singleton styling (+ emojiToImage())
```

## resources/

```
resources/
├── CoffeeCup/              # 3D-rendered cup images for CupFillView
│   ├── BackGround.png      # Cup back, interior, handle (701x432)
│   ├── Mask.png            # Black = coffee area, white = no coffee
│   ├── Overlay.png         # Rim, front highlights (lighten blend)
│   └── FullRes.7z          # Source PSD archive
├── emoji/                  # ~750 Twemoji SVGs (CC-BY 4.0)
├── emoji.qrc               # Qt resource file for emoji SVGs
└── resources.qrc           # Icons, fonts, other assets
```

## shaders/

```
shaders/
├── crt.frag                # CRT/retro display effect
├── cup_mask.frag           # Masks coffee to cup interior (inverts Mask.png)
└── cup_lighten.frag        # Lighten (MAX) blend + brightness-to-alpha
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
