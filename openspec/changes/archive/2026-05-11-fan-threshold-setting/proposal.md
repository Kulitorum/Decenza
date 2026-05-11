## Why

Decenza hardcodes the DE1 fan threshold at 60°C on every connect, ignoring any user preference. The de1app stores this value per-user in `settings(fan_threshold)` and writes that value on connect — Decenza should do the same, exposing the setting alongside the other heater calibration controls.

## What Changes

- Add `fanThreshold` property to `SettingsHardware` (default 60, stored under `calibration/fanThreshold`)
- Replace the hardcoded `writeMMR(FAN_THRESHOLD, 60)` in `sendInitialSettings()` with the user's saved value
- Add a `ValueInput` slider for fan threshold to the existing heater calibration popup in `SettingsCalibrationTab.qml`
- Update the "Defaults for cafe" button to reset `fanThreshold` to 60

## Capabilities

### New Capabilities

- `fan-threshold-setting`: User-configurable DE1 fan temperature threshold, persisted in `SettingsHardware` and applied on every BLE connect via MMR write

### Modified Capabilities

- `settings-architecture`: `SettingsHardware` gains a new `fanThreshold` property (requirement change: new domain property with persist/notify)

## Impact

- `src/core/settings_hardware.h` / `settings_hardware.cpp` — new `fanThreshold` Q_PROPERTY
- `src/ble/de1device.cpp` — `sendInitialSettings()` reads from settings instead of hardcoding 60
- `qml/pages/settings/SettingsCalibrationTab.qml` — new `ValueInput` row in the heater calibration popup + updated `KeyNavigation` chain + `defaultsButton` reset
