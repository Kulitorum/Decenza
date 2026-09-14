## Why

An MCP client cannot do what Brew Settings does. Testing #1941 over MCP showed it: there is no way to dial a ratio. The gaps are wider than that:

- `settings_set targetWeight` and `espressoTemperature` edit the **profile**, while Brew Settings' Stop-at and Temp Delta are **brew overrides**. The same words mean different things on the two surfaces.
- No tool sets a ratio, clears overrides, or applies overrides without starting a shot. `machine_start action=espresso` takes overrides, can only send grams, and says its overrides "apply to this shot only and clear when it ends", which is false: they persist.
- Nothing reads the session yield anchor, the temperature override, the recipe → bag → profile baseline, or which store Update Recipe/Bag would write. `settings_get targetWeightG` reads a legacy key, not the target the machine stops at.
- Ratio presets, the dose-cup tare, the capture sound, "Update Profile" for temperature, and creating an equipment package have no MCP path.

## What Changes

- **`settings_set` applies Brew Settings.** `targetWeight` (grams) and new `yieldRatio` arm the brew yield override exactly as Brew Settings OK does; they are mutually exclusive and `0` clears the yield. `espressoTemperature` sets or clears the temperature override (70-100 °C). New `clearBrewOverrides` does what Clear + OK does. New `ratioPreset1`-`3`, `doseCupTareWeight`, `doseCaptureSoundEnabled`. Non-numbers are refused, and the reply's `brew` object reports what took effect. **BREAKING:** `targetWeight` and `espressoTemperature` no longer edit the profile.
- **`settings_get` category `espresso` reports Brew Settings state:** the effective stop-at target, the yield anchor, the temperature override, the baseline and its source, whether each value really overrides it, where Update would save, presets, tare and sound.
- **`profiles_edit_params` gains `espressoTemperature`**, sent on its own: shifts every frame and saves like Brew Settings' Update Profile, reporting whether it saved and why not.
- **`equipment` gains `action=create`**, returning an identical existing package with `created:false`.
- **`machine_start` refuses brew arguments** (BREAKING); set values with `settings_set` first.
- The recipe → bag → profile baseline, the recipe temperature baseline and the Update Recipe/Bag target move into header-only `src/core/brewbaseline.h`, read by `MainController` (and through it Brew Settings) and the MCP tools. Equipment creation becomes one static shared by the dialog path and MCP.
- `McpSurfaceVersion` bumps to 1.9.0.

Dose from the scale stays a sequence (`scale_tare`, `scale_get_weight`, `settings_set dyeBeanWeight`): the dialog's virtual zero lives in QML (`StableWeightCapture.qml`) and has no C++ owner.

## Capabilities

### Modified Capabilities
- `mcp-server`: Brew Settings parity through `settings_set`, `settings_get`, `profiles_edit_params`, `equipment` and `machine_start`.
- `grind-rpm-pairing`: the independent RPM override moves from `machine_start` to `settings_set`.

## Impact

- `src/mcp/mcptools_write.cpp` (`settings_set`, `equipment`), `src/mcp/mcptools_settings.cpp`, `src/mcp/mcptools_profiles.cpp`, `src/mcp/mcptools_control.cpp`, `src/mcp/mcpserver.{h,cpp}`.
- `src/core/brewbaseline.h` (new), `src/controllers/maincontroller.{h,cpp}`, `src/controllers/profilemanager.{h,cpp}` (`applyTemperatureToProfile` returns whether it saved), `src/history/equipmentstorage.{h,cpp}`, `qml/components/BrewDialog.qml`.
- `resources/ai/tools/settings_set.md`, `equipment.md`; `docs/CLAUDE_MD/MCP_SERVER.md`, `RECIPES.md`.
- Tests: `tst_mcptools_write` (including the `BrewBaseline` ladder), `tst_mcptools_profiles`, register stubs in `tst_mcpserver_session`/`tst_mcpserver_protocol`/`tst_mcpremoteaccess`.
