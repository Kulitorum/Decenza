## Why

A tablet could not find a WiFi scale sitting two feet away and fully reachable by IP, for five days. The instrumented session on 2026-08-06 established what is happening, and it is not what this capability's spec currently says.

Measured, one LAN, two scales on openscale 3.1.13:

- The tablet sent 7 A-record queries per attempt, 5 s per attempt, and received **0 records** — every attempt, for hours. A Mac on the same LAN resolved the same name in **272 ms, one query**, throughout.
- Making the tablet open one TCP connection to a scale (loading `http://192.168.10.145/`) flipped it to **1 query / 272 ms** — and *only for that scale*. The second scale, never contacted from the tablet, stayed silent to it while answering the Mac normally in the same window.
- The tablet's send path was verified on the wire: a third host listening on 224.0.0.251:5353 saw the tablet's queries leave. The tablet also resolved an unrelated `.local` host (Home Assistant) in 7 ms in the same session.

So the responder answers **per peer**, and having dialled it is what puts a device on the answering side. That refutes the paragraph this spec currently carries under "Saved WiFi scale reconnect resolves by service browse", which says the failure "is NOT a property of the responder" and is "a host-side condition" cleared by a tablet reboot. One resolver stack cannot produce opposite outcomes for two peers at the same moment. The reboot appeared to work because it was followed by traffic to the scale.

The suspected mechanism — a legacy (ephemeral-port) query must be answered by unicast, so the scale must ARP for the querier, and ESP-IDF ships `ETHARP_TRUST_IP_MAC` off — fits every measurement but is **not proven**: no ARP frames were captured, the scale's ARP table was never read, and this spec already records a counter-example it does not explain (misses continuing against a scale that had served a WebSocket on its IP 16 s earlier). A bug report has gone to the openscale side; this change is what the app can do without waiting for firmware.

Three app-side consequences follow, and none is a workaround for a guess — all hold whatever the responder is really doing.

Chasing the last of them turned up a defect that had nothing to do with the scale. Android's Wi-Fi driver discards multicast frames not addressed to this device unless the app holds a `WifiManager.MulticastLock`, and the app was not holding one: the only lock belonged to the shot server, which is **disabled by default**, while three comments in the source described it as held for the whole app lifetime. Every mDNS answer the app has ever received on Android came back unicast — which is the only reason anything worked, and exactly why it stopped working against a responder that will not unicast to a stranger.

## What Changes

- **Android browses with NsdManager as a second, independent path**, started alongside the app's own browse rather than chained after it. The system daemon owns port 5353, so its queries are not "legacy", its answers come back multicast to the group, and it sees unsolicited announcements that need no query at all. That makes it the only path that can find a scale this device has never talked to — the case the app's own browse cannot recover from on its own. Both browses feed one `resultFound` stream; deduplication is by hostname, as it already was.
  - This restores a mechanism deleted in #1249, where the NsdManager helper browsed `_http._tcp` — the wrong service type, because the scale published only a hostname at the time. openscale v3.0.9 added `_decentscale._tcp`, so the reason it was removed no longer holds.
  - Below Android API 36 there is no public way to read a resolved instance's SRV target (`NsdServiceInfo.getHostname()` is API 36). The hostname is the identity key every result is stored under and the key the saved `"wifi:<hostname>"` primary uses, so a row without one is dropped with a log line naming the API level — never with a guessed name.
- **The app takes its own multicast lock**, reference-counted, for the duration of each lookup and browse, instead of inheriting one from a default-off feature. This and the 5353 port are two halves of one change: a query from an ephemeral port is answered by unicast and needs no lock, one from 5353 is answered by multicast and is worthless without it. Shipping the port change alone would have reproduced the older Android measurement rather than fixed it.
- **Queries go out from port 5353** where the bind succeeds, falling back to an ephemeral port only when it does not. mjansson then drops the QU bit by itself, so the query stops being "legacy" and the answer comes back multicast to the group with nothing per-peer in the path. This is the openscale maintainers' own recommended client-side workaround.
- **A cached IP that answers nothing is kept, not evicted.** Silence at the recognition timeout is not evidence about which device owns the address; it is what a scale that is rebooting, asleep or momentarily unreachable looks like. It matters more than it looks: dialling the cached IP is what appears to put this device back on the responder's answering side, so evicting on silence removes the one path that could recover the other two.
- **The `resolver` selector added for diagnosing this is refused on Android.** It is process-wide and sticky, and Android's `getaddrinfo` returns NXDOMAIN for every `.local` name, so one call would leave hostname discovery dead until the app restarts.

## Capabilities

### Modified Capabilities

- `wifi-scale-discovery`:
  - **Adds** an Android-only second browse path via NsdManager, running beside the app's own.
  - **Adds** a requirement that the app hold its own reference-counted multicast lock for every lookup and browse, rather than inheriting one from a feature that is disabled by default.
  - **Adds** a requirement that queries be sent from port 5353 where the bind succeeds, with the bound port reported so a zero-record result can be interpreted.
  - **Modifies** "WiFi connect tries cached IP first with hostname fallback" so a recognition timeout with no peer answer preserves the cache instead of evicting it.
  - **Modifies** "Saved WiFi scale reconnect resolves by service browse" to replace the refuted host-side-state rationale with what the two-scale control established, and to mark the ARP mechanism as suspected rather than settled.

## Impact

- `android/src/io/github/kulitorum/decenza_de1/WifiScaleNsdHelper.java` (new)
- `src/network/multicastlock.{h,cpp}` (new), `src/network/shotserver.{h,cpp}` (moved onto it)
- `src/network/wifiscalediscovery.{h,cpp}` — `startNsdBrowse()`, `parseNsdLine()`, runtime resolver branch in `probe()`
- `src/network/mdnsresolver.{h,cpp}` — `HostnameResolver` selector
- `src/ble/scales/decentscalewifi.cpp` — `onRecognitionTimeout()` eviction gate
- `src/mcp/mcptools_devices.cpp`, `resources/ai/tools/devices_wifi.md` — `resolver` and `queryPort` arguments
- No database, settings-schema or QML changes.
