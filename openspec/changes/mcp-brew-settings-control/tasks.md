## 1. One definition of the baseline

- [x] 1.1 `MainController`: `activeBaselineYieldSource` (`recipe` | `bag` | `profile`) from the same ladder walk as the baseline value and mode.
- [x] 1.2 `MainController`: `yieldPersistTarget` (`recipe` | `bag` | empty), moved from `BrewDialog.qml`.
- [x] 1.3 `BrewDialog.qml` reads both from `MainController` instead of re-deriving them. The walk itself lives in header-only `src/core/brewbaseline.h` so the MCP tests reach it without linking `MainController`.

## 2. settings_set

- [x] 2.1 `targetWeight` / `yieldRatio` arm the brew yield override (mutually exclusive, 0 clears); dose in the same call applies first.
- [x] 2.2 `espressoTemperature` sets or clears the temperature override; no profile edit.
- [x] 2.3 `clearBrewOverrides` restores the baseline yield anchor and temperature.
- [x] 2.4 `ratioPreset1`-`3`, `doseCupTareWeight`, `doseCaptureSoundEnabled`.
- [x] 2.5 Tool description and `resources/ai/tools/settings_set.md` describe brew-override semantics and the dose-from-scale sequence.

## 3. settings_get

- [x] 3.1 Category `espresso` reports effective target, anchor, temperature override, baseline + source, real-override flags, persist target, presets, tare, sound.

## 4. Other tools

- [x] 4.1 `profiles_edit_params espressoTemperature` through `applyTemperatureToProfile`, which now returns whether the profile was saved so `saved` reports the save rather than the attempt.
- [x] 4.2 `equipment action=create`; `equipment.md` updated. Creation shares `EquipmentStorage::createPackageStatic` with the dialog path.
- [x] 4.3 `machine_start` loses its override arguments; description points to `settings_set`.
- [x] 4.4 Bump `McpSurfaceVersion` + fingerprint; `check_mcp_tool_budget.py` passes.
- [x] 4.5 `docs/CLAUDE_MD/MCP_SERVER.md` tool list.

## 5. Tests

- [x] 5.1 `tst_mcptools_write`: yield ratio, absolute, both-keys rejection, temperature override, clear; existing targetWeight/espressoTemperature tests follow the new semantics. Presets are plain setters and carry no test. The baseline ladder is asserted directly on `BrewBaseline`.
- [x] 5.2 `tst_mcptools_write`: `equipment action=create`.
- [x] 5.3 `tst_mcptools_profiles`: `espressoTemperature` saves and clears the override (the frame shift stays asserted in `tst_profilemanager`).
- [x] 5.4 Register-function stubs updated in `tst_mcpserver_session`, `tst_mcpserver_protocol`, `tst_mcpremoteaccess`.
- [x] 5.5 Break each new path and watch its test go red; full suite green via Qt Creator; qmllint clean for `BrewDialog.qml`.
- [x] 5.6 Live check over the `decenza` MCP: ratio, temperature override, both-keys refusal, Clear back to the recipe baseline, `settings_get` readback, equipment create dedup. It found create reporting `shotCount: 0` for an existing package; fixed by filling the count as update and merge do.
