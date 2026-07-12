# Tasks: Heater Calibration Inline Labels

## 1. Restructure the calibration popup layout

- [x] 1.1 In `qml/pages/settings/SettingsCalibrationTab.qml` (`calibrationPopup`), wrap the "Heater idle temperature" label and `heaterIdleTempSlider` in a `RowLayout` (label left with `Layout.fillWidth: true` + `wrapMode: Text.WordWrap`, `ValueInput` right at implicit width — remove its `Layout.fillWidth`)
- [x] 1.2 Apply the same row structure to "Heater warmup flow rate" + `heaterWarmupFlowSlider`
- [x] 1.3 Apply the same row structure to "Heater test flow rate" + `heaterTestFlowSlider`
- [x] 1.4 Apply the same row structure to "Heater test time-out" + `heaterTestTimeoutSlider`
- [x] 1.5 Apply the same row structure to "Fan temperature threshold" + `fanThresholdSlider` (keep the sentinel/unit-conversion comments with the control)

## 2. Honor profile tank_desired_water_temperature

- [x] 2.1 In `src/ble/de1device.cpp`, add the tank-threshold MMR write to `uploadProfile()`: `writeMMR(DE1::MMR::TANK_TEMP_THRESHOLD, std::clamp(qRound(profile.tankDesiredWaterTemperature()), 0, 45))` after the frame writes (mirror de1app's ordering in `de1_send_shot_frames`)
- [x] 2.2 Add the same write to `uploadProfileAndStartEspresso()` (before the queued espresso-start state write)
- [x] 2.3 Extend `tests/tst_profileupload.cpp`: upload a profile with `tankDesiredWaterTemperature = 35` through MockTransport and assert an MMR write to 0x80380C carrying 35 is queued; assert a 60 value clamps to 45

## 3. Verify

- [x] 3.1 Build via Qt Creator MCP (quick compile check) and confirm no QML warnings referencing SettingsCalibrationTab
- [x] 3.2 Run the test suite (at least tst_profileupload) and confirm green (2750 passed, 0 failed, 0 warnings — includes fixing 3 pre-existing tst_CoffeeBags schema-version assertions stale since migration 30 landed in #1472)
- [x] 3.3 Ask Jeff to open Settings → Calibration → Heater Calibration and confirm: labels sit left of values, Done/Cancel visible without scrolling, drag/+/-/tap-to-scrub still work, tab order unchanged (idle temp → warmup flow → test flow → time-out → fan threshold → defaults → done), fan threshold still shows "Always on" at 0 (verified in-app; labels bumped from captionFont to bodyFont per feedback)
- [x] 3.4 Ask Jeff to load a profile with a nonzero tank_desired_water_temperature and confirm the BLE log shows the 0x80380C write with the profile's value (waived at merge time — covered by tst_profileupload MockTransport assertions; Jeff opted to merge without the hardware log check)
