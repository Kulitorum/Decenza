# Tasks — Maintenance Card: Descale + Transport Mode

## 1. Maintenance card + Descale relocation
- [x] 1.1 Add a Maintenance card (`objectName: "maintenance"`) to
  `SettingsMachineTab.qml`, positioned directly after the Shot Map card, using
  the existing card grammar.
- [x] 1.2 Add a "Descaling Wizard" row that calls `goToDescaling()` (via an
  `openDescaling` signal forwarded by `SettingsPage`, since tabs load through a
  Loader that breaks `root` resolution).
- [x] 1.3 Remove the Descaling Wizard `AccessibleButton` from
  `ProfileSelectorPage.qml` (the `viewFilter.currentIndex === 1` button).
- [x] 1.4 Add the maintenance card/rows to the settings search index
  (`SettingsSearchIndex.js`).
- [x] 1.5 Remove the placeholder descale-wizard *profile* (the wizard was
  implemented as a fake, step-less profile). Deletes
  `resources/profiles/descale_wizard.json`, its two `.qrc` registrations, and the
  `descale_wizard.json` / `beverageType === "descale"` special-case tap handler
  in `ProfileSelectorPage.qml`. Prune the stale `profile_knowledge.json`
  `alsoMatches` entry and the `tst_blefidelity.cpp` comment example.

## 2. Transport Mode — machine layer
- [x] 2.1 Add `DE1Device::startAirPurge()` → `requestState(DE1::State::AirPurge)`.
- [x] 2.2 Add `Phase::Transport` to `MachineState::Phase` (machinestate.h) with a
  descriptive comment.
- [x] 2.3 Map `State::AirPurge` → `Phase::Transport` in `updatePhase()`
  (machinestate.cpp).
- [x] 2.4 Add simulator support for the AirPurge state
  (`DE1Simulator::startAirPurge()`) so the flow runs without hardware.

## 3. Transport Mode — UI
- [x] 3.1 Create `TransportPage.qml` (prepare → running → done), modelled on
  `DescalingPage.qml`. Include the "machine must be ready" gate on the start
  action.
- [x] 3.2 Register `TransportPage.qml` in `CMakeLists.txt` (qml module file list).
- [x] 3.3 Register the page component + title in `main.qml`; add a
  `goToTransport()` navigation function. (Also added a Transport auto-nav branch
  and TalkBack phase announcement for parity with Descaling.)
- [x] 3.4 Add a "Transport Mode" row to the Maintenance card that calls
  `goToTransport()` (via the `openTransport` signal).
- [x] 3.5 Add `Phase.Transport` to `operationActive` in `main.qml` so auto-sleep
  is suppressed during a drain.

## 4. Internationalisation & accessibility
- [x] 4.1 Add translation keys for all new user-visible strings (card title,
  descriptions, Transport steps, buttons) via `TranslationManager.translate` / `Tr`.
- [x] 4.2 Ensure every new interactive element has role, name, focusable, and
  press action (Maintenance rows use `AccessibleMouseArea`; inner text is
  `Accessible.ignored` to avoid double-reads). Fix any pre-existing a11y
  violations in files touched.

## 5. Manual & docs
- [ ] 5.1 Add a Maintenance section to the wiki manual
  (`Kulitorum/Decenza.wiki`) covering Descaling Wizard (new location) and
  Transport Mode. Hold the push per the wiki-edits-held-for-release convention.

## 6. Verify & review
- [x] 6.0 Clean build via Qt Creator — 0 errors, 0 warnings. (Confirms C++ +
  resource changes; QML is runtime-loaded so the screens still need a live run.)
- [ ] 6.1 Run the Transport flow end-to-end in the simulator (start → Transport
  phase → Idle → done); confirm auto-sleep is suppressed during the drain.
- [ ] 6.2 Confirm Descale still launches and completes from its new location, and
  that the placeholder descale profile is gone from the Profiles list.
- [x] 6.3 Run `/pr-review-toolkit:review-pr`. Fixed: stale ProfileSelectorPage
  comment; STOP-mid-drain no longer shows the false "machine is empty"
  confirmation and completion now latches on any non-Transport landing phase
  (not just Idle/Ready); added `Phase::Transport` to `MachineStatusItem` status
  color/text and to the `machine_stop` MCP guard, the idle-GC `enteringOp` list,
  and the `uploadCurrentProfile` active-phase guard; switched the checkmark to
  `Theme.emojiToImage`. Rebuilt clean.
- [ ] 6.4 **Hardware verify (real GHC machine, fw 1333/1352):** confirm AirPurge
  self-terminates (returns to Idle on its own) and that the physical GHC stop
  exits AirPurge. If it can hang with no on-screen stop on GHC, show the STOP
  button on GHC machines too (currently `isHeadless`-gated, matching Descale).
  Not fixed pending that observation.
