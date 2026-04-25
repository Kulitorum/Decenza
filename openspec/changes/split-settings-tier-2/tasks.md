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

- [ ] Create `src/core/settings_app.{h,cpp}` per `SETTINGS.md` step 1
- [ ] Move updates properties (auto-update, channel, last-checked, etc.)
- [ ] Move backup properties (`backupEnabled`, retention, paths, etc.)
- [ ] Move developer/platform flags (`devMode`, `isDebugBuild`, `hasQuick3D`, `use12HourTime`, `launcherMode`, simulation mode)
- [ ] Move water-level/refill properties
- [ ] Move profile management (`favoriteProfiles`, `selectedFavoriteProfile`, `selectedBuiltInProfiles`, `hiddenProfiles`, `currentProfile`) + invokables
- [ ] Move `pocketPairingToken`, `deviceId`
- [ ] Add `Q_PROPERTY(QObject* app …)` accessors + typed inline + out-of-line upcast
- [ ] Construct `m_app` in `Settings::Settings()`
- [ ] Register `qmlRegisterUncreatableType<SettingsApp>` in `main.cpp`
- [ ] Add to `CMakeLists.txt` + `tests/CMakeLists.txt`
- [ ] Migrate QML readers (~6 files): `SettingsUpdatesTab.qml`, `SettingsBackupTab.qml`, `SettingsDevTab.qml`, `IdlePage.qml` (water level), profile choosers
- [ ] Migrate `databasebackupmanager.*` to take `SettingsApp*` if it only reads backup settings (verify; otherwise leave wide)
- [ ] Migrate `updatechecker.*` to take `SettingsApp*` if it only reads update settings (verify)
- [ ] Audit `Connections` blocks for moved app signals

## Wide-consumer cleanup

- [ ] `MainController` — switch every moved property to `m_settings->brew()->X()` / `dye()->X()` / `network()->X()` / `app()->X()`
- [ ] `settingsserializer.cpp` — switch all moved properties to sub-object accessors; preserve JSON key names exactly
- [ ] `mcptools_*.cpp` — sweep all `m_settings->X()` calls for moved properties
- [ ] `shotserver_*.cpp` — sweep all `m_settings->X()` calls for moved properties
- [ ] Any other `Settings*` consumer found via `rg "settings->\w+\(\)" src/`

## Final cleanup

- [ ] `settings.h` reduced to ≤200 lines (only sub-object accessors + `sync()` + `factoryReset()` + cross-domain wiring declarations)
- [ ] Every `Q_PROPERTY` and `Q_INVOKABLE` removed from `Settings` belongs to a sub-object now
- [ ] No `#include "settings_<any-domain>.h"` in `settings.h` (only forward decls)
- [ ] Verify final transitive includer count for `settings.h` (target: ≤ 10; was 39)
- [ ] Verify final `wc -l src/core/settings.h` (target: ≤ 200; was 904)
- [ ] All `tst_settings.cpp` tests pass
- [ ] App boots, all settings tabs load without QML "Cannot read property" errors (check via `mcp__de1__debug_get_log`)
- [ ] Run `openspec validate split-settings-tier-2 --strict --no-interactive`

## PR

- [ ] Open PR with before/after measurements (settings.h line count, transitive includer count) in description
- [ ] Reference #852 and the archived `split-headers-by-domain` change in the PR body
- [ ] After merge, follow the OpenSpec archive flow (`openspec archive split-settings-tier-2 --yes`)
