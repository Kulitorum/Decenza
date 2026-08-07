# Tasks

## 1. Android NsdManager browse

- [x] 1.1 `WifiScaleNsdHelper.java`: per-token browse (`startBrowse` / `pollBrowse` / `stopBrowse`), no static listener state, so concurrent `WifiScaleDiscovery` instances never cancel each other.
- [x] 1.2 Serialize `resolveService()` — a single slot on the supported API levels, so a second concurrent call fails with `FAILURE_ALREADY_ACTIVE` and drops the second scale on a two-scale LAN.
- [x] 1.3 Read the SRV target through `NsdServiceInfo.getHostname()` (API 36, via reflection). Never `InetAddress.getHostName()`: the address arrives through a Parcel with no cached host, so that call always does a blocking reverse lookup and returns the IP literal or a router PTR.
- [x] 1.4 `WifiScaleDiscovery::startNsdBrowse()` — poll in slices so results are incremental and the shared browse cancel token is honoured within one slice.
- [x] 1.5 Drop a row with no hostname, with a log line naming the device's API level; never guess a name that would be persisted as the saved primary.
- [x] 1.6 Extract `parseNsdLine()` so the wire format is testable off-device.

## 2. Cached-IP eviction

- [x] 2.1 Gate eviction in `onRecognitionTimeout()` on `m_wsHandshakeDone` — evict when a peer answered and was not an HDS, keep when nothing answered.

## 3. Multicast lock

- [x] 3.a `MulticastLock` — reference-counted, thread-safe RAII holder, taken by `resolveHostname()` and `browseServiceMjansson()` for their duration. MqttClient's `.local` resolve inherits it through the same call.
- [x] 3.b Move ShotServer onto it and delete its hand-rolled JNI, so there is one definition rather than two.
- [x] 3.c Keep the ShotServer member OUT of `#ifdef Q_OS_ANDROID` — `MulticastLock` is a no-op type elsewhere, so the two lines that manage it get type-checked on the development platform instead of only in CI.
- [x] 3.d Correct the three comments (`mdnsresolver.h`, `wifiscalediscovery.cpp`, `shotserver.h`) that described the lock as held for the whole app lifetime. It was held only while a default-off feature ran.

## 4. Query source port

- [x] 4.0 Bind the query socket to 5353 with `SO_REUSEPORT` (the library already sets it), falling back to ephemeral only if the bind fails. mjansson then drops the QU bit on its own, so the query stops being "legacy" and the answer comes back multicast.
- [x] 4.0-android **EXCEPT on Android, which uses an ephemeral port unconditionally.** Not a fallback — the bind there SUCCEEDS and the socket is then starved by the system daemon that already owns 5353. Reproduced on the current build with the multicast lock held: `srcPort= 5353`, zero records from any host, while the same build resolves an unrelated `.local` name from an ephemeral port in 12 ms. This item read as universal until the exclusion shipped in `dd84974c`; the earlier wording is what let the exclusion look like a regression.
- [x] 4.0a Centralize both socket-open sites (`resolveHostname` and `browseServiceMjansson`) into one `openQuerySocket()` — they were already duplicated.
- [x] 4.0b Report the bound port in `ResolveStats`/`BrowseStats` and in every start/done log line.
- [x] 4.0c `queryPort` argument on `devices_wifi` so the A/B against the older on-device 5353 measurement can be run without a rebuild.

## 4b. macOS default

- [x] 4b.1 macOS `auto` browse backend is now mjansson, not Bonjour. macOS is the development platform (about two installs) while the shipped populations are Android (hundreds) and iOS, so the default is chosen to exercise what most users run rather than what the platform prefers.
- [x] 4b.2 Both backends stay compiled into the macOS binary — only the default moved; `backend=bonjour` still switches at runtime. The `#ifndef Q_OS_IOS` / `if(NOT IOS)` guards are untouched.
- [x] 4b.3 The hostname resolver default deliberately does NOT follow: the mjansson resolver is Android-only and QHostInfo is what iOS ships, so flipping both would strip dev coverage from both iOS paths at once.
- [x] 4b.4 Record what the default gives up: an iOS release build is only compiled by CI, so macOS was the one place a Bonjour regression surfaced early. Run `backend=bonjour` before an iOS release.

## 5. Diagnostics

- [x] 5.1 `MdnsResolver::HostnameResolver` selector so a desktop build can run Android's exact A-record path.
- [x] 5.2 Refuse `resolver=system` on Android in `devices_wifi` — it is sticky and would disable `.local` resolution for the session.
- [x] 5.3 Bump `McpSurfaceVersion`.

## 6. Corrections to the record

- [x] 6.1 Replace the "host-side resolver state" rationale in `attemptHostname()` and in the spec with what the two-scale control established.
- [x] 6.2 State the ARP mechanism as suspected, not settled, at every site that asserts it, and name the counter-example it does not explain.
- [x] 6.3 Keep the measured window (several hours) and the user-visible outage (five days) as separate numbers wherever either is cited.
- [x] 6.4 Fix the stale `resolveHostname() is only called on Android` comment in `mdnsresolver.cpp`.

## 7. Tests

- [x] 7.1 `tst_wifiscalediscovery`: resolver selector default, reports-what-ran, probe honours a pinned resolver.
- [x] 7.2 `tst_wifiscalediscovery`: `parseNsdLine()` field alignment, including the empty-hostname and start-failure cases.
- [x] 7.3 `tst_decentscalewifi`: a silent cached IP is kept, an answering one is evicted.

- [x] 7.4 `tst_wifiscalediscovery`: query-port default and readback.
- [x] 7.5 `tst_wifiscalediscovery`: the macOS browse-backend default, that both backends stay selectable there, and that the resolver default does not follow it.

## 8. Docs

- [x] 8.1 `resources/ai/tools/devices_wifi.md` — the `resolver` and `queryPort` arguments, and why `resolver=system` is refused on Android.
- [ ] 8.2 No wiki manual entry: nothing here is user-visible. Discovery either finds the scale or does not, and the user-facing wording for that is unchanged.
