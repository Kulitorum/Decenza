# Proposal

## Why

Weight-timed steaming scales steam time from one global rate (seconds of steam per gram of milk), so every pitcher preset heats the same fluid to the same temperature. That is wrong for two real cases: different fluids (oat, whole milk, water for cocoa) need different amounts of steam per gram, and different people in one household want different final temperatures. Presets already carry their own steam flow and heater temperature, which change how fast milk heats, so a single shared rate was never exact even for one fluid.

## What Changes

- Each steam pitcher preset stores its own steam rate (seconds per gram of milk). Weight scaling uses the selected preset's rate.
- **BREAKING** The global steam rate is removed as a setting. On upgrade, and when an older backup is imported, every existing preset is seeded with the old global rate, so nobody's steam time changes until they edit a preset.
- The Steam page's "Steam rate" control edits the selected preset's rate, like Duration, Flow and Temperature already do. The "one rate for every pitcher" note is removed.
- "Use last steam" calibrates the preset that steam was done with. The last pour remembers its pitcher, and the action is offered only while that pitcher is selected, so an oat-milk pour cannot calibrate the whole-milk preset.
- A new preset starts with the rate of the preset selected when it was created, so a second pitcher for the same fluid works without recalibrating.
- A preset with no rate (0) steams for its fixed duration, as an uncalibrated app does today. Milk is still weighed for it, since a weighed pour is what calibrates it.
- The MCP `steam_pitcher` tool reports and accepts the per-preset rate; the remote/Wasm steam state and the backup format carry it inside each preset.

Not changing: the weight-timed on/off toggle stays global, the 5–120 s clamp, milk capture, and how recipes pick a pitcher (by name — the rate follows the preset).

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `weight-timed-steaming`: the rate that scales steam time becomes per pitcher preset instead of global; calibration, the uncalibrated fallback, and the debug-log line follow the selected preset; the "One global steam rate" requirement is replaced.

## Impact

- **Settings**: `SettingsBrew` preset storage (`steam/pitcherPresets` gains a rate key), `steam/steamSecondsPerGram` becomes a migration source only, last-pour record gains the pitcher name; `settingsserializer.cpp` export/import.
- **Calculation**: `SteamCalculations` callers in `SettingsBrew`, `PreparationController`, `SteamService`/`SteamServiceSource` stop passing a global rate.
- **Editor contract**: `SteamEditRequest` (`set-steam-seconds-per-gram`, `calibrate-from-last-steam`), `SteamPageEditor`, `SteamPitcherSnapshot` field list, `MachineUiSnapshot`, `steamservice.rep` — shared native/Wasm surfaces, see `docs/CLAUDE_MD/SHARED_UI.md`.
- **UI**: `qml/pages/SteamPage.qml` steam-rate section, translations, `SettingsSearchEntries.js`.
- **MCP**: `steam_pitcher` in `src/mcp/mcptools_presets.cpp`, `resources/ai/tools/steam_pitcher.md`, `McpSurfaceVersion`, `docs/CLAUDE_MD/MCP_SERVER.md`.
- **Docs**: wiki manual steam section.
- **Tests**: `tst_settings`, `tst_preparationcontroller`, `tst_steamservice`, `tst_steampageeditor`, `tst_steameditrequest`, `tst_steampitchersnapshot`, `tst_machineuisnapshot`, `tst_mcptools_presets`.
