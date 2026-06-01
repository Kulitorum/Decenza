## Why

On some Android tablets (confirmed on a Decent P80X, issue #1303) the DE1 BLE link drops after hours and never recovers until the app is restarted. Root cause, established by log analysis: when a saved BT scale is powered off, the scale reconnect path fires a **direct `connectToDevice()` to the absent address every 60 s**, and each attempt parks the Android BLE stack in `Connecting` for the full ~30 s supervision timeout because the connection timeout handler only clears a flag and never closes the `QLowEnergyController`. That sustained connect-to-nothing contends with the DE1 link and occasionally tips it into an `AuthorizationError` that the reconnect loop cannot recover from (it stops on `isConnecting()` and stays wedged).

A controlled comparison proves the mechanism: a known-good setup (scale connected; refractometer pursued via passive `startScan()` ~1825×) showed **zero** DE1 BT errors over 3 days, while the failing P80X (scale absent, pursued via direct connect ~744×) wedged — same firmware (v1352), same 60 s USB-charger keepalive (which is exonerated: it succeeds silently when the radio is uncontended). de1app avoids this entirely: it tries a direct connect only briefly, then closes it and relies on a passive scanner.

## What Changes

- **Background/steady-state scale reconnect becomes scan-then-connect only** — never a parked direct-connect to an absent address. A passive scan is the steady-state hunt; a direct `connectToDevice()` is issued only once the scale is actually seen advertising. This mirrors the refractometer path that already coexists with the DE1.
- **A single bounded ~4 s direct-connect is allowed only at intentional foreground triggers** — switching to a BT scale in the device picker, and app startup (and optionally DE1-wake / app-resume) — to keep connects fast when the scale is likely present.
- **The direct-connect deadline actually aborts** — on timeout it calls `disconnectFromDevice()` and deletes the controller (not just clears `m_directConnectInProgress`), freeing the radio so it cannot park to the ~30 s Android timeout.
- **Double-connect races are prevented** — direct-then-scan sequencing, or dedup on already-connecting.
- **DE1 connect-hang recovery** — a `Connecting` state that never resolves is torn down (close + recreate controller) and retried, instead of the current `isConnecting()` guard giving up permanently and wedging until app restart.
- Direct-connect remains the **sustained** strategy for the DE1 only (it sleeps but stays connectable). No supported scale needs connect-to-wake; scale `sleep()` powers the device down and `wake()` is a post-connection command.
- **(Optional follow-on)** Consolidate the fragmented per-device reconnect orchestration (DE1, scale, refractometer each drive the one shared discovery agent on their own timers plus parallel direct-connects) into a single reconnect coordinator that scans while anything is missing and connects matching devices on discovery.

## Capabilities

### New Capabilities
- `device-reconnect`: How the app re-establishes BLE links to the DE1, scales, and the refractometer after disconnect — the scan-vs-direct-connect policy, the bounded foreground direct-connect fast-path with hard abort, the DE1 sleep-wake exception, and recovery from a connect attempt that hangs in `Connecting`.

### Modified Capabilities
<!-- None. `ble-connection-priority` (HIGH/BALANCED backoff) is a separate concern and is unchanged. -->

## Impact

- **Code**: `src/ble/blemanager.cpp` / `.h` — `tryDirectConnectToScale()`, `onScaleConnectionTimeout()`, `startScan()` / `doStartScan()`, the scale reconnect timer, and the device-discovery dispatch. `tryDirectConnectToDE1()` for the bounded-abort change; the DE1 reconnect timer in `src/main.cpp` and the `Connecting`-hang recovery in `src/ble/bletransport.cpp`.
- **Behavior**: Faster, more reliable scale connects at switch/startup; no parked direct-connects during background reconnect; DE1 link survives an absent scale and self-recovers from a hung connect.
- **Platforms**: Primarily Android (where the parked-connect contention manifests); the scan-first policy is platform-neutral. Note Android 8+ often requires a scan to have seen a device before a direct connect succeeds, so the 4 s direct is best-effort with scan as the reliable fallback.
- **Related**: Interacts with but does not modify `ble-connection-priority` (the dual-HIGH/BALANCED backoff). Fixes issue #1303.
