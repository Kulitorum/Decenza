## Why

Two recent de1app fixes (upstream commits `13a30463`, `04d3b02e`) turned out to have live
counterparts in Decenza. Stop-at-Volume on a Pressure-type profile counts the "forced rise
without limit" ramp — water spent bringing the group to pressure before any coffee pours — as
part of the pour volume, so a shot set to stop at 40 mL can stop noticeably short. And the DE1's
front standby-switch condition (substate `Error_NoAC`, "machine has no AC power") has no handling
at all: no warning is shown, and if it did occur mid-session Decenza's substate tracking would
never clear it on disconnect, exactly the staleness bug de1app had to fix alongside its own
warning page.

## What Changes

- Fix `RecipeGenerator::generatePressureFrames()` / `Profile::countPreinfuseFrames()` so the
  synthetic "forced rise without limit" frame(s) generated for a Pressure-type profile's rise
  (and, when present, decline) ramp are counted into the preinfusion frame count. The DE1 then
  reports those frames under the Preinfusion substate, so `MachineState` routes their flow into
  `m_preinfusionVolume` instead of `m_pourVolume`, and Stop-at-Volume no longer starts counting
  before coffee actually pours. Flow-type profiles generate no such frame and are unaffected.
- Add `Error_NoAC` (substate 217, "front standby switch is cutting AC") to `DE1::SubState`, and
  reset it on disconnect in `DE1Device::onTransportDisconnected()` so a stale value can't linger
  across a BLE drop.
- Add a "Push the switch on" full-screen warning, shown while `Error_NoAC` is the live substate
  and gated on `firmwareBuildNumber() >= 1337` (older firmware reports this substate spuriously,
  matching why de1app gated the same way). Dismissible by tap; reappears if the condition recurs.
  Driven from the machine-state signal path, so it works uniformly rather than depending on a
  particular skin/page — the bug that limited de1app's original attempt to DSx only.

## Capabilities

### New Capabilities
- `standby-switch-warning`: detecting the DE1 front standby-switch AC-loss condition
  (`Error_NoAC`) and showing/clearing a full-screen warning for it.

### Modified Capabilities
- `de1app-profile-parity`: preinfusion frame count for a generated Pressure profile must include
  its synthetic forced-rise (and decline) ramp frames, matching de1app's SAV-exclusion behavior.

## Impact

- `src/profile/recipegenerator.cpp` (`generatePressureFrames()`), `src/profile/profile.cpp`
  (`countPreinfuseFrames()`) — preinfusion frame count for generated Pressure profiles.
- `src/ble/protocol/de1characteristics.h` (`DE1::SubState`) — add `Error_NoAC`.
- `src/ble/de1device.cpp` (`parseStateInfo()`, `onTransportDisconnected()`) — recognize and reset
  the new substate.
- `src/machine/machinestate.cpp` — expose the condition to QML (existing substate/phase dispatch
  path).
- `qml/main.qml` or a new QML overlay — the warning page/dialog, following the existing
  machine-driven modal-overlay pattern (e.g. the refill dialog).
- No BLE protocol change: `Error_NoAC` is an existing DE1 firmware substate value, already sent
  today and previously unrecognized by Decenza.
- `tools/profile_sync.cpp` — fixed a masking bug in `normaliseSimpleProfile()` that made its
  `--sync`/compare mode blind to `preinfuseFrameCount` drift (found while regenerating the 17
  affected built-ins for this change; not itself part of the Stop-at-Volume behavior fix).
