# Design

## Context

See proposal.md for motivation. Current state, as read from the code:

- The rate is one QSettings value, `steam/steamSecondsPerGram` (`SettingsBrew::steamSecondsPerGram`, `settings_brew.cpp:225`). Every calculation takes it as a parameter: `SteamCalculations::scaledSteamTime` / `effectiveSteamDuration` / `steamScalingBlocker` (`src/core/steamcalculations.h`), called from `SettingsBrew`, `PreparationController` (`:557`, `:603`, `:720`), `SteamServiceSource` (`:269`) and the replica-side `SteamService` (`steamservice.cpp:184`).
- Presets are a JSON array in `steam/pitcherPresets` with `name`, `duration`, `flow`, `temperature`, `pitcherWeightG` and a dead legacy `calibMilkG`. Field edits go through `updateSteamPitcherPresetFields`. A preset without `temperature` already reads the global steam temperature (`mcptools_presets.cpp:47`, `settingsserializer.cpp:517`) — the codebase's precedent for a per-preset field added after the fact.
- The Steam page edits through the `SteamEditRequest` operations (`set-steam-seconds-per-gram` with `value`, `calibrate-from-last-steam` with no fields), dispatched by `SteamPageEditor`. The rate also travels as `SteamState.steamSecondsPerGram` (`steamservice.rep:33`) and `MachineUiSnapshot.brewSteamSecondsPerGram`, which feeds the editor revision.
- `SteamPitcherSnapshot` parses presets against a closed field list (`steampitchersnapshot.cpp:19`) and rejects unknown fields.
- The last pour is two settings, `steam/lastSteamMilkG` and `steam/lastSteamTimeS`, written together in `PreparationController` (`:636`). Nothing records which pitcher it was.
- Recipes re-select their pitcher by name on activation, so the rate follows the preset. The recipe steam block also snapshots the rate beside the empty weight, so a pitcher recreated from a recipe keeps it.

## Goals / Non-Goals

**Goals:**

- One definition of "this preset's rate", read by every calculation and every surface.
- No user's steam time changes on upgrade.
- A calibration can only land on the preset whose pour produced it.

**Non-Goals:**

- A separate "fluid" or "target temperature" concept. A preset is the unit; one physical pitcher used for two fluids is two presets with the same empty weight.
- Deriving the rate from flow, heater temperature or milk start temperature.
- Per-preset on/off for weight timing. The toggle stays global.
- Removing the dead `calibMilkG` field.

## Decisions

### 1. The rate is a preset field, `secondsPerGram`, seeded by a one-time copy

Store `secondsPerGram` in each preset object. On the first launch of a build with this change, a one-time step copies the current `steam/steamSecondsPerGram` into every stored preset that has no `secondsPerGram`, then sets `migration/steamRatePerPitcher` (the same flag pattern as `migration/remoteMcpIndependentOfLocal`, `settings.cpp:173`). After that an absent key means 0, uncalibrated, and the legacy key is never read or written again. It is left in place so a rollback still finds it.

The copy and the backup import (Decision 2) share one helper, `seedSteamPitcherRates(rate)`, which fills only presets lacking the key. If the preset array cannot be parsed, the step writes nothing and does not set the flag, so it retries on the next launch rather than marking a copy that never happened as done. The flag is set even when the old rate is 0: there is nothing to copy, and the step must not later pick up a stale value.

The `SteamCalculations` functions lose their `secondsPerGram` parameter and read `preset["secondsPerGram"]`, so QML, the Wasm replica, MCP and the calculations all read the one stored number. `steamSecondsPerGram` / `setSteamSecondsPerGram` / `calibrateSteamFromReference` leave the `SettingsBrew` API, replaced by `trySetSteamPitcherSecondsPerGram(index, expectedName, rate)` beside the existing `trySetSteamPitcher*` family. Hand-entered rates are bounded to 0–2.0 s/g at the editor and MCP inputs; a rate measured from a pour or copied from the old global value is stored unchanged.

*Alternative considered: fall back to the legacy key on every read when a preset has none.* No step to run, but the old global value would stay live as hidden state that no screen shows or edits, and each preset's rate would be "own or inherited" instead of simply its own. A one-time copy leaves every preset holding a plain, visible number.

### 2. Backup export/import

Export writes `secondsPerGram` per preset and stops writing the top-level `steamSecondsPerGram`. Import passes a preset's own rate through when present. For an older backup that carries only the top-level value, import calls `seedSteamPitcherRates` with it after the preset loop, so those presets get the backup's rate. `tryAddSteamPitcherPreset` gains an optional rate; absent means "write no key".

### 3. New presets copy the selected preset's rate

`SteamPageEditor`'s `CreatePreset` and MCP `add` (when no rate is given) pass the selected preset's resolved rate, 0 when Heater off is selected. One helper on `SettingsBrew` supplies that default so the two callers cannot drift.

*Alternative: start uncalibrated.* Rejected — adding a second jug for the same milk would silently drop to fixed duration.

### 4. Editor contract

- `set-steam-seconds-per-gram {value}` becomes `set-preset-seconds-per-gram {index, expectedName, value}`, joining the `set-preset-*` family and its name-guarded addressing.
- `calibrate-from-last-steam` keeps no fields. The host resolves the pitcher from the last-pour record and refuses, with a message, unless that name is the selected preset's.
- `SteamState.steamSecondsPerGram` and `MachineUiSnapshot.brewSteamSecondsPerGram` are removed; the rate is already inside `steamPitcherPresets`, which the pitcher snapshot revision covers. `secondsPerGram` is added to the `SteamPitcherSnapshot` field list.
- `SteamState` gains `lastSteamPitcherName` so the page can decide whether to offer "Use last steam". `MachineUiSnapshot` does not: nothing reads its last-pour fields.

Follow `docs/CLAUDE_MD/SHARED_UI.md` for the `.rep` and source-registration changes.

### 5. The last pour remembers its pitcher by name

Add `steam/lastSteamPitcherName` and `steam/lastSteamPourMilkG`, written with the pour's time at session end. The pour needs its own milk value because recipe activation overwrites `lastSteamMilkG` with the recipe's planned milk. Name, not index: indexes shift on delete and reorder. The name follows a rename and is cleared on delete, both in `SettingsBrew`, so it cannot come to mean a different pitcher. An old record with no name matches nothing; one weighed steam repairs it.

### 6. Steam page

The Steam rate section stays where it is, under the Weight-timed steaming heading: everything in that block is already hidden for Heater off and already follows the selected pitcher, and moving it would separate the rate from the switch that governs it. Its value line names the pitcher ("Oat: 0.18 sec per gram of milk"). `steam.rate.note` is deleted. "Use last steam" is enabled only when `lastSteamPitcherName` equals the selected preset's name; otherwise the line names the pitcher the last steam was with. Bindings that read `SteamService.state.steamSecondsPerGram` read the selected preset's `secondsPerGram` through two page properties, `currentPitcherRate` and `currentPitcherName`. The Duration fallback hint keeps its existing requirement: "the steam rate is calibrated" now reads the selected preset.

### 7. MCP

`steam_pitcher` `list` reports `secPerGramMilk` on each heater-on preset; `add` and `update` accept it. No new tool. Bump `McpSurfaceVersion`; keep the property description under 120 characters and rewrite the `calibMilkG` paragraph in `resources/ai/tools/steam_pitcher.md`, which currently says the rate is global.

## Risks / Trade-offs

- [A Wasm client and host on different builds: the closed preset field list rejects `secondsPerGram`] → Not reachable: `SHARED_UI.md` ("Verify the shared change") states the client is shipped with its host and connects only to the matching package and host epoch.
- [The recipe steam block now carries `secondsPerGram`, so a recipe saved before this change recreates a deleted pitcher uncalibrated] → Only for recipes not re-saved since; the pitcher steams for its fixed duration until calibrated.

## Migration Plan

The one-time copy in Decision 1 is the whole migration; it runs at settings start-up, before any steam can be planned. Rollback to the previous build still finds `steam/steamSecondsPerGram` untouched and ignores the new preset key.
