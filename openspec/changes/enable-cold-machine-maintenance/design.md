# Design — Enable Cold-Machine Maintenance (ON HOLD)

> Parked pending firmware. This file records the decision and the fallback so the
> work can resume without re-deriving it.

## The firmware behaviour we are working around

DE1 firmware ≤ 1352 on GHC-fitted hardware refuses (silently drops) a maintenance
state request — `AirPurge` (`0x14`), `Descale` (`0x0A`), `Clean` (`0x12`) —
while the machine is in a preheating/heating state. The request is only honored
after the machine leaves preheat and reaches its goal temperature.

Decenza already has everything needed to detect the relevant conditions:
- `m_firmwareBuildNumber` (DE1Device) — the connected machine's CPU firmware build.
- GHC status (`requestGHCStatus()` / GHC_INFO MMR read).
- Machine phase (`Preheating`/`Heating` vs `Idle`/`Ready`).

## Decision: wait for the native firmware fix

**Chosen:** rely on a firmware build that honors cold maintenance natively. When
the connected machine reports build ≥ threshold, drop the "must be ready"
precondition; below it, keep the precondition from `add-maintenance-card`.

```
canStartColdMaintenance =
    firmwareBuild >= COLD_MAINTENANCE_NATIVE_BUILD   // exact value TBD from the real release
```

Rationale:
- No app-side heuristic in the maintenance path.
- Self-retiring and forward-safe: the gate turns itself off as users update.
- Matches how reaprime frames it (a build threshold), without adopting reaprime's
  unproven pre-profile workaround.

## Rejected (for now): the pre-maintenance-profile workaround

Both reference apps make cold maintenance work on old firmware by loading a
throwaway 1°C profile first:

- **de1app** — `de1_send_pre_maintenance_profile` + `de1_send_shot_frames`
  (`machine.tcl`, `de1_comms.tcl`), then `after 1000` before sending the state.
  Long-standing.
- **reaprime** — `_prepareColdMaintenanceWorkaround` inside `requestState()`
  (`unified_de1.dart`, added 2026-07-11): single-frame 1°C pressure profile,
  `tankTemperature: 0`, gated on `cold && ghcPresent && fwBuild < 1356`, wait 1s,
  send state. Centralized so every caller benefits. Self-described "v1."

If we ever adopt this instead of waiting, the shape in Decenza would be:
- Fold a cold-maintenance prep into `DE1Device::requestState()` (or a helper the
  maintenance starters call) gated on `isMaintenanceState && cold && ghcPresent`.
- Send a minimal 1°C profile (group goal 1°C so the group stops heating; tank
  threshold 0 so the tank stops heating), wait ~1s, then send the state.
- No save/restore needed — the next brew's profile re-sets the tank threshold, as
  de1app does every shot.

Why not now: it is a fragile, recently-written heuristic in a path where a clean
firmware fix is expected. We keep the design here so the option is one edit away
if the firmware stalls.

## Interaction with `add-maintenance-card`

That change gates the Transport (and descale) start on ready temperature
unconditionally. This change makes that gate **firmware-conditional**: ready
required below the threshold build, not required at/above it. The spec delta here
modifies that requirement rather than removing the guard outright — cold starts
are enabled only where the firmware actually supports them.
