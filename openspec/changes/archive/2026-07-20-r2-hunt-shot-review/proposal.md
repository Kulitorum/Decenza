# Proposal: r2-hunt-shot-review

## Why

The R2 refractometer auto-reconnect loop scans for 15 seconds out of every 60, so the app is deaf to a freshly powered-on R2 for up to ~45 seconds. The post-shot review page is exactly where users power on their refractometer expecting immediate pickup — a field log from 2026-07-20 shows an R2 powered on during the dead window sitting undiscovered until a manual button press forced a scan, which then found it in 120 ms. While the review page is open with a refractometer configured, the app should be scanning continuously, not on the background duty cycle.

## What Changes

- `BLEManager` gains a "hunt" mode: while active, refractometer scans run back-to-back (each scan cycle's `finished` event immediately starts the next) instead of waiting for the 60-second reconnect tick. Event-driven — no new timers.
- `PostShotReviewPage` activates hunt mode on page activation and deactivates it on page deactivation/teardown.
- Hunt mode self-suspends (stops restarting scans) once the refractometer connects, when no refractometer is configured, when BLE is disabled (simulator mode), or when Bluetooth is unavailable.
- The existing 60-second background reconnect cadence is unchanged everywhere else in the app.

## Capabilities

### New Capabilities

- `refractometer-review-page-discovery`: continuous BLE discovery of a configured refractometer while the post-shot review page is active, including activation/deactivation rules and the conditions under which continuous scanning suspends.

### Modified Capabilities

<!-- none — device-reconnect covers scales/DE1 only; refractometer-tds-capture governs TDS reading capture, not discovery. No existing spec's requirements change. -->

## Impact

- `src/ble/blemanager.h` / `src/ble/blemanager.cpp`: hunt flag, activation API, scan-finished restart hook.
- `qml/pages/PostShotReviewPage.qml`: activate/deactivate hunt on page lifecycle.
- Battery/radio: continuous scanning is bounded by page visibility — the review page is typically open for a minute or two. Scan restarts occur every ~15 s (2 per 30 s), safely under Android's 5-starts-per-30-s scan throttle.
- No settings, no UI changes, no wiki-manual impact (behavior now matches what the manual already implies: the R2 is picked up when turned on during review).
