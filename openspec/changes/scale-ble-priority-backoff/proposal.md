## Why

On weak Android BT chipsets, requesting `CONNECTION_PRIORITY_HIGH` on **both** the DE1 link and the scale link simultaneously starves one of the two links — manifesting either as DE1 GATT write/connection failures (issue #1093) or as silent scale-notification starvation that skips extraction frames (issue #1176, where an infuse frame was abandoned at ~6 s / 9.6 g because the scale feed froze for ~6 s and resumed past the move-on weight).

The existing #1097 mitigation suppresses the scale-side HIGH request only on Android SDK < 30, on the assumption that weak chipsets correlate with old Android. Issue #1176 disproves that: a Galaxy Tab A8 (Unisoc Tiger T618 — a weak budget radio) on **Android 14 / SDK 34** still requests dual-HIGH and exhibits the contention, with DE1 controller errors appearing within ~1–15 s of the scale connecting. The SDK-version proxy is the wrong predictor; chipset capability must be observed at runtime, not inferred from the OS version — and the fix must not downgrade capable hardware that handles dual-HIGH fine.

## What Changes

- The success criterion is **an attached scale reliably delivers weight during a shot**. The primary fault *signal* is a DE1 error cluster (`CharacteristicWriteError` / "FAILED after retries" / controller `ConnectionError`) shortly after the scale connects at HIGH — confirmed in both logs as the fastest, pre-shot, causally-scale-conflict signal (#1093: ~13 s after scale connect, cascading; #1176: 0.8–15 s, every session, before any shot). Scale weight-feed liveness *while extraction is active* (gated so an idle/absent scale is never a fault) is the in-shot backstop for the residual case where the DE1 cluster does not appear early — which is exactly the #1176 shot-1151 session.
- Evaluate the in-shot liveness backstop in the **scale-agnostic** extraction/`WeightProcessor` path (which already knows a shot is active and computes the inter-sample gap), **not** in any one scale driver. The `DecentScale` watchdog is Decent-only and lifecycle-coupled to sleep/wake; it is left untouched for its own reconnect-recovery role. This makes the fix work for all 14+ scale drivers (#1093 was a Eureka Precisa, #1176 a Decent).
- On a trigger, set the **app-run skip-HIGH latch on the `BLEManager` singleton** and trigger a fast self-reconnect (`disconnectFromDevice()` + emit `disconnected()` → existing scale auto-reconnect). On reconnect `onControllerConnected()` sees the latch and takes the **pre-existing SDK<30 skip branch**, so the link comes up at platform-default BALANCED. No live renegotiation, no `connectionUpdated`/weight-resumes confirmation state machine, no new coordinator class — detection lives on the scale transport; the latched decision lives on BLEManager. The DE1 link keeps HIGH unconditionally.
- Confirmation is structural: post-reconnect the scale is no longer at HIGH so the contention is gone by construction; the latch means HIGH is never re-requested this run, so detection never re-arms (no reconnect loop, no re-trigger by any scale).
- Detection/backoff are **app-run-scoped — no disk persistence**. The first scale to prove the device can't sustain dual-HIGH latches it for the whole run; every scale this run (including after a scale-type change) then skips HIGH. The contention is a device-level property, so it is NOT re-detected per scale. Cleared only by an app restart. Avoids a settings field, cross-run lock-in, and stale classification; cost is one ~1–15 s detection window per app run.
- Retire the Android SDK-30 gate as the predictor: the scale always requests HIGH initially (like the DE1), and observed runtime behavior — not the OS version — decides whether to back off.

## Capabilities

### New Capabilities
- `ble-connection-priority`: Runtime management of BLE connection priority for the DE1 and scale links — initial priority requests, DE1 error cluster as the primary (fast, pre-shot) fault signal, scale-weight-feed liveness (gated on active extraction) as the in-shot backstop, and an app-run skip-HIGH latch (BLEManager singleton, shared by all scales) + self-reconnect that brings the scale back at BALANCED via the existing skip path. No disk persistence; no live renegotiation.

### Modified Capabilities
- None — no existing spec's requirements change.

## Impact

- **Code** (all scale-agnostic — no `*scale.cpp` driver is modified): `src/ble/transport/qtscalebletransport.cpp` (shared scale transport — owns the pure `BlePriorityDetector`, the watch-window clock, the detection slots, and the self-reconnect; reuses the existing skip branch; reads/sets the app-run latch on BLEManager), `src/ble/blemanager.{h}` (app-run skip-HIGH latch, in-memory), `src/ble/bletransport.cpp` (DE1 write-failed/controller-error surfacing), the extraction/`WeightProcessor` path (extraction-scoped `scaleFeedStalled` signal, read-only), and main.cpp wiring of `de1LinkFault` + `scaleFeedStalled` into the transport slots at the existing central site.
- **Settings**: none — no persisted state, no new settings field.
- **Platforms**: Android-relevant (the dual-HIGH contention is an Android-BLE phenomenon); the skip-HIGH path is the existing cross-platform branch.
- **Behavior**: capable hardware unchanged (keeps scale HIGH / low-latency weight; never trips the detector). An idle or absent scale never triggers backoff. Weak hardware detects the conflict (DE1 errors pre-shot, or scale stall mid-shot), self-reconnects the scale at BALANCED, and latches that for the whole app run across every scale. A false-positive costs at most one app run at BALANCED and self-corrects on the next app restart.
- **Cross-reference**: closes the gap left by #1097 for SDK ≥ 30 weak chipsets; relates to #1093 and #1176.
- **Risk**: detector thresholds must avoid false-positives from transient startup hiccups on capable hardware (mitigated by a count threshold; with no persistence a false-positive is non-sticky). One integration point to verify: a self-initiated `disconnectFromDevice()` flows into the existing scale auto-reconnect.
