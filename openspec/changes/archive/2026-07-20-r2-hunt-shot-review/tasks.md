# Tasks: r2-hunt-shot-review

## 1. BLEManager hunt mode

- [x] 1.1 Add `Q_INVOKABLE void setRefractometerHunt(bool active)` and `bool m_refractometerHunt = false;` to `src/ble/blemanager.h`, with a comment explaining the review-page hunt semantics
- [x] 1.2 Implement `setRefractometerHunt()` in `src/ble/blemanager.cpp`: no-op on same value, `[R2-diag]` log on transition, and kick `tryDirectConnectToRefractometer()` when activating while not connected
- [x] 1.3 In `BLEManager::onScanFinished()`, after the existing flag clearing, restart the scan (set `m_scanningForScales = true`, `startScan()`) when `m_refractometerHunt` is set, a refractometer address is saved, the refractometer is not connected, BLE is not disabled, and Bluetooth is available; log the restart with `[R2-diag]`

## 2. PostShotReviewPage wiring

- [x] 2.1 In `qml/pages/PostShotReviewPage.qml` `StackView.onActivated`, replace the direct `tryDirectConnectToRefractometer()` call with `BLEManager.setRefractometerHunt(true)` (keep the existing configured/not-connected condition shape — the C++ guards make it safe either way)
- [x] 2.2 Deactivate the hunt in `StackView.onDeactivating` and in `Component.onDestruction` (`BLEManager.setRefractometerHunt(false)`)

## 3. Verification

- [x] 3.1 Build via Qt Creator MCP (quick compile check), then run the full local test suite before opening the PR
- [x] 3.2 Ask Jeff to verify on-device: open a shot review page with the R2 off, power the R2 on at an arbitrary moment, confirm it connects within one scan cycle without touching the R2 button; confirm scanning stops (log: no hunt restarts) after connect and after leaving the page

## 4. Archive

- [x] 4.1 Run `/opsx:archive` as the last commit on the feature branch right before merge so the archive + spec promotion lands in the PR
