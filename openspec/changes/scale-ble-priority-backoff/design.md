## Context

Both BLE transports request `CONNECTION_PRIORITY_HIGH` after connecting: the DE1 unconditionally ([`bletransport.cpp:344`](src/ble/bletransport.cpp:344)), the scale only when Android SDK ≥ 30 ([`qtscalebletransport.cpp:264-285`](src/ble/transport/qtscalebletransport.cpp:264)). On Qt 6.11.1, `QLowEnergyController::requestConnectionUpdate()` is the working path (the pre-Qt-6.10 JNI reflection silently failed; commit 950ddb05 switched to the Qt API). A default-constructed `QLowEnergyConnectionParameters` maps to HIGH on Android; **not** issuing the request at all leaves the link at Android-default BALANCED — this is precisely what the existing SDK<30 skip branch does and what this change reuses (no populated-params renegotiation needed).

The #1097 SDK-30 gate assumes weak chipset ⟺ old Android. Issue #1176's reporter device — Galaxy Tab A8 `SM-X200`, Unisoc Tiger T618, Android 14 / SDK 34 — is a weak radio above the gate, so it still requests dual-HIGH. Its log shows `[BLE DE1] CONTROLLER ERROR: ConnectionError` and a `CharacteristicWriteError FAILED after 10 retries (uuid=0000a002)` within ~1–15 s of the scale connecting at HIGH; in one shot the scale notify feed froze ~6 s mid-preinfusion (no disconnect logged) and resumed at 9.6 g, skipping the infuse frame. The developer runs `SM-X210` (Tab A9+, Snapdragon 695) with no issues and values the low-latency scale link — so a global downgrade is unacceptable.

`BleTransport` already counts DE1 write retries (`m_writeRetryCount` / `MAX_WRITE_RETRIES`) and emits `errorOccurred` on the cascade. `WeightProcessor` already computes inter-sample gap (`sinceLast`) and defines `kReconnectGapMs = 2000`, and the extraction path already knows when a shot is active. The `DecentScale` watchdog (`kWatchdogTickleTimeoutMs = 2000`) exists but its lifecycle is coupled to sleep/wake/connection state and to the `Scale::Decent::READ` notification path ([`decentscale.cpp:65,164,328`](src/ble/scales/decentscale.cpp:164)); it is **not** shot-scoped and did not fire during the #1176 in-shot stall. Code review (apply Task 2.1) showed the root cause is not cleanly determinable and, more importantly, that the watchdog is the wrong vehicle for in-shot liveness — so this change evaluates liveness in the extraction path instead (D2a), leaving the watchdog's sleep/wake/reconnect role untouched.

## Goals / Non-Goals

**Goals:**
- **Primary: an attached scale reliably delivers weight during a shot.** Success is measured by weight notifications flowing at the expected cadence while extraction is active — not by a connection-parameter value. Eliminating the dual-HIGH contention is the means; working weight is the end.
- **Scale-agnostic: works for any of the 14+ supported scale drivers, not just Decent.** All detection, backoff, and confirmation live in shared layers — the Qt scale BLE transport and the extraction/`WeightProcessor` weight path — never in a single `*scale.cpp` driver. (#1093 was a Eureka Precisa; #1176 a Decent — same root cause.)
- Eliminate the dual-HIGH contention symptom on weak chipsets regardless of Android version, by observing behavior at runtime rather than inferring from SDK.
- No false-positive when the scale is legitimately idle or absent: the scale-liveness signal (D2) is gated on "weight should be flowing", and the DE1 signal (D1) only arms after a scale connects at HIGH — so an idle scale or no-scale state never triggers backoff.
- Zero behavioral change on capable hardware: scale stays HIGH / low-latency; the detector never trips.
- Self-correcting per session: a weak device detects and backs off within the connection's detection window; no stored state, so it re-evaluates from HIGH every connection.
- Recover via a fast self-reconnect at BALANCED (no app restart, no persistence) — reusing the proven #1097 skip-HIGH path rather than a live-renegotiation state machine.

**Non-Goals:**
- Fixing the underlying weak-chipset BLE stack (out of our control; vendor OS updates won't help — Tab A8 is at terminal Android 14).
- Modifying or "repairing" the `DecentScale` watchdog — its sleep/wake/reconnect-recovery role is unchanged; in-shot liveness is handled separately in the extraction path (D2a).
- Any user-facing setting or UI for connection priority (per "prefer fewer settings").
- Changing DE1-side priority (DE1 keeps HIGH unconditionally).
- Improving scale behavior when no scale is attached (out of scope by definition — all logic here is inert without a connected scale).

## Decisions

### D1: Primary signal — DE1 error cluster correlated with scale-HIGH timing
When the scale transport requests HIGH it records an internal watch-window-start timestamp; ≥ N DE1 `CharacteristicWriteError` / "FAILED after retries" / controller `ConnectionError` (received via the `de1LinkFault` signal) within the window classifies the session dual-HIGH-incapable and triggers backoff.

*Why (confirmed from both logs):* This is the **fastest** signal and is **causally a scale conflict** — the DE1 link is healthy until the scale connects and goes HIGH, then errors begin: #1093 (Teclast P80X / SDK 28) first `CharacteristicWriteError` ~13 s after the scale connects, cascading relentlessly (701 error lines); #1176 (Tab A8 / SDK 34) DE1 `ConnectionError` 0.8–15 s after scale connect across many sessions — all **pre-shot**. The scale-liveness stall, by contrast, only surfaced ~1407 s (≈23 min) into a session during an actual shot. So DE1 lets a weak device back off *before* it can ruin a shot, by minutes. The counting infrastructure already exists in `BleTransport`.

*Alternatives considered:* (a) Scale-liveness as the *primary* signal — rejected: it is far slower (only manifests during an active shot) and would let the first shot be ruined before reacting; kept as the in-shot backstop (D2). (b) SoC allowlist via `Build.SOC_MANUFACTURER` — brittle, "Unisoc = weak" misfires. (c) SDK gate — the original #1097 proxy this change replaces.

### D2: Secondary signal — scale weight-feed liveness while weight should be flowing (in-shot backstop)
The scale failing to deliver weight notifications at expected cadence *while extraction is active and weight should be changing* is an additional trigger, gated so an idle or absent scale is never a fault. Evaluated in the extraction path (D2a), not via the `DecentScale` watchdog.

*Why:* Covers the residual case D1 misses — the actual #1176 shot-1151 session had **no** early DE1 cluster, and the stall appeared only mid-shot. D1 catches most sessions pre-shot; D2 is the safety net for sessions where DE1 errors do not cluster early. The activity gate removes the idle/no-scale false-positive a raw notify-gap would cause (a scale legitimately goes quiet when nothing changes).

### D2a: In-shot liveness is evaluated in the extraction path, not the `DecentScale` watchdog
The liveness backstop is computed where the needed state already exists and is reliable: the extraction path / `WeightProcessor`, which already knows a shot is active and already computes the inter-sample gap (`sinceLast` vs `kReconnectGapMs`). It emits an extraction-scoped `scaleFeedStalled` signal that the scale transport consumes as the D2 trigger. (No "resumed" confirmation signal is needed — D4 confirmation is structural; the standalone `scaleFeedResumed` added in the Group 2 increment is removed as dead code.) The `DecentScale` watchdog is left as-is for its own sleep/wake/reconnect-recovery role.

*Why:* Apply Task 2.1 (reading [`decentscale.cpp`](src/ble/scales/decentscale.cpp)) showed the watchdog is the wrong vehicle: `stopWatchdog()` is called by `sleep()` and `onTransportDisconnected()`, it only re-arms via fresh-connect or `wake()` (guarded by `m_characteristicsReady`), and it is tickled only by `Scale::Decent::READ` notifications — so its liveness coverage is coupled to sleep/wake/connection state and is not guaranteed to be armed during an extraction. The #1176 log is consistent with this (started at session start, no "stale" lines near the shot, no disconnect) but does not pin one definitive trigger; "repairing" it would be guessing at a mechanism and fighting an inherently non-shot-scoped design. The extraction path, by contrast, deterministically knows when weight should be flowing — the correct scope for the backstop. This also keeps the change minimal (no scale-driver surgery; reuse of existing `sinceLast`/extraction-active state).

### D3: Backoff = skip-HIGH session flag + self-reconnect (reuse the existing skip path)
Detection lives **on `QtScaleBleTransport` itself** — no new coordinator class. The decision *logic* is factored into a pure, header-only, Qt-free `BlePriorityDetector` the transport owns (not an architectural "new place" — no QObject, no wiring, no lifetime/threading concerns; it exists purely so the state machine is unit-testable in isolation with an injected clock). The transport owns the HIGH request (it feeds the detector the watch-window start) and the `QLowEnergyController` (it knows how to reconnect). main.cpp wires the two external detection inputs into transport slots: `DE1Device::de1LinkFault` (D1, forwarded from the DE1 transport so it survives transport swaps) and `WeightProcessor::scaleFeedStalled` (D2). On a trigger the detector latches skip-HIGH (once per session), and the transport calls `disconnectFromDevice()`; the existing scale auto-reconnect brings the same transport object back, and `onControllerConnected()` — seeing the latched skip-HIGH — skips the request, so the link comes up at platform-default BALANCED (the pre-existing skip behaviour, with the SDK<30 gate retired).

*Why:* The transport object persists across a reconnect (only its inner `QLowEnergyController` is recreated; the `ScaleDevice`/transport is recreated only on scale-*type* change), so a member flag survives the bounce. "Skip HIGH ⇒ BALANCED" is not new code — it is the exact path #1097 already ships and proved good. This deletes the live-renegotiation + confirmation state machine and the new coordinator class entirely. Scale-agnostic: `QtScaleBleTransport` is shared by all 14 scale drivers.

*Alternative considered (rejected):* live `requestConnectionUpdate(BALANCED)` on the wedged controller plus a `connectionUpdated`/weight-resumes confirmation state machine — more code, advisory/async, and fragile on a controller that is already in trouble. A fresh reconnect at BALANCED is the proven-good state and needs no confirmation machinery.

### D4: Confirmation is structural, not a state machine
No `connectionUpdated` subscription, no weight-resumes measurement. After the self-reconnect the scale is simply no longer at HIGH, so the dual-HIGH contention is gone by construction. Detection only ever arms while HIGH was actually requested (internal timestamp set only on the HIGH path); once the skip flag is set, HIGH is never requested again this session, so detection never re-arms and there is **no reconnect loop**. If a weak controller still cannot deliver weight even at BALANCED, that is the residual hardware failure (rare; the #1176/#1093 devices responded to BALANCED) — surfaced via existing scale-disconnect/log paths, not hidden.

### D5: Session-only detection — no persistence
Detection and backoff state live only for the current scale connection. Every connection starts the scale at HIGH and re-runs detection; on a trigger, back off this session. Nothing is stored across reboots/reconnects — no settings field, no per-device classification.

*Why:* The re-detection cost is only the ~1–15 s window, once per connection. Not persisting eliminates: a new settings field, cross-connection corroboration logic, lock-in of a misclassification, and any stale-classification path when scale firmware/OS/RF conditions change. A false-positive costs at most one session at BALANCED and self-corrects on the next connection. Trade-off accepted explicitly: a genuinely weak device re-pays the detection window each connection rather than learning once.

*Alternative considered:* persist after corroboration (self-heal once, never re-suffer the window) — rejected for this iteration: adds a settings field + corroboration + a re-probe/clear path to avoid permanent misclassification, and the per-connection cost it saves is only a few seconds. Can be revisited if the per-boot window proves painful in practice.

### D6: No feed-freshness guard on the per-frame weight-exit (considered, rejected)
A defensive guard was considered: make `WeightProcessor` refuse a weight-driven frame skip / stop on the first sample after a `sinceLast > kReconnectGapMs` gap, requiring a fresh confirming sample.

*Why rejected:* Evidence is a single shot (1151) on one flaky device — too thin to justify it. It adds per-sample state to the hot weight path and would delay a *legitimate* frame transition by up to one sample (not strictly zero-regression). Once the root cause (dual-HIGH contention) is addressed by D1–D5/D7, the feed does not stall, so the guard would mostly defend a scenario that no longer occurs — defensive code for a path that shouldn't exist post-fix, against the project's "don't handle scenarios that can't happen" ethos. Excluded from scope; revisit only if stalls are observed *after* the BLE backoff ships.

### D7: Retire the SDK-30 gate as the predictor
The scale always requests HIGH initially (symmetric with the DE1), and observed runtime behavior (D1/D2) decides backoff. The `kMinSdkForScaleHighPriority` SDK-30 branch is removed entirely — with session-only re-detection there is no need for any pre-classification default, including the SDK proxy: even genuinely old devices simply start HIGH and back off within the detection window each session.

## Risks / Trade-offs

- **Populated-params → BALANCED mapping may differ across Qt/Android backends** → Validate on a real weak device; D4 treats backoff as effective only when weight actually resumes, so an ACKed-but-ineffective param change is not mistaken for success.
- **In-shot liveness must be scale-agnostic** (codebase has 14+ scale drivers; #1093 was a Eureka Precisa, #1176 a Decent) → D2a evaluates it in the shared extraction/`WeightProcessor` path, not in any one `*scale.cpp` driver. The `DecentScale` watchdog's silence is therefore not a blocker — it is bypassed for this purpose, not repaired.
- **Idle scale / no scale misread as a stall** → D1 gates the liveness fault on "weight should be flowing" (DE1 in active extraction); a quiet idle scale or absent scale never triggers backoff.
- **Detector false-positive on capable hardware** (transient hiccup during a pour) → Require a sustained miss (not a single late sample) within the active-extraction window; with no persistence (D5) a false-positive is non-sticky — at worst one session at BALANCED, self-corrected next connection.
- **Weak controller honors neither HIGH-sustain nor the relaxation** → D4 surfaces this (weight never resumes); the session stays contended — accepted residual failure mode for hardware that cannot deliver weight under any priority (rare; the #1176 device did respond to priority requests). Surfaced for diagnostics rather than hidden.
- **Weak device re-pays the ~1–15 s detection window every connection** (the explicit cost of D5 session-only) → Accepted; detection is fast and pre-shot in the observed timings. Revisit persistence only if the per-boot window proves painful in practice.
- **Cross-component coupling** → Detection lives on the scale transport (which already owns the HIGH request and the controller). The DE1 transport and `WeightProcessor` only emit lightweight signals with no scale knowledge; main.cpp does the wiring at its existing central site. No new class, no cross-transport knowledge embedded in either side.
- **Mid-shot reconnect disruptiveness** (D2 backstop bounces the scale during a shot) → Accepted: that shot is already compromised by the stall, and BLEManager/watchdog already reconnect on scale loss; the bounce recovers the rest/next shot. The fast D1 path is pre-shot, so it bounces before any shot is affected.
- **Self-initiated disconnect must trigger auto-reconnect** → Implementation must confirm `disconnectFromDevice()` flows into the existing scale auto-reconnect (same path as a real drop); verified during Group 3.

## Migration Plan

- No data migration and no persisted state — nothing to migrate or clean up.
- Rollback: revert restores the SDK-30 gate. No schema, settings, or profile impact.
- Sequencing: single PR — D1–D3 + D7 (session-only, no persistence, no live renegotiation). Cross-referenced to #1093/#1176; plain issue links until the reporter confirms.

## Open Questions

- Exact detector thresholds (error count N, watch-window length, liveness stall bound) — to be calibrated against the #1176 log timings (DE1 errors ~0.8/2.1/7.5/14.4 s post scale-connect; in-shot stall ~6 s) during implementation.
- Confirm the self-initiated `disconnectFromDevice()` reliably flows into the existing scale auto-reconnect (same path as a real drop) — verified during Group 3.
- Whether the per-boot re-detection window is acceptable long-term, or whether persistence (the rejected D5 alternative) should be reconsidered after real-world use.
