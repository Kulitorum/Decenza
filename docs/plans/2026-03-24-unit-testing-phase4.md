# Phase 4: Integration Tests

> **For Claude:** Create both test files, create MockTransport, add friend class declarations, add CMake targets, build and run. All expected values from de1app Tcl source (de1app at `/Users/jeffreyh/Documents/GitHub/de1app` or `C:\code\de1app`). Profile snapshot tests should load built-in JSON files and compare frame-by-frame against recipe generator output.

**Rationale:** These tests verify end-to-end behavior: recipe generator output matches de1app's built-in profiles, and the BLE wire format matches de1app's `pack_shot_settings`. These catch regressions in the most complex code paths.

---

## Task 1: `tests/tst_recipegenerator.cpp` (~15 tests)

**Bug context:** D-Flow/A-Flow frame generation had a line-by-line audit (#422) that fixed ramp splitting, rounding, and frame count. The generator was reworked 3 times. Testing against built-in profile snapshots catches regressions.

**Source deps:** PROFILE_SOURCES + CODEC_SOURCES. No mocks.

**CMake addition:**
```cmake
add_decenza_test(tst_recipegenerator tst_recipegenerator.cpp
    ${PROFILE_SOURCES} ${CODEC_SOURCES})
```

**Test approach:** Load built-in profile JSON from `resources/profiles/`, extract the recipe params, run the generator, compare output frames field-by-field against the JSON's frames. This is a golden-file regression test — if the generator changes, the test catches it.

### Test Cases

#### D-Flow (de1app `dflow_generate_frames`)

- [ ] `dflowDefaultMatchesBuiltIn` — load `resources/profiles/d_flow_default.json`, extract recipe params, generate frames with RecipeGenerator, compare frame-by-frame against the profile's stored frames (pressure, flow, seconds, temperature, exitIf, exitType, exitValue, weight, transition, sensor, pump)
- [ ] `dflowDefaultFrameCount` — default D-Flow produces exactly 3 frames (Filling, Infusing, Pouring)
- [ ] `dflowInfuseDisabled` — set infuseEnabled=false, verify Infusing frame seconds=0
- [ ] `dflowFillExitFormula` — infusePressure=3.0: exitPressure = round((3.0/2+0.6)*10)/10 = 2.1 (de1app `dflow_filling_exit_pressure`)
- [ ] `dflowFillExitClamp` — infusePressure=0.5: exitPressure clamped to minimum 1.2 (de1app minimum)
- [ ] `dflowPreinfuseFrameCount` — verify preinfuseFrameCount matches number of frames before pouring

#### A-Flow (de1app `aflow_generate_frames`)

- [ ] `aflowDefaultMatchesBuiltIn` — load `resources/profiles/a_flow_default_medium.json`, same golden-file test
- [ ] `aflowDefaultFrameCount` — default A-Flow: verify frame count (expected ~9)
- [ ] `aflowSecondFillEnabled` — secondFillEnabled=true: verify extra frames inserted at correct position
- [ ] `aflowRampDownSplits` — rampDownEnabled=true: rampTime split between Up and Decline frames (de1app ramp splitting logic)

#### Pressure/Flow (de1app `pressure_to_advanced_list` / `flow_to_advanced_list`)

- [ ] `pressureRecipeMatchesRegenerate` — Pressure recipe params → RecipeGenerator output matches Profile::regenerateSimpleFrames() for same inputs (equivalence test)
- [ ] `flowRecipeMatchesRegenerate` — same for Flow recipe

#### Metadata

- [ ] `createProfileSetsModeAndType` — EditorType::Pressure → profileType="settings_2a", isRecipeMode=true
- [ ] `createProfilePreservesRecipeParams` — created profile's recipe params match input
- [ ] `createProfilePreinfuseFrameCount` — preinfuseFrameCount in created profile matches frame inspection

---

## Task 2: `tests/tst_shotsettings.cpp` (~10 tests)

**Bug context:** `DE1Device::setShotSettings` had TargetEspressoVol hardcoded to 36 instead of 200 (#556). This test verifies the exact 9-byte wire format sent over BLE, with expected byte values from de1app's `pack_shot_settings`.

**Source deps:** BLE_SOURCES + new MockTransport.

**Prerequisites:**
1. Create `tests/mocks/MockTransport.h` (new file)
2. Add `friend class tst_ShotSettings;` to `src/ble/de1device.h` under `#ifdef DECENZA_TESTING`

### New Mock: `tests/mocks/MockTransport.h`

```cpp
#pragma once

#include "ble/transport/de1transport.h"
#include <QBluetoothUuid>
#include <QByteArray>
#include <QList>
#include <QPair>

class MockTransport : public DE1Transport {
    Q_OBJECT
public:
    explicit MockTransport(QObject *parent = nullptr) : DE1Transport(parent) {}

    // Capture all writes
    QBluetoothUuid lastWriteUuid;
    QByteArray lastWriteData;
    QList<QPair<QBluetoothUuid, QByteArray>> allWrites;

    // Record writes instead of sending over BLE
    void writeCharacteristic(const QBluetoothUuid &uuid, const QByteArray &data) override {
        lastWriteUuid = uuid;
        lastWriteData = data;
        allWrites.append({uuid, data});
    }

    // No-ops for other transport methods
    void connectToDevice(const QBluetoothDeviceInfo &) override {}
    void disconnectFromDevice() override {}
    bool isConnected() const override { return true; }

    // Test helper
    void clearWrites() { allWrites.clear(); lastWriteData.clear(); }
};
```

> **Note:** Adjust method signatures to match the actual DE1Transport abstract interface. Read `src/ble/transport/de1transport.h` before implementing.

**CMake addition:**
```cmake
add_decenza_test(tst_shotsettings tst_shotsettings.cpp
    ${BLE_SOURCES} ${PROFILE_SOURCES} ${CORE_SOURCES}
    ${CMAKE_SOURCE_DIR}/tests/mocks/MockTransport.h)
```

### Test Cases (de1app `pack_shot_settings`)

Each test calls `setShotSettings(steamTemp, steamDuration, hotWaterTemp, hotWaterVol, groupTemp)` with known values and verifies the exact byte written to the ShotSettings BLE characteristic.

- [ ] `shotSettingsTotalLength` — verify exactly 9 bytes written
- [ ] `shotSettingsHeaderVersion` — byte[0] = version byte (expected 1)
- [ ] `shotSettingsSteamTemp` — byte[1] = encodeU8P0(steamTemp) — e.g., 160 → 0xA0
- [ ] `shotSettingsSteamDuration` — byte[2] = encodeU8P0(steamDuration) — e.g., 120 → 0x78
- [ ] `shotSettingsHotWaterTemp` — byte[3] = encodeU8P0(hotWaterTemp) — e.g., 80 → 0x50
- [ ] `shotSettingsHotWaterVolume` — byte[4] = encodeU8P0(hotWaterVol) — e.g., 200 → 0xC8
- [ ] `shotSettingsTargetHotWaterLength` — byte[5] = encodeU8P0(targetHotWaterLength)
- [ ] **`shotSettingsTargetEspressoVol`** — byte[6] = encodeU8P0(**200**) = **0xC8** (THE BUG: was 36/0x24)
- [ ] `shotSettingsGroupTemp` — bytes[7:8] = encodeU16P8(groupTemp) — e.g., 93.0 → 0x5D00
- [ ] `shotSettingsDefaultValues` — call with typical defaults, verify complete byte array matches de1app `pack_shot_settings` output for same inputs

---

## Verification

After completing both test files:
```bash
cmake -DBUILD_TESTS=ON -B build/tests
cmake --build build/tests
cd build/tests && ctest --output-on-failure
```

### Full Suite Check

Expected test executables after all 4 phases:
```
tst_sav          (existing, ~42 runs)
tst_saw          (existing, ~13 runs)
tst_binarycodec  (Phase 1, ~35 tests)
tst_profile      (Phase 1, ~55 tests)
tst_machinestate (Phase 2, ~25 tests)
tst_profileframe (Phase 2, ~30 tests)
tst_recipeparams (Phase 3, ~20 tests)
tst_settings     (Phase 3, ~20 tests)
tst_recipegenerator (Phase 4, ~15 tests)
tst_shotsettings    (Phase 4, ~10 tests)
─────────────────────────────────────
Total: ~265 automated tests
```

All tests must pass with `ctest --output-on-failure` showing 0 failures.
