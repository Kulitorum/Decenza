## Context

`BLEManager::tryDirectConnectToScale()` handles a saved WiFi scale by handing off to `DecentScaleWifi::connectToHost()`, which tries the persisted peer IP and falls back to resolving the hostname. On Android that resolve is a direct mDNS A-query; elsewhere it is `QHostInfo`. When the cached IP is stale and the A-query returns nothing, the attempt ends, the 20 s scale-connection timer fires, and the app falls back to BLE or the estimated flow scale. There is no third mechanism.

The discovery path already has that third mechanism. `WifiScaleDiscovery::browse()` runs a DNS-SD browse for `_decentscale._tcp.local`, and `BLEManager`'s `resultFound` handler already matches a resolved instance against `m_savedScaleAddress` and calls `setPendingWifiConnect()` + `emit scaleDiscovered()`. **The auto-connect half of this change already exists** — its comment even says a browse fires "per user action or reconnect tick", which was intent that was never wired.

Field evidence is in the proposal. The short version: the responder does not answer a direct A-query for its hostname but does answer the service browse, and this is not a timing problem — #1737 raised the deadline from 2 s to 5 s and the miss simply moved to ~5002 ms with `records= 0`.

Constraints that shape the design:

- `WifiScaleDiscovery::browse()` calls `stopBrowse()` first, so two callers sharing one instance cancel each other.
- `BLEManager::isScanning()` folds `m_wifiDiscovery->isBrowsing()` into the composite property that drives the Scan button.
- The user-scan entry points clear `m_wifiResults` and the derived rows before browsing; the reconnect path must not.
- Reconnect ticks run on a ladder (~30 s, slowing to 5 min), so a browse here repeats, unlike a user scan.
- `wifiscalediscovery.h` documents "Neither does background work: nothing runs until a caller asks." That contract is what this change alters.

## Goals / Non-Goals

**Goals:**

- A saved WiFi scale whose cached IP is stale reconnects without the user opening Connections.
- Works on every platform, since `browseService()` already abstracts the backend.
- A reconnect browse is invisible to the user and cannot disturb a user-initiated scan.
- The "browse only finds, saved-address match decides" anti-substitution rule is preserved exactly.

**Non-Goals:**

- Changing cached-IP-first ordering, the recognition window, or the driver's own A-query fallback. The browse is a recovery layer above them.
- Reverting `kHdsResolveTimeoutMs`. It matches the discovery path and is harmless; only the comment claiming it fixes the 82 misses is wrong.
- Fixing the separate defect where a *transport* error short-circuits before the mDNS fallback (reproduced on macOS). Related, separately scoped.
- Continuous or idle browsing. The browse is tied to a failing reconnect attempt for a saved WiFi primary.

## Decisions

**A second `WifiScaleDiscovery` instance for reconnect, not the shared one.**
`browse()` cancels any in-flight browse, so a reconnect tick landing during a user scan would kill the user's scan — the exact failure the user is trying to fix by scanning. A separate instance also keeps `isScanning()` correct for free, since that property references `m_wifiDiscovery` specifically. Precedent exists in the same file: `m_manualEntryDiscovery` was split out for this reason and carries a comment explaining it.
*Alternative rejected:* one instance plus an "is this a reconnect browse" flag. That needs correct behaviour at three call sites (cancellation, indicator, row clearing) and one missed check silently reintroduces the bug.

**Browse only after the direct attempt has failed, not alongside it.**
The cached IP succeeds in the common case and costs one TCP connect. Browsing in parallel every tick would put multicast traffic on the network on a repeating schedule for no benefit. Gate the browse on the attempt failing.
*Alternative rejected:* browse on every reconnect tick — simpler, but it makes idle multicast traffic proportional to uptime and contradicts the on-demand principle the spec keeps.

**Reconnect browse deadline shorter than the user scan's 15 s.**
15 s is justified for a user scan because a DNS-SD browse's first callback is a stale-cache dump and real pruning arrives seconds later. A reconnect browse repeats, so it can be shorter and still converge across ticks. The exact value is an implementation task with a stated justification, not a guess baked in here.

**Reuse the existing `resultFound` auto-connect path.**
The handler already does saved-address matching and connect dispatch. Wiring the new instance to the same logic keeps one code path for "a browse found the saved scale", rather than a second matcher that can drift from the first. The file already records a drift bug of exactly that shape (two dedupes that diverged).

**Do not clear `m_wifiResults` or the device rows from the reconnect path.**
Those are user-facing scan output. A background browse that wiped them would make rows vanish while the user was reading the list.

## Risks / Trade-offs

- **Background multicast traffic where there was none** → Gated on a saved WiFi primary AND a failed direct attempt, with a short deadline, on a ladder that slows to 5 min. Worst case is a bounded browse every 5 min while the scale is genuinely gone.
- **The spec's on-demand principle is weakened** → The requirement already carved out "unless a WiFi scale is saved as the primary scale". This extends the same carve-out to the browse and keeps the prohibition absolute when no WiFi scale is saved. Written into the delta rather than left implicit.
- **A stale DNS-SD registration could resolve to a dead address** → `browseService()` already drops instances that never resolve SRV + address, and the saved-address match plus the driver's recognition window remain in force.
- **Two `WifiScaleDiscovery` instances mean two mDNS sockets** → Only one is ever browsing in the common case; they are short-lived and already ref-counted by the OS resolver.
- **~~Verification is deferred to the Android beta~~ — macOS DOES reproduce it** → An earlier draft of this document claimed macOS could not reproduce the failure because it resolves through `QHostInfo`. That was wrong, and a macOS session disproved it: the reconnect logged `QHostInfo resolution failed for hdstest.local: Host not found`, while a user scan resolved that same host by browse in 1.32 s, with `hds.local` resolving by A-query in 3.07 s in the same scan as a control. So the fix is verifiable locally on macOS with a renamed scale, and the Android beta confirms rather than being the only evidence. The reconnect browse's start/finish lines and resolved count are already logged by `WifiScaleDiscovery`.
- **The fix may not be sufficient either** → #1737 was also argued from evidence and was falsified. The falsifier here: if reconnect browses and still fails to resolve while a user scan immediately succeeds, then something about the user-scan context — not the browse itself — is the operative difference, and the next suspect is the concurrent BLE scan or the A-record probe running alongside it.

## Migration Plan

No data migration, no schema change, no persisted state. The change is additive: when the browse finds nothing, behaviour is exactly what ships today (timeout → WiFi→BLE fallback). Rollback is reverting the commit.

## Open Questions

- The reconnect browse deadline. Needs a value with a justification tied to observed resolve latency (the one measured browse resolved in 362 ms), not a round number.
- Whether the browse should also run on the *first* reconnect attempt after an app start with a stale cache, or only after the ladder's first failure. The spec as written allows either; the implementation should pick one and say why.
- Whether the A-record probe (`defaultFallbackHostnames()`) should accompany the reconnect browse as it does in a user scan. It would help pre-v3.0.9 firmware, but those scales answer A-queries — which is precisely what already works — so the likely answer is no.
