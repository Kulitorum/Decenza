## ADDED Requirements

### Requirement: Refractometer readings only populate the active shot review page

Refractometer TDS readings from the DiFluid R2 (received via the BLE `tdsChanged` signal) SHALL populate shot review fields only when the PostShotReviewPage is the active page in the QML navigation stack. When the review page is not active, the system SHALL silently drop any incoming `tdsChanged` signal without modifying `Settings.dyeDrinkTds`, `Settings.dyeDrinkEy`, any shot record, or any other persisted state.

#### Scenario: Reading arrives while review page is active

- **WHEN** the PostShotReviewPage is the top of the navigation stack
- **AND** the DiFluid R2 emits a valid `tdsChanged` signal (value in plausible range, see threshold requirement)
- **THEN** the page's local `editDrinkTds` field is set to the received value
- **AND** the page's local `editDrinkEy` field is recomputed from the current `editDoseWeight` and `editDrinkWeight`
- **AND** the user can review the value, edit it, and save it to the shot record via the existing save flow

#### Scenario: Reading arrives while review page is not active

- **WHEN** any other page is active (idle, espresso-running, settings, history, etc.)
- **AND** the DiFluid R2 emits a `tdsChanged` signal
- **THEN** the signal is logged at debug level with the received value and the reason "review page not active"
- **AND** no Settings field is modified
- **AND** no shot record is modified
- **AND** no carry-over occurs into the next shot's metadata

#### Scenario: Reading arrives while review page is opening (race window)

- **WHEN** a shot has just ended and the review page is mid-navigation but not yet the active page
- **AND** the DiFluid R2 emits a `tdsChanged` signal during this window
- **THEN** the signal is dropped (treated identically to "review page not active")
- **AND** the user can press the R2 button again once the page is visible

### Requirement: Refractometer readings outside the plausible espresso range are rejected

The system SHALL reject incoming `tdsChanged` values that are below 3.0% as calibration / empty-cuvette / rinse-water readings. Rejected readings SHALL NOT modify any shot record or settings field, even when the PostShotReviewPage is the active page.

#### Scenario: Sub-threshold reading dropped during review

- **WHEN** the PostShotReviewPage is the active page
- **AND** a `tdsChanged` signal is emitted with value < 3.0%
- **THEN** the signal is logged at debug level with the received value and the reason "below plausible-espresso threshold"
- **AND** the page's `editDrinkTds` and `editDrinkEy` remain at their previous values

#### Scenario: Threshold value accepted

- **WHEN** the PostShotReviewPage is the active page
- **AND** a `tdsChanged` signal is emitted with value ≥ 3.0%
- **THEN** the page's `editDrinkTds` is updated to the received value
- **AND** `editDrinkEy` is recomputed from current dose/yield

### Requirement: TDS capture has no global side effects on the BLE signal path

The BLE-level R2 signal handler in `MainController` SHALL NOT write directly to `Settings.dyeDrinkTds` or `Settings.dyeDrinkEy` from the `tdsChanged` callback. The only writers to these settings SHALL be: (a) shot-end cleanup writes that reset to 0, (b) MCP tool writes, and (c) the legacy settings serializer at app startup.

#### Scenario: Connecting refractometer does not install a global auto-populate handler

- **WHEN** `MainController::setRefractometer()` is called with a non-null R2 instance
- **THEN** no `connect(...tdsChanged → setDyeDrinkTds...)` wiring is created
- **AND** disconnecting from a refractometer instance does not require tearing down any such wiring

#### Scenario: Settings remain zero between shots when no review takes place

- **WHEN** a shot completes and the user dismisses or navigates away from the review page without saving a TDS value
- **AND** the R2 emits subsequent `tdsChanged` signals (e.g., device-initiated button presses)
- **AND** the next shot subsequently completes
- **THEN** the next shot's metadata captures TDS as 0 (unmeasured), not as any leaked R2 value

### Requirement: Refractometer-driven EY values are computed from current page dose and yield

When a `tdsChanged` signal is accepted by the PostShotReviewPage, the page SHALL recompute `editDrinkEy` using its currently-displayed `editDoseWeight` and `editDrinkWeight` values, rather than reading from any Settings field. This keeps the on-screen TDS and EY consistent with whatever dose/yield edits the user has made in the review page since the shot ended.

#### Scenario: EY reflects in-page dose/yield edits

- **WHEN** the user edits `editDoseWeight` from 18.0g to 18.5g on the review page
- **AND** subsequently presses the R2 button and a valid `tdsChanged` arrives
- **THEN** `editDrinkEy` is computed using 18.5g (the edited value), not the original dose stored in the shot record

#### Scenario: EY computation handles zero yield gracefully

- **WHEN** a valid `tdsChanged` arrives and `editDrinkWeight` is 0
- **THEN** `editDrinkEy` is set to 0 (no division-by-zero), or left at its previous value, with a debug log entry
- **AND** `editDrinkTds` is still updated
