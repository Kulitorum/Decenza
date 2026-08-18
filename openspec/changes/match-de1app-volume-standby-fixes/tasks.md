## 1. Stop-at-Volume: forced-rise frames excluded from pour volume

- [x] 1.1 Confirm forced-rise-without-limit frame exit-condition semantics (`BLE_PROTOCOL.md` /
      firmware behavior) to decide between design.md's option (a) (`exitIf==true` on the
      forced-rise frame) and option (b) (separate preinfusion-count increment). Confirmed via
      `ProfileFrame::computeFlags()` (`profileframe.cpp:571-574`): `exitIf` drives the real
      DE1 `DoCompare` flag byte, so option (a) would change on-machine exit behavior. Used
      option (b).
- [x] 1.2 Fix a Pressure-type profile's preinfusion frame count to include each "forced rise
      without limit" frame, without touching frame `exitIf`. Added shared helper
      `Profile::countPreinfuseFramesWithForcedRise()` and applied it at all four sites that
      independently compute this count: `RecipeGenerator::createProfile()`,
      `Profile::regenerateSimpleFrames()`, `Profile::regenerateFromRecipe()`, and
      `Profile::loadFromTclString()` (the de1app `.tcl` import path — the actual bug-report
      scenario). Originally scoped to just the first site; expanded once the other three
      turned out to share the same bug independently.
- [x] 1.3 Verify `MachineState::onFlowSample()` routes forced-rise-frame flow into
      `m_preinfusionVolume` (not `m_pourVolume`) once the frame count is corrected — no code
      change expected here, confirm behavior only. Confirmed: `m_phase` (`machinestate.cpp:387`)
      is derived from the DE1 firmware's own SubState, which the firmware computes by comparing
      the live frame index against `NumberOfPreinfuseFrames` — the header byte this fix corrects.
      No client-side routing change needed.
- [x] 1.4 Added unit tests: `tests/tst_recipegenerator.cpp`
      (`pressureProfileCountsForcedRiseAsPreinfusion`, `pressureProfileCountsBothForcedRiseFrames`,
      `flowProfilePreinfuseCountUnaffectedByForcedRiseFix`) for the Recipe Editor path, and
      `tests/tst_profile.cpp` (assertions added to `pressureProfileFrameGeneration`,
      `pressureProfileShortHold`, `tempSteppingPressure`) for the JSON/`.tcl`-import
      materialization path.
- [x] 1.5 `tests/data/shots/` (recorded shot samples) had no match — unrelated to profile
      generation. But the actual test run (see 5.1) surfaced two STATIC fixture sets that
      genuinely needed updating, since they snapshot de1app's/Decenza's PRE-fix output:
      - `tests/data/de1app_packed/*.txt` — 17 goldens regenerated via
        `python3 tools/gen_de1app_pack_corpus.py <de1app-checkout>/de1plus` against the local
        de1app checkout (already at the fixing commit `13a30463`), which runs de1app's REAL
        `pressure_to_advanced_list` live — so these now reflect de1app's actual fixed behavior,
        not a hand-edited guess.
      - `resources/profiles/*.json` (17 shipped built-ins) — `number_of_preinfuse_frames` /
        `target_volume_count_start` were stored as static fields and stale. `profile_sync`'s own
        `--sync` comparator couldn't catch or fix this: `normaliseSimpleProfile()`
        (`tools/profile_sync.cpp`) recomputed `preinfuseFrameCount` fresh on BOTH sides before
        comparing (needed to materialize empty-`steps` built-ins for the frame diff, but it
        clobbered the field being compared as a side effect), so the two sides always agreed
        with themselves regardless of what was actually stored — compare mode read "0 different"
        against the true drift. Fixed `normaliseSimpleProfile()` to keep the JSON side's ORIGINAL
        stored count after materializing its frames, so the comparison is against what's really
        on disk. Verified: reverted the 17 files to their stale committed values, ran
        `profile_sync --sync`, and it correctly re-derived and rewrote the exact same 17 files —
        confirmed byte-for-byte against the original hand-patch, plus a deliberate-corruption
        test proving the comparator now flags a real mismatch. Full suite re-run clean after.
      - Also fixed one stale hardcoded expectation in `tst_profile.cpp`
        (`tempSteppingEmitsBoostFrameEvenAtZeroPreinfusion`, was asserting the old un-fixed count).

## 2. Standby-switch (Error_NoAC) detection

- [x] 2.1 Add `Error_NoAC` (217) to `DE1::SubState` (`src/ble/protocol/de1characteristics.h`).
- [x] 2.2 Reset `m_subState` in `DE1Device::onTransportDisconnected()`
      (`src/ble/de1device.cpp`) so a stale `Error_NoAC` cannot persist across a BLE drop.
- [x] 2.3 Added `MachineState.standbySwitchOpen` (bool property + `standbySwitchOpenChanged`
      signal, `machinestate.h`/`.cpp`), computed in `updatePhase()` from `Error_NoAC` +
      `firmwareBuildNumber() >= 1337`, forced false in the disconnected branch.

## 3. Standby-switch warning UI

- [x] 3.1 Added `standbySwitchDialog` (`qml/main.qml`), a `DecenzaDialog` sized to
      `Overlay.overlay` width/height (true full-screen, following the refill-dialog pattern but
      edge-to-edge rather than a centered card), driven by `MachineState.standbySwitchOpen`.
      Text goes through `Tr` (`main.dialog.standbySwitch.message`, fallback "Push the switch
      on"), matching every other dialog in the file — no hardcoded string.
- [x] 3.2 `AccessibleMouseArea` covers the full dialog content; `onAccessibleClicked` calls
      `standbySwitchDialog.close()`. Dismissal reveals the page underneath automatically (modal
      Popup over the StackView) — no manual return-page tracking, matching design.md.
- [x] 3.3 `Connections { target: MachineState; function onStandbySwitchOpenChanged() }` opens on
      the true transition and closes when it goes false (condition cleared or DE1 disconnected —
      `MachineState.standbySwitchOpen` is forced false in both). Reacting only to the CHANGE
      (not "still true") means a tap-dismiss isn't immediately reopened by the same occurrence,
      but a fresh false→true transition (the condition recurring) opens it again. Also wired
      into the screensaver popup-queue (`standbySwitch` id) alongside `refillDialog`.
- [x] 3.4 Verified structurally: `standbySwitchDialog` is a `DecenzaDialog`/`T.Dialog` declared
      as a direct sibling of `refillDialog` at `main.qml`'s root level, not nested in any page.
      Qt Quick Controls renders every `Popup`/`Dialog` through `Overlay.overlay`, not as a child
      of its QML parent in the StackView hierarchy — the same mechanism that already makes
      `refillDialog` appear over every page in production. Its visibility is driven solely by
      `MachineState.standbySwitchOpenChanged`, with no page-specific gating anywhere. I can't
      launch the app myself to click through idle/brew/settings on-device — that on-device pass
      is still for Jeff.

## 4. Manual testing

- [ ] 4.1 On real hardware, run a Pressure-profile Stop-at-Volume shot at a known target and
      confirm delivered volume is close to target rather than short by roughly the forced-rise
      volume.
- [ ] 4.2 If a DE1 running firmware >= 1337 is available, physically flip the standby switch mid
      idle/mid-session and confirm the warning appears, dismisses on tap, and clears when power is
      restored. If unavailable, verify via simulated substate injection instead and note the gap.

## 5. Review and merge

- [x] 5.1 Ran the full local test suite via Qt Creator MCP (`run_tests`, scope `all`): 113/113
      pass, 0 failures. (First run surfaced 3 failing binaries from stale fixtures — see 1.5 —
      fixed, then re-ran clean.)
- [ ] 5.2 Open the PR.
- [ ] 5.3 Run `/pr-review-toolkit:review-pr` and address findings.
- [ ] 5.4 Merge via `/merge-pr`.
