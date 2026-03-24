# Tasks: Extract ProfileManager from MainController

## Phase 1: Mechanical extraction
- [x] Create `src/controllers/profilemanager.h` with method declarations moved from MainController
- [x] Create `src/controllers/profilemanager.cpp` with implementations moved from MainController
- [x] Add `ProfileManager*` member to MainController, create in constructor
- [x] Replace moved method bodies in MainController with forwarding calls to ProfileManager
- [x] Update `CMakeLists.txt` to compile `profilemanager.cpp`
- [x] Build and verify all platforms (Qt Creator)
- [x] Run existing test suite — all tests pass unchanged

## Phase 2: MCP tool migration
- [x] Change `registerProfileTools()` to take `ProfileManager*`
- [x] Change `registerWriteTools()` profile paths to use `ProfileManager*`
- [x] Update `McpServer` to hold and pass `ProfileManager*`
- [x] Write `tst_mcptools.cpp` with ProfileManager + MockTransport fixture
- [x] Verify: `profiles_edit_params` triggers BLE upload (the PR #561 test)
- [x] Verify: `settings_set` temperature/weight triggers BLE upload
- [x] Verify: all sync profile tools return correct responses
- [x] Remove MainController forwarding for methods only used by MCP

## Phase 3: QML migration + stub removal
- [x] Register `ProfileManager` as QML context property in `main.cpp`
- [x] Update all 21 QML files to call `ProfileManager` directly (not just 4 editor pages)
- [x] Rewire C++ signal consumers (McpServer, simulator, MQTT) to ProfileManager
- [x] Rewire MCP resources/tools (mcpresources, mcptools_machine) to ProfileManager
- [x] Rewire profile importers (profilesavehelper, profileimporter, visualizerimporter) via profileManager()
- [x] Remove all profile Q_PROPERTY declarations from MainController
- [x] Remove all profile forwarding methods from MainController
- [x] Remove profile signal forwarding connections from MainController
- [x] Remove profile signals (currentProfileChanged, etc.) from MainController
- [x] Build and verify — compiles and runs
