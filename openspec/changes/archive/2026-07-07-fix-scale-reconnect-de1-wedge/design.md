## Context

Issue #1303: the DE1 BLE link drops after hours on a Decent P80X (Android 9) and never recovers until the app is restarted. Restarting the DE1 or toggling tablet Bluetooth does not help — only a process restart does, which points to wedged per-process Android BLE state rather than an adapter or machine fault.

Log analysis (the reporter's #1303 log vs. a known-good 3-day log on identical firmware v1352) established the mechanism:

- When a saved BT scale is **absent**, `BLEManager::tryDirectConnectToScale()` issues a direct `connectToDevice()` to the scale's MAC. With the device gone, the Android stack parks the `QLowEnergyController` in `Connecting` for the full ~30 s supervision timeout. This repeats every 60 s.
- `onScaleConnectionTimeout()` (the ~20 s app-level timer) clears `m_directConnectInProgress` but **never closes the controller**, so the connect keeps running to the ~30 s Android timeout.
- During those windows the DE1's writes (e.g. the 60 s USB-charger keepalive on MMR `0x803854`) fail; occasionally — coincident with the scale connect's teardown — the DE1 controller fires `AuthorizationError` and the link dies.
- The DE1 reconnect timer in `main.cpp` then bails on `de1Device.isConnecting()` ("already connected/connecting, stopping retries") and never rearms, because the wedged connect stays in `Connecting` forever and never emits the error that would re-trigger retries. Permanent wedge.

Controlled evidence: the known-good setup pursues its absent refractometer with a passive `startScan()` (~1825×) and shows **zero** DE1 errors over 3 days; the failing setup pursues its absent scale with direct connect (~744×) and wedges. The USB-charger keepalive is identical on both and succeeds silently when the radio is uncontended — it is a victim, not a cause.

Reference design: de1app tries a direct connect only briefly (~4 s), then `ble close`s it and relies on a shared passive scanner, and defers scale connects under DE1 command backpressure (`bluetooth.tcl`).

Current Decenza BLE structure: a single shared `QBluetoothDeviceDiscoveryAgent` (`m_discoveryAgent`), but fragmented orchestration — DE1, scale, and refractometer each have their own reconnect timer that independently calls `startScan()` (10+ call sites) and/or a parallel direct-connect.

## Goals / Non-Goals

**Goals:**
- Stop background scale reconnect from parking a direct-connect against an absent address; make it scan-then-connect like the refractometer path.
- Keep scale connects fast at intentional moments via a single bounded (~4 s) direct-connect that actually aborts on timeout.
- Make the DE1 link survive an absent scale, and self-recover from a connect that hangs in `Connecting` (no more app-restart-only recovery).
- Preserve the DE1's legitimate sustained direct-connect for sleep-wake.

**Non-Goals:**
- Changing the `ble-connection-priority` HIGH/BALANCED backoff machinery (separate concern; unchanged).
- Changing the USB-charger keepalive cadence or the MMR write/retry mechanism (exonerated by the evidence).
- The unified reconnect coordinator is a desirable follow-on but is **not required** to fix #1303; it is scoped as optional.

## Decisions

### D1: Background scale reconnect = scan-then-connect; direct-connect only on triggers
The periodic scale reconnect tick starts/continues a passive scan and issues `connectToDevice()` only when the saved scale is seen advertising (the scan-discovery path already exists for the "found saved scale in scan, using scanned device" case). A direct-first attempt is reserved for foreground triggers.
- **Why:** A passive scan provably coexists with the DE1 (the refractometer does exactly this); a parked direct-connect to an absent address is the demonstrated contention source.
- **Alternative considered:** Keep parallel direct+scan but shorten the direct timeout globally. Rejected — even a short repeating direct-connect every 60 s reintroduces periodic radio holds; the background case has no upside (an absent scale cannot be woken by a connect).

### D2: Foreground fast-path is one bounded direct-connect with a hard abort
Triggers: device-picker switch to a BT scale, app startup, and (optionally) DE1-wake / app-resume. On the ~4 s deadline, abort via `disconnectFromDevice()` + delete the controller, then fall back to scan.
- **Why:** When the user just selected the scale or just launched the app, the scale is likely present and direct-connect saves a ~15 s scan cycle. Bounding + aborting removes the parked-radio downside.
- **Alternative considered:** Always scan-then-connect, even on triggers. Rejected — measurably slower connect at exactly the moments latency is visible to the user. Note Android 8+ may still require a prior scan sighting, so the direct is best-effort and scan remains the reliable fallback.

### D3: The timeout must tear down the controller, not flip a flag
Fix `onScaleConnectionTimeout()` (and the new bounded-direct path) to close/delete the connecting `QLowEnergyController`. This is the single most load-bearing fix: without it, every other change still leaves connects parking to ~30 s.

### D4: DE1 connect-hang recovery (watchdog)
Add a bounded watchdog on the DE1 connect: if the controller stays in `Connecting` past the deadline without connecting or erroring, tear it down (close + recreate) and re-attempt. Decouple the `main.cpp` reconnect-loop "stop retrying" decision from a bare `isConnecting()` check so a wedged connect can no longer silently end the retry chain.
- **Why:** The reporter's wedge persists precisely because a hung `Connecting` never errors and the loop treats "connecting" as progress. The DE1 transport already fully recreates its controller per attempt, so the missing piece is the abort+rearm trigger.
- **Caveat:** A second failing log showed fresh controllers still erroring (`ConnectionError`) repeatedly — i.e. some wedges live below the controller in the Android stack and may not be cured by recreation alone. The watchdog is necessary but may not be sufficient for every case; deeper process-level recovery is out of scope here and tracked as an open question.

### D5: DE1 keeps sustained direct-connect; scales do not
Scale `sleep()` powers the device down (or stops it answering) and `wake()` is a post-connection command, so no supported scale needs connect-to-wake. The DE1's ESP32 stays connectable while asleep and is not discoverable by scan, so it must keep direct-connect.

### D6: Coordinator consolidation is optional follow-on
A single reconnect coordinator (scan while anything is missing; connect matching devices on discovery; one owner of the shared agent; DE1-priority/backpressure ordering) is the cleaner long-term architecture but is separable. Land the targeted scale + DE1-watchdog fixes first; pursue the coordinator as a follow-up to avoid a large refactor on the critical-path bugfix.

## Risks / Trade-offs

- **[Foreground direct-connect still briefly holds the radio (~4 s)]** → Acceptable: it only runs at intentional moments with no active shot, versus 30 s every 60 s forever. Stagger the startup scale direct-connect after the DE1 connect so the two do not contend during the most fragile window.
- **[Slower scale connect when a scale auto-sleeps mid-session and the trigger has passed]** → Background scan reconnects it as soon as it re-advertises; acceptable and matches the refractometer behavior users already accept.
- **[A specific scale might actually rely on connect-to-wake]** → Not observed in any of the ~12 scale drivers, but not exhaustively bench-tested. Mitigation: if a named scale needs it, that one driver can opt into a single bounded direct try; the default stays scan-only.
- **[DE1 watchdog may not recover every wedge]** (D4 caveat) → Ship the watchdog as a strict improvement; track process-level recovery separately. Even partial recovery is strictly better than restart-only.
- **[Behavior change to a widely-used reconnect path]** → Mitigate with focused testing on the absent-scale and present-scale-switch cases, and validation with the #1303 reporter.

## Migration Plan

1. Land D3 (timeout aborts controller) and D1 (background scan-only) together — these remove the contention.
2. Add D2 (bounded foreground direct-connect) so connect speed at switch/startup is preserved.
3. Add D4 (DE1 connect-hang watchdog + decouple the `isConnecting()` give-up) so an already-wedged link can self-heal.
4. Validate with the #1303 reporter (absent scale should no longer drop the DE1) and on the known-good setup (no regression in connect speed).
5. (Optional, later) D6 coordinator refactor.

Rollback: changes are localized to `BLEManager` reconnect/timeout logic, the DE1 reconnect timer in `main.cpp`, and the DE1 transport connect path; revert per-decision if a regression appears.

## Open Questions

- Should the foreground trigger set include DE1-wake / app-resume, or only switch + startup? (Leaning include, staggered after DE1.)
- Exact watchdog deadline for the DE1 `Connecting` hang (must exceed the slowest legitimate connect observed, ~26 s, so ~30–40 s).
- For wedges that survive controller recreation (D4 caveat), is an in-process Android adapter/stack reset feasible, or is process self-restart the only reliable recovery? (Out of scope here.)
