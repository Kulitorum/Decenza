## Why

The Steam page's Purge buttons were removed in `remove-steam-purge-buttons` (2026-07-23) because nobody knew how to make a purge work: they only sent Idle, which duplicated Stop. The DE1 has a dedicated steam-purge state, SteamRinse (0x10), that the firmware ends by itself, and it does not need a GHC tap, even on a machine whose group head controller is active. A Purge button can now do something Stop cannot.

## What Changes

- **Purge** requests SteamRinse. The app allows it whenever the machine is ready, including with the GHC active, and the firmware ends it.
- After steaming ends, the Steam page stays up for 10 seconds with **Purge** and **Done**, instead of leaving after the 1.5 s completion message. Hot water and flush keep 1.5 s.
- The idle page's steam row gets a **Purge** pill after **Heater off**. One tap starts a purge; it is not a pitcher preset.
- While purging, the app reports a new `SteamPurge` phase: the machine shows as busy, other starts are unavailable, and the home-screen widget, MQTT and MCP name it.
- The simulator runs a purge for a few seconds and ends it by itself, so the flow can be tested there first.
- **Supersedes** the `steam-stop-controls` requirement that the Steam page SHALL NOT present a dedicated Purge control.

## Capabilities

### New Capabilities

- `steam-purge`: starting a firmware steam purge from the Steam page and the idle steam row, what the app shows while it runs, and the post-steam window that offers it.

### Modified Capabilities

- `steam-stop-controls`: removes "Steam page SHALL NOT present a dedicated Purge control", which described buttons that only sent Idle.

## Impact

- `MachineState` phase list (`machinephase.h`) and every consumer that names phases: widget label tables (iOS, Android, `WIDGET_SNAPSHOT.md`). Three hand-written "operation running" lists (`mcptools_control.cpp`, `profilemanager.cpp`, `main.cpp`) and `main.qml`'s `operationActive` collapse onto `MachineState::isOperating()`, so the new phase is counted once.
- `DE1Device` purge start, `MachineControlService` command with a GHC-exempt start policy, simulator purge.
- QML: the post-steam window in the shared `OperationCompletionOverlay.qml`, Back on `SteamPage.qml`, the idle steam pill rows (`IdlePage.qml`, `SteamItem.qml`), and a screen-reader announcement for a purge in `main.qml`.
- Wiki manual: the Steam section.
