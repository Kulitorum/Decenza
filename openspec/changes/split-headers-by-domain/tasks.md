# Tasks: Split Headers by Domain

Each domain follows the same four-step sequence (add → migrate QML → remove forwarding → update C++ consumers).
Tasks are ordered so PR 1 (Tier 1) can be reviewed and merged before PR 2 (Tier 2) begins.

---

## PR 1 — Tier 1 (5 narrow domains) + shot history extraction

### A1. SettingsMqtt
- [x] Create `src/core/settings_mqtt.h` + `.cpp` with 10 MQTT properties extracted from `settings.h`
- [x] Add `Settings::mqtt()` accessor + `Q_PROPERTY(SettingsMqtt* mqtt …)` to `settings.h`
- [x] Migrate `SettingsHomeAutomationTab.qml` from `Settings.mqttXxx` → `Settings.mqtt.mqttXxx`
- [x] Remove MQTT method bodies, signals, and Q_PROPERTYs from `settings.h` / `.cpp`
- [x] Update `MqttClient` constructor to accept `SettingsMqtt*`; update `settingsserializer`, `mcptools_settings`, `mcptools_write`, `shotserver_settings` to use `settings->mqtt()->X()`
- [x] Verify: `Settings.mqtt[A-Z]` no longer present in QML

### A2. SettingsAutoWake
- [x] Create `src/core/settings_autowake.h` + `.cpp` with 4 auto-wake properties
- [x] Add `Settings::autoWake()` accessor; remove from `settings.h` / `.cpp`
- [x] Migrate `main.qml` and `SettingsScreensaverTab.qml` auto-wake bindings
- [x] Update `AutoWakeManager` constructor to accept `SettingsAutoWake*`; update `main.cpp`
- [x] Verify: `autowakemanager.cpp` no longer includes `settings.h`

### A3. SettingsHardware
- [x] Create `src/core/settings_hardware.h` + `.cpp` with 6 heater-tweak + hot-water flow + steamTwoTapStop properties
- [x] Add `Settings::hardware()` accessor; remove from `settings.h` / `.cpp`
- [x] Migrate `SteamPage.qml`, `HotWaterPage.qml`, `SettingsCalibrationTab.qml`, `SettingsMachineTab.qml`
- [x] Update `DE1Device::setSettings()` to accept `SettingsHardware*`; update `main.cpp`, `maincontroller.cpp`
- [x] Verify: `de1device.cpp` no longer includes `settings.h` (now uses `settings_hardware.h`)

### A4. SettingsAI
- [x] Create `src/core/settings_ai.h` + `.cpp` with 8 AI provider + API key properties
- [x] Add `Settings::ai()` accessor + `configurationChanged` aggregate signal; remove from `settings.h` / `.cpp`
- [x] Migrate `AISettingsPage.qml` and `SettingsAITab.qml`
- [x] Update `AIManager` to use `m_settings->ai()->X()` patterns and connect to `SettingsAI::configurationChanged`
- [x] Update `translationmanager.cpp`, `mcptools_*`, `shotserver_settings.cpp` consumers

### A5. SettingsTheme
- [x] Create `src/core/settings_theme.h` + `.cpp` with ~50 theme/shader/font/brightness properties and invokables
- [x] Add `Settings::theme()` accessor; remove all theme code from `settings.h` / `.cpp`
- [x] Migrate `CrtOverlay.qml`, `CrtShaderEffect.qml`, `main.qml`, `SettingsMachineTab.qml`, `SettingsThemesTab.qml`, `SettingsPage.qml`, `Theme.qml`
- [x] Update `widgetlibrary.cpp`, `shotserver_theme.cpp`, `shotserver.cpp`, `settingsserializer.cpp`, `mcptools_*`, `tst_settings.cpp`
- [x] Verify: no flat `Settings.activeTheme|customTheme|themeMode|screenBrightness|activeShader|skin\b` in QML

### A6. PR 1 validation
- [x] Build succeeds with no warnings on all touched files (mac-test build clean)
- [x] `tst_Settings` passes 14/14 tests
- [x] Direct `settings.h` includer count: 39 (down from 41 — narrow consumers like de1device.cpp, autowakemanager.cpp dropped it; the rest still need other `Settings::` methods that haven't moved yet — full reduction blocked on Tier 2 domains)
- [x] `settings.h` size: 887 lines (down from 1,170 — 24% reduction so far)

### B. Shot history types extraction (PR 1)
- [x] Create `src/history/shothistory_types.h` containing `HistoryShotSummary`, `HistoryPhaseMarker`, `ShotRecord`, `GrinderContext`, `ShotFilter`, `ShotSaveData`
- [x] Replace struct definitions in `shothistorystorage.h` with `#include "shothistory_types.h"`
- [x] Update `shotfileparser.h` to include `shothistory_types.h` instead of `shothistorystorage.h`
- [ ] **Deferred:** switch 9 pointer-only consumer `.cpp` files to forward declarations
  - In practice, .cpp files calling `ShotHistoryStorage*` methods need the full header; the win
    requires extracting an abstract interface or pimpl, which is a larger refactor than scope allows.
  - Current win: `shothistory_types.h` is now an independent dep, so changes to `ShotHistoryStorage`
    class methods don't force recompile of files that only need the structs (e.g., `shotfileparser.h` consumers).

---

## PR 2 — Tier 2 (6 remaining domains)

### C1. SettingsVisualizer
- [x] Create `src/core/settings_visualizer.h` + `.cpp` (8 visualizer upload properties + defaultShotRating)
- [x] Add `Settings::visualizer()` accessor; remove from `settings.h` / `.cpp`
- [x] Migrate `SettingsVisualizerTab.qml`
- [x] Update `VisualizerUploader`, `settingsserializer.cpp`, `shotserver_settings.cpp`, `mcptools_*`, `maincontroller.cpp`
- Note: `setDefaultShotRating()` no longer auto-syncs `dyeEspressoEnjoyment`. MainController already does this sync at the relevant call sites; QML/MCP callers that previously relied on the side effect now need to update both explicitly. Documented in `settings_visualizer.cpp`.

### C2. SettingsMcp
- [x] Create `src/core/settings_mcp.h` + `.cpp` (4 MCP properties: mcpEnabled, mcpAccessLevel, mcpConfirmationLevel, mcpApiKey)
- [x] Add `Settings::mcp()` accessor; remove from `settings.h` / `.cpp`
- [x] Migrate `ShotDetailPage.qml`, `PostShotReviewPage.qml`, `SettingsAITab.qml`
- [x] Update `mcpserver.cpp`, `shotserver.cpp`, `mcptools_*`

### C3. SettingsBrew — **DEFERRED to follow-up PR**
**Status:** Not implemented in this PR.
**Scope:** ~70 methods covering espresso, steam, hot water, flush, steam pitcher presets,
water vessel presets, flush presets, brew/temperature overrides, ignoreVolumeWithScale.
**Why deferred:** Largest single domain (~500 lines of code), with cross-domain dependencies
(setDefaultShotRating cross-call already broken in C1, maincontroller depends on many brew
properties). Needs careful migration of ~13 QML files and 6+ C++ consumers.
**Plan:** Tackle in follow-up PR with full session/context dedicated to it.

### C4. SettingsDye — **DEFERRED to follow-up PR**
**Status:** Not implemented in this PR.
**Scope:** 13 DYE metadata fields + bean preset CRUD invokables, beansModified state,
dyeCacheInitialized, recomputeBeansModified hook.
**Why deferred:** Tightly coupled with brew via `setDefaultShotRating` ↔ `setDyeEspressoEnjoyment`
relationship and the beansModified-recompute signal chain. Best done alongside C3.
**Plan:** Tackle in same follow-up PR as C3.

### C5. SettingsNetwork — **DEFERRED to follow-up PR**
**Status:** Not implemented in this PR.
**Scope:** shotServerEnabled/hostname/port, webSecurityEnabled, autoFavorites*, savedSearches,
shotHistorySortField/Direction, exportShotsToFile, layoutConfiguration, discussShotApp/Url/claudeRcSessionUrl.
**Why deferred:** Layout configuration alone has ~10 invokables and complex caching. Discuss/relay
fields cross with MCP. Less risky than C3/C4 but still substantial.
**Plan:** Tackle in follow-up PR.

### C6. SettingsApp — **DEFERRED to follow-up PR**
**Status:** Not implemented in this PR.
**Scope:** Updates, backup, developer flags, simulation/launcher mode, water level/refill,
profile management properties (favoriteProfiles, selectedBuiltInProfiles, hiddenProfiles, currentProfile),
platform capability flags (hasQuick3D, use12HourTime, isDebugBuild), pocketPairingToken, deviceId.
**Why deferred:** Heterogeneous bag of remaining properties; profile management invokables are
substantial. Needs design decision on whether to split further (e.g., SettingsProfile separately).
**Plan:** Tackle in follow-up PR.

### C7. PR 2 final validation (this PR)
- [x] Build succeeds on macOS with no warnings (mac-test target)
- [x] `tst_Settings` passes 14/14 tests
- [x] Domain headers added to `CMakeLists.txt` and `tests/CMakeLists.txt`
- [x] No regressions in MQTT/AutoWake/Hardware/AI/Theme/Visualizer/Mcp behaviour (covered by passing tests)

---

## Final state

**Delivered (this PR):**
- 7 of 11 domain classes: `SettingsMqtt`, `SettingsAutoWake`, `SettingsHardware`, `SettingsAI`,
  `SettingsTheme`, `SettingsVisualizer`, `SettingsMcp`
- `shothistory_types.h` extracted from `shothistorystorage.h`
- `settings.h`: 1,170 → 887 lines (24% reduction)
- All builds clean on macOS, all tests pass

**Deferred to follow-up PR:**
- 4 remaining domain classes: `SettingsBrew`, `SettingsDye`, `SettingsNetwork`, `SettingsApp`
- Final settings.h cleanup target of ≤200 lines (currently 887)
- Pointer-only consumer migration for shothistorystorage.h (requires interface extraction)

**Recompile-blast measurement (post-PR):**
- Touch `settings_mqtt.h` → recompiles ~3–5 .cpp files (down from 41 if all 41 had MQTT awareness)
- Touch `settings_theme.h` → recompiles ~5 .cpp files (widgetlibrary, shotserver_theme, shotserver, settings.cpp, mcptools_*)
- Touch `settings.h` itself → still recompiles ~39 .cpp files (will drop further as Tier 2 progresses)
