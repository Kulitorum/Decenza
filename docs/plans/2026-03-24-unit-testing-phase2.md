# Phase 2: Machine State Gaps + Frame Serialization

> **For Claude:** Create both test files, add friend class declarations, add CMake targets, build and run. Expected values from de1app Tcl source. Comment de1app proc names next to assertions.

**Rationale:** Fills known gaps in existing SAV/SAW coverage (phase transitions, volume reset, tare lifecycle) and adds ProfileFrame struct-level serialization tests. MachineState extends the existing TestFixture pattern from tst_sav.

---

## Task 1: `tests/tst_machinestate.cpp` (~25 tests)

**Bug context:** 10 bug-fix PRs for machine state. Bug #505 (stale volume counters cause instant stop on second shot), #504 (false tare warnings), #530 (hot water frozen weight persists into espresso), #509 (hot water weight incorrect on slow-tare scales).

**Source deps:** Same as tst_sav (MACHINE_SOURCES, BLE_SOURCES, PROFILE_SOURCES, CORE_SOURCES, CONTROLLER_SOURCES, SIMULATOR_SOURCES)

**Prerequisites:**
- Add `friend class tst_MachineState;` to `src/machine/machinestate.h` under `#ifdef DECENZA_TESTING` (alongside existing `friend class tst_SAV`)

**CMake addition:**
```cmake
add_decenza_test(tst_machinestate tst_machinestate.cpp
    ${MACHINE_SOURCES} ${BLE_SOURCES} ${PROFILE_SOURCES}
    ${CORE_SOURCES} ${CONTROLLER_SOURCES} ${SIMULATOR_SOURCES}
    ${CMAKE_SOURCE_DIR}/tests/mocks/MockScaleDevice.h)
```

### Test Cases

#### Phase Mapping (de1app `update_de1_state`)

- [ ] `phaseMapping_data` — data-driven test across all DE1::State → Phase mappings:
  - Sleep → Sleep
  - Idle → Idle
  - Espresso/Preinfusion → Preinfusion
  - Espresso/Pouring → Pouring
  - Steam → Steaming
  - HotWater → HotWater
  - HotWaterRinse → Flushing
  - Descale → Descaling
  - Clean → Cleaning
- [ ] `isFlowingForPhases_data` — data-driven: Pouring=true, Preinfusion=true, HotWater=true, Idle=false, Sleep=false, Steaming=false

#### Volume Reset (bug #505: stale counters)

- [ ] `volumeResetOnNewExtraction` — run extraction 1 (accumulate volume), transition to Idle, start extraction 2, verify pourVolume and preinfusionVolume are 0
- [ ] `volumeResetDoesNotAffectHotWater` — hot water volume tracked separately from espresso

#### Tare Lifecycle (bugs #504, #509)

- [ ] `tareLifecycle` — call tareScale → m_waitingForTare=true → feed ~0g weight → m_tareCompleted=true
- [ ] `tareBlockedDuringSleep` — tareScale in Sleep phase → no state change
- [ ] `tareBlockedWhenAlreadyWaiting` — double tareScale → second call ignored

#### Hot Water Weight (bugs #530, #509)

- [ ] `hotWaterTareBaseline` — set tare baseline, verify effective weight = scale - baseline
- [ ] `hotWaterWeightClearedOnPhaseChange` — transition from HotWater to Idle → hot water weight state reset
- [ ] `hotWaterWeightDoesNotLeakToEspresso` — start hot water → accumulate weight → end → start espresso → espresso weight starts at 0 (bug #530)

#### Stop-at-Time

- [ ] `stopAtTimeFires` — set shot timeout > 0, simulate time past threshold → stop signal emitted
- [ ] `stopAtTimeDisabledWhenZero` — timeout=0 → no stop signal even after long extraction
- [ ] `stopAtTimeRespectsPhase` — only fires during active extraction phases

#### Signal Verification

- [ ] `espressoCycleStartedSignal` — transition to EspressoPreheating → signal emitted with QSignalSpy
- [ ] `shotStartedSignal` — transition to Preinfusion → signal emitted
- [ ] `shotEndedSignal` — transition from Pouring to Idle → signal emitted
- [ ] `phaseChangedSignal` — every phase transition → phaseChanged signal emitted with correct new phase

#### Edge Cases

- [ ] `rapidPhaseTransitions` — Sleep → Idle → Espresso in quick succession → final state correct
- [ ] `disconnectDuringExtraction` — device disconnects mid-pour → phase becomes Disconnected, extraction state cleaned up

---

## Task 2: `tests/tst_profileframe.cpp` (~30 tests)

**Bug context:** ProfileFrame serialization bugs caused corrupted frame uploads. Pure struct with all-public members — no friend access needed. Tests the JSON/TCL parsing and BLE flag encoding at the individual frame level.

**Source deps:** `src/profile/profileframe.cpp` + `src/ble/protocol/binarycodec.cpp`

**CMake addition:**
```cmake
add_decenza_test(tst_profileframe tst_profileframe.cpp
    ${CMAKE_SOURCE_DIR}/src/profile/profileframe.cpp
    ${CODEC_SOURCES})
```

### Test Cases

#### JSON Round-Trip

- [ ] `jsonNestedExitPressureOver` — exit: {type: "pressure", condition: "over", value: 3.0} → serialize → deserialize → exact match
- [ ] `jsonNestedExitFlowUnder` — exit: {type: "flow", condition: "under", value: 1.5}
- [ ] `jsonNestedExitPressureUnder` — exit: {type: "pressure", condition: "under", value: 2.0}
- [ ] `jsonNestedExitFlowOver` — exit: {type: "flow", condition: "over", value: 4.0}
- [ ] `jsonWeightExitIndependent` — weight: {value: 4.0} without exit object → exitIf stays false, weight saved
- [ ] `jsonLegacyFlatFields` — flat `exit_if`, `exit_type`, `exit_pressure_over` → parsed correctly
- [ ] `jsonLimiterNested` — limiter: {value: 0, range: 0.2} round-trips
- [ ] `jsonLimiterFlat` — flat `max_flow_or_pressure` fallback
- [ ] `jsonAllFieldsRoundTrip` — set every field, serialize, deserialize, compare field-by-field

#### TCL Parse (de1app `parse_profile_frame`)

- [ ] `tclStandardFrame` — all fields present, correct values
- [ ] `tclTransitionSlow` — `transition slow` → `"smooth"`
- [ ] `tclTransitionFast` — `transition fast` → `"fast"` (or `"immediate"`)
- [ ] `tclBracedValue` — `name {rise and hold}` → name = "rise and hold"
- [ ] `tclWeightIndependentOfExitIf` — `exit_if 0 weight 4.0` → weight=4.0, exitIf=false
- [ ] `tclEmitRoundTrip` — `toTclList()` → `fromTclList()` → field-by-field match

#### computeFlags (de1app `calculate_frame_flag`)

- [ ] `flagsPressureNoExit` — pressure pump, no exit → IgnoreLimit only
- [ ] `flagsFlowNoExit` — flow pump, no exit → CtrlF | IgnoreLimit
- [ ] `flagsPressureExitPressureOver` — pressure, exit_pressure_over → DoCompare | DC_GT | IgnoreLimit
- [ ] `flagsFlowExitPressureOver` — flow, exit_pressure_over → CtrlF | DoCompare | DC_GT | IgnoreLimit
- [ ] `flagsFlowExitFlowUnder` — flow, exit_flow_under → CtrlF | DoCompare | DC_CompF
- [ ] `flagsPressureExitFlowOver` — pressure, exit_flow_over → DoCompare | DC_GT | DC_CompF
- [ ] `flagsSmoothTransition` — transition=smooth → Interpolate flag set
- [ ] `flagsWaterSensor` — sensor=water → TMixTemp flag set

#### Value Routing

- [ ] `getSetValFlow` — flow pump returns flow value
- [ ] `getSetValPressure` — pressure pump returns pressure value
- [ ] `getTriggerValEachType` — each exit type returns correct trigger value
- [ ] `getTriggerValNoExit` — exitIf=false → returns 0
- [ ] `withSetpointImmutability` — withSetpoint returns copy, original unchanged

---

## Verification

After completing both test files:
```bash
cmake -DBUILD_TESTS=ON -B build/tests
cmake --build build/tests
cd build/tests && ctest --output-on-failure
```

Expected: all Phase 1 + Phase 2 tests pass (tst_binarycodec, tst_profile, tst_machinestate, tst_profileframe, plus existing tst_sav, tst_saw).
