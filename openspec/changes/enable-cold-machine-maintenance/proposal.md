# Enable Cold-Machine Maintenance (ON HOLD)

> **Status: ON HOLD — do not implement yet.**
> Blocked on a DE1 firmware release that natively honors cold maintenance
> requests on GHC-fitted hardware. reaprime estimates this is build **1356**;
> no such build is available today (the newest bundled stable firmware in the
> ecosystem is **1352**, which Decenza already supports). Unblock this change
> when that firmware is publicly released and Decenza can validate against it.

## Why

On current firmware (builds 1333 / 1352), a DE1 with a Group Head Controller
(GHC) **silently drops** a maintenance state request — `AirPurge`, `Descale`, or
`Clean` — while the machine is still preheating or heating. The firmware only
honors the request once the machine has left preheat and reached its goal
temperature.

The sibling change `add-maintenance-card` ships Descale and Transport Mode with a
deliberate mitigation: it requires the machine to be **ready** before the drain
can start. That is correct and honest, but it is a limitation — a user who just
woke a cold machine (or whose machine froze during storage) cannot start a
transport drain immediately, which is precisely the moment Transport Mode is most
useful.

There are two ways to make cold starts work:

1. **Native firmware fix (chosen).** A newer firmware honors cold maintenance
   directly. When it ships, Decenza offers/supports it and simply drops the
   "must be ready" gate — no app-side heuristic. This is the path we are waiting
   on.
2. **App-side workaround (rejected for now).** Mirror de1app's
   `de1_send_pre_maintenance_profile` / reaprime's `_prepareColdMaintenanceWorkaround`:
   before a cold maintenance request, load a 1°C single-frame profile (group goal
   1°C, tank threshold 0) so the machine leaves preheat, wait ~1s, then send the
   state. de1app has used the underlying trick for years, but reaprime's
   packaged, build-gated version is a one-week-old "v1" from a single author. We
   would rather not ship a fragile pre-profile heuristic into the maintenance
   path if a clean firmware fix is imminent.

This change is the **firmware-fix path** and stays parked until that firmware is
real. If the firmware fix stalls indefinitely, revisit option 2 — the design
below records exactly what it would entail.

## What Changes (when unblocked)

- **Support the fixed firmware.** Ensure Decenza's firmware handling recognizes
  and (if bundled/offered) provides the build that natively honors cold
  maintenance; confirm the machine's reported build gates behavior correctly.
- **Relax the "must be ready" gate.** When the connected machine reports a build
  ≥ the fixed threshold, Transport Mode (and Descale) SHALL allow starting from a
  cold machine. On older builds the gate from `add-maintenance-card` stays.
- **Remove the cold-start limitation note** from the manual/UI for supported
  firmware.

## Capabilities

### Modified Capabilities

- `machine-maintenance` — cold-machine reliability added; the ready-temperature
  precondition becomes firmware-conditional rather than absolute.

## Impact

- **C++:** firmware-build-aware gating (Decenza already tracks
  `m_firmwareBuildNumber` and GHC status); no new BLE states.
- **QML:** `TransportPage` (and any descale gate) consult the build to decide
  whether the ready precondition applies.
- **Manual:** update the Maintenance section once cold starts are supported.

## Open Questions

- The exact firmware build that fixes this natively (reaprime's 1356 is an
  estimate and self-described as unconfirmed). Confirm against the real release
  before wiring the threshold.
- Whether the fixed firmware will be bundled (like reaprime's 1352) or offered
  via the existing updater path.
