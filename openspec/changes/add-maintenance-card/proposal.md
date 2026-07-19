# Maintenance Card: Descale + Transport Mode

## Why

Machine-maintenance operations have no home in Decenza. The Descaling Wizard is
reachable only from an obscure corner of the Profiles page — a button that
appears only when the profile filter happens to be set to "Cleaning/Descale"
(`ProfileSelectorPage.qml`). And there is no way at all to drain the machine for
storage or transport, which users have asked for (issue
[#1152](https://github.com/Kulitorum/Decenza/issues/1152)): when a machine will
be off for days or weeks, water left inside can stagnate or, in freezing
transport, damage the machine.

Both reference apps put maintenance in one place. de1app has a dedicated
maintenance page with Descale, Clean, and Transport buttons; its `travel_prepare`
flow is the model for a guided drain. This change gives Decenza the same home: a
**Maintenance card** on Settings → Machine, directly below Shot Map.

## What Changes

- **New Maintenance card** on the Settings → Machine tab, placed after the Shot
  Map card. It lists maintenance operations, each launching a full-screen guided
  page.
- **Descaling Wizard moves here.** The card gains a "Descaling Wizard" entry that
  launches the existing `DescalingPage`. The old launch button on
  `ProfileSelectorPage` is **removed** — the Maintenance card becomes the single
  home for it.
- **The placeholder descale-wizard profile is removed.** The wizard was surfaced
  in the profile list as a fake, step-less profile (`descale_wizard.json`,
  `beverage_type: "descale"`, `legacy_profile_type: "wizard"`) whose only purpose
  was a special-case tap handler that opened the wizard. With the wizard now a
  first-class Maintenance action, that hack is deleted along with its `.qrc`
  registrations and the special-case tap handler. The real cleaning profiles
  (Forward Flush, Weber Spring Clean, …) stay in the list unchanged.
- **Transport Mode (new).** A new guided page drives the DE1 through an air purge
  (firmware `AirPurge` state, `0x14`), emptying the internal water system so the
  machine can be safely powered off, stored, or shipped. The page mirrors
  `DescalingPage`'s structure: prepare instructions → running/drain progress →
  "safe to power off" confirmation, following de1app's `travel_prepare` flow.
- **New `Transport` machine phase.** `AirPurge (0x14)` maps to a new
  `MachineStateType.Phase.Transport`, added to the auto-sleep-suppression set
  (`operationActive` in `main.qml`) so a multi-minute drain cannot be cut off by
  the inactivity timer — matching how Descaling/Cleaning are already handled.
- **Wiki manual** gains a Maintenance section covering both operations.

## Non-Goals (explicitly deferred)

- **Cold-machine maintenance reliability.** On current firmware (1333 / 1352) a
  GHC-fitted machine's firmware silently drops an `AirPurge`/`Descale`/`Clean`
  request while the machine is still preheating/heating. Both operations here
  therefore assume the machine has reached ready temperature before starting.
  The workaround that makes cold starts work is deferred to a separate, on-hold
  change (`enable-cold-machine-maintenance`) — see its proposal. This change adds
  a clear "wait until the machine is ready" gate rather than shipping a fragile
  workaround.
- **Firmware Clean cycle.** de1app's Clean state (`0x12`) is a group-head
  soak/flush routine. Decenza already ships equivalent "Forward Flush" *profiles*,
  so surfacing the firmware Clean state would duplicate existing functionality.
  Not in scope; may be reconsidered later.

## Capabilities

### New Capabilities

- `machine-maintenance` — a settings-hosted home for machine upkeep operations,
  starting with Descale (relocated) and Transport Mode (new).

## Impact

- **QML:** new `SettingsMachineTab` Maintenance card; new `TransportPage.qml`
  (register in `CMakeLists.txt` and `main.qml`); remove the descale button from
  `ProfileSelectorPage.qml`; add `Phase.Transport` to `operationActive` in
  `main.qml`.
- **C++:** `DE1Device::startAirPurge()` (sends `requestState(AirPurge)`);
  `MachineState::Phase::Transport` enum + `updatePhase()` mapping from
  `State::AirPurge`; simulator support for the AirPurge state so the flow is
  testable without hardware.
- **Docs/manual:** new Maintenance section on the wiki; task included below.
- **No settings-schema change** beyond the card (the operations are actions, not
  persisted preferences).

## Open Questions

- Exact copy for the Transport "prepare" and "done" steps (pull the water tank
  forward, then power off once dry). de1app's wording is the starting point.
