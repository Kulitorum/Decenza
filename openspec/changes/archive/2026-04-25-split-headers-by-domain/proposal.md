# Change: Split wide headers by domain to reduce recompile blast radius

## Why

Any edit to `settings.h` — even adding one method — forces recompilation of 41 `.cpp` files because every component in the codebase includes it. The same pattern (wide header, many consumers) affects `shothistorystorage.h` (22 files). PR #742 triggered a near-full recompile to add flow calibration batch methods used only by `maincontroller.cpp`. Splitting headers by domain means a change to MQTT settings only recompiles the 2–3 files that touch MQTT.

## What Changes

### Primary: `settings.h` split by domain (issue #743, option 1)

`Settings` is refactored from a 1,170-line monolith into a **composition façade** that owns domain sub-objects. The change is delivered in two PRs.

**All domain classes:**

| Domain class | Key properties | Tier |
|---|---|---|
| `SettingsMqtt` | 10 MQTT broker/publish properties | 1 |
| `SettingsAutoWake` | 4 auto-wake schedule properties | 1 |
| `SettingsHardware` | 6 heater-tweak + hot-water + steam-stop properties | 1 |
| `SettingsAI` | 8 AI provider + API key properties | 1 |
| `SettingsTheme` | ~25 theme/shader/font/brightness properties | 1 |
| `SettingsVisualizer` | 8 visualizer upload properties | 2 |
| `SettingsMcp` | 4 MCP server properties | 2 |
| `SettingsBrew` | espresso, steam, hot water, flush extraction settings | 2 |
| `SettingsDye` | 13 DYE metadata + bean preset properties | 2 |
| `SettingsNetwork` | shot server, auto-favorites, shot history sort | 2 |
| `SettingsApp` | updates, backup, developer, layout config, platform caps | 2 |

**Migration pattern:** For each domain:
1. Create the domain class (`SettingsMqtt`, etc.) with full `Q_PROPERTY` declarations and signals
2. `Settings` temporarily retains forwarding `Q_PROPERTY`s during QML migration
3. Migrate all QML from `Settings.mqttEnabled` → `Settings.mqtt.mqttEnabled`
4. Remove the forwarding `Q_PROPERTY`s and signal re-emits from `Settings` for that domain
5. Update C++ consumers to accept the domain type instead of `Settings*`

After all domains are extracted, `Settings.h` contains only sub-object accessors (`Q_PROPERTY(SettingsMqtt* mqtt READ mqtt CONSTANT)`) and any properties that don't belong to a narrow domain.

**QML impact:** ~35 QML files change access patterns across all domains. Each settings page migrates when its domain is extracted. QML uses `Settings.mqtt.mqttEnabled` instead of `Settings.mqttEnabled`; signal connections change from `Connections { target: Settings }` to `Connections { target: Settings.mqtt }`.

**C++ API change:** Narrow consumers (e.g., `MqttClient`, `AutoWakeManager`, `DE1Device`, `AIManager`, `ShotServer` theme endpoint) change their constructor parameter from `Settings*` to the domain type. `main.cpp` passes `settings.mqtt()` etc. at each call site.

### Secondary: `shothistorystorage.h` types extraction

`ShotRecord`, `HistoryShotSummary`, `ShotFilter`, `ShotSaveData`, `GrinderContext`, and `HistoryPhaseMarker` are extracted to `shothistory_types.h`. Nine `.cpp` files that only store a pointer and call simple methods (`databasePath()`, `totalShots()`, `isReady()`) switch to forward declarations.

**Win:** Struct-change blast drops from 22 files to 13.

### Tertiary: `de1device.h` assessment

Already lean (only Qt + protocol headers, no app dependencies). Of 24 consumers, 19 need the full class for signal connections. **No action** — effort/reward too low.

## Impact

- **Affected specs:** `settings-architecture` (new)
- **Affected code:** `src/core/settings.h` + `.cpp`, all 11 domain class pairs, ~35 QML files, narrow C++ consumers, `src/history/shothistorystorage.h`
- **No functional change:** pure build infrastructure
- **Breaking changes:** None to external behavior. `Settings` public C++ API unchanged for callers that pass `Settings*`; only narrow-consumer constructors change to accept domain types.

## Quantified wins (after full split)

| Header changed | Before (files recompiled) | After |
|---|---|---|
| Any single domain header | 41 | 2–6 (domain consumers only) |
| `settings.h` façade | 41 | ~10 (only files that use the façade directly) |
| `shothistorystorage.h` structs | 22 | 13 |
