## Context

`DE1Device::sendInitialSettings()` writes `FAN_THRESHOLD = 60` unconditionally on every BLE connect. The DE1 firmware default is fan-always-on (threshold near 0°C), so if this write ever fails the fan never turns off. de1app stores this as `$::settings(fan_threshold)` — a user-configurable value defaulting to 60 — and writes that value on connect.

The other heater calibration values (`heaterIdleTemp`, `heaterWarmupFlow`, `heaterTestFlow`, `heaterWarmupTimeout`) are already user-configurable in `SettingsHardware` and editable via the heater calibration popup in `SettingsCalibrationTab.qml`. Fan threshold belongs in the same place.

## Goals / Non-Goals

**Goals:**
- Persist a user-configurable fan threshold in `SettingsHardware` (default 60)
- Write the user's value instead of the hardcoded 60 in `sendInitialSettings()`
- Expose the setting in the existing heater calibration popup alongside the other hardware tuning sliders
- Update the "Defaults for cafe" button to reset fan threshold to 60

**Non-Goals:**
- Reading back the current FAN_THRESHOLD from the DE1 on connect (de1app does this but adds round-trip complexity; the user's saved value is authoritative)
- Exposing fan threshold outside the calibration popup (not a common adjustment; the warning gate is appropriate)
- Firmware version checks or conditional writes (existing `sendInitialSettings()` does not do these)

## Decisions

**Add to `SettingsHardware`, not a new domain.**
All heater calibration values live in `SettingsHardware`. FAN_THRESHOLD is written from `sendInitialSettings()` alongside the other hardware MMR writes, and shown in the same UI popup. It is the natural domain fit.

**Range 0–60°C, matching de1app.**
de1app's calibration slider is defined as `-from 0 -to 60`. 0 means "fan always on" (the DE1 firmware default); 60 is the standard quiet-operation value and the slider maximum. The label for 0 shows "always on" (mirroring de1app's `return_fan_threshold_calibration`). Step is 1°C.

**Place in existing heater calibration popup, not a new card.**
The popup is already behind a warning dialog (`calibrationWarningDialog`). Fan threshold is an expert setting with the same risk profile as the other heater values — adding it to the popup is the correct UX, not a new surface.

**Key navigation must be updated.**
The popup uses `KeyNavigation.tab`/`.backtab` chains. The new slider must be inserted into the chain (after heaterTestTimeout, before the "Defaults for cafe" button) and the `defaultsButton` reset handler extended.

## Risks / Trade-offs

[Existing users had an implicit 60°C they couldn't change] → No migration needed. The `QSettings` default is 60, so existing installations get 60 on first read — identical to the previous hardcoded value.

[Fan threshold is an expert setting that can affect machine behaviour] → Mitigated by the existing warning dialog gate. No additional protection needed.

## Migration Plan

No data migration. The property reads from `QSettings` with a default of 60, matching the previously hardcoded value. Existing users see no change in behaviour.
