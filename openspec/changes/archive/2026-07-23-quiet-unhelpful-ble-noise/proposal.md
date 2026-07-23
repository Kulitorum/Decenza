## Why

Two BLE surfaces produce noise that helps no one. First, an idle DE1 whose encrypted link is torn down under BLE contention (the dual-HIGH signature — scale left at HIGH priority in observe mode by design) raises a modal **"Connection Error / Authorization error"** the user can only dismiss, even though the link auto-reconnects seconds later and there is no user action to take. Second, the refractometer (R2), which is only used to capture TDS/EY on the post-shot review page, is pursued by an app-wide reconnect ladder that keeps scanning and attempting connections **everywhere, forever** — off the review page it produces an endless stream of failed connect attempts that contend with the DE1/scale links and spam the log, for a device that is not needed there at all.

## What Changes

- DE1 `AuthorizationError` BLE controller errors are logged (WARN) and still drive the `de1LinkFault` wedge signal, but **no longer raise a user-facing dialog** — they are never a user-actionable pairing failure on these PIN-less devices, and the link self-heals.
- The refractometer auto-reconnect is **scoped to the post-shot review page**: off that page the R2 is not scanned for or connected, and it is disconnected on leaving the page (not only on page destruction).
- While the review page is open, the hunt remains persistent — including re-kicking the scan chain itself when a connected R2 drops mid-page, rather than depending on the removed app-wide reconnect tick.
- The scale's always-on reconnect is **explicitly unchanged** — the scale is needed everywhere and keeps its own separate reconnect path.

## Capabilities

### New Capabilities
<!-- None: both changes modify existing capability requirements. -->

### Modified Capabilities
- `ble-error-surfacing`: adds DE1 `AuthorizationError` (and the connection-teardown contention family) to the set of controller errors that are logged and drive `de1LinkFault` but do NOT surface a dialog.
- `refractometer-review-page-discovery`: reverses the off-page behavior (no background refractometer reconnect off the review page), makes leaving the page disconnect the R2, and makes the hunt re-kick its own scan chain on a mid-page drop instead of relying on the app-wide reconnect tick.

## Impact

- `src/ble/bletransport.cpp` — gate the `errorOccurred` dialog emit for `AuthorizationError`.
- `src/ble/blemanager.cpp` / `blemanager.h` — gate `tryDirectConnectToRefractometer()` on the hunt; re-kick the hunt on a mid-page R2 drop; add `isRefractometerHunt()`.
- `src/main.cpp` — the app-wide refractometer reconnect tick self-stops when the review page is closed (scale-adjacent reconnect lambdas untouched).
- `qml/pages/PostShotReviewPage.qml` — disconnect the R2 on `StackView.onDeactivating`.
- No API, dependency, or schema changes. Scale reconnect behavior is unaffected.
