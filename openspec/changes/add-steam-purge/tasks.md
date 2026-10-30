## 1. The SteamPurge phase

- [x] 1.1 Add `SteamPurge` to `DECENZA_MACHINE_PHASES` and map `DE1::State::SteamRinse` to it in `MachineState` (D1). Verify: `tst_machinestate` `phaseMapping` row for SteamRinse; `isFlowing`, `isReady` and `isOperating` asserted for it.
- [x] 1.2 Replace the hand-written operation lists in `mcptools_control.cpp`, `profilemanager.cpp` and `main.cpp` with `MachineState::isOperating()`, after diffing each against it (D1). Verify: build passes; an existing `machine_stop` or auto-load test, or a new `_data` row, shows SteamPurge counted.
- [x] 1.3 Add "SteamPurge" → "Purging" to the iOS and Android widget label tables, `WIDGET_SNAPSHOT.md`, the in-app `MachineStatusItem`, and the `machine_get_state` phase list (which also lacked Transport; `McpSurfaceVersion` 1.21.0) (D6). Verify: each lists it; the MCP budget check passes.

## 2. Starting a purge

- [x] 2.1 Add `DE1Device::startSteamPurge()` and a `MachineControlStartPolicy::purgeCanStart` that ignores the GHC state (D2). Verify: a policy test (the file that tests `machineCanStart`) accepts a ready machine with the GHC active and refuses a busy one.
- [x] 2.2 Add the `start-steam-purge` command to `MachineControlService` (`.rep` is unchanged; facade method and native source) (D2). Verify: `tst_machinecontrolservice` asserts the command reaches the source with its context.
- [x] 2.3 Simulator: enter SteamRinse, return to Idle after `STEAM_PURGE_SECONDS`, with the duration marked as an assumption (D5). Verify: `tst_de1simulator` sees SteamRinse, then Idle without a stop.

## 3. The UI

- [x] 3.1 Post-steam window in the shared `OperationCompletionOverlay` (D3): a 10 s "steam" completion with Purge and Done; Purge holds it until the purge returns to idle; Done finishes it. Hot water and flush keep 1.5 s. Verify: qmllint gate passes (done); in the simulator, steam, then each of Purge, Done and no tap, behaves as specified. Simulator 2026-10-30: Purge showed "Purging…", held through SteamRinse and left on Idle (~3 s); Done left at once; no tap stayed up past 6 s and was gone by 12 s with no purge sent.
- [x] 3.2 Purge pill after Heater off via `SteamLabels.steamRow()` in `IdlePage.qml` and `SteamItem.qml`: one tap starts a purge, reads "Purging…" and ignores taps while it runs, and is not a stored preset (D4). Verify: in the simulator, the pill purges with the selected pitcher unchanged. Simulator 2026-10-30, idle-page row: "Purging…" and status Purging, back to Ready after ~3 s, Small Pitcher still selected, page unchanged. The `SteamItem` popup shares `steamRow()` and was not opened.
- [x] 3.3 Bottom bar with Back on the steam completion (D3): Back requests Idle if a purge or steam is running, then leaves. Verify: in the simulator, Back with nothing running leaves; Back mid-purge ends the purge (State -> Idle before the 3 s) and leaves without a second Steam Complete. Simulator 2026-10-30: Back with nothing running went to idle; Back 0.3 s into a purge logged Stop requested, State -> Idle, and the shell saw idlePage, so no re-announce.
- [x] 3.4 Steam page bottom bar shown while steam runs; Back acts as Stop then, and leaves otherwise. Verify: in the simulator, Back mid-steam stops it and the Steam Complete window follows; Back on the settings view leaves. Simulator 2026-10-30: the bar showed while steaming with Back announced as "Stop steaming and go back"; Back stopped steam and the Steam Complete window followed, as Stop does. Back on the settings view (no steam) read "Back. Return to previous screen" and returned to idle with no stop sent.

## 4. Docs and validation

- [x] 4.1 Wiki manual, Steam section: two sentences on Purge after steaming and the idle Purge pill. Verify: drafted in the wiki clone, pushed only on the user's OK.
- [x] 4.2 Full suite through Qt Creator. Verify: all green.
- [ ] 4.3 Real DE1 with the GHC active: Purge from the idle pill starts without a tap and ends by itself; time it and correct `STEAM_PURGE_SECONDS`. Verify: result recorded here; held for the user if no machine is available.
