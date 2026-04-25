# Proposal: Extract ProfileManager from MainController

## Change ID
`refactor-extract-profile-manager`

## Problem

`MainController` is a 4,100-line god object with 77 profile-related methods mixed with shot lifecycle, BLE management, AI integration, network, and history code. This makes it impossible to unit test profile/MCP functionality in isolation:

- **PR #561 regression**: removing `uploadCurrentProfile()` from `uploadProfile()` broke 4 MCP callers silently. Code review caught it, not tests.
- **Phase 10 (MCP tests) blocked**: compiling `MainController` in a test requires MQTT, OpenSSL, Qt Positioning, Qt CorePrivate, location services, shot server, crash handler, AI manager — essentially the entire app.
- **Every future test** that touches profiles, recipes, or BLE uploads hits the same wall.

## Proposed Solution

Extract profile management into a new `ProfileManager` class that owns:
1. Profile CRUD (load, save, delete, list, create)
2. Profile editing (upload params, recipe params, frame operations)
3. BLE upload coordination (`uploadCurrentProfile()`)
4. Profile state (current profile, modified flag, editor type)

`MainController` retains shot lifecycle, phase handling, QML wiring, and delegates profile operations to `ProfileManager`.

## Dependencies (ProfileManager only)

| Dependency | Why |
|---|---|
| `Profile` | Core data structure |
| `Settings` | Temperature override, current profile name, favorites |
| `DE1Device` | BLE upload (`uploadProfile()`, `setShotSettings()`) |
| `MachineState` | SAW/SAV target sync, active phase guard |
| `ProfileStorage` | Android SAF file access |

**NOT needed**: MQTT, location, shot server, OpenSSL, AI, history, shot data model, network, crash handler, visualizer. This is the key win — `ProfileManager` compiles with ~5 dependencies instead of ~25.

## Method Migration

### Move to ProfileManager (~40 methods, ~1,500 lines)

**Profile CRUD:**
- `loadProfile()`, `loadProfileFromJson()`, `refreshProfiles()`
- `saveProfile()`, `saveProfileAs()`, `deleteProfile()`
- `profileExists()`, `findProfileByTitle()`, `getProfileByFilename()`
- `allBuiltInProfileNames()`, `allProfileTitles()`, `profileCount()`

**Profile editing:**
- `uploadProfile()` — update in-memory state (no BLE)
- `uploadRecipeProfile()` — update recipe + regenerate frames (no BLE)
- `uploadCurrentProfile()` — BLE upload (the single upload entry point)
- `getOrConvertRecipeParams()`, `convertCurrentProfileToAdvanced()`
- `createNewRecipe()`, `createNewAFlowRecipe()`, `createNewPressureProfile()`, `createNewFlowProfile()`, `createNewBlankProfile()`

**Frame operations (advanced editor):**
- `addFrame()`, `deleteFrame()`, `moveFrameUp()`, `moveFrameDown()`, `duplicateFrame()`
- `setFrameProperty()`, `setGlobalTemperature()`

**Profile state:**
- `currentProfileName()`, `baseProfileName()`, `isProfileModified()`, `markProfileClean()`
- `currentEditorType()`, `isCurrentProfileRecipe()`
- `getCurrentProfile()`, `profileTargetWeight()`, `profileTargetTemperature()`

**Favorites:**
- `favoriteProfileNames()`, `addFavoriteProfile()`, `removeFavoriteProfile()`, etc.

### Stays in MainController (~2,600 lines)

- Shot lifecycle (start/stop/phase tracking)
- ShotDataModel wiring (graph updates)
- Shot saving (history, visualizer upload)
- BLE device event handling
- Scale integration
- QML property forwarding (delegates to ProfileManager)
- AI integration, MQTT, location, crash handler

## MCP Tool Changes

MCP tools currently capture `MainController*`. After refactor:

```cpp
// Before (PR #561 pattern):
mainController->uploadProfile(profileData);
mainController->uploadCurrentProfile();

// After:
profileManager->uploadProfile(profileData);
profileManager->uploadCurrentProfile();
```

MCP registration functions change signature:
```cpp
// Before:
void registerProfileTools(McpToolRegistry* registry, MainController* mainController);

// After:
void registerProfileTools(McpToolRegistry* registry, ProfileManager* profileManager);
```

## QML Changes

QML editors currently call `MainController.uploadProfile(profile)`. Options:

**Option A (minimal QML changes):** `MainController` exposes `ProfileManager` as a QML property and forwards calls. QML calls `MainController.profileManager.uploadProfile(profile)` or `MainController` keeps thin forwarding slots.

**Option B (clean but more QML changes):** Register `ProfileManager` as a QML context property. QML calls `ProfileManager.uploadProfile(profile)` directly.

Recommend **Option A** for Phase 1 (zero QML changes — MainController forwards), then Option B later.

## Test Payoff

After extraction, `ProfileManager` compiles with:
```cmake
add_decenza_test(tst_profilemanager
    tst_profilemanager.cpp
    ${PROFILE_SOURCES}
    ${CODEC_SOURCES}
    ${CORE_SOURCES}
    src/controllers/profilemanager.cpp
    src/ble/de1device.cpp
    src/machine/machinestate.cpp
    mocks/MockTransport.h
)
```

No MQTT, no OpenSSL, no Qt Positioning, no network. MCP tests become:
```cmake
add_decenza_test(tst_mcptools
    tst_mcptools.cpp
    src/mcp/mcptools_profiles.cpp
    src/controllers/profilemanager.cpp
    ...same lightweight deps...
)
```

## Phases

### Phase 1: Extract ProfileManager (mechanical move)
- Create `src/controllers/profilemanager.h/.cpp`
- Move methods listed above from MainController
- MainController holds `ProfileManager*` and forwards QML calls
- All existing QML and MCP code unchanged (calls go through MainController forwarding)
- **Verify**: app builds and runs identically

### Phase 2: Update MCP tools
- Change MCP registration to take `ProfileManager*` instead of `MainController*`
- Remove MainController forwarding for MCP-only methods
- **Verify**: MCP tools work, write tst_mcptools against ProfileManager

### Phase 3: Update QML editors (optional, lower priority)
- Register ProfileManager as QML context property
- Update editor pages to call ProfileManager directly
- Remove remaining MainController forwarding stubs

## Risk

- **Low**: Phase 1 is a mechanical move with no behavioral changes
- **Medium**: Phase 2 changes MCP tool signatures but the tool logic is identical
- **Low**: Phase 3 is optional and can be deferred indefinitely
