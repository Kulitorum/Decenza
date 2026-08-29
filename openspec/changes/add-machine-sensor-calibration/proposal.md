## Why

Decent's own "pressure calibration test" profile ends with an instruction Decenza cannot satisfy: *"Go to Settings->Machine->Calibrate and enter the held pressure value, and then retest until the two agree."* Decenza has no field for that value. A user whose DE1 reports 9 bar while the portafilter gauge reads 8.2 has no way to correct the machine from this app, and the same gap applies to the group temperature. The BLE characteristic that carries the correction (`A012`, `DE1::Characteristic::CALIBRATION`) is already declared in `de1characteristics.h` and has never had a reader or a writer.

The nearby heater-voltage setting has the same shape: Decenza reads `MMR 0x803834` and shows it in the version line, but cannot write it, so a machine reporting an unknown or wrong nominal voltage stays wrong.

## What Changes

- Add a **Sensor Calibration** card to Settings → Calibration, beside the existing Flow Calibration, Weight Stop Timing and Heater Calibration cards. Its two rows reuse the Maintenance card's row format, which is extracted to a shared `SettingsActionRow` component so Descaling Wizard and Transport Mode stop being two hand-written copies of it. It is the **only** way to change sensor calibration in Decenza — there is deliberately no settings field, no popup, and no second entry point. Calibration is changed rarely, and a single supervised path is the safest one.
- The wizard owns the whole loop for one sensor at a time (**pressure** or **temperature**): load the matching test profile, tell the user what hardware to fit, show the hold that profile declares, take the external instrument's reading, write the correction, then offer a re-run so the user can watch the two converge.
- The machine-reported half of the correction is the loaded profile's **declared hold**, never typed. A correction is computed only while that sensor's own test profile is active, so the value is the one the machine is holding to and showing.
- Implement the `A012` Calibration characteristic in `DE1Device`: pack/parse the 14-byte big-endian record (`WriteKey`, `CalCommand`, `CalTarget`, `DE1ReportedVal`, `MeasuredVal`, the last two S32P16), subscribe for notifications, and expose read / write / factory-reset for the pressure and temperature targets.
- Show the machine's current **saved** and **factory** offsets inside the wizard, and offer a per-sensor restore-to-factory there — as a return-to-known-state action for a machine being handed on or a suspected sensor fault, not as the way to undo a bad entry. With an instrument in hand, the correct undo is another run.
- Take the external temperature reading in Celsius with an explicit unit label, whatever the user's display preference, without changing that preference.
- Add heater-voltage **120 V / 230 V** selection writing `MMR 0x803834` **inside the existing Heater Calibration popup**, after the fan threshold row. It is deliberately out of the way: behind that popup's existing destructive-change warning, among expert parameters, and off any tab a user browses casually. "Defaults for cafe" does not touch it — it is machine state, not a preference.
- Add the manual entry to the wiki manual.

Explicitly **out of scope** (decided with the user):
- **A settings-tab calibration card or popup.** One path only.
- **Flow sensor calibration** (`A012` target 0). Decenza already exposes flow correction through the Flow Calibration card (multiplier `MMR 0x80383C` plus auto-calibration); a second, firmware-side flow correction would fight it. de1app has this row coded but commented out for the same reason.
- **Slow start** (`insert_preinfusion_pause`). It is a profile-generator option, not machine calibration.
- **Replacing or removing the test profiles.** `test_pressure_calibration.json` and `test_temperature_calibration.json` are real profiles that pull real shots; the wizard drives them and they remain independently runnable. This is unlike the placeholder descale-wizard profile that `machine-maintenance` removed, which pulled nothing.

## Capabilities

### New Capabilities
- `machine-sensor-calibration`: two separate guided wizards — Pressure Calibration and Temperature Calibration — as the single path for correcting each DE1 sensor, reading, writing and restoring factory calibration over the `A012` characteristic.

### Modified Capabilities
- `heater-calibration-layout`: the Heater Calibration popup gains a sixth row, nominal heater voltage, which changes its parameter count, its height budget and its tab order.

## Impact

- `src/ble/protocol/de1characteristics.h` — calibration record layout, target and command enums (the `CALIBRATION` UUID already exists).
- `src/ble/de1device.{h,cpp}` — send/read/reset calibration, notification handling, properties for saved and factory values per target; heater-voltage write.
- `src/ble/bletransport.cpp` — subscribe to `A012`. Serial transport already maps the UUID (`serialtransport.cpp:465` covers through `0xA012`).
- New guided page under `qml/pages/` following `TransportPage.qml`, registered in `CMakeLists.txt`; a Sensor Calibration card and a heater-voltage row in `SettingsCalibrationTab.qml`; a new shared `qml/components/SettingsActionRow.qml` that `SettingsMachineTab.qml`'s two Maintenance rows also move onto (no behaviour change there).
- Translations for the new strings; wiki manual entry.
- No new setting is persisted by the app: the calibration lives in the machine and is read back from it. Heater voltage is likewise machine state, not a Decenza setting.

### Reference implementations consulted

- **de1app** is the only working implementation of `A012` writes: `de1_send_calibration` / `de1_read_calibration` (`de1plus/de1_comms.tcl:1632`, `:1665`), the packet spec `calibrate_spec` (`de1plus/binary.tcl:414`), notification demux `calibration_ble_received` (`de1plus/bluetooth.tcl:3314`), and the Saved / Factory / Sensor / Goal / Measured grid on its Calibrate page (`de1plus/skins/default/de1_skin_settings.tcl:2328`+).
- **Decaid** declares `A012` as a read-only endpoint with transport plumbing (`lib/src/models/device/impl/de1/de1.models.dart:27`) but implements no sensor calibration and ships no calibration UI. It does expose heater voltage as a clamped two-value enum with a set path (`De1HeaterVoltage`, `unified_de1.dart:859`) and serves it over its web API — that enum shape, including the `+1000` "already set" encoding, is worth copying.
