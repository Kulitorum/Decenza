# Phase 1: Pure Logic Tests (Zero Mocks, Highest Bug Coverage)

> **For Claude:** Create both test files, add CMake targets, build with `-DBUILD_TESTS=ON`, run `ctest --output-on-failure`. All expected values derived from de1app Tcl source. Comment the de1app proc name next to each assertion.

**Rationale:** These two test files cover the BLE codec and profile parser — the areas where bugs silently corrupt machine behavior. They compile against plain C++ with no QObject mocking, so they're fastest to implement and will never flake.

---

## Task 1: `tests/tst_binarycodec.cpp` (~35 tests)

**Bug context:** TargetEspressoVol was hardcoded to 36 instead of 200 (#556), truncating the first shot after wake. Codec precision bugs corrupt BLE commands sent to the machine.

**Source deps:** `src/ble/protocol/binarycodec.cpp` only — no mocks, no MOC, no friend access.

**CMake addition to `tests/CMakeLists.txt`:**
```cmake
set(CODEC_SOURCES
    ${CMAKE_SOURCE_DIR}/src/ble/protocol/binarycodec.cpp
)

add_decenza_test(tst_binarycodec tst_binarycodec.cpp ${CODEC_SOURCES})
```

### Test Cases

**U8P4 (pressure/flow encoding — de1app `encode_U8P4`):**
- [ ] `encodeDecodeU8P4_data` with values {0, 1.0, 9.0, 15.9375, 16.0}
- [ ] Clamping: encode(-1.0) → 0, encode(20.0) → max (0xFF)
- [ ] Precision: encode(9.2) → decode within 1/16 tolerance

**U8P1 (temperature — de1app `encode_U8P1`):**
- [ ] Round-trip: {0, 93.0, 127.5, 128.0}

**U8P0 (integers — de1app `encode_U8P0`):**
- [ ] Round-trip: {0, 60, **200**, 255, 256 → clamp to 255}
- [ ] **200 encodes to 0xC8** — the TargetEspressoVol fix

**U16P8 (precise temperature — de1app `encode_U16P8`):**
- [ ] Round-trip: {0, 93.0, 255.996}
- [ ] Precision: within 1/256 tolerance

**S32P16 (calibration — de1app `encode_S32P16`):**
- [ ] Round-trip: {-1.0, 0, 1.0, 65535.0}

**F8_1_7 (frame duration custom float — de1app `encode_F8_1_7`):**
- [ ] Round-trip: {0, 5.0, 12.7, 12.75, 13.0, 60.0, 127.0}
- [ ] Boundary: 12.74 vs 12.75 (sign bit flips encoding range)
- [ ] Short duration (<12.75): 0.1s resolution
- [ ] Long duration (≥12.75): 0.5s resolution

**U10P0 (volume limit):**
- [ ] Encode 0 → bit 10 set (0x0400)
- [ ] Decode strips flag bit

**Multi-byte endianness:**
- [ ] U24P0: encode 0x123456, verify byte order [0x12, 0x34, 0x56]
- [ ] U32P0: encode 0xDEADBEEF, verify byte order

**Shot sample parsing:**
- [ ] decode3CharToU24P16: known byte triple → expected float
- [ ] ShortBE round-trip at offset 0 and offset 2
- [ ] SignedShortBE: negative value (-100) round-trips

**Edge cases (defensive guards):**
- [ ] Empty buffer → returns 0
- [ ] Short buffer (1 byte for 2-byte decode) → returns 0

---

## Task 2: `tests/tst_profile.cpp` (~55 tests)

**Bug context:** Profile system had 83% fix rate. D-Flow frame generation diverged from de1app (#422 line-by-line audit). Simple profiles corrupted into recipe mode (#517). Preinfuse frame count not preserved (#425). JSON format must handle both de1app nested and legacy Decenza flat fields.

**Source deps:** PROFILE_SOURCES + CODEC_SOURCES. No MOC needed (Profile is not a QObject).

**CMake addition:**
```cmake
add_decenza_test(tst_profile tst_profile.cpp ${PROFILE_SOURCES} ${CODEC_SOURCES})
```

### Test Cases

#### JSON Round-Trip (de1app v2 format)

- [ ] `roundTripAdvancedProfile` — load `resources/profiles/d_flow_default.json`, serialize with `toJson()`, reload with `fromJson()`, compare all fields
- [ ] `legacyFlatFieldsFallback` — JSON with `profile_notes`, `profile_type`, `preinfuse_frame_count`, flat exit fields → parses correctly
- [ ] `de1appStringNumbers` — `"target_weight": "36.0"` (string not number) → `jsonToDouble()` handles
- [ ] `nestedExitConditions` — all 4 exit types (pressure_over/under, flow_over/under) + weight
- [ ] `limiterRoundTrip` — value=0 with range=0.2 (D-Flow limiter pattern)
- [ ] `preinfuseFrameCountPreserved` — JSON with `number_of_preinfuse_frames: 2`, verify not recomputed (bug #425)
- [ ] `preinfuseFrameCountFallback` — JSON without key → auto-counted from frames
- [ ] `simpleRecipeModeAutoFix` — settings_2a profile with `is_recipe_mode: true` loads as `false` (bug #517)
- [ ] `editorTypeInference` — recipe without `editorType`: settings_2a → Pressure, title "A-Flow..." → AFlow
- [ ] `espressoTempSyncFromFirstFrame` — advanced profile: `espresso_temperature` syncs from first frame
- [ ] `espressoTempNoSyncForSimple` — settings_2a: `espresso_temperature` stays authoritative
- [ ] `titleStripsStar` — `"*D-Flow"` loads as `"D-Flow"` (de1app modified indicator)

#### TCL Import (expected values from de1app procs)

- [ ] `tclAdvancedShot` — parse real de1app TCL with `advanced_shot` block
- [ ] `tclSimplePressure` — settings_2a TCL generates frames (de1app `pressure_to_advanced_list`)
- [ ] `tclSimpleFlow` — settings_2b TCL generates frames (de1app `flow_to_advanced_list`)
- [ ] `tclBracedValues` — `name {rise and hold}` (space in value)
- [ ] `tclWeightExitIndependentOfExitIf` — `exit_if 0 weight 4.0` → weight stored, exitIf stays false
- [ ] `tclTransitionSlow` — `transition slow` maps to `"smooth"`
- [ ] `tclFallbackWeightKeys` — `final_desired_shot_weight_advanced` preferred over non-advanced key

#### Simple Profile Frame Generation (de1app reference)

- [ ] `pressureProfileFrameGeneration` — settings_2a, holdTime > 3: forced rise + hold + decline (de1app `pressure_to_advanced_list`)
- [ ] `pressureProfileShortHold` — holdTime ≤ 3, declineTime > 3: forced rise in decline (de1app edge case from #422)
- [ ] `flowProfileDeclineGating` — decline only when holdTime > 0 (de1app behavior)
- [ ] `tempSteppingPressure` — tempStepsEnabled=true: preinfusion splits into temp boost + main → 4 frames (de1app `espresso_temperature_steps_list`)
- [ ] `tempSteppingShortPreinfusion` — preinfusionTime < 2s: only boost frame, no second (de1app edge case)
- [ ] `emptyFrameFallback` — all times zero: single empty frame (safety net)

#### BLE Byte Generation

- [ ] `headerBytes` — 5-byte header: HeaderV=1, frameCount, preinfuseCount, MinP=0, MaxF=6.0 (de1app `pack_profile_header`)
- [ ] `frameBytesFlags` — flow frame with exit_pressure_over: flags match de1app `calculate_frame_flag`
- [ ] `extensionFrames` — frame with maxFlowOrPressure > 0: extension frame at index+32
- [ ] `tailFrame` — last frame: FrameToWrite = numFrames, volume = 0 (de1app `pack_profile_tail`)

#### computeFlags (de1app `calculate_frame_flag`)

- [ ] `flagsPressureNoExit` — pressure pump, no exit: IgnoreLimit only
- [ ] `flagsFlowWithPressureOver` — flow pump, exit pressure_over: CtrlF|DoCompare|DC_GT|IgnoreLimit
- [ ] `flagsFlowUnder` — flow_under: DoCompare|DC_CompF (no DC_GT)
- [ ] `flagsSmoothWaterSensor` — transition=smooth, sensor=water: Interpolate|TMixTemp

#### RecipeGenerator (de1app `dflow_generate_frames`)

- [ ] `dflowDefaultFrames` — default recipe: 3 frames (Filling, Infusing, Pouring)
- [ ] `dflowInfuseDisabled` — infuseEnabled=false: Infusing seconds=0
- [ ] `dflowFillExitFormula` — infusePressure=3: exitP = round((3/2+0.6)*10)/10 = 2.1 (de1app formula)
- [ ] `dflowFillExitClamp` — infusePressure=0.5: exitP clamped to 1.2 (de1app minimum)
- [ ] `aflowFrameCount` — default A-Flow: 9 frames
- [ ] `pressureRecipeFrames` — Pressure recipe matches `regenerateSimpleFrames()` output (de1app `pressure_to_advanced_list`)

---

## Verification

After completing both test files:
```bash
cmake -DBUILD_TESTS=ON -B build/tests
cmake --build build/tests
cd build/tests && ctest --output-on-failure
```

Cross-check profile tests:
```bash
python scripts/compare_profiles.py
```
