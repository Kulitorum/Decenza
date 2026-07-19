# Design — Maintenance Card: Descale + Transport Mode

## Card placement and grammar

`SettingsMachineTab.qml` is a vertical stack of self-contained `Rectangle`
cards, each with an `objectName`, `Theme.cardBackgroundColor`, and
`Theme.cardRadius`. The Shot Map card (`objectName: "shotMap"`, ~line 368) is the
anchor; the new card (`objectName: "maintenance"`) is inserted immediately after
it, before `themeMode`. No new layout machinery — same card idiom as its
neighbours.

The card is a title + short description + one row per operation. Each row is an
`AccessibleButton` (or an `AccessibleMouseArea`-backed row) with an emoji marker
paired with a word (never emoji alone), a one-line description, and a chevron.
Emoji render through `Theme.emojiToImage()` / `Theme.replaceEmojiWithImg()` — never
a bare `Text`.

```
┌──── 🧰 Maintenance ───────────────────────────┐
│  Keep your machine clean and ready for storage.│
│                                                │
│  🧽  Descaling Wizard              [ Open › ]  │
│      Remove scale buildup from the boiler      │
│                                                │
│  🧳  Transport Mode                [ Open › ]  │
│      Drain all water before storage/transport  │
└────────────────────────────────────────────────┘
```

## Descale relocation

The wizard page (`DescalingPage.qml`) and its navigation (`goToDescaling()` in
`main.qml`) already exist and are unchanged. This change only moves the *entry
point*:

- **Add:** the Maintenance card's "Descaling Wizard" row calls `goToDescaling()`.
- **Remove:** the `AccessibleButton` in `ProfileSelectorPage.qml` (~line 242,
  `visible: viewFilter.currentIndex === 1`). The descale *profiles* remain in the
  list; only the wizard-launch button is removed.

`DescalingPage` drives the machine via `DE1Device.startDescale()` (firmware
`Descale` state), so it does not depend on the Profiles page to function.

## Transport Mode

### Machine driving

Add `DE1Device::startAirPurge()`:

```cpp
void DE1Device::startAirPurge() {
    requestState(DE1::State::AirPurge);   // 0x14, already defined in de1characteristics.h
}
```

`AirPurge` is already in the `DE1::State` enum. The simulator
(`DE1Simulator`) gains a matching `startAirPurge()` that transitions through a
short drain and back to Idle, mirroring `startDescale()`/`startClean()`, so the
whole flow is exercisable without hardware.

### New Transport phase

`MachineState::Phase` currently ends at `Descaling`, `Cleaning`. Add `Transport`:

- `machinestate.h`: add `Transport` to the `Phase` enum (comment: "Machine is
  draining water for transport").
- `machinestate.cpp` `updatePhase()`: map incoming `State::AirPurge` →
  `Phase::Transport`.
- `main.qml` `operationActive` (~line 402): add
  `phase === MachineStateType.Phase.Transport` so the auto-sleep countdown pauses
  during a drain, exactly as it already does for Descaling/Cleaning.

### The page

`TransportPage.qml`, modelled on `DescalingPage.qml`:

1. **Prepare step.** Explains what Transport Mode does and instructs the user
   (pull the water tank forward when prompted, per de1app's `travel_prepare`).
   A **"machine ready" gate**: the Start button is enabled only when the machine
   has reached ready temperature (not `Preheating`/`Heating`). This is the
   deliberate, honest mitigation for the deferred cold-maintenance limitation —
   on current firmware + GHC a cold `AirPurge` request is dropped, so we require
   ready before starting rather than fail silently.
2. **Running step.** Shows drain progress while `phase === Phase.Transport`.
3. **Done step.** Once the machine returns to Idle/Ready with the tank empty,
   shows "You can power off now — the machine is ready for transport."

Register `TransportPage.qml` in `CMakeLists.txt` (qml module file list) and in
`main.qml` (page component + title map), and add a `goToTransport()` navigation
function alongside `goToDescaling()`.

## Why the cold-maintenance workaround is *not* here

Both supported firmwares (1333, 1352) sit below the build reaprime estimates
(1356) will natively honor cold maintenance on GHC hardware. reaprime ships a
one-week-old "v1" workaround (load a 1°C profile, wait 1s, then send the state).
We are choosing **not** to port that unproven heuristic now; instead we:

- require the machine to be ready before Transport/Descale can start (the gate
  above), which sidesteps the cold case for the common path, and
- capture the full cold-start fix in a separate on-hold change
  (`enable-cold-machine-maintenance`) that unblocks when the fixed firmware is
  released.

This keeps this change small and avoids shipping a fragile timer-adjacent
heuristic into the maintenance path. See the sibling change for the rationale and
the two options considered.

## Accessibility

Each operation row: `Accessible.role`, `Accessible.name` (word, not emoji —
`Theme.toAccessibleText()`), `Accessible.focusable`, `Accessible.onPressAction`.
`TransportPage` step headings and the Start button follow the same rules as
`DescalingPage`. Fix any pre-existing violations in files touched.

## Testing

- Simulator path lets the Transport flow run end-to-end (start → Transport phase
  → Idle → done) with no hardware.
- No QML test harness exists (per project convention); the C++ additions
  (`startAirPurge`, `Phase::Transport` mapping) are the testable surface if a
  unit test is warranted. Verify manually in the app per the `verify` skill.
