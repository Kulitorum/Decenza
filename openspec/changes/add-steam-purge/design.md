## Context

- `SteamRinse` (0x10) is defined in `de1characteristics.h` and is already in `requestState`'s GC-abort block list, but nothing requests it. `MachineState` has no case for it, so it falls to `default: Phase::Idle`. A purge would therefore read as an idle, ready machine.
- de1app's `start_steam_rinse` sends Idle (optional) then SteamRinse, and nothing ends it from the app. The user has confirmed the firmware ends a purge by itself and accepts it without a GHC tap.
- Steam completion today: on Idle/Ready, `main.qml` shows "Steam Complete" through the shared `OperationCompletionOverlay` (1.5 s, `completionDuration`), then `leaveOperationPage()`. Hot water and flush use the same overlay.
- Heater off is a built-in entry in the steam pitcher list (`SettingsBrew::heaterOffPitcherEntry`). The idle steam row renders that list with `PresetPillRow` in two places: `IdlePage.qml` (full) and `SteamItem.qml` (compact popup).
- The app's start gate (`MachineControlStartPolicy::machineCanStart`) refuses every start when the GHC is active and not in simulation.
- Four hand-written allow-lists of "an operation is running" (`mcptools_control.cpp` stop, `profilemanager.cpp`, `main.cpp`, plus `maincontroller.cpp`'s review kind) would each need the new phase. `MachineState::isOperating()` is a deny-list and already counts it.

## Goals / Non-Goals

**Goals:**
- A purge is visible and blocks other starts while it runs, without counting as a steam session.
- The new phase is added in one place; collections of phases derive from `isOperating()`.

**Non-Goals:**
- No Stop button for a purge (the firmware ends it). Back on the post-steam window is the one way to cut one short.
- No MCP action to start a purge (`machine_start` is unchanged). MCP still reports the phase.
- No change to the two-tap steam stop (`steam_two_tap_stop`) or the auto-flush countdown.

## Decisions

### D1. A `SteamPurge` phase
Add `SteamPurge` to `DECENZA_MACHINE_PHASES` and map `DE1::State::SteamRinse` to it. It is not flowing (no shot or scale timer, like `Transport`), not ready (`isReady` is an allow-list), and operating (`isOperating` is a deny-list). `phaseString()` gives "SteamPurge" for MQTT and MCP.
The hand-written lists in `mcptools_control.cpp` (`operationRunning`), `profilemanager.cpp` (`isActivePhase`) and `main.cpp` (`enteringOp`) become `isOperating()`, so `machine_stop` can end a purge and the phase is listed once. Before replacing `main.cpp`'s entering edge, check that it differs only in using EspressoPreheating as the first espresso signal. `reviewOperationKind` keeps `SteamPurge` at 0: a purge is not a reviewed operation.
*Alternative:* map SteamRinse to `Steaming`. Rejected: that phase drives steam-session finalisation, the health tracker and milk capture, and a purge is none of those. Mapping it to `Flushing` would show "Flush Complete".

### D2. One GHC-exempt start
`DE1Device::startSteamPurge()`: Idle first if not idle, then `requestState(SteamRinse)`, as `start_steam_rinse` does. A `start-steam-purge` command on `MachineControlService` checks the context and `isReady()`, but not `isHeadless`, through a `MachineControlStartPolicy::purgeCanStart` beside `machineCanStart`. That keeps the exemption as one named rule rather than a missing check. The Steam page and the idle pill both use this command.

### D3. A 10 s post-steam window in the shared completion overlay
Steam already ends through `OperationCompletionOverlay` in both shells, and the steam page's post-session warnings already suspend and finish it, so the window lives there rather than in a new page state. A "steam" completion lasts `steamCompletionDuration` (10 s) instead of 1.5 s and shows Purge and Done. Done finishes it. A bottom bar's Back (steam only) requests Idle if a purge or steam is running, then finishes it. Purge calls `start-steam-purge` and `OperationCompletionState.hold()`s the countdown, then finishes once the phase has been through `SteamPurge` and is idle again (or the request fails). A repeat steam completion during the purge is ignored, since native `main.qml` re-announces one when the purge returns to Idle. Hot water and flush keep 1.5 s. The countdown is a UI auto-dismiss, which the timer rule allows.
*Alternative:* a post-steam view inside `SteamPage`. Rejected: it would duplicate the overlay's timing and the suspend/finish handshake the warning dialogs depend on.

### D4. A Purge action pill
`SteamLabels.steamRow()` appends one action entry (`purgePill`) after the pitcher list, which ends with Heater off; both the idle row and the `SteamItem` popup use it, so they cannot drift. It is not in `steamPitcherPresets`, so it is never stored, edited, reordered or selected. `HomePresetActions.tapSteamRowEntry()` routes a tap on it to `startSteamPurge()`, which ignores taps while a purge runs; the pill reads "Purging…" meanwhile. Long-press does nothing on it.

### D5. Simulator purge
`DE1Simulator::startSteamPurge()` enters SteamRinse and returns to Idle after `STEAM_PURGE_SECONDS` (3 s). That is an assumption about the firmware, which we have not timed, and it is recorded as such beside the constant. `DE1Device::requestState` routes SteamRinse to it in simulation. The simulated GHC (D7 of `add-one-tap-americano-long-black`) does not refuse a purge, since the app's controls stand in for taps there anyway.

### D6. Status surfaces
Widget label tables (`ios/widget/MachineStatusSnapshot.swift`, `MachineStatusWidgetProvider.java`, `WIDGET_SNAPSHOT.md`) gain "SteamPurge" → "Purging". MQTT and MCP carry the raw phase string, as they do for every phase.

## Risks / Trade-offs

- [The purge's real duration and wand behaviour are untimed.] → The 10 s window only has to cover the tap; the page waits for the phase, not a clock. Time a real purge during validation and correct the simulator constant.
- [A new phase reaches platform widget code that CI builds only on tag push.] → Platform test builds for iOS and Android before release, as the widget tables are platform-guarded.
- [Collapsing the operation lists changes `machine_stop` and the auto-load guard for any phase they missed.] → Diff each list against `isOperating()` before replacing it. A phase in one and not the other is either a bug fixed or a deliberate exception, which keeps its own check.

## Migration Plan

No data migration. Rollback is reverting the code.
