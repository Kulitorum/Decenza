## Why

The DiFluid R2 refractometer auto-populates `Settings.dyeDrinkTds` whenever it emits a `tdsChanged` signal, with no context gating and no value validation. Because BLE characteristic notifications are pushed by the device, this fires for **any** R2 reading — including ones triggered by the physical button on the R2 device itself (calibrations, accidental presses, empty-cuvette readings, rinse-water samples). These ghost readings are then captured as the next shot's TDS, producing physically-impossible values (0.02–0.64% TDS observed) in shot records. Analysis of 50 shots between Apr 13 and May 11 found 11 corrupted TDS readings from this path; PR #814 closed an unrelated leak from PostShotReviewPage but did not address the device-initiated path.

## What Changes

- Refractometer TDS readings only populate shot review fields when the PostShotReviewPage is the active page (context gate).
- Refractometer TDS readings outside the plausible espresso range (configurable; default ≥3.0%) are rejected as calibrations/accidents.
- The unconditional `tdsChanged → setDyeDrinkTds` wiring in `MainController::setRefractometer()` is replaced by review-page-scoped subscription that writes directly to the page's edit fields, removing the global `Settings.dyeDrinkTds` write-through entirely on the R2 path.
- **BREAKING (internal):** `MainController` no longer owns the R2 auto-populate side effect; QML test fixtures that simulate R2 readings to drive Settings will need to update.

## Capabilities

### New Capabilities
- `refractometer-tds-capture`: Defines when and how refractometer TDS readings are accepted into shot records — page-context gating, value validation, and the boundary between BLE-device signals and shot-record writes.

### Modified Capabilities
<!-- None — no existing spec covers refractometer behavior; this is greenfield. -->

## Impact

- `src/controllers/maincontroller.cpp`: remove `tdsChanged` connection at lines 2500–2505 (the unconditional auto-populate). The reset-to-0 writes at lines 1875–1876 and 2142–2143 stay (they prevent stale values being captured if review page never opens).
- `qml/pages/PostShotReviewPage.qml`: subscribe to `MainController.refractometer.tdsChanged` while page is visible; validate value; write to local `editDrinkTds` and recompute `editDrinkEy` from current dose/yield.
- `src/ble/refractometers/difluidr2.h`/`.cpp`: documentation comment updates only (the protocol behavior — emitting on every device-side measurement — is correct and stays).
- Shot-record schema: no changes (still uses `drinkTdsPct` / `drinkEyPct`).
- Tests: add unit test for the validation threshold; add integration test that a `tdsChanged` emission while review page is not visible does NOT mutate Settings or any shot record.
- No BLE protocol changes, no database migration, no UI layout changes.
