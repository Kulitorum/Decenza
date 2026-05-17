## 1. Primary signal — DE1 error surfacing + watch window

- [x] 1.1 In `src/ble/bletransport.cpp`, emit a lightweight, distinct signal on DE1 `CharacteristicWriteError` retry, on "FAILED after retries", and on controller `ConnectionError` (reuse/extend existing `errorOccurred`; do not embed scale knowledge here). — added `DE1Transport::de1LinkFault(QString kind)` ([de1transport.h:129](src/ble/de1transport.h:129)); emitted at write-retry/write-failed (both paths) and connection-teardown controller errors in `bletransport.cpp`.
- [x] 1.2 In `src/ble/transport/qtscalebletransport.cpp`, emit a signal/timestamp when the scale issues its HIGH-priority `requestConnectionUpdate()` (the watch-window opener). — added `ScaleBleTransport::highPriorityRequested()`; emitted only when HIGH is actually requested (not on the SDK<30 skip path).
- [x] 1.3 Confirm the watch window only arms after a scale connects at HIGH (no scale ⇒ DE1 errors never trigger a scale priority change). — structural: the HIGH request happens only in the scale transport's post-connect HIGH path; no scale ⇒ no HIGH ⇒ window never opens. Group 1 has no consumer yet, so zero behavior change. (Note: Group 3 makes detection internal to the transport via a timestamp; the `highPriorityRequested()` signal added here is removed there as unneeded.)

## 2. In-shot backstop — scale-agnostic liveness in the extraction path

- [x] 2.1 Identify the scale-agnostic point where weight samples + extraction-active state coexist (`WeightProcessor` / extraction path) — the same place `sinceLast`/`kReconnectGapMs` is already computed. Do NOT touch any `*scale.cpp` driver or the `DecentScale` watchdog. — done; staleness evaluated on the DE1-driven `setCurrentFrame()` ~5Hz tick (independent of scale), resumption on `processWeight()`.
- [x] 2.2 Derive an extraction-scoped "scale stale" condition: weight feed gap exceeds the reconnect-gap threshold *while extraction is active and weight should be changing* (gate on extraction/pour state so an idle or absent scale is never a fault). Works for any connected scale type since it observes processed weight, not a driver. — gate: `m_active && m_tareComplete && m_lastWallClockMs>0 && gap>kScaleStaleMs(2000)`; idle/no-scale excluded since `m_active` is false then.
- [x] 2.3 Emit a distinct, lightweight scale-liveness signal for the detection consumer; read-only w.r.t. weight processing — no change to SAW/flow/frame behavior. — added `WeightProcessor::scaleFeedStalled()` (the D2 backstop trigger) + flag reset in `startExtraction()`; verified no SAW/flow/frame decision path touched (observation only). Note: also added `scaleFeedResumed()` in this increment under the old D4 design — now superseded; it is removed in Group 3 (task 3.1) as dead code since confirmation is structural.

## 3. Skip-HIGH flag + transport-hosted detection + self-reconnect

- [x] 3.1 `QtScaleBleTransport`: `m_skipHighPriority` + `setSkipHighPriority()` (override of new base virtual); `onControllerConnected()` skips HIGH when flag set → BALANCED; uses `QElapsedTimer m_highWindow` instead of a signal. Removed `highPriorityRequested()` (base) and dead `WeightProcessor::scaleFeedResumed`. DE1 fault reaches it via `DE1Device::de1LinkFault` (forwarded from transport — stable across swaps). `ScaleDevice::bleTransport()` accessor added; `ScaleFactory::makeScale<T>()` helper registers the transport (one chokepoint, zero driver edits).
- [x] 3.2 `onDe1LinkFault(kind)`: ignores if skip/backed-off/window-invalid; if elapsed > `kDe1FaultWindowMs` invalidate+return; else ++count, ≥ `kDe1FaultThreshold` → `triggerScaleBackoff`.
- [x] 3.3 `onScaleFeedStalled()`: backstop — if HIGH window valid and not backed off → `triggerScaleBackoff`.
- [x] 3.4 `triggerScaleBackoff()`: once-per-session guard (`m_backoffTriggered`); set `m_skipHighPriority`, invalidate window, `disconnectFromDevice()` → existing auto-reconnect → BALANCED.
- [x] 3.5 Constants: `kDe1FaultThreshold = 2`, `kDe1FaultWindowMs = 20000` (covers #1093 ~13 s, #1176 ~0.8–15 s); backstop bound is `WeightProcessor::kScaleStaleMs`.
- [x] 3.6 main.cpp: at the scale-(re)creation site, wire `DE1Device::de1LinkFault` and `WeightProcessor::scaleFeedStalled` → `physicalScale->bleTransport()` slots (transport as context ⇒ auto QueuedConnection for the worker-thread signal + auto-disconnect on scale-type change). Same-type reconnect reuses the existing transport (connections persist) — verified via the main.cpp:1327 path.
- [x] 3.7 Guards in place: base virtual no-ops mean idle/no-scale/CoreBluetooth never act; window only arms on HIGH. Self-disconnect → auto-reconnect verified at [main.cpp:1421](src/main.cpp:1421). (On-device repro still pending in Group 5.)

## 4. Retire the SDK-30 gate

- [x] 4.1 Done as part of 3.1: the `kMinSdkForScaleHighPriority` SDK-30 branch in `qtscalebletransport.cpp` is removed; the only skip condition is now `m_skipHighPriority`. Scale requests HIGH on connect (symmetric with DE1) unless detection backed off this session — no SDK/persisted-state lookup.
- [x] 4.2 Confirmed: no remaining scale-priority code path consults Android SDK (the `QJniObject`/`SDK_INT` block is gone from the priority path).

## 5. Verification & wiring

- [ ] 5.1 Build via Qt Creator MCP; confirm no regressions on a capable device (scale stays HIGH, weight low-latency, no spurious backoff) and no false-positive with an idle scale or no scale attached.
- [ ] 5.2 Reproduce the #1176/#1093-class scenario (weak chipset or simulated DE1-error / stall injection): verify the DE1 cluster triggers a pre-shot skip-HIGH + self-reconnect at BALANCED; verify the liveness backstop triggers when no early DE1 cluster occurs; verify no reconnect loop; verify a fresh app run starts at HIGH again.
- [x] 5.3 Decision logic extracted into pure header-only `BlePriorityDetector` (Qt-free, owned by the transport). `tests/tst_scaleblepriority.cpp` (registered, zero link deps) covers: DE1-cluster threshold trigger, post-window faults ignored, scale-stall backstop, never-armed (idle/no-scale) no-trigger, skip-flag blocks re-arm + detection (loop guard), disarm stops detection. Window-expiry path now deterministic via injected clock.
- [ ] 5.4 Single PR; cross-reference #1093 and #1176 using plain issue links (no `Fixes #`) until the reporter confirms.

## 6. Code-review fixes (post-review, commits 6fd287e3 + follow-up)

- [x] 6.1 Critical: `triggerScaleBackoff()` now emits `disconnected()` after `disconnectFromDevice()` (which severs controller signals / skips the controller disconnect in DiscoveredState and so never emits it) — without this the backoff was a permanent scale drop instead of a reconnect-at-BALANCED.
- [x] 6.2 `BlePriorityDetector` no longer resets the fault count/window on every `armWindow()`; faults accumulate across reconnects, window anchors to the first fault and slides — a flapping weak link no longer starves the threshold. Tests updated + anti-starvation regression test added.
- [x] 6.3 `bletransport.cpp` no longer emits `de1LinkFault` on a transient `write-retry` (only `write-failed` / `controller-error` count) — removes a capable-hardware false-positive; matches design D1.
- [x] 6.4 Cross-thread `WeightProcessor::scaleFeedStalled` → transport connection pinned `Qt::QueuedConnection` explicitly; backoff log no longer prints the always-constant count; duplicate comment block removed.
- [x] 6.5 **App-run scope (this directive):** skip-HIGH latch moved from the per-transport `BlePriorityDetector` to the process-lifetime `BLEManager` singleton (`scaleSkipHighPriority()` / `setScaleSkipHighPriority()`, in-memory). `onControllerConnected()` seeds the detector from it; `triggerScaleBackoff()` sets it. Once any scale triggers backoff, every scale this app run (including after a scale-type change) skips HIGH; cleared only by an app restart. Contention is device-level, not per-scale.
