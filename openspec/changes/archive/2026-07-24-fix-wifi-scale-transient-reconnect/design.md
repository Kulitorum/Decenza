## Context

`DecentScaleWifi` connects to the Half Decent Scale over `ws://<host>/snapshot`. To survive unreliable mDNS it caches the peer IP and dials that first, falling back to the hostname when the cached IP does not validate as an HDS.

`onError` currently routes **every** error class into cache eviction plus an immediate hostname fallback. The in-code rationale is sound for one specific case it names — a home router answering `ws://192.168.1.1/snapshot` with HTTP 403 — where the cached IP genuinely belongs to another device. The defect is that the rule was generalised from "some errors prove the IP is wrong" to "any error proves the IP is wrong".

Observed failure on macOS 26, against a scale that was healthy throughout (`curl` obtained a `101 Switching Protocols` and streaming `{"grams":...}` frames from the same URL):

```
78.461  Connecting to ws://192.168.10.241/snapshot (cached IP)
78.463  WebSocket disconnected (unexpected) — peer close (code 1000)
78.464  WebSocket error: Host unreachable (code 7) — target=192.168.10.241 local=<unbound>
78.465  Cached IP 192.168.10.241 unreachable — evicting cache and falling back to hostname
78.504  Resolved hds.local to 192.168.10.241 via QHostInfo     ← same address
78.506  Connecting to ws://192.168.10.241/snapshot (cached IP)
78.509  WebSocket error: Host unreachable (code 7) — target=192.168.10.241 local=<unbound>
83.645  No recognizable HDS frame within 5000 ms
83.651  WiFi scale did not respond as HDS — giving up this attempt
```

The failures are 2 ms after `open()` with no local port ever bound, which is faster than any real round trip to that host (measured 5–125 ms). They are the operating system refusing locally. Reproduced against a neighbouring address on the same subnet:

| attempt | latency | result |
|---|---|---|
| first | 8001 ms | timeout — ARP resolution fails |
| second | **0.4 ms** | `EHOSTDOWN` — instant, from the cached reject route |

macOS installs a reject route (`UHRLWI`, `R` = Reject) for a host whose ARP resolution failed, and while that route is live every `connect()` fails instantly. Its expiry is on the order of 20 s. The ESP32-based scale uses WiFi power-save and intermittently misses an ARP request, which is what creates the route.

So both dials in the sequence above land inside one ~20 s negative-cache window, 45 ms apart. Neither can succeed. The driver then treats the second failure as exhausting the fallback and gives up, and on the manual-scan path `recognitionFailed` → `disconnectScaleRequested` → `physicalScale.reset()` destroys the driver, so nothing retries. That matches the reported symptom: only a manual rescan restores the scale.

## Goals / Non-Goals

**Goals:**

- Stop treating "nothing answered" as evidence that the cached IP is the wrong host.
- Stop the 45 ms in-cycle retry that is guaranteed to fail against a ~20 s condition.
- Let the existing app-level `scaleReconnectTimer` own retry timing, as `wifi-scale-discovery` already specifies.
- Re-resolve a `.local` hostname on each retry so the retry is not dialling a stale address, and so mDNS traffic gives the OS a chance to re-learn the peer.
- Keep the 403-from-a-router case working: an IP that answers with a non-HDS service is still evicted.

**Non-Goals:**

- Changing the `{5 s, 30 s, 60 s}` backoff ladder. It is shared with BLE scale reconnect and retuning it is a separate concern with wider blast radius.
- Any BLE scale transport change.
- Working around the OS reject route directly (raw ARP probes, route manipulation). Out of scope and not portable.
- Changing the 5 s recognition window or the `rate 10k` / `events on` connect sequence.

## Decisions

### Classify by error class, not by "any error"

Split `QAbstractSocket::SocketError` into two sets at the point of failure:

- **Transient (nothing answered)** — `NetworkError` (what Qt reports for `EHOSTUNREACH`, `ENETUNREACH` and `ETIMEDOUT`) and `SocketTimeoutError`. Preserve cache, do not consume the hostname fallback, end the attempt. `HostNotFoundError` is deliberately NOT here: it only arrives from a bare-hostname dial, which was already excluded from cached-IP eviction, so classifying it transient protects no cache decision and only suppresses the manual-add failure dialog. `EHOSTDOWN` is not mapped to `NetworkError` by Qt at all — it falls through to `UnknownSocketError`, i.e. non-transient.
- **Wrong-host (a peer answered, but it is not an HDS)** — everything else, including `ConnectionRefusedError`, `RemoteHostClosedError`, `WebSocketProtocolError`, and the `UnknownSocketError` that an HTTP 403 surfaces as. Existing evict-and-fall-back behaviour is unchanged.

`ConnectionRefusedError` belongs on the wrong-host side and this is load-bearing, not incidental: a TCP RST means a host is up at that address and actively refused the port. That is genuine evidence the address no longer belongs to the scale — the DHCP-reassignment case the fallback exists for. Only a total absence of response carries no such evidence. The observed defect was `NetworkError`, never `ConnectionRefusedError`.

The recognition timeout stays a wrong-host signal: a peer completed a WebSocket upgrade and then failed to speak HDS, which is exactly the DHCP-reassignment case the fallback exists for.

*Alternative considered:* discriminate on whether the underlying TCP connection ever reached `ConnectedState`, which is the more semantically precise question. Rejected because `QWebSocket` does not expose that transition cleanly, and reading it would mean extending the existing `QWebSocketPrivate::m_pSocket` access-bypass further into private Qt internals for a distinction the error enum already carries. The enum split is legible and testable; if it proves too coarse, the state-based check is the escalation path.

*Alternative considered:* keep evicting but add a delay before the fallback. Rejected — a timer-as-guard, which the project explicitly forbids, and it would still discard correct cached state.

### Route the retry through the app-level loop rather than an internal retry

On a transient failure the driver ends the attempt and propagates `setConnected(false)`. It does not dial anything else in that cycle. `main.cpp`'s `scaleReconnectTimer` already owns backoff and is already the specified owner of reconnect; this makes the connect-failure path match the disconnect path it was always supposed to mirror.

This is the change that actually fixes the bug: the retry moves from 45 ms to at least 5 s, and subsequent steps to 30 s and 60 s, which straddle the reject-route lifetime.

### Force a fresh hostname resolution on the retry after a transient failure

`connectToHost(hostname, preferredIp)` short-circuits to `preferredIp` when set and to the cached IP otherwise, neither of which performs a lookup. After a transient failure the next attempt SHALL take `attemptHostname()`, which already re-resolves — `QHostInfo::lookupHost` on desktop, `MdnsResolver` on Android. A driver-level flag set on transient failure and cleared on a recognised connection selects this path; it is an event-based flag, not a timer.

The cached IP is preserved rather than evicted, so identity knowledge survives; it is simply not the address dialled on the attempt immediately following a transient failure.

**The justification is address freshness, and nothing else.** When DHCP moves the scale, the address it vacated usually goes dark rather than being reassigned. A dark address answers nothing, so it classifies as transient, so the cache is retained — and without a re-resolve the driver would re-dial that dead address on every cycle indefinitely, with no other correction path (the WiFi→BLE fallback scan is useless for a WiFi-only scale, and the proactive switch-back probe only re-tests the same cached IP). Before this change any error evicted, so the case self-corrected. This restores that.

An earlier draft justified the re-resolve differently — that the mDNS exchange clears an operating-system negative route for an unreachable peer. **That claim was never instrumented and is not made.** For a `.local` name the mDNS responder *is* the scale, so during an unreachability window the resolve most likely fails too and falls straight back to the cached IP; recovery in that scenario comes from the backoff delay, not from re-resolving. The address-freshness argument above needs no measurement — it is checkable by reading the classification and cache-retention logic.

### Logging distinguishes the two classes

Transient and wrong-host failures currently produce the same `Cached IP ... unreachable — evicting cache` line, which actively misled diagnosis here: it asserted the IP was bad when it was correct. They get distinct messages, and the transient line states that the cache was retained and the retry deferred.

## Risks / Trade-offs

**A genuinely stale cached IP now takes longer to correct** → If DHCP moves the scale and the old address goes fully dark, the failure is transient-classified, so recovery waits for a backoff step instead of falling back within the same cycle. Bounded: the first retry re-resolves the hostname, so correction happens on the next attempt (≥5 s) rather than never. The prior behaviour "corrected" faster only by discarding good state far more often than bad.

**The error-enum split may misclassify some peer-answered failures as transient** → `NetworkError` is the coarse one: Qt collapses several distinct `errno` values onto it, and while the ones seen here (`EHOSTUNREACH`, `ENETUNREACH`, `ETIMEDOUT`) all mean "nothing answered", the enum alone cannot prove no future `errno` maps there with different meaning. Accepted — the cost of a misclassification is a delayed correction, not a permanent failure. A task records that `errorString()` is logged alongside the enum so support logs can distinguish them retrospectively.

**The first backoff step (5 s) may still fall inside the reject-route window** → Reject-route lifetime was observed around 20 s. The 5 s attempt may fail; the 30 s attempt should not. The user-visible outcome is still automatic recovery without a rescan, which is the goal. Retuning the ladder is explicitly out of scope.

**`recognitionFailed` still destroys the driver on the manual path** → Unchanged by this design; a transient failure no longer reaches that branch, but a genuine wrong-host failure still does. Correct for a manual "Add WiFi Scale" validation, where telling the user the address is wrong is the right outcome.

## Migration Plan

No data, settings, or protocol migration. Behaviour-only change inside one driver plus the reconnect call path. Rollback is reverting the commit; no persisted state changes shape, and a cached IP written by either version is readable by the other.

## Open Questions

- ~~Does an mDNS probe measurably clear the OS reject route, or is the backoff delay doing all the work?~~ **Resolved without measurement** (task 9.5), the way the task permitted: the claim is dropped rather than shipped unverified. The re-resolve is kept on the address-freshness justification, which needs no instrumentation. End-to-end hardware verification remained blocked throughout by an unrelated macOS code-signing issue on the development machine.
- Is the reject-route behaviour macOS-specific? Linux and Windows have their own negative-caching strategies, and Android is the platform most users run. The fix is OS-agnostic (do not misclassify, do not retry instantly), but the ~20 s figure is a macOS observation and should not be treated as universal.
