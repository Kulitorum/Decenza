# Design: Settings Tier 2 split

This document captures the design decisions for finishing the Settings split. The architectural pattern is fixed by `docs/CLAUDE_MD/SETTINGS.md` — this doc only covers the **non-obvious choices** specific to the four remaining domains.

## Domain boundaries

The 4 new sub-objects:

### `SettingsBrew`
Espresso, steam, hot water, flush, presets, brew/temperature overrides, ignoreVolumeWithScale.

Key prefix: stays as-is (`espressoTemperature`, `steamTemperature`, `flushFlow`, etc. — most have no prefix today; do **not** rename, see `SETTINGS.md` "Storage keys").

Properties (rough count): ~30 scalars + 5 list/preset properties (`steamPitcherPresets`, `selectedSteamPitcher`, `waterVesselPresets`, `selectedWaterVessel`, `flushPresets`, `selectedFlushPreset`).

Invokables: ~25 — preset CRUD (`addSteamPitcherPreset`, `removeSteamPitcherPreset`, `renameSteamPitcherPreset`, `addWaterVesselPreset`, …), flush controls, brew override accessors.

### `SettingsDye`
The 13 `dye*` fields plus `beanPresets` / `idleBeanPresets` / `selectedBeanPreset` / `beansModified` / `dyeCacheInitialized`, and the bean-preset CRUD invokables.

Key prefix: keep `dye*` for the metadata, `beanPresets` for the presets list (do not rename).

### `SettingsNetwork`
`shotServer*`, `webSecurityEnabled`, `autoFavorites*`, `savedSearches`, `shotHistorySort*`, `exportShotsToFile`, `layoutConfiguration` + its ~10 invokables, `discussShotApp/Url`, `claudeRcSessionUrl`.

Key prefix: keep existing keys (`shotServer/enabled`, `autoFavorites/groupBy`, etc.).

### `SettingsApp`
Updates, backup, dev flags (`devMode`, `isDebugBuild`, `hasQuick3D`, `use12HourTime`), simulation/launcher mode, water level/refill, profile management (`favoriteProfiles`, `selectedFavoriteProfile`, `selectedBuiltInProfiles`, `hiddenProfiles`, `currentProfile`), `pocketPairingToken`, `deviceId`.

Open question: **should profile management split into its own `SettingsProfile`?** Tier 1 tasks.md flagged this. Decision: **keep it in `SettingsApp` for now**. Rationale: the profile properties are mostly bookkeeping (which profile is current, which are favorited/hidden) — they are read by many consumers but their setters are simple. Splitting would create a 5th new sub-object in this PR for marginal isolation gain. Revisit only if `settings_app.h` becomes a recompile hotspot post-merge.

## Cross-domain wiring

Two side-effect chains today live inside `Settings::set...` methods that span what will become Brew/Dye:

### 1. Rating ↔ enjoyment sync
`SettingsVisualizer::setDefaultShotRating` already fires a connect-based wire to `Settings::setDyeEspressoEnjoyment` (set up in `Settings::Settings()` after Tier 1). After Tier 2, the target moves: `setDyeEspressoEnjoyment` lives on `SettingsDye`, so the connect becomes:

```cpp
connect(m_visualizer, &SettingsVisualizer::defaultShotRatingChanged, this, [this]() {
    m_dye->setDyeEspressoEnjoyment(m_visualizer->defaultShotRating());
});
```

This is a one-line edit to the existing connect in `Settings::Settings()`.

### 2. `beansModified` recompute
`Settings::beansModified` is computed against the active bean preset; it has to fire whenever any dye field that participates in the comparison changes. After the split, this signal lives on `SettingsDye` but is fired by setters that span both `SettingsDye` (the dye fields) and `SettingsBrew` (no — confirmed: only dye fields participate, so this chain stays internal to `SettingsDye`). **No cross-domain wiring needed for `beansModified`.** Verify during implementation that no brew setter today calls `recomputeBeansModified()`.

### 3. Profile-change side effects
Setters like `setCurrentProfile` today emit signals that other domains listen to (e.g., recipe/dose defaults). If any such cross-call surfaces during migration, wire it via `connect()` in `Settings::Settings()` — never inline-couple sub-objects.

## QML migration scope

Estimated QML files to migrate (`Settings.X` → `Settings.<domain>.X`):

- **Brew:** `BrewSettingsPage.qml`, `SteamSettingsTab.qml`, `HotWaterSettingsTab.qml`, `FlushSettingsTab.qml`, `EspressoPage.qml`, `SteamPage.qml`, `HotWaterPage.qml`, `FlushPage.qml`, plus any preset editors → ~10 files
- **Dye:** `DyeEditor.qml`, `BeanPresetEditor.qml`, `PostShotReviewPage.qml`, `ShotDetailPage.qml`, plus dye fields surfaced in idle/espresso pages → ~6 files
- **Network:** `SettingsNetworkTab.qml`, `SettingsWebTab.qml`, `LayoutEditor*` files, `AutoFavorites*` filters, `ShotHistoryPage.qml` (sort fields) → ~8 files
- **App:** `SettingsUpdatesTab.qml`, `SettingsBackupTab.qml`, `SettingsDevTab.qml`, `IdlePage.qml` (water level), `ProfileChooser*` → ~6 files

Total: ~30 QML files. Use `rg -l "Settings\.X"` per property to find readers. **Do not skip the `Connections { target: Settings }` audit** — those don't warn when signals move (the bug that broke the theme editor in PR #852).

## Narrow C++ consumer migration

Candidates to move to typed sub-object pointers (drop `Settings*`):

- `MainController` — currently wide; will stay wide because it touches all 4 new domains
- `settingsserializer.cpp` — wide; stays wide
- `mcptools_*.cpp` — most are wide; leave wide
- `shotserver_*.cpp` — `shotserver_layout.cpp` could narrow to `SettingsNetwork*`, others stay wide
- `databasebackupmanager.*` — could narrow to `SettingsApp*` if it only reads backup settings; verify
- Profile-related controllers (`ProfileManager`?) — verify if any only read profile management settings

**Don't force-narrow** — narrow consumers must genuinely only need one domain. A class that takes `SettingsApp*` but secretly needs `SettingsBrew*` for one read is a refactor smell.

## Build-blast verification

Goal: after Tier 2, `settings.h` ≤ 200 lines, transitive `.cpp` includers ≤ 10 (today: 39).

Measure with:
```bash
wc -l src/core/settings.h
grep -rl '#include "core/settings\.h"\|#include "settings\.h"' src/ | wc -l
```

Run before and after, post in PR description.

## Risks and mitigations

| Risk | Mitigation |
|------|-----------|
| QML blanks again (PR #852 regression) | Use `QObject*` Q_PROPERTY type per `SETTINGS.md` step 3; add `qmlRegisterUncreatableType` per step 7 — verified pattern. Test by running app and reading `mcp__de1__debug_get_log` for "Cannot read property" errors after each domain. |
| Storage key rename loses user data | Keep every `m_settings.value(key)` / `setValue(key)` string identical to today's. `SETTINGS.md` "Storage keys" rule. |
| Cross-domain `connect()` not firing | Wire in `Settings::Settings()` constructor body **after** all `m_<domain>` are constructed. Add a unit test for the rating↔enjoyment sync. |
| `beansModified` chain breaks silently | Add a test: set `dyeBeanWeight`, observe `beansModifiedChanged`. |
| Brew is too large for one PR | Acceptable risk — the user has agreed to a single PR for Tier 2. If size becomes unwieldy mid-implementation, split Brew off as a sub-PR but keep Dye/Network/App together. |
| `Connections { target: Settings }` blocks silently break | Audit by grepping `Connections.*target:\s*Settings\b` (no dot) and verifying each handler signal is still emitted by `Settings` itself. |
