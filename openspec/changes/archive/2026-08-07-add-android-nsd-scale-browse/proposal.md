## Why

A tablet could not find a WiFi scale sitting two feet away and fully reachable by IP, for five days. The instrumented session on 2026-08-06 established what is happening, and it is not what this capability's spec currently says.

Measured, one LAN, two scales on openscale 3.1.13:

- The tablet sent 7 A-record queries per attempt, 5 s per attempt, and received **0 records** — every attempt, for hours. A Mac on the same LAN resolved the same name in **272 ms, one query**, throughout.
- Making the tablet open one TCP connection to a scale (loading `http://192.168.10.145/`) flipped it to **1 query / 272 ms** — and *only for that scale*. The second scale, never contacted from the tablet, stayed silent to it while answering the Mac normally in the same window.
- The tablet's send path was verified on the wire: a third host listening on 224.0.0.251:5353 saw the tablet's queries leave. The tablet also resolved an unrelated `.local` host (Home Assistant) in 7 ms in the same session.

So the responder answers **per peer**, and having sent it IP traffic is what puts a device on the answering side. That refutes the paragraph this spec currently carries under "Saved WiFi scale reconnect resolves by service browse", which says the failure "is NOT a property of the responder" and is "a host-side condition" cleared by a tablet reboot. The reboot appeared to work because it was followed by traffic to the scale.

**A controlled test on 2026-08-06 establishes this with the two controls the first attempt lacked.** From a cold tablet: TCP to the gateway changed nothing (0/6, 0/6), so it is not a device-wide wake; a *failed* TCP connect to `hds` moved `hds.local` to 4/6 with `hdstest.local` still 0/6; a *failed* connect to `hdstest` then moved `hdstest.local` to 5/6 with `hds.local` holding 6/6. Each scale is the other's control, in both directions. A failed connect suffices, so it is the packets sent trying — not anything at the TCP layer.

The mechanism — an outbound packet makes the tablet ARP, the scale answers and caches the MAC, and only then can it unicast the reply a legacy query requires — fits every measurement and survives both controls, but remains **an inference**: no ARP frames were captured and the scale's ARP table was never read. It is also not a firmware bug worth filing: the responder answers the standard (port 5353, QM) query shape correctly, and its multicast loss rate is the network's, shared with every other host on the segment. See `docs/WIFI_SCALE_MDNS.md` for the measurements and for the two accounts this one displaced.

Three app-side consequences follow, and none is a workaround for a guess — all hold whatever the responder is really doing.

Chasing the last of them turned up a defect that had nothing to do with the scale. Android's Wi-Fi driver discards multicast frames not addressed to this device unless the app holds a `WifiManager.MulticastLock`, and the app was not holding one: the only lock belonged to the shot server, which is **disabled by default**, while three comments in the source described it as held for the whole app lifetime. Every mDNS answer the app has ever received on Android came back unicast, which is the only reason anything worked at all. That lock is still worth holding — it is required for the multicast half of mDNS to reach us — but it is not the fix for the dormancy above, which drops unicast and multicast alike.

## What Changes

- **Android browses with NsdManager as a second, independent path**, started alongside the app's own browse rather than chained after it. The system daemon owns port 5353, so its queries are not "legacy", its answers come back multicast to the group, and it sees unsolicited announcements that need no query at all. It has NOT been shown to find a scale this device has never contacted: on-device, a never-contacted scale stayed invisible to NsdManager throughout. Every measurement of it so far was taken while the app's own socket was bound to 5353 and starving the daemon NsdManager depends on, so its value is unproven rather than disproven. It is retained as an independent second path pending a fair test on the fixed build, and should be removed if that test finds nothing. Both browses feed one `resultFound` stream; deduplication is by hostname, as it already was.
  - This restores a mechanism deleted in #1249, where the NsdManager helper browsed `_http._tcp` — the wrong service type, because the scale published only a hostname at the time. openscale v3.0.9 added `_decentscale._tcp`, so the reason it was removed no longer holds.
  - Below Android API 36 there is no public way to read a resolved instance's SRV target (`NsdServiceInfo.getHostname()` is API 36). The hostname is the identity key every result is stored under and the key the saved `"wifi:<hostname>"` primary uses, so a row without one is dropped with a log line naming the API level — never with a guessed name.
- **The app takes its own multicast lock**, reference-counted, for the duration of each lookup and browse, instead of inheriting one from a default-off feature. This stands on its own and is NOT contingent on the query source port. An earlier draft claimed the two were halves of one change; they are not. The lock was genuinely absent on a default install and is needed regardless. Binding 5353 on Android was a separate idea, and a wrong one — see below.
- **Queries go out from port 5353 everywhere except Android**, falling back to an ephemeral port when the bind fails. Android is excluded on measurement: the system daemon owns 5353 there, `SO_REUSEPORT` lets our bind succeed, and inbound packets then go to the daemon instead of to us — `records=0` for every host, the MQTT broker included. mjansson then drops the QU bit by itself, so the query stops being "legacy" and the answer comes back multicast to the group with nothing per-peer in the path. This is the openscale maintainers' own recommended client-side workaround.
- **A cached IP that answers nothing is kept, not evicted.** Silence at the recognition timeout is not evidence about which device owns the address; it is what a scale that is rebooting, asleep or momentarily unreachable looks like. It matters more than it looks: dialling the cached IP is what appears to put this device back on the responder's answering side, so evicting on silence removes the one path that could recover the other two.
- **macOS now defaults to the mjansson browse backend rather than Bonjour.** Not because it is better there — Bonjour reaches a first row in 66-113 ms against mjansson's 160-270 ms, since mDNSResponder is always listening — but because macOS is the development platform rather than a shipped one: roughly two installs, both developers, against hundreds on Android. Defaulting to Bonjour meant the browse three platforms ship was never run by the only machine anyone develops on, which is the asymmetry that let this outage survive review. Both backends stay compiled in and `backend=bonjour` still selects it; what the default gives up is early warning, since an iOS release build is only compiled by CI.
- **The `resolver` selector added for diagnosing this is refused on Android.** It is process-wide and sticky, and Android's `getaddrinfo` returns NXDOMAIN for every `.local` name, so one call would leave hostname discovery dead until the app restarts.

## Capabilities

### Modified Capabilities

- `wifi-scale-discovery`:
  - **Adds** an Android-only second browse path via NsdManager, running beside the app's own.
  - **Adds** a requirement that the app hold its own reference-counted multicast lock for every lookup and browse, rather than inheriting one from a feature that is disabled by default.
  - **Adds** a requirement that queries be sent from port 5353 where the bind succeeds, with the bound port reported so a zero-record result can be interpreted.
  - **Adds** a requirement that macOS default to the mjansson browse backend while keeping both compiled and selectable.
  - **Modifies** "WiFi connect tries cached IP first with hostname fallback" so a recognition timeout with no peer answer preserves the cache instead of evicting it.
  - **Modifies** "Saved WiFi scale reconnect resolves by service browse" to replace the refuted host-side-state rationale with what the controlled per-peer test established, and to mark the ARP mechanism as inferred rather than settled.

## Impact

- `android/src/io/github/kulitorum/decenza_de1/WifiScaleNsdHelper.java` (new)
- `src/network/multicastlock.{h,cpp}` (new), `src/network/shotserver.{h,cpp}` (moved onto it)
- `src/network/wifiscalediscovery.{h,cpp}` — `startNsdBrowse()`, `parseNsdLine()`, runtime resolver branch in `probe()`
- `src/network/mdnsresolver.{h,cpp}` — `HostnameResolver` selector
- `src/ble/scales/decentscalewifi.cpp` — `onRecognitionTimeout()` eviction gate
- `src/mcp/mcptools_devices.cpp`, `resources/ai/tools/devices_wifi.md` — `resolver` and `queryPort` arguments
- No database, settings-schema or QML changes.
