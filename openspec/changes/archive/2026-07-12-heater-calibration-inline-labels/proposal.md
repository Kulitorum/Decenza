# Heater Calibration: Inline Labels + Tank Preheat Fix

## Why

The Heater Calibration dialog (opened from Settings → Calibration) stacks each parameter's description label *above* its `ValueInput` row, so five parameters consume ten rows of vertical space. On typical tablet screens the dialog exceeds its 85%-height cap and the user must scroll to reach the Cancel/Done buttons — poor use of a dialog that is mostly empty horizontally.

Additionally, while comparing our heater/thermal MMR handling against ReaPrime, we found a bug: Decenza parses `tank_desired_water_temperature` from profile JSON (and round-trips it to Visualizer) but never writes it to the DE1 — `sendInitialSettings()` hardcodes `TANK_TEMP_THRESHOLD` (MMR 0x80380C) to 0 and no profile-upload path touches it. Profiles that request tank preheat silently get none. de1app writes the profile's value on every shot-frame upload (`de1_comms.tcl` `de1_send_shot_frames`, clamped 0–45 by `range_check_variable`); ReaPrime does the same on every profile upload.

## What Changes

- In the Heater Calibration popup (`qml/pages/settings/SettingsCalibrationTab.qml`), each parameter's label moves from above the `ValueInput` to its left: one `RowLayout` per parameter with the label on the left and the value control on the right.
- The five affected parameters: Heater idle temperature, Heater warmup flow rate, Heater test flow rate, Heater test time-out, Fan temperature threshold.
- The dialog becomes short enough that all controls, including the Done button, are visible without scrolling at typical window/tablet sizes (the Flickable remains as a safety net for extreme cases).
- No behavioral changes to the dialog: values, ranges, step sizes, settings keys, keyboard navigation order, and accessibility names are unchanged.
- **Bug fix**: `DE1Device` writes the active profile's `tank_desired_water_temperature` (clamped 0–45 °C, rounded to int) to `DE1::MMR::TANK_TEMP_THRESHOLD` as part of every profile upload (`uploadProfile` and `uploadProfileAndStartEspresso`), so profile-requested tank preheat reaches the machine. The connect-time write of 0 in `sendInitialSettings()` remains as the pre-profile baseline; the existing `m_lastMMRValues` dedup cache elides repeat writes.

## Capabilities

### New Capabilities

- `heater-calibration-layout`: Layout requirements for the Heater Calibration dialog — inline label placement (label left of value), no-scroll visibility of the Done button, label wrapping/elision behavior for long translations.
- `profile-tank-preheat`: The DE1 tank water preheat threshold follows the active profile's `tank_desired_water_temperature` on every profile upload.

### Modified Capabilities

<!-- none — fan-threshold-setting covers value semantics and tab order, which do not change -->

## Impact

- **Code**: `qml/pages/settings/SettingsCalibrationTab.qml` (Heater Calibration popup only, lines ~720–836) for the layout; `src/ble/de1device.cpp` (`uploadProfile`, `uploadProfileAndStartEspresso`) for the tank preheat fix. No new files, no CMakeLists changes.
- **Translations**: existing keys reused; no new strings.
- **Accessibility**: `accessibleName` on each `ValueInput` already carries the parameter name, so screen-reader behavior is unaffected; visible label association is preserved by proximity.
- **Specs**: `fan-threshold-setting` unaffected (its positioning/tab-order requirements still hold).
- **Tests**: `tests/tst_profileupload.cpp` (MockTransport) gains an assertion that the tank-temp MMR write is queued with the profile's value.
- **Machine behavior**: profiles with `tank_desired_water_temperature > 0` will now actually preheat tank water — a user-visible (intended) change for imported profiles that carry the field.
