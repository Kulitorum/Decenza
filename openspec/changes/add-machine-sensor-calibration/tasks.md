## 1. Protocol layer

- [x] 1.1 Add the `DE1::Calibration` target/command enums and write/read keys to `src/ble/protocol/de1characteristics.h`, beside the existing `CALIBRATION` UUID; verify the tree still builds via `mcp__qtcreator__build`
- [x] 1.2 Add private pack/parse helpers for the 14-byte big-endian record in `de1device.cpp` (WriteKey u32, CalCommand u8, CalTarget u8, DE1ReportedVal S32P16, MeasuredVal S32P16) reusing `BinaryCodec::encodeS32P16`/`decodeS32P16`; verify with round-trip slots added to `tests/tst_binarycodec.cpp` covering a positive value, a negative measured value, and byte order against a de1app-shaped packet
- [x] 1.3 Subscribe to `DE1::Characteristic::CALIBRATION` in `bletransport.cpp` as a non-required subscription following the `WATER_LEVELS` pattern; verify a connect log shows the attempt and that connect still succeeds when the subscription fails

## 2. DE1Device calibration API

- [x] 2.1 Add `sendCalibration(target, command, reported, measured)` as the single writer, with `readCalibration(target)` expressed in terms of it; verify each emits the expected 14-byte payload in slots added to `tests/tst_de1device_mmrreads.cpp` (no new test file)
- [x] 2.2 Handle `A012` notifications: parse, discard any reply whose `WriteKey != 0`, and store stored/factory per target as `std::optional<double>`; verify a test feeds one echo then one real value and asserts only the real value lands
- [x] 2.3 Expose stored/factory values and their presence as read-only `Q_PROPERTY`s with change signals so QML can render unavailable states; verify by reading them back in the same test
- [x] 2.4 Log every calibration write at INFO with target, command, reported and measured under the `Calibration` marker (`src/controllers/calibrationlogging.h`; read `docs/CLAUDE_MD/LOGGING.md` first); verify `scripts/check_log_markers.py` passes on the touched files
- [x] 2.5 Add `setHeaterVoltage(int)` writing `MMR::HEATER_VOLTAGE`, plus a bucket helper mapping a raw readback (0 / 120 / 230 / 1120 / 1230, ranges 90–150 and 180–260) to unknown/120/230 per decaid's `De1HeaterVoltage.fromInt`; verify with a data-driven slot covering each bucket and the unknown fall-through

## 3. Capture controller

- [x] 3.1 Add the per-sensor fact table (target, test-profile filename, unit, physical range, maximum correction, instrument-instruction key, Maintenance-row label) as one C++ definition exposed to QML, and derive both Maintenance rows from it; verify no per-sensor branch or second list exists anywhere else in the feature
- [x] 3.2 Replace the capture state machine with the declared-hold read: `SensorCalibrationController` takes its profile facts through a closure and reads the loaded profile's final frame; verify with slots covering the hold coming from the final frame, another profile yielding nothing, and one sensor's profile not satisfying the other
- [x] 3.3 SUPERSEDED by 3.2. The steady-hold window search was built, shipped a bug (it picked the 20 s 7 bar lead-in over the 60 s 9 bar hold), and was deleted — the profile's declared frame is already the right number
- [x] 3.4 SUPERSEDED by 3.2. The Armed/Observing/Measured/NoHold/Aborted state machine went with the measurement; the wizard now keeps the screen during its own run instead (main.qml phase-handler exemption)

## 4. Wizard page

- [x] 4.1 Add a Sensor Calibration card to `SettingsCalibrationTab.qml` (NOT the Machine tab) with two rows — Pressure and Temperature — derived from the table, each naming its required instrument and each disabled with a stated reason when no DE1 is connected; share the Maintenance rows' visual format by extracting `SettingsActionRow.qml` and moving Descaling Wizard and Transport Mode onto it too; verify both rows appear, state their hardware without being opened, and read correctly when disabled
- [x] 4.2 Create ONE guided page under `qml/pages/` parameterized by sensor, following `TransportPage.qml`'s grammar, register it in `CMakeLists.txt` and wire navigation through `AppShell` so both Maintenance rows open it with different arguments; verify each row opens the correct sensor's session and returns cleanly
- [x] 4.3 Implement the prepare step — no sensor-choice step exists — stating the sensor's required hardware in full and making its test profile active without starting the shot; verify the active profile changes, the wizard waits for the user, and leaving at this step writes nothing
- [x] 4.4 Implement the observe step driven by the controller, reporting hold-measured or no-hold-measured and offering another run in both cases; verify a run that never holds offers a retry and yields nothing
- [x] 4.5 Implement the entry step: instrument reading only, Celsius with an explicit unit label for temperature regardless of display preference and without writing any setting, both guards enforced with a message naming the failed check; verify a PSI-shaped and a °F-shaped entry are each refused with the right message
- [x] 4.6 Implement the confirm-and-write step showing the machine's declared hold, the entered reading and the resulting correction together behind an explicit confirmation; verify the write is refused when the test profile is not active, when the two already agree, and when a guard fails
- [x] 4.7 Re-read the sensor after writing and offer a verification run, showing the previous cycle's difference beside the current one; verify the displayed calibration is the machine's readback and that convergence is visible across two cycles
- [x] 4.8 Show stored and factory calibration for that wizard's sensor, both read from the machine, and offer NO restore-to-factory affordance; verify both values display and that no control attempts a restore
- [x] 4.9 Refuse to write and say so when the sensor's stored value is unavailable because a read went unanswered; verify with the read suppressed
- [x] 4.10 Add the heater-voltage 120 V / 230 V row to the existing `calibrationPopup` in `SettingsCalibrationTab.qml` after the fan threshold row, with an explicit unknown state and neither option preselected, and insert it into the `KeyNavigation` chain between fan threshold and the defaults button; verify selecting one writes the MMR, the display follows the readback, and tab order runs idle temp → warmup flow → test flow → time-out → fan threshold → voltage → defaults → done
- [x] 4.11 Leave "Defaults for cafe" untouched by the new row and confirm it still resets exactly the five `Settings.hardware` values; verify pressing it does not write a heater voltage to the machine
- [x] 4.12 Confirmed by screenshot: six rows, Defaults, Cancel and Done all visible without scrolling. Fixed by pinning the action row OUTSIDE the Flickable, so reachability no longer depends on the content fitting, plus row spacing 16 to 10
- [x] 4.13 Route every new string through `TranslationManager.translate` / `Tr` under new keys, and give every interactive element its accessibility role, name, focusable and press action; verify no literal user-visible text remains and the page is traversable by keyboard

## 5. Verification and documentation

- [x] 5.1 Run the full suite via `mcp__qtcreator__run_tests` (scope `all`) and confirm zero failures and zero new warnings
- [x] 5.2a DONE on hardware (2026-08-29). Two results: (1) the machine applies a TENTH of each requested correction — a +0.2 entry moved the stored offset +0.02, a +0.4 entry moved it +0.04; (2) CalCommand 3 returns the CURRENT value, not a distinct factory one — both columns read +0.89 before a write and +0.91 after. Simulator corrected to 10%, Factory row removed, reset closed out for good
- [ ] 5.2 Run the whole loop on the real machine for pressure: fit the gauge portafilter, run the wizard end to end, confirm the correction lands, then verify with a second run that the readings agree
- [ ] 5.3 DEFERRED (agreed with the user): the temperature hardware loop needs a thermocouple basket that is not available. The temperature row, its table entry and profile wiring still ship; its end-to-end run stays unverified and is called out in the PR
- [x] 5.4 Manual entry added to the Calibration section of Decenza.wiki Manual.md — two bullets in the existing list, not a new section. NOT PUSHED: wiki timing is the user's call
- [ ] 5.5 Open a PR, then run `/pr-review-toolkit:review-pr` and address findings before merge
- [ ] 5.6 Archive this change and sync specs as the final commit on the PR
