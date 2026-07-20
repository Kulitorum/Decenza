# Design: r2-hunt-shot-review

## Context

The R2 auto-reconnect loop (`main.cpp`, `refractometerReconnectTimer`) ticks every 60 s and each tick calls `BLEManager::tryDirectConnectToRefractometer()`, which piggybacks on the scale scan infrastructure: it sets `m_scanningForScales` and calls `startScan()`. A scan runs ~15 s and ends in `BLEManager::onScanFinished()`, which clears the scan flags. Result: a 15 s listening window per 60 s cycle.

`PostShotReviewPage` already calls `tryDirectConnectToRefractometer()` once in `StackView.onActivated` — a single 15 s window when the page opens. An R2 powered on after that window closes is invisible until the next background tick (up to 45 s), which contradicts the "turn on your refractometer and it just connects" expectation on this page.

Discovery→connect wiring lives in `main.cpp`'s `refractometerDiscovered` handler: it tears down any existing non-connected instance and creates a fresh `DiFluidR1`/`DiFluidR2`, so all this change needs is to make advertisements *observable* continuously; the connect path is untouched.

## Goals / Non-Goals

**Goals:**
- While `PostShotReviewPage` is active and a refractometer is configured but not connected, BLE scans run back-to-back so a powered-on R2 is discovered within one scan cycle (~sub-second once advertising, worst case one 15 s cycle boundary).
- Purely event-driven: the next scan is started from the previous scan's `finished` event. No new timers (project rule: timers are never guards).
- Zero behavior change off the review page; the 60 s background cadence stays as-is.

**Non-Goals:**
- No change to the reconnect backoff/ramp, the discovery→connect handler, or `refractometer-tds-capture` semantics.
- No continuous scanning on other pages (settings Connections tab has its own user-initiated scan UI).
- No new user-facing setting (project preference: smarter defaults over settings).

## Decisions

**1. Hunt mode lives in `BLEManager`, activated by the page.**
`Q_INVOKABLE void setRefractometerHunt(bool active)` sets `m_refractometerHunt`. QML calls it from `StackView.onActivated` (true) and `StackView.onDeactivating` + `Component.onDestruction` (false; both, because a page can be destroyed without deactivating on app teardown, and deactivated without destruction when pushing a page on top). Alternative considered: driving it entirely from C++ by observing navigation state — rejected because C++ has no clean view of the QML StackView, and the page already owns refractometer lifecycle calls (`tryDirectConnectToRefractometer` on activate, `disconnectFromDevice` on destruction); this follows the established pattern.

**2. Restart point is `onScanFinished()`, guarded.**
After the existing flag-clearing, if `m_refractometerHunt && !m_savedRefractometerAddress.isEmpty() && !isRefractometerConnected() && !m_disabled && isBluetoothAvailable()`, set `m_scanningForScales = true` and call `startScan()` again (same shape as `tryDirectConnectToRefractometer()`). This is the event-based continuation: scan end is the event that starts the next scan. Alternative considered: shortening the reconnect timer interval while on the page — rejected as it keeps dead windows (just smaller) and adds timer-state coupling between main.cpp and the page.

**3. `setRefractometerHunt(true)` kicks an immediate scan.**
If activating and the refractometer isn't connected, call `tryDirectConnectToRefractometer()` so the hunt starts now rather than at the next tick. The page's existing explicit `tryDirectConnectToRefractometer()` call in `onActivated` is replaced by the single `setRefractometerHunt(true)` call (which subsumes it), keeping one activation path.

**4. Hunt does not force-restart on scan errors or disconnects.**
The transient `MissingPermissionsError` path in `onScanError` clears flags without `finished` firing; the hunt deliberately does not retry from the error path — the 60 s background tick recovers, matching the existing "let the next scan tick retry" comment. Likewise, if a connected R2 drops while the page is open, the existing 5 s first-retry reconnect timer starts a scan, and the hunt then keeps it continuous from that scan's end. Wiring an immediate scan restart off `refractometerConnectedChanged` was considered and rejected: that signal fires transiently during the discovery handler's teardown-and-recreate churn, and reacting to it risks scan-connect feedback during connection setup.

**5. Connect-in-progress churn is acceptable, not guarded.**
A restarted scan could re-discover the R2 while a previous instance is still connecting, and the `refractometerDiscovered` handler would tear it down and recreate. In practice this cannot loop: discovery fires once per device per scan cycle, so the worst case (advert lands in the last ~2 s of a cycle, restart re-discovers mid-handshake) is a single teardown-and-reconnect costing ~2 s, after which the next re-discovery is a full cycle later and hits the "same device already connected" guard. A connect that takes >15 s is wedged, and teardown-and-retry is the desired recovery. Guarding on "connect in flight" was rejected: there is no connecting-state tracker, and `m_refractometerDevice && !connected` cannot distinguish "connecting" from "dropped" — using it would stall the hunt after a mid-page disconnect. No extra state is added (minimal root-cause scope).

**6. The pre-existing scan-request flag wedge is fixed in this PR.**
`tryDirectConnectToRefractometer()` set `m_scanningForScales = true` before `startScan()`, and every `startScan()` bail-out (Bluetooth off, permission denied) returned without clearing it — stranding the flag so all later reconnect attempts no-op'd on "scan flag already set", forever, even after Bluetooth returned. Pre-existing (the 60 s tick could always trigger it), but the hunt adds a new entry point and a page whose promise depends on the flag being truthful, and project rule is to fix pre-existing issues in touched files. Fix: `tryDirectConnectToRefractometer()` checks `isBluetoothAvailable()` before setting the flag (mirroring `tryDirectConnectToDE1`), and `startScan()`/the permission-denied paths clear the request flags via `clearScanRequestFlags()` when a requested scan will not start.

## Risks / Trade-offs

- [Radio/battery: continuous scanning while the page is open] → Bounded by page visibility; review pages are open for a minute or two. Scanning stops as soon as the R2 connects (the dominant case) — the guard in `onScanFinished` simply declines to restart.
- [Android scan throttling (max 5 scan starts per 30 s)] → Restart cadence is one start per ~15 s; safely under the limit.
- [Repeated `doStartScan()` clears BLE-discovered scale rows each cycle (`m_scanningForScales` path)] → Identical to today's periodic reconnect tick behavior; the affected list is only rendered on the Connections settings page, which cannot be active while the review page holds the hunt.
- [Hunt flag leaks on if a page teardown path misses deactivation] → Both `StackView.onDeactivating` and `Component.onDestruction` clear it; the guards (`connected`, no saved address, BLE disabled/unavailable) make a leaked flag inert in the common cases anyway. The screensaver path replaces the stack (destroying the page), so the hunt cannot outlive the page into the screensaver.
- [App backgrounded while the review page is current] → `StackView.onDeactivating` fires only on stack transitions, not on app suspend, so the hunt flag stays armed. On mobile the suspended event loop stops `onScanFinished` from firing, so no scans restart; on resume the hunt resumes with the page still open, which is the desired behavior. No suspend-handler wiring added (speculative for a foreground-resident tablet app).

## Open Questions

None.
