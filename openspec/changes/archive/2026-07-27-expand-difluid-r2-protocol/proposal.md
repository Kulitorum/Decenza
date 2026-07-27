## Why

Decenza's DiFluid R2 driver implements roughly a third of the device's published protocol. It can request one single test, names 2 of 13 status codes, surfaces 2 of 4 error codes, and drops the serial number on the floor. Comparing it against DiFluid's own `protocolR2.md` and against [Beanconqueror's R2 driver](https://github.com/graphefruit/Beanconqueror/tree/master/src/classes/devices) — a second independent implementation of the same device — surfaced concrete, already-specified capability we are simply not using.

The headline gap is **averaging**. A single refractometer reading on espresso carries meaningful run-to-run scatter; averaging several is standard practice, and the R2 does it in firmware. We already parse the averaged result packets (pack 3 and pack 4) — we have simply never sent the command that produces them. Users get the noisier number because of a missing request, not a missing capability.

The rest is diagnosability. A user reporting "my R2 hangs" or "TDS looks like Brix" currently produces a log reading `Status: 5` and `R2 error: class=2 code=1`, which tells nobody anything.

## What Changes

- **Average Test**: request an averaged reading over N tests (Func 3 Cmd 1), with N settable 1–10 (Func 1 Cmd 3). Exposed as a user choice on the TDS capture affordance, defaulting to the current single-test behaviour.
- **Measurement lifecycle becomes status-driven**: an average of 5 tests can exceed the driver's fixed 15-second measurement watchdog, which would abort a healthy measurement. The watchdog must be fed by the device's own progress packets — including status 10, which the R2 emits specifically to say "still working, this one is taking a while".
- **Full status table (0–12)**: name every code — Calibration Started/Finished, Average Test Started/Ongoing/Finished, Loop Test Started/Ongoing/Finished — instead of only 0 and 11.
- **Full error table**: name class-2 codes 1 (Test Error) and 2 (Calibration Failed) alongside the existing 3 (No Liquid) and 4 (Beyond Range), and class 3 (Hardware) with the code shown on the device screen.
- **Serial number reassembly**: the SN arrives as 3 packets, `Data0` = part index, 5 bytes each. Capture and log it. This feeds the open Brix-vs-TDS variant identification work ([#1386](https://github.com/Kulitorum/Decenza/pull/1386)), where the model string alone has not been enough to tell a genuine Extract from a Brix rebrand.
- **Auto Test** (Func 1 Cmd 1, optional): let the R2 start a measurement itself on a prism/tank temperature change, so TDS arrives when the sample is loaded with no button press.

Not in scope — shipped separately in [#1656](https://github.com/Kulitorum/Decenza/pull/1656): temperature-unit (Data5) handling, set-to-Celsius on connect, and Microbalance Ti service `0x00DD` support.

No breaking changes. Every addition is opt-in or log-only; the default measurement path stays a single test.

## Capabilities

### New Capabilities

- `refractometer-averaged-measurement`: requesting an N-test averaged TDS reading, how the averaged result is distinguished from a single reading, and how the measuring lifecycle stays alive across a multi-test run without a fixed timer.
- `refractometer-device-diagnostics`: what the driver records about device state and identity — the full status and error tables, and serial-number capture — so a field log is readable by a human or an AI triaging it.

### Modified Capabilities

<!-- None. `refractometer-tds-capture` governs which readings may populate the review
     page; an averaged reading arrives through the same tdsChanged path under the same
     gating, so its requirements are unchanged. -->

## Impact

- `src/ble/refractometers/difluidr2.{h,cpp}` — command construction, packet dispatch, measurement lifecycle. The bulk of the change.
- `src/ble/refractometers/refractometerdevice.h` — the abstract interface gains averaged-measurement entry points; `DiFluidR1` must remain buildable (it has no averaging, so it declines).
- QML TDS capture affordance on `PostShotReviewPage.qml` — a way to choose an averaged read.
- `Settings` — if the test count is remembered, it belongs in an existing domain sub-object, never on `Settings` directly. Prefer no new setting at all if a sensible fixed count works (see `docs/CLAUDE_MD/SETTINGS.md` and the project's bias against new user-facing settings).
- `tests/tst_difluidr2.cpp` — packet-level coverage for each new path.
- Wiki manual (`Manual.md`, refractometer section) — averaging is user-visible and must be documented in the same change.
- No BLE UUID, database, or migration impact.
