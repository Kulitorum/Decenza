## 1. Steam-start decision logging

- [x] 1.1 In `SteamPage.qml`'s `onIsSteamingChanged` (~line 152-176), right where `_scaledNow` is computed and before it's applied, add a `console.log` reporting: `AppShell.sessionMeasuredMilkG`, `steamPage.lastOnScaleMilk`, `Settings.brew.milkAutoCaptureEnabled`, `Settings.brew.steamSecondsPerGram`, the selected pitcher's name and `disabled` flag (via `Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)`), and the `_scaledNow` result.
- [x] 1.2 Right after the existing `if (_scaledNow > 0) { ... }` block, log the value actually written to `Settings.brew.steamTimeout` and tag it `"scaled"` or `"fixed-fallback"` (i.e. `_scaledNow > 0` vs not) — or `"user-adjusted, unchanged"` when `steamTimeoutUserAdjusted` short-circuited the calculation.

## 2. Page-activation / pitcher-lift sync logging

- [x] 2.1 In `syncSteamTimeout()` (~line 434-448), add the same set of values (milk sources, toggle, calibration rate, pitcher name/disabled, computed `scaled` result) as one `console.log` at the top of the function, before the early-return for a disabled pitcher.
- [x] 2.2 Log the final `scaled` value and whether it was applied, at the point the function currently sets/skips the timeout.

## 3. Verify

- [x] 3.1 Build via Qt Creator MCP (`mcp__qtcreator__build`), confirm no warnings introduced.
- [ ] 3.2 Manually exercise weight-timed steaming from the idle screen (existing working path) and confirm the new log lines show milk/calibration/outcome as expected — this validates the logging itself before waiting on the harder-to-reproduce GHC/Shot-Review path.
- [ ] 3.3 Ask the user to reproduce the original GHC-from-Shot-Review sequence once, capture the resulting debug log (`debug_get_log` or on-device), and confirm which of the four gates (or none) explains the fallback.

## 4. Follow-up (not part of this change)

- [ ] 4.1 Once the log confirms the actual cause, file the real fix as a separate change.
- [ ] 4.2 Separately reconcile `openspec/specs/weight-timed-steaming/spec.md`'s "Per-pitcher calibration" requirement, which still describes the old per-preset `calibMilkG`/`duration` ratio math — the current implementation (`SettingsBrew::scaledSteamTime`) uses a single global `steamSecondsPerGram` rate instead (`settings_brew.cpp:614-624`).
