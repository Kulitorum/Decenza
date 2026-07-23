## Context

Two independent BLE noise sources, both observed on real hardware (session log: `[BLE DE1] AuthorizationError` recurring every 10–25 min; `[BLE DiFluidR2]` failed connect attempts every ~60s for over half an hour while off the review page):

1. **DE1 dialog** — `BleTransport::onControllerError` unconditionally emits `errorOccurred(userMessage)` for every `QLowEnergyController::Error`. For `AuthorizationError` that reaches `DE1Device::errorOccurred → BLEManager::onDe1Error → bleErrorDialog` and blocks the user with "Connection Error / Authorization error", even though the same handler already emits `de1LinkFault("controller-error")` and the link auto-reconnects.

2. **R2 reconnect** — `main.cpp` owns a persistent, app-wide `refractometerReconnectTimer` ladder that arms on every disconnect (and on startup, app-resume, screensaver-exit) and calls `BLEManager::tryDirectConnectToRefractometer()` forever, independent of the post-shot review page. The page-scoped "hunt" (`setRefractometerHunt` + `onScanFinished` chaining) was meant to be the review-page mechanism, but the ladder runs alongside it everywhere.

Hard constraint from the maintainer: the **scale reconnect must not change** — the scale is needed on every page and keeps its own always-on path. The R2 reconnect arming lives in the *same* app-resume and screensaver lambdas that also arm the scale reconnect, so those lambdas are a hazard to edit.

## Goals / Non-Goals

**Goals:**
- Stop the DE1 `AuthorizationError` modal; keep the WARN log and `de1LinkFault`.
- Confine all R2 scanning/connecting to the post-shot review page; disconnect the R2 on leaving.
- Keep the hunt persistent while the page is open, including recovery from a mid-page drop.
- Zero behavioral change to scale reconnect.

**Non-Goals:**
- Changing the connection-priority (observe/balanced) behavior — the scale staying at HIGH in observe mode is intentional; this change does not demote it.
- Altering manual refractometer pairing from Settings.
- Touching the DE1's own reconnect/wedge-recovery logic.

## Decisions

**1. Suppress the DE1 dialog at the transport, gated on the error type — not downstream.**
`BleTransport::onControllerError` skips `emit errorOccurred(userMessage)` when `error == AuthorizationError`; the `de1LinkFault` emit for the contention-teardown family (Connection/RemoteHostClosed/Authorization) is unchanged. Chosen over matching the translated user string in `onDe1Error` (fragile) — the transport has the enum. `BleTransport` is shared by the DE1 and R2 accessories, so both benefit uniformly, and scales use a different transport so they are untouched.

**2. Gate the R2 reconnect at the single chokepoint `tryDirectConnectToRefractometer()`, not by editing the arming lambdas.**
Every auto-reconnect path (reconnect tick, startup, app-resume, screensaver-exit) funnels through this one function. Adding `if (!m_refractometerHunt) return;` at its top scopes all of them at once **without touching the scale-sharing lambdas** — the key safety property. Alternative considered: remove the ladder and strip R2 lines out of each shared lambda. Rejected: high risk of breaking scale reconnect for no added benefit, since the chokepoint gate already neutralizes every path.

**3. Let the reconnect tick self-stop off-page.**
The tick handler returns (without rescheduling) when `!isRefractometerHunt()`, so the 60s log spam and timer churn stop when the page closes. The tick is not needed on-page: `setRefractometerHunt(true)` kicks an immediate scan and `onScanFinished` chains it. Any arming site that still starts the timer (resume/screensaver/startup) simply ticks once and stops — harmless, and again avoids editing those lambdas.

**4. Re-kick the hunt on a mid-page drop inside `setRefractometerDevice`.**
`onScanFinished` only re-chains when a scan is already in flight; once the R2 was connected there is none. So the device `connectedChanged` handler, when it observes a disconnect while `m_refractometerHunt` is set and no scan is in flight, calls `tryDirectConnectToRefractometer()` to restart the chain. Without this, a mid-page drop would have depended on the now-removed app-wide tick.

**5. Disconnect the R2 in `PostShotReviewPage.StackView.onDeactivating`.**
Previously the disconnect lived only in `Component.onDestruction`. Deactivation (navigating away) is the correct lifecycle point for "off the page → not needed", mirroring the existing `setRefractometerHunt(false)` already there.

## Risks / Trade-offs

- [A genuinely un-pairable DE1 would now show no `AuthorizationError` dialog] → Acceptable: these devices have no PIN, so `AuthorizationError` is always contention teardown; genuine "can't find/reach DE1" surfaces as `UnknownRemoteDeviceError`/`ConnectionError`, which still dialog. The wedge detector still fires on `de1LinkFault`.
- [The reconnect tick timer is left armed but inert off-page] → Harmless: it ticks once and returns; no scanning, no BLE activity, no log spam. Chosen deliberately to avoid editing scale-adjacent code.
- [R2 no longer auto-connects at startup / outside the review page] → Intended per Option A: the R2 is only used for TDS/EY capture on the review page. Manual pairing from Settings still works.
- [Regression surface for the scale] → Mitigated by construction (no scale code touched) and verified by the existing scale tests (`tst_scaleblepriority`, `tst_scalefeedliveness`, `tst_wifiscalediscovery`, etc.).

## Migration Plan

Pure behavioral change, no data/schema/migration. Rollback is a straight revert of the four touched files. Verified locally: full suite 92/92 passing, 0 warnings, on a sanitizer-instrumented debug build.

## Open Questions

None.
