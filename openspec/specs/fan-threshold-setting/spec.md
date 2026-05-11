# fan-threshold-setting Specification

## Purpose
Make the DE1 fan threshold user-configurable, persist it across launches, write it to the DE1 on every BLE connect, and expose it as a slider in the heater calibration popup.

## Requirements

### Requirement: Fan threshold is user-configurable and persisted
`SettingsHardware` SHALL expose a `fanThreshold` property (`int`, range 0–60, default 60) backed by `QSettings` key `calibration/fanThreshold`. It SHALL follow the standard domain property pattern: `Q_PROPERTY` with `READ`/`WRITE`/`NOTIFY`, setter that guards on value change and emits the signal, and getter that reads from `QSettings` with the default.

#### Scenario: Default value on fresh install
- **WHEN** a user launches Decenza for the first time (no existing `calibration/fanThreshold` key in QSettings)
- **THEN** `Settings.hardware.fanThreshold` returns 60
- **AND** the DE1 receives `FAN_THRESHOLD = 60` on the next BLE connect

#### Scenario: Persisted value survives restart
- **WHEN** the user sets fan threshold to 40 and restarts the app
- **THEN** `Settings.hardware.fanThreshold` returns 40
- **AND** the DE1 receives `FAN_THRESHOLD = 40` on the next BLE connect

### Requirement: Fan threshold is written to the DE1 on every BLE connect
`DE1Device::sendInitialSettings()` SHALL write `DE1::MMR::FAN_THRESHOLD` using the value from `SettingsHardware::fanThreshold()` instead of a hardcoded literal. The write SHALL occur on every connection (including reconnects), consistent with all other MMR writes in `sendInitialSettings()`.

#### Scenario: User-configured value is sent on connect
- **WHEN** the user has set fan threshold to 45 and the DE1 connects
- **THEN** the BLE log shows `[MMR] write: 0x803808 = 45`
- **AND** NOT `[MMR] write: 0x803808 = 60`

#### Scenario: Default value sent when never explicitly set
- **WHEN** no fan threshold has been configured and the DE1 connects
- **THEN** the BLE log shows `[MMR] write: 0x803808 = 60`

### Requirement: Fan threshold slider in heater calibration popup
The heater calibration popup in `SettingsCalibrationTab.qml` SHALL include a `ValueInput` slider for fan threshold. The slider SHALL have range 0–60, step 1, and display the value as `N°C` for N > 0 or `"Always on"` for N = 0 (matching de1app's label behaviour). It SHALL be positioned after the existing heater test timeout slider. The `KeyNavigation` chain SHALL include the new slider (tab order: heaterTestTimeout → fanThreshold → defaultsButton). The "Defaults for cafe" button SHALL reset `Settings.hardware.fanThreshold` to 60 alongside the other hardware defaults.

#### Scenario: Slider shows current value
- **WHEN** the user opens the heater calibration popup
- **THEN** the fan threshold slider reflects the current `Settings.hardware.fanThreshold` value

#### Scenario: Zero displays as "Always on"
- **WHEN** the user drags the fan threshold slider to 0
- **THEN** the slider label reads "Always on" (not "0°C")

#### Scenario: Non-zero displays temperature
- **WHEN** the slider is set to 42
- **THEN** the label reads "42°C"

#### Scenario: Defaults for cafe resets fan threshold
- **WHEN** the user clicks "Defaults for cafe" in the heater calibration popup
- **THEN** `Settings.hardware.fanThreshold` is set to 60
