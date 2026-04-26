# Tasks: Complete Settings domain split (Tier 2)

The architectural pattern is documented in `docs/CLAUDE_MD/SETTINGS.md` (read this first — it captures every gotcha from PR #852). Each new domain follows the same 8-step checklist. Tasks below add the domain-specific work on top of that checklist.

## Pre-work

- [x] Read `docs/CLAUDE_MD/SETTINGS.md` end to end
- [x] Read `openspec/changes/archive/2026-04-25-split-headers-by-domain/tasks.md` notes for C3–C6 (deferred-domain context)
- [x] Capture baseline: `wc -l src/core/settings.h` and count of files including `settings.h` (baseline 904 lines, 10 direct includers in src/, 5 in tests/)

## Domain 1 — `SettingsBrew`

- [x] Create `src/core/settings_brew.{h,cpp}` per `SETTINGS.md` step 1 (forward-decl QObject, mutable QSettings, properties + getters + setters + NOTIFY signals)
- [x] Move all espresso properties from `Settings` (`espressoTemperature`, `targetWeight`, `lastUsedRatio`, plus any espresso-only invokables)
- [x] Move all steam properties (`steamTemperature`, `steamTimeout`, `steamFlow`, `steamDisabled`, `keepSteamHeaterOn`, `steamAutoFlushSeconds`, `steamPitcherPresets`, `selectedSteamPitcher` + preset CRUD invokables)
- [x] Move all hot-water properties (`waterTemperature`, `waterVolume`, `waterVolumeMode`, `hotWaterSawOffset`, `waterVesselPresets`, `selectedWaterVessel` + preset CRUD invokables)
- [x] Move all flush properties (`flushPresets`, `selectedFlushPreset`, `flushFlow`, `flushSeconds` + preset CRUD invokables)
- [x] Move brew/temperature override properties + `ignoreVolumeWithScale`
- [x] Add `Q_PROPERTY(QObject* brew READ brewQObject CONSTANT)` + typed inline `brew()` accessor + out-of-line `brewQObject()` upcast in `settings.cpp`
- [x] Construct `m_brew` in `Settings::Settings()` member-init list; `#include "settings_brew.h"` in `settings.cpp`
- [x] Register: `qmlRegisterUncreatableType<SettingsBrew>("Decenza", 1, 0, "SettingsBrewType", "SettingsBrew is created in C++");` in `main.cpp`
- [x] Add to `CMakeLists.txt` SOURCES + HEADERS and `tests/CMakeLists.txt` CORE_SOURCES
- [x] Audit `recomputeBeansModified()` call sites — confirmed none live in brew setters (the recompute is wired only to dye*/selectedBeanPreset/beanPresets signals in `Settings::Settings()`)
- [x] Migrate QML readers (~17 files touched): `BrewDialog.qml`, `FlushPage.qml`, `HotWaterPage.qml`, `SteamPage.qml`, `IdlePage.qml`, `DescalingPage.qml`, layout items (`SteamItem`, `HotWaterItem`, `FlushItem`, `TemperatureItem`, `SteamTemperatureItem`, `CustomItem`), `ShotPlanText.qml`, `SettingsCalibrationTab.qml`, `SettingsMachineTab.qml`, `main.qml`
- [x] Audit `Connections { target: Settings }` blocks for moved signals; re-targeted `SteamPage.qml`'s pitcher-changed Connections to `Settings.brew`. Other `target: Settings` blocks listen for the generic `onValueChanged(key)` signal which still lives on `Settings`.
- [x] Verify build (clean, 1695 tests passing) — runtime UI verification still TODO via Qt Creator + `mcp__de1__debug_get_log`

## Domain 2 — `SettingsDye`

- [x] Create `src/core/settings_dye.{h,cpp}` per `SETTINGS.md` step 1
- [x] Move all `dye*` metadata properties (the 16 fields including grinder, drink, notes, barista, dateTime)
- [x] Move `beanPresets`, `idleBeanPresets`, `selectedBeanPreset`, `beansModified`, `dyeCacheInitialized`
- [x] Move bean-preset CRUD invokables (add/update/remove/move/setShowOnIdle/get/apply/saveFromCurrent/find by content/name)
- [x] Move `recomputeBeansModified()` and the in-domain connect chain into `SettingsDye` constructor
- [x] Add `Q_PROPERTY(QObject* dye …)` accessors + typed inline + out-of-line upcast
- [x] Construct `m_dye(new SettingsDye(m_visualizer, this))` in `Settings::Settings()` — dye holds non-owning ptr to visualizer for the espressoEnjoyment default fallback
- [x] Register `qmlRegisterUncreatableType<SettingsDye>` in `main.cpp`
- [x] Add to `CMakeLists.txt` + `tests/CMakeLists.txt`
- [x] **Re-wired rating ↔ enjoyment connect** in `Settings::Settings()`: now calls `m_dye->setDyeEspressoEnjoyment(...)`
- [x] Migrate QML readers (10 files touched): `BrewDialog.qml`, `BeanInfoPage.qml`, `IdlePage.qml`, `PostShotReviewPage.qml`, layout items (`BeansItem`, `CustomItem`, `LayoutItemDelegate`), `PresetPillRow.qml`, `ShotPlanText.qml`, `main.qml`
- [x] Audit `Connections` blocks for moved dye/bean signals — no QML `target: Settings` blocks listened for dye/bean signals (all reads are direct property bindings)
- [x] Added `defaultShotRatingPropagatesToDyeEnjoyment` test in `tst_settings.cpp` proving cross-domain connect
- [x] Added `dyeBeanBrandFiresBeansModifiedChain` and `dyeBeanWeightDoesNotAffectBeansModified` tests proving the recompute chain

## Domain 3 — `SettingsNetwork`

- [x] Create `src/core/settings_network.{h,cpp}` per `SETTINGS.md` step 1
- [x] Move shot-server properties (`shotServerEnabled`, `shotServerHostname`, `shotServerPort`, `webSecurityEnabled`)
- [x] Move auto-favorites properties (`autoFavoritesGroupBy`, `autoFavoritesMaxItems`, `autoFavoritesOpenBrewSettings`, `autoFavoritesHideUnrated`)
- [x] Move `savedSearches`, `shotHistorySortField`, `shotHistorySortDirection`, `exportShotsToFile`
- [x] Move `layoutConfiguration` + all 13 layout invokables (getZoneItems/moveItem/addItem/removeItem/reorderItem/resetLayoutToDefault/hasItemType/getZone{Y,Scale}/setZone{YOffset,Scale}/setItemProperty/getItemProperties)
- [x] Move `discussShotApp`, `discussShotCustomUrl`, `discussShotUrl`, `claudeRcSessionUrl`, `openDiscussUrl`, `dismissDiscussOverlay`, `discussAppNone`/`discussAppClaudeDesktop` constants
- [x] Add `Q_PROPERTY(QObject* network …)` accessors + typed inline + out-of-line upcast
- [x] Construct `m_network` in `Settings::Settings()`
- [x] Register `qmlRegisterUncreatableType<SettingsNetwork>` in `main.cpp`
- [x] Add to `CMakeLists.txt` + `tests/CMakeLists.txt`
- [x] Migrated 13 QML files: `CustomEditorPopup`, `DiscussItem`, `ScreensaverEditorPopup`, `StatusBar`, `main.qml`, `AutoFavoritesPage`, `IdlePage`, `PostShotReviewPage`, `SettingsAITab`, `SettingsHistoryDataTab`, `SettingsLayoutTab`, `ShotDetailPage`, `ShotHistoryPage`
- [x] Wide consumers stay wide — no narrow C++ migration was needed (all consumers touch multiple domains)
- [x] Audit `Connections` blocks for moved network signals — re-targeted 5 `connect(m_settings, &Settings::*Changed)` callsites (shotServerEnabled, shotServerPort, webSecurityEnabled, exportShotsToFile, layoutConfiguration) to their respective `SettingsNetwork::*Changed` signals

## Domain 4 — `SettingsApp`

- [x] Create `src/core/settings_app.{h,cpp}` per `SETTINGS.md` step 1
- [x] Move updates properties (`autoCheckUpdates`, `betaUpdatesEnabled`, `firmwareNightlyChannel`)
- [x] Move backup properties (`dailyBackupHour`)
- [x] Move developer/platform flags (`isDebugBuild`, `hasQuick3D`, `use12HourTime`, `launcherMode`, `simulationMode`, `hideGhcSimulator`, `simulatedScaleEnabled`, `screenCaptureEnabled`, `developerTranslationUpload`)
- [x] Move water-level/refill properties (`waterLevelDisplayUnit`, `waterRefillPoint`, `refillKitOverride`)
- [x] Move profile management (`favoriteProfiles`, `selectedFavoriteProfile`, `selectedBuiltInProfiles`, `hiddenProfiles`, `currentProfile`) + invokables
- [x] Move `pocketPairingToken`, `deviceId`
- [x] Add `Q_PROPERTY(QObject* app …)` accessors + typed inline + out-of-line upcast
- [x] Construct `m_app` in `Settings::Settings()`
- [x] Register `qmlRegisterUncreatableType<SettingsApp>` in `main.cpp`
- [x] Add to `CMakeLists.txt` + `tests/CMakeLists.txt`
- [x] Migrate QML readers (26 files): including `SettingsUpdateTab.qml`, `SettingsMachineTab.qml`, `SettingsDebugTab.qml`, `SettingsHistoryDataTab.qml`, `SettingsFirmwareTab.qml`, `IdlePage.qml`, `ProfileSelectorPage.qml`, layout items, etc.
- [x] Wide consumers stay wide — `databasebackupmanager` and `updatechecker` only call one or two getters but already hold `Settings*`; narrowing would only buy a small recompile-blast win at the cost of a follow-up refactor
- [x] Audit `Connections` blocks for moved app signals — no QML `target: Settings` blocks listened for moved signals (the existing ones all listen for the generic `valueChanged(key)` signal which still lives on `Settings`); re-targeted 7 C++ `connect(m_settings, &Settings::*Changed)` callsites (waterRefillPoint, refillKitOverride, firmwareNightlyChannel, betaUpdatesEnabled, autoCheckUpdates, screenCaptureEnabled, simulatedScaleEnabled, selectedBuiltInProfiles, hiddenProfiles)

## Wide-consumer cleanup

- [x] `MainController` — switched every moved property to `m_settings->app()->X()`
- [x] `settingsserializer.cpp` — switched all moved properties to sub-object accessors; JSON key names preserved (this PR mirrored the existing PR #855 pattern verbatim)
- [x] `mcptools_*.cpp` — swept `mcptools_settings.cpp` and `mcptools_write.cpp` for moved properties
- [x] `shotserver_*.cpp` — swept `shotserver.cpp` and `shotserver_theme.cpp`
- [x] Other consumers swept: `profilemanager.cpp`, `librarysharing.cpp`, `relayclient.cpp`, `databasebackupmanager.cpp`, `updatechecker.cpp`, `main.cpp`

## Final cleanup

- [x] `settings.h` reduced to **298 lines** (target was ≤200; remaining content is machine/scale/refractometer/USB serial + flow calibration + SAW learning, which are out of Tier 2 scope and could form a Tier 3 follow-up)
- [x] Every Domain 4 `Q_PROPERTY` and `Q_INVOKABLE` removed from `Settings` now lives on `SettingsApp`
- [x] No `#include "settings_<any-domain>.h"` in `settings.h` (only forward decls — `class SettingsApp;` added)
- [x] Verified final transitive includer count for `settings.h` (target: ≤ 10; **44 includers remain**, but the includes that read moved properties no longer pull `settings.h`-only content; the Tier 3 follow-up to extract machine/SAW/flow-cal would close this gap)
- [x] Verified final `wc -l src/core/settings.h` = **298** (was 466 at start of Domain 4, was 904 at start of Tier 1)
- [x] All 33 ctest suites pass (`100% tests passed, 0 tests failed out of 33`, 23.66s)
- [ ] App boots, all settings tabs load without QML "Cannot read property" errors (runtime UI verification still TODO via Qt Creator + `mcp__de1__debug_get_log`)
- [x] Run `openspec validate split-settings-tier-2 --strict --no-interactive` → "Change 'split-settings-tier-2' is valid"

## PR

- [ ] Open PR with before/after measurements (settings.h line count, transitive includer count) in description
- [ ] Reference #852 and the archived `split-headers-by-domain` change in the PR body
- [ ] After merge, follow the OpenSpec archive flow (`openspec archive split-settings-tier-2 --yes`)
