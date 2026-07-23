## 1. DE1 AuthorizationError dialog suppression

- [x] 1.1 In `src/ble/bletransport.cpp` `onControllerError`, skip `emit errorOccurred(userMessage)` when `error == QLowEnergyController::AuthorizationError`; keep the WARN log and the `de1LinkFault` emit for the contention-teardown family unchanged.

## 2. Refractometer scoped to the review page

- [x] 2.1 Add `bool isRefractometerHunt() const` to `src/ble/blemanager.h`.
- [x] 2.2 In `src/ble/blemanager.cpp` `tryDirectConnectToRefractometer()`, return early (no-op) when `!m_refractometerHunt`, before the saved-address/disabled/availability checks.
- [x] 2.3 In `src/ble/blemanager.cpp` `setRefractometerDevice()`, replace the direct `connectedChanged → refractometerConnectedChanged` wiring with a handler that also re-kicks `tryDirectConnectToRefractometer()` when the R2 drops while `m_refractometerHunt` is set and no scan is in flight.
- [x] 2.4 In `src/main.cpp`, make the `refractometerReconnectTimer` timeout handler return (without rescheduling) when `!bleManager.isRefractometerHunt()`; leave the scale-adjacent app-resume/screensaver arming lambdas untouched.
- [x] 2.5 In `qml/pages/PostShotReviewPage.qml` `StackView.onDeactivating`, disconnect a connected refractometer (guarded) in addition to ending the hunt.

## 3. Verification

- [x] 3.1 Build via Qt Creator (Decenza project) — 0 errors, 0 warnings.
- [x] 3.2 Run the full test suite via Qt Creator — 92/92 passing, no warnings (scale tests included: `tst_scaleblepriority`, `tst_scalefeedliveness`, `tst_wifiscalediscovery`, `tst_difluidr2`).
