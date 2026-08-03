## Why

[#1754](https://github.com/Kulitorum/Decenza/pull/1754) made the wire honest: a
tool that returns an `error` key is now reported to the client as a failed call.
An audit of all 15 `mcptools_*.cpp` files while verifying that fix found the
convention is followed with unusual consistency — roughly 180 failure returns,
every one a top-level `error`, no `errors` / `errorMessage` / `status: "failed"`
variants anywhere.

The remaining gap is the other half of the problem, and #1754 cannot reach it:
**tools that report success for an operation that did not happen.** No `error`
key is written, so there is nothing for the wrap step to mark. Four of these are
outright wrong rather than merely vague:

- `profiles_set_active` reports "Profile activated" for a profile the
  ProfileManager **refused**. `loadProfile` is `void` and its refusal path
  (`src/controllers/profilemanager.cpp:1599-1606`) deliberately keeps the
  *previously* active profile. The machine goes on brewing the old profile while
  MCP tells the model the new one is live.
- `shots_delete` reports "Shot N deleted" when nothing was deleted, and on a
  genuine database failure **never responds at all** — the failure path emits
  `errorOccurred`, not the `shotDeleted` the tool waits on, so the client hangs
  forever with no error and no timeout.
- `shots_update` reports success for a shot ID that does not exist:
  `updateShotMetadataStatic` returns `true` on `query.exec()` without consulting
  `numRowsAffected()`.
- `apply_theme` reports "Applied theme: X" for a theme name that matches nothing;
  `applyPresetTheme` is `void` and falls off the end of its loop.

A model told an operation succeeded does not retry, does not warn the user, and
proceeds on a false premise. That is the same failure class #1754 fixed one layer
down, and the same reason it went unnoticed: nothing on the wire contradicted it.

## What Changes

- **An operation reports the outcome it actually had.** Where the underlying call
  can report success, the tool consults it instead of assuming; where it cannot,
  the call is given a way to say so. Covers `profiles_set_active`, `shots_delete`,
  `shots_update`, and `apply_theme`.
- **A tool that waits for a signal also handles the failure signal.**
  `shots_delete`'s never-responds path is closed. This is the one defect here
  with no wire representation at all — not a wrong answer, no answer.
- **An unavailable dependency is reported, not rendered as emptiness.** Four
  profile read tools return a bare `{}` when `ProfileManager` is null while every
  sibling guard in the same file sets `error`; `steam_get_health` returns
  `status: ""`, which is both unreadable and a violation of the
  human-readable-enum convention.
- **Silently dropped inputs are named.** `shots_compare` discards shot IDs that do
  not resolve and returns a shorter list; the caller can only detect it by
  comparing counts, and no ID is named.
- **A no-op is distinguishable from a success.** `devices_connect_de1` returns a
  bare `message` when already connected — discarding the address the caller asked
  for — and `mqtt_disconnect` signals "nothing to disconnect" by `message` alone.
  Neither carries `success` or `error`, which is a third state a model cannot
  classify. Both become a success flagged as a no-op. (`mqtt_publish_discovery`
  keeps treating a missing connection as an error, which is not an
  inconsistency: it cannot do its job without one, whereas `mqtt_disconnect`'s
  job is already done.)
- **Scale timer tools stop claiming to have done something.**
  `ScaleDevice::startTimer/stopTimer/resetTimer` are virtual with empty default
  bodies, so on a scale without timer support all three return
  `{"success": true}`. The device gains a capability flag the tools consult.
The seventh audit item — the server's own silent refusals and dropped responses —
**already shipped in #1754**, which added the rate-limit log, the dropped-response
logs, and the `sendHttpResponse` short-write check while registering the `[MCP]`
log subsystem. It is listed here only so a reader of the audit does not go
looking for it.

## Capabilities

### New Capabilities

- `mcp-tool-outcome-reporting` — what a tool's result must reflect about the
  operation it performed: outcome over intent, dependency unavailability as an
  error rather than emptiness, and dropped inputs named rather than omitted.

### Modified Capabilities

None. No existing spec constrains these tools' success semantics.

## Impact

- `src/mcp/mcptools_write.cpp` — `profiles_set_active`, `shots_delete`,
  `shots_update`.
- `src/mcp/mcptools_control.cpp` — `apply_theme`, `mqtt_disconnect`.
- `src/mcp/mcptools_profiles.cpp` — four null-dependency guards.
- `src/mcp/mcptools_shots.cpp` — `shots_compare` unresolved IDs.
- `src/mcp/mcptools_machine.cpp` — `steam_get_health` unavailable tracker.
- `src/mcp/mcptools_devices.cpp` — `devices_connect_de1` already-connected.
- `src/mcp/mcptools_scale.cpp`, `src/ble/scaledevice.h` — timer capability flag,
  plus the drivers that implement timers.
- `src/controllers/profilemanager.h/.cpp`, `src/history/shothistorystorage.cpp`,
  `src/core/settings_theme.cpp` — outcome-reporting return values or signals for
  three currently-`void` operations.
- `docs/CLAUDE_MD/MCP_SERVER.md` — the outcome convention beside the `error`-key
  convention #1754 documented.
- **Wire-visible**: calls that previously reported success will start reporting
  failure. That is the point, and per the project's standing position MCP surfaces
  are free to change. No tool's success payload changes shape when the operation
  actually succeeds.
