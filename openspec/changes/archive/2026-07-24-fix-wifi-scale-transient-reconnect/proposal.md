## Why

A WiFi Half Decent Scale that is briefly unreachable — the normal consequence of ESP32 WiFi power-save missing an ARP request — permanently loses its connection until the user manually rescans. The driver misreads a "nothing answered" transport error as "this IP is the wrong host", discards a known-good cached IP, burns its single hostname fallback 45 ms later against the same unreachable address, and gives up. The entire recovery sequence spans ~48 ms against a host-unreachable condition that lasts ~20 s, so it cannot succeed.

Observed on macOS 26 against a healthy scale: the WebSocket endpoint accepted handshakes and streamed weight frames throughout, while the app reported `WebSocket error: Host unreachable (code 7) — target=192.168.10.241 local=<unbound>` twice, 45 ms apart, then `WiFi scale did not respond as HDS — giving up this attempt`.

## What Changes

- **Classify socket-layer transport errors as transient, not as cache invalidation.** `DecentScaleWifi::onError` currently evicts the cached IP on *any* error. Errors that mean *nothing answered* (host unreachable, network unreachable, connect timeout) carry no evidence about whether the IP is the right host, and MUST NOT evict the cache or consume the hostname fallback. Errors that mean *something else answered* — a TCP RST (connection refused), or a WebSocket handshake/protocol failure such as an HTTP 403 from a router — retain the existing eviction behaviour. Note `EHOSTDOWN` is NOT in this transient set: Qt does not map it to `NetworkError`, it falls through to `UnknownSocketError`.
- **Stop the instant in-cycle retry against an unreachable host.** On a transient transport error the driver ends the attempt and lets the existing app-level `scaleReconnectTimer` own the retry, per the already-specified reconnect-ownership requirement.
- **Re-resolve the hostname via mDNS before each reconnect attempt.** The mDNS exchange is what clears the operating system's cached negative route for the peer — empirically, a manual rescan is the only thing that currently recovers the connection, and mDNS is the traffic that rescan generates. Re-resolving makes the first backoff retry (5 s) able to succeed instead of failing inside the still-live negative-cache window.
- Diagnostic logging distinguishes a transient transport failure from a wrong-host failure, so the two are separable in a support log.

Two further defects were found while implementing, both of which independently defeat the fix above and are included here:

- **The recognition window is armed after `open()` instead of before.** `open()` fails synchronously for an address the OS rejects at the routing layer, so the error handler's `stop()` ran against a timer that had not started yet; `attemptTarget` then started it anyway and `onRecognitionTimeout` fired 5 s later, evicting the very cached IP the error handler had just decided to keep. Arming the timer before `open()` makes the stop land. Found from a device log, not from the tests — the first version of the new test waited 600 ms and passed with the bug live.
- **The DE1 simulator flag gates real scale connectivity.** `BLEManager::switchScale` and `tryDirectConnectToScale` both early-return on the flag set from `simulationMode` ("no DE1 attached"), which is a *different* switch from `simulatedScaleEnabled` ("Simulated Scale"). With the DE1 simulator on and the scale simulator off, real scales — including the WiFi scale, which shares no radio with the DE1 — could not connect at all, and the app-level reconnect loop this change hands off to never ran. Scale scanning was already correctly exempt (`blemanager.cpp` notes "suppress DE1 scanning but allow scale/refractometer scans"); these two sites were outliers. They now gate on the scale simulator instead.

## Capabilities

### New Capabilities

None. This corrects the behaviour of an existing capability.

### Modified Capabilities

- `wifi-scale-discovery`: The requirement *WiFi connect tries cached IP first with hostname fallback* currently specifies, in its *Cached IP refuses connection — fall back to hostname* scenario, that a TCP-layer failure (refused / unreachable / timed out before WS upgrade) triggers the same immediate hostname fallback as a recognition timeout. That conflates an unreachable host with a wrong host and is the direct cause of the defect. The requirement changes so that socket-layer failures are transient — cache preserved, fallback not consumed, retry delegated to the app-level loop — while handshake-layer failures keep the existing fallback-and-evict behaviour. The requirement *Reconnect on transient drop is owned by the app-level reconnect loop* extends to cover a connect attempt that never reached the connected state, and gains the mDNS re-resolve-before-retry obligation.

## Impact

- `src/ble/scales/decentscalewifi.cpp` / `.h` — error classification in `onError`, the cached-IP eviction path, and the hostname-fallback trigger.
- `src/ble/blemanager.cpp` / `.h` — the simulator-gate split (`setScaleSimulated`, `scaleSimulatedChanged`, `savedScaleIsSimulated`), the guard changes in `connectToScale` / `connectToSavedScale` / `tryDirectConnectToScale`, and the connection timer armed on the discovered-WiFi-scale path.
- `src/controllers/maincontroller.cpp` — the pre-shot scale-missing abort, re-gated on the scale simulator rather than the DE1 one.
- `src/main.cpp` — the simulator-gate wiring and the reconnect re-arm. The `{5 s, 30 s, 60 s}` backoff ladder itself is unchanged, and the retry path is NOT modified: the re-resolve lives entirely inside the driver, keyed off its own state.
- No settings, schema, BLE protocol, or QML changes. BLE scale transports are untouched.
- User-visible behaviour: a WiFi scale that drops out recovers on its own within one backoff step instead of requiring a manual rescan. Wiki manual update is not expected — the documented behaviour ("the scale reconnects automatically") is what this change makes true; confirm during implementation.
