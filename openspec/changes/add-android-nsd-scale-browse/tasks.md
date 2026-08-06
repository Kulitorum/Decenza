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

## 3. Query source port

- [x] 3.0 Bind the query socket to 5353 with `SO_REUSEPORT` (the library already sets it), falling back to ephemeral only if the bind fails. mjansson then drops the QU bit on its own, so the query stops being "legacy" and the answer comes back multicast.
- [x] 3.0a Centralize both socket-open sites (`resolveHostname` and `browseServiceMjansson`) into one `openQuerySocket()` — they were already duplicated.
- [x] 3.0b Report the bound port in `ResolveStats`/`BrowseStats` and in every start/done log line.
- [x] 3.0c `queryPort` argument on `devices_wifi` so the A/B against the older on-device 5353 measurement can be run without a rebuild.

## 4. Diagnostics

- [x] 4.1 `MdnsResolver::HostnameResolver` selector so a desktop build can run Android's exact A-record path.
- [x] 4.2 Refuse `resolver=system` on Android in `devices_wifi` — it is sticky and would disable `.local` resolution for the session.
- [x] 4.3 Bump `McpSurfaceVersion`.

## 5. Corrections to the record

- [x] 5.1 Replace the "host-side resolver state" rationale in `attemptHostname()` and in the spec with what the two-scale control established.
- [x] 5.2 State the ARP mechanism as suspected, not settled, at every site that asserts it, and name the counter-example it does not explain.
- [x] 5.3 Keep the measured window (several hours) and the user-visible outage (five days) as separate numbers wherever either is cited.
- [x] 5.4 Fix the stale `resolveHostname() is only called on Android` comment in `mdnsresolver.cpp`.

## 6. Tests

- [x] 6.1 `tst_wifiscalediscovery`: resolver selector default, reports-what-ran, probe honours a pinned resolver.
- [x] 6.2 `tst_wifiscalediscovery`: `parseNsdLine()` field alignment, including the empty-hostname and start-failure cases.
- [x] 6.3 `tst_decentscalewifi`: a silent cached IP is kept, an answering one is evicted.

- [x] 6.4 `tst_wifiscalediscovery`: query-port default and readback.

## 7. Docs

- [x] 7.1 `resources/ai/tools/devices_wifi.md` — the `resolver` and `queryPort` arguments, and why `resolver=system` is refused on Android.
- [ ] 7.2 No wiki manual entry: nothing here is user-visible. Discovery either finds the scale or does not, and the user-facing wording for that is unchanged.
