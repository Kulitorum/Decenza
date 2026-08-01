## Why

A saved WiFi scale cannot reconnect on its own when its cached IP goes stale. The reconnect path resolves the hostname with a direct mDNS A-query, and this responder does not answer one — 82 consecutive misses over 7.5 h in one tablet session, every one receiving **zero records**, while the scale was awake and on mains power throughout. The user spent that session on the estimated flow scale. Opening Connections and tapping Scan resolves the same hostname immediately, because the scan uses a **DNS-SD service browse** instead.

Raising the A-query deadline from 2 s to 5 s (#1737) was tried first and **did not help**: the miss now ends at ~5002 ms with `records= 0`. That falsifies the timeout explanation and leaves the browse as the mechanism that demonstrably works. Decisive evidence that the scale is reachable and awake: 16 s before seven unanswered A-queries, the same scale served a WebSocket on its IP.

**This is not Android-specific.** macOS reproduces it through an entirely different resolver (Bonjour via `QHostInfo`, not the mjansson A-query). In one session the reconnect reported `QHostInfo resolution failed for hdstest.local: Host not found`, and a user scan minutes later resolved that same host **by browse in 1.32 s**. The same scan carried its own control: `hds.local` resolved by A-query in 3.07 s while `hdstest.local` was found only by the browse. So the failure is a property of the responder rather than of any one platform's resolver — which is why the fix is scoped to all platforms rather than gated to Android.

## What Changes

- The saved-scale reconnect path SHALL run a DNS-SD service browse for `_decentscale._tcp.local`, so a WiFi scale whose cached IP is stale is recovered without the user opening Connections.
- The browse runs on **every platform**, not only Android. `MdnsResolver::browseService()` already selects Bonjour on Apple (no multicast entitlement needed) and the mjansson backend elsewhere. Only the A-query path is Android-gated; the reconnect logic is platform-neutral.
- Reconnect gets its **own** `WifiScaleDiscovery` instance, so a reconnect tick cannot cancel a user's in-flight scan (`browse()` cancels any previous browse) and cannot make the Scan button read "Scanning…".
- The reconnect browse uses a **shorter deadline** than the 15 s user-scan browse, because it repeats on the reconnect ladder rather than once per user action.
- The comment added by #1737 claiming the 5 s deadline addresses those 82 misses is now known to be wrong and SHALL be corrected. The constant itself stays — it matches the discovery path and is harmless.

Not in scope: the cached-IP-first ordering, the recognition window, and the driver's own A-query fallback all stay as they are. This adds a recovery mechanism above them.

## Capabilities

### New Capabilities

_None._ This extends an existing capability rather than introducing one.

### Modified Capabilities

- `wifi-scale-discovery`: two requirement changes.
  - **On-demand WiFi scale discovery** — currently states "no browse SHALL be open outside a user-initiated scan cycle". Narrowed so a reconnect-scoped browse is permitted when a WiFi scale is the saved primary. The requirement already carves out that case for probing ("unless a WiFi scale is saved as the primary scale"); this extends the same carve-out to the browse, and keeps the prohibition absolute when no WiFi scale is saved.
  - **New requirement: saved WiFi scale reconnect resolves by service browse** — describes the reconnect browse, its isolation from user scans, and its effect on the scanning indicator.

## Impact

- `src/ble/blemanager.{h,cpp}` — a second `WifiScaleDiscovery` instance and its `resultFound` wiring, a browse call in the WiFi branch of `tryDirectConnectToScale()`, and the `isScanning()` composite left untouched by the new instance. The auto-connect half already exists: the existing `resultFound` handler matches the saved primary and calls `setPendingWifiConnect()` — its own comment already anticipates a browse firing "per user action or reconnect tick".
- `src/network/wifiscalediscovery.h` — the class doc states "Neither does background work: nothing runs until a caller asks". That contract changes and the doc must change with it.
- `src/ble/scales/decentscalewifi.cpp` — correct the falsified claim in the `kHdsResolveTimeoutMs` call-site comment.
- `tests/` — reconnect-browse behaviour and the scan-isolation invariant.
- Field verification is Android-only and arrives with the next beta; macOS cannot reproduce the A-query failure (it resolves through `QHostInfo`).
