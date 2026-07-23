## 1. DE1 AuthorizationError dialog suppression

- [x] 1.1 In `src/ble/bletransport.cpp` `onControllerError`, skip `emit errorOccurred(userMessage)` when `error == QLowEnergyController::AuthorizationError`; keep the WARN log and the `de1LinkFault` emit for the contention-teardown family unchanged.

## 2. Refractometer scoped to the review page

- [x] 2.1 Add `bool isRefractometerHunt() const` to `src/ble/blemanager.h`.
- [x] 2.2 In `src/ble/blemanager.cpp` `tryDirectConnectToRefractometer()`, return early (no-op) when `!m_refractometerHunt`, before the saved-address/disabled/availability checks.
- [x] 2.3 In `src/ble/blemanager.cpp` `setRefractometerDevice()`, replace the direct `connectedChanged → refractometerConnectedChanged` wiring with a handler that also re-kicks `tryDirectConnectToRefractometer()` when the R2 drops while `m_refractometerHunt` is set and no scan is in flight.
- [x] 2.4 In `src/main.cpp`, make the `refractometerReconnectTimer` timeout handler return (without rescheduling) when `!bleManager.isRefractometerHunt()`; leave the scale-adjacent app-resume/screensaver arming lambdas untouched.
- [x] 2.5 In `qml/pages/PostShotReviewPage.qml` `StackView.onDeactivating`, disconnect a connected refractometer (guarded) in addition to ending the hunt.

## 3. Review-driven hardening

- [x] 3.1 Add `refractometerHuntChanged(bool)` signal (`blemanager.h`), emitted from `setRefractometerHunt()`; in `main.cpp`, arm the reconnect tick on hunt activation and stop it on deactivation. This restores the hunt's recovery path if the scan chain dies (e.g. `onScanFinished` has no in-flight scan to re-chain after an `onScanError`), scoped to the review page.
- [x] 3.2 Fix the now-backwards `[R2-diag]` hunt-OFF diagnostic string in `setRefractometerHunt()` ("resumes" → "stops until the review page reopens").
- [x] 3.3 Update stale comments left describing the old always-on R2 reconnect: the timer declaration, the removed startup-arm block (now dead code, replaced with an accurate comment), and the app-resume / screensaver-exit blocks (`main.cpp`).
- [x] 3.4 Complete the `bletransport.cpp` contention-family comment to list `RemoteHostClosedError` (#1238) alongside `AuthorizationError`/`ConnectionError`, matching the `if` below it.

## 4. Verification

- [x] 4.1 Build via Qt Creator (Decenza project) — 0 errors, 0 warnings.
- [x] 4.2 Run the full test suite via Qt Creator — 92/92 passing, no warnings (scale tests included: `tst_scaleblepriority`, `tst_scalefeedliveness`, `tst_wifiscalediscovery`, `tst_difluidr2`).
