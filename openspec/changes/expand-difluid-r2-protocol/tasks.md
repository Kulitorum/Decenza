## 1. Diagnostics tables (no behaviour change, lands first)

- [x] 1.1 Add a status-code translation covering 0–12 in `difluidr2.cpp`, returning the numeric value for anything unrecognised
- [x] 1.2 Replace the two hardcoded status log branches (0 and 11) with the table
- [x] 1.3 Add an error-code translation for class 2 codes 1–4 and class 3 (Hardware), including the on-screen code in the hardware message
- [x] 1.4 Route the existing error handling through the table, leaving the user-actionable/log-only division exactly as it is
- [x] 1.5 Tests: every status code logs its name; unknown status logs its number; hardware error names the on-screen code; class 2 code 1 is named but raises no new user-visible error

## 2. Serial number capture

- [x] 2.1 Add a fixed-width serial buffer plus per-part arrival tracking to `DiFluidR2`
- [x] 2.2 Handle Func 0 Cmd 0 packets — splice 5 bytes at `Data0 * 5`, order-independent
- [x] 2.3 Log the serial only once all three parts have arrived; never log a partial as the device identity
- [x] 2.4 Send the Get-SN query alongside the existing model/firmware queries in the connect handshake
- [x] 2.5 Tests: in-order assembly, out-of-order assembly produces the identical string, partial set logs no identity

## 3. Measurement lifecycle

- [x] 3.1 Restart `m_measurementTimer` on pack 0 statuses 4, 5 and 10, and on per-test result packets
- [x] 3.2 Rewrite the timer's comment to say what it now is — a liveness watchdog fed by device traffic — and why that remains the documented exception to the no-timers-as-guards rule
- [x] 3.3 Clear the measuring state on transport disconnect, not only on result or timeout (already correct in `onTransportDisconnected`/`onTransportError`; locked in with a test rather than changed)
- [x] 3.4 Tests: progress packets past the watchdog interval do not abort; status 10 restarts it silently; total silence still times out; status 6 stops it; disconnect mid-run clears measuring

## 4. Averaged measurement

- [x] 4.1 Add an averaged-measurement entry point to `RefractometerDevice` with a base implementation that falls back to a single measurement (keeps `DiFluidR1` untouched)
- [x] 4.2 Implement it in `DiFluidR2` as Device Action Cmd 1 with a one-byte count, clamped to 1–10, checksum computed the same way as every other command
- [x] 4.3 Dispatch result packets on the response's action code: pack 2 is terminal only under cmd 0, and an unrecognised cmd falls back to that same single-test interpretation
- [x] 4.4 Emit each pack 3 as it converges (latest-wins, never contingent on a terminal packet), and move completion — measuringChanged/measurementComplete — onto status 6
- [x] 4.5 Surface the M-of-N progress from pack 4 as a signal the UI can bind to
- [x] 4.6 Confirm pack 3 and pack 4 parse correctly against the spec's worked example — this code has never run
- [x] 4.7 Tests: request bytes for count 3; clamping at 0 and 25; pack 2 under cmd 1 emits nothing; unknown cmd still emits; each pack 3 emits; run does not complete until status 6; device-initiated average (no requestMeasurement) still delivers; single test unchanged

## 5. Auto Test

- [x] 5.1 Add the Auto Test set command (Device Settings Cmd 1) and log the device's echoed confirmation
- [x] 5.2 Confirm a device-initiated reading reaches consumers through the unchanged `tdsChanged` path with no extra side effects
- [x] 5.3 Tests: command bytes for on and off; echo logs the resulting state
- [x] 5.4 Decided: Auto Test ships with UI — an on/off button on the Connections page refractometer row, reflecting the device's own stored setting

## 6. User-facing capture

- [x] 6.1 Add an averaged-read affordance to the TDS control on `PostShotReviewPage.qml`, defaulting to single test
- [x] 6.2 Show multi-test progress while a run is in flight so the control does not read as hung
- [x] 6.3 Accessibility: `Accessible.role`/`name`/`focusable`/`onPressAction` on any new control, and fix any pre-existing violations in the files touched
- [x] 6.4 Internationalize all new visible text via `TranslationManager.translate` / `Tr`, reusing existing keys where they fit
- [x] 6.5 Resolved: not distinguished after the fact. The saved value is a TDS measurement either way; M-of-N progress during the run is what the user needs, and a persisted "this was an average" flag has no consumer

## 7. Documentation and verification

- [ ] 7.1 Update the wiki manual's refractometer section for averaged reads (and Auto Test, if it ships with UI) — averaging is user-visible and the manual entry is part of this change, not a follow-up
- [ ] 7.2 Note in `docs/CLAUDE_MD/BLE_PROTOCOL.md` which parts of the R2 command set are now implemented and which remain deliberately unimplemented (calibration, loop test, remaining settings)
- [ ] 7.3 Run the full suite through the Qt Creator MCP (`run_tests`, scope `all`) and clear every warning
- [ ] 7.4 Verify on physical R2 hardware: an averaged run completes, exceeds the old 15s ceiling without aborting, and reports a plausible averaged TDS. This has never run against a device — do not merge on test-suite green alone
- [ ] 7.5 Archive this change with `openspec archive expand-difluid-r2-protocol` as the last commit on the feature branch
