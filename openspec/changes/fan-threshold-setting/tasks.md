## 1. SettingsHardware — fanThreshold property

- [x] 1.1 Add `Q_PROPERTY(int fanThreshold READ fanThreshold WRITE setFanThreshold NOTIFY fanThresholdChanged)` to `src/core/settings_hardware.h`
- [x] 1.2 Add `fanThreshold()` getter (reads `calibration/fanThreshold`, default 60) and `setFanThreshold()` setter (guards on change, writes key, emits signal) to `src/core/settings_hardware.cpp`
- [x] 1.3 Add `void fanThresholdChanged()` signal declaration to `src/core/settings_hardware.h`

## 2. BLE — write user value on connect

- [x] 2.1 In `src/ble/de1device.cpp` `sendInitialSettings()`, replace `writeMMR(DE1::MMR::FAN_THRESHOLD, 60)` with `writeMMR(DE1::MMR::FAN_THRESHOLD, m_settings->hardware()->fanThreshold())`

## 3. UI — fan threshold slider in heater calibration popup

- [x] 3.1 In `qml/pages/settings/SettingsCalibrationTab.qml`, add a label and `ValueInput` slider for fan threshold after the heater test timeout slider (`heaterTestTimeoutSlider`): range 0–60, step 1, `displayText` shows `N + "°C"` for N > 0 or `TranslationManager.translate("settings.calibration.fanAlwaysOn", "Always on")` for N = 0, bound to `Settings.hardware.fanThreshold`
- [x] 3.2 Update `KeyNavigation` chain: `heaterTestTimeoutSlider.KeyNavigation.tab` → new fanThreshold slider; new slider `KeyNavigation.tab` → `defaultsButton`; `defaultsButton.KeyNavigation.backtab` → new slider
- [x] 3.3 Add `Settings.hardware.fanThreshold = 60` to the `defaultsButton` `onClicked` handler alongside the other hardware resets
