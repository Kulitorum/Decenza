## ADDED Requirements

### Requirement: Android browses with the system DNS-SD daemon alongside the app's own

On Android the app SHALL run a second DNS-SD browse for `_decentscale._tcp` through `NsdManager`, started at the same time as the app's own browse and reporting into the same discovered-scales stream. It SHALL NOT be chained after the app's browse as a fallback: the case it exists for is one where the app's browse returns cleanly and finds nothing, which is indistinguishable from an empty LAN and therefore cannot be used as a trigger.

The two paths fail independently, which is the point. The app's browse queries from an ephemeral source port, so under RFC 6762 §6.7 the responder answers by unicast, and an openscale responder has been measured answering **per peer**: a tablet that has never opened a socket to a given scale received nothing from it for hours while a Mac on the same LAN resolved it in 272 ms, and a second scale on that LAN — never contacted from the tablet — stayed silent to the tablet while answering the Mac normally in the same window. `NsdManager` queries from port 5353 as the system daemon, so its answers return multicast to the group and it additionally observes unsolicited announcements that need no query at all.

It has NOT been shown to find a scale this device has never contacted. An earlier version of this requirement claimed exactly that as its justification; on-device, a never-contacted scale stayed invisible to NsdManager throughout. Every measurement of it so far was taken while the app's own socket was bound to 5353 and starving the daemon it depends on, so its value remains unproven rather than disproven. It is retained as an independent second path pending a fair test, and this requirement SHALL be revisited — including removal — once one exists.

Results from both paths SHALL be deduplicated by hostname, as they already are: the same scale answering both is the expected case, not an error.

A resolved instance whose SRV target this Android version does not expose SHALL be dropped, with a log line naming the device's API level, and SHALL NOT be reported under a synthesized name. `NsdServiceInfo.getHostname()` is API 36; the hostname is the identity key results are stored under and the key the saved `"wifi:<hostname>"` primary uses, so a guessed name would be persisted as a scale the app can never match again.

The NsdManager browse SHALL be keyed per browse instance, SHALL deliver results incrementally rather than in one batch at its deadline, and SHALL stop within one poll interval of the browse being cancelled. Concurrent `WifiScaleDiscovery` instances — the user scan and the reconnect ladder each own one — SHALL NOT cancel each other's NsdManager browse.

#### Scenario: A scale this device has never contacted is found
- **WHEN** a user-initiated scan runs on Android against a scale the device has never opened a socket to, and the app's own browse and A-record lookups receive no records at all
- **THEN** the NsdManager browse resolves the scale and its row appears in the discovered-scales list

#### Scenario: Both browses find the same scale
- **WHEN** both browse paths resolve the same scale within one scan
- **THEN** one row appears, keyed by hostname, not two

#### Scenario: A resolved instance with no exposable hostname is dropped, loudly
- **WHEN** the NsdManager browse resolves an instance on a device below API 36, where the SRV target cannot be read
- **THEN** no row is added, and the log records the instance name, its address, the device's API level, and that API 36 is required

#### Scenario: Discovery could not start
- **WHEN** `NsdManager` is unavailable or rejects `discoverServices`
- **THEN** the log distinguishes that from a browse that ran and found nothing

#### Scenario: The reconnect ladder's browse does not stop the user's
- **WHEN** a reconnect browse and a user-initiated scan are both running on Android
- **THEN** each has its own NsdManager browse and neither cancels the other

#### Scenario: Cancelling a scan stops the system browse promptly
- **WHEN** a browse is cancelled or the app is torn down while an NsdManager browse is in flight
- **THEN** that browse stops within one poll interval rather than running out its deadline

### Requirement: Multicast reception is licensed by the app, not by an unrelated feature

On Android the app SHALL hold a `WifiManager.MulticastLock` for the duration of every mDNS lookup and every mDNS browse it performs. The lock SHALL be reference-counted across all concurrent users and released when the last one finishes, and it SHALL NOT depend on any user-facing feature being enabled.

Android's Wi-Fi driver discards multicast frames not addressed to this device unless such a lock is held, and the failure is completely silent: the socket opens, the group join succeeds, queries go out, and nothing arrives. Until now the only lock in the app belonged to the shot server, taken in its `start()` and released in its `stop()` — and that server is **disabled by default**, so on a default install no lock was ever held while three separate comments in the source described one as held for the whole app lifetime.

This requirement stands on its own and is NOT contingent on the query source port. An earlier version of this text claimed the two were halves of one change; they are not. The lock is required for any multicast reception the app relies on, and it was demonstrably absent on a default install. Binding 5353 on Android was a separate idea, and a wrong one — see the query-port requirement below.

The lock SHALL be scoped to the operations that need it rather than taken once at startup, because it disables a hardware filter and therefore wakes the CPU for multicast traffic addressed to the whole LAN.

Failure to take the lock SHALL NOT prevent discovery from running. It SHALL be logged, once rather than per attempt, since discovery repeats on a reconnect ladder.

#### Scenario: Discovery works with every optional feature off
- **WHEN** an Android device runs a scan with the shot server, MQTT and every other network feature disabled
- **THEN** the multicast lock is held for the duration of the lookups and browses, and multicast answers are received

#### Scenario: The lock is not held when nothing is discovering
- **WHEN** no lookup or browse is in flight
- **THEN** no multicast lock is held, so the Wi-Fi hardware filter is back on

#### Scenario: Concurrent discovery does not release the lock early
- **WHEN** a user scan and a reconnect browse overlap and one of them finishes
- **THEN** the lock remains held until the last one finishes

#### Scenario: A device that cannot take the lock still discovers
- **WHEN** no Android context or WifiManager is available
- **THEN** the lookup or browse still runs, and the inability to take the lock is logged once rather than on every attempt

### Requirement: mDNS queries are sent from port 5353 except on Android

The app's own mDNS queries SHALL be sent from a socket bound to port 5353, falling back to an ephemeral source port when that bind fails — **except on Android, where an ephemeral port SHALL be used unconditionally.**

The Android exclusion is measured, not assumed. Android's system mDNS daemon already owns 5353; `SO_REUSEPORT` lets the app's bind succeed and then inbound packets are delivered to the daemon rather than to the app. On-device, with a multicast lock held, this produced `records=0` for **every** host — including the MQTT broker's `.local` name, which resolves normally from an ephemeral port and which an unrelated feature depends on. In the same browse, NsdManager (the daemon, on 5353) resolved a scale in 41 ms while the app's own 5353 socket saw 2 records and failed.

A prior version of this codebase recorded that measurement in a comment. It was deleted on the theory that the result was an artifact of the app not holding a `WifiManager.MulticastLock`; that theory was wrong — the lock was subsequently held and 5353 remained blind — and acting on it broke every `.local` lookup on Android. The platform split SHALL be asserted by a test rather than left to a comment.

This is not a preference about sockets; it changes what the responder is obliged to do. A query from an ephemeral source port is a "legacy" query under RFC 6762 §6.7, which a responder must answer by **unicast** — so the answer depends on the responder being able to address this host directly, and an openscale scale has been measured declining to answer a peer it has no fresh path to for hours while answering another host on the same LAN in 272 ms. From 5353 the query is ordinary, the answer returns multicast to the group, and nothing per-peer is in the path. This is the openscale maintainers' own recommended client-side workaround, and it is available now, without a firmware change.

The port a socket actually bound to SHALL be reported with every lookup and every browse, in the log and in the discovery statistics. Without it, `records=0` cannot be interpreted: it is the difference between "nothing is there" and "the responder will not answer this particular host".

The policy SHALL be forceable to either extreme from the diagnostic surface. An earlier on-device test recorded a 5353 socket receiving zero records for all hosts on Android, which is why the ephemeral bind shipped; that result has never been reproduced against the current build, and it predates the app holding a process-wide `WifiManager.MulticastLock`, which a 5353 socket depends on and an ephemeral one does not. Neither measurement SHALL be treated as settled, and the disagreement SHALL be resolvable by forcing each and reading the bound port back, rather than by argument.

#### Scenario: Android never binds 5353 by default
- **WHEN** a lookup or browse runs on Android with no query port explicitly selected
- **THEN** the socket binds an ephemeral port, so the system daemon keeps receiving its own traffic

#### Scenario: The bound port is in the log either way
- **WHEN** any hostname lookup or mjansson browse runs
- **THEN** its log line reports the local port the socket bound to, so a zero-record result can be attributed to a legacy query or ruled out as one

#### Scenario: 5353 is unavailable
- **WHEN** the bind to 5353 fails because another process holds it without `SO_REUSEPORT`
- **THEN** the query still goes out from an ephemeral port rather than the lookup failing, and the log reports that port

#### Scenario: The A/B can be run without a rebuild
- **WHEN** an operator forces the ephemeral policy and then the 5353 policy against the same LAN
- **THEN** each run reports which port it used, so the two results can be compared as different treatments rather than assumed to be the same one

## MODIFIED Requirements

### Requirement: WiFi connect tries cached IP first with hostname fallback

To make WiFi reconnect robust against unreliable mDNS resolution, the WiFi driver SHALL cache the resolved peer IP after each successful connection, attempt the cached IP on subsequent connects, and fall back to the hostname when the cached IP does not deliver a recognizable HDS frame within a short window.

A failed attempt SHALL be classified by what the failure proves about the cached IP:

- A **wrong-host failure** is one where a peer answered but is not an HDS — a refused connection (TCP RST, proving a host is up at that address), a completed TCP connection that fails the WebSocket handshake (HTTP 403/404, protocol error), or one that **completes the WebSocket upgrade** but delivers no recognizable HDS frame within the recognition window. This is evidence the cached IP now belongs to a different device, so the driver SHALL evict the cached IP and fall back to the hostname.
- A **transient transport failure** is one where nothing answered at all — host unreachable, network unreachable, or connect timeout before the WebSocket upgrade. This is NOT evidence about which device owns the IP, so the driver SHALL preserve the cached IP, SHALL NOT consume the hostname fallback, and SHALL end the attempt so the app-level reconnect loop owns the retry.

A recognition timeout where the WebSocket upgrade never completed SHALL be treated as the second case, not the first. Silence says nothing about which device owns the address; it is what a scale that is rebooting, asleep or momentarily unreachable looks like. Evicting on silence is also actively harmful, because the cached IP is not merely an optimisation: dialling it is what has been measured to put this device back on the responder's answering side, so discarding it leaves both remaining paths — hostname resolve and service browse — asking a scale that has no fresh reason to answer this peer. The driver SHALL still fall back to the hostname within that connect cycle; only the eviction is withheld.

#### Scenario: First connect by hostname caches the peer IP
- **WHEN** the driver opens a WebSocket using the hostname (no cached IP available) and receives the first snapshot or status frame within the recognition window
- **THEN** the driver writes the WebSocket peer address into the IP cache, keyed by hostname, so subsequent connects can skip mDNS

#### Scenario: Subsequent connect tries the cached IP first
- **WHEN** a previously cached IP exists for the saved hostname and the driver is asked to connect
- **THEN** the driver opens the WebSocket against `ws://<cached-ip>/snapshot` first, without performing a hostname resolution

#### Scenario: Cached IP succeeds — no mDNS lookup performed
- **WHEN** the cached IP responds with a recognizable HDS frame (snapshot or `type:"status"`) within the 5 s recognition window
- **THEN** the driver treats the connection as established and does not attempt to resolve the hostname

#### Scenario: Cached IP answers but is not an HDS — evict and fall back
- **WHEN** the cached IP attempt completes the WebSocket upgrade but no recognizable HDS frame arrives within 5 s (e.g., DHCP reassigned the IP to a different host running a WebSocket server)
- **THEN** the driver evicts the cached IP, closes the socket and retries via `ws://<hostname>/snapshot`; on success, the cache is overwritten with the new peer IP

#### Scenario: Cached IP goes silent — keep the cache, still fall back
- **WHEN** the cached IP attempt reaches the recognition timeout without ever completing the WebSocket upgrade
- **THEN** the driver retains the cached IP and retries via the hostname within the same connect cycle, because silence is not evidence the address is wrong and re-dialling that address is what lets the scale answer this device at all

#### Scenario: Cached IP answers but rejects the WebSocket handshake — evict and fall back
- **WHEN** the cached IP completes a TCP connection but the WebSocket upgrade fails at the handshake or protocol layer (for example a router answering `ws://<ip>/snapshot` with HTTP 403)
- **THEN** the driver evicts the cached IP and retries via the hostname, because a non-HDS peer demonstrably owns that address

#### Scenario: Cached IP refuses the connection — evict and fall back
- **WHEN** the cached IP attempt is refused at the TCP layer (RST), proving a host is up at that address but is not serving the scale endpoint
- **THEN** the driver evicts the cached IP and retries via the hostname within the same connect cycle, because a refusal is evidence the address has been reassigned

#### Scenario: Cached IP is unreachable — preserve cache, defer to the reconnect loop
- **WHEN** the cached IP attempt fails before the WebSocket upgrade with a socket-layer error indicating nothing answered (host unreachable, network unreachable, or connect timeout)
- **THEN** the driver retains the cached IP, does NOT mark the hostname fallback as consumed, does NOT dial the hostname within this connect cycle, and ends the attempt so the app-level `scaleReconnectTimer` schedules the next try

#### Scenario: An unreachable scale recovers without user action
- **WHEN** a saved WiFi scale is briefly unreachable at the network layer and a connect attempt fails transiently
- **THEN** a later app-level reconnect attempt succeeds once the scale is reachable again, with no manual rescan required by the user

#### Scenario: Both cached IP and hostname fail
- **WHEN** the cached IP attempt is classified as a wrong-host failure, the driver falls back to the hostname, and the hostname attempt also fails to deliver a recognizable HDS frame
- **THEN** the driver emits `errorOccurred("WiFi scale did not respond as HDS")` and stops; no further retries on this connect cycle. The user can rescan to retry.

### Requirement: Saved WiFi scale reconnect resolves by service browse

When a WiFi scale is the saved primary and a reconnect attempt fails to establish a connection, the app SHALL run a DNS-SD service browse for `_decentscale._tcp.local` as part of the reconnect ladder, and SHALL auto-connect when a resolved instance matches the saved primary address.

This exists because the direct A-record query for the saved hostname has been observed to enter a state where it returns nothing at all, for hours, while the scale is plainly reachable. On Android: 82 consecutive reconnect attempts over 7.5 h each received **zero** mDNS records, while the scale was awake, mains-powered and serving WebSocket traffic on its IP; a DNS-SD browse resolved the same hostname in 362 ms, and raising the A-query deadline from 2 s to 5 s did not change the outcome.

That state is a **host-side condition**, as this requirement has said from the start. A draft of this change briefly replaced that with a per-peer responder account, on the strength of one window in which a tablet resolved the scale it had just dialled while a second scale stayed silent to it. **A controlled repeat on 2026-08-06 refutes the per-peer account and restores this one.** Holding `hdstest` back as an untouched control, a single TCP connection from the tablet to `hds` alone moved `hds.local` from 0/10 to 10/10 *and* `hdstest.local` from 0/10 to 9/10. A per-peer responder cannot restore a peer that was never contacted; the tablet's own receive path was dormant, and outbound traffic wakes it for every peer at once. The same tablet was the worst of 21 mDNS responders measured on that LAN (1/12 answered, 1253 ms median), so it under-receives in both directions.

The per-peer claim failed on sample size: where an ordinary host on that LAN answers 41-75% of a repeated query, a handful of trials per scale yields "worked for A, failed for B" by chance. The ARP/`ETHARP_TRUST_IP_MAC` mechanism built on it is refuted with it, as is the earlier scale-side power-save account. `docs/WIFI_SCALE_MDNS.md` carries the measurements and all three retractions. Nothing in this specification SHALL depend on any of them.

The browse is justified independently of the mechanism: when the direct query returns nothing there is no signal from inside the app to distinguish "the responder will not answer us" from "the scale is genuinely absent", and the browse succeeds in the first case.

The browse SHALL run on every platform. The same SYMPTOM has been seen off Android — macOS once logged `QHostInfo resolution failed for hdstest.local: Host not found` on the reconnect path while a browse resolved that host in 1.32 s — but one occurrence through a different resolver is not evidence of a shared root cause, and that macOS state did not recur. Running everywhere is a cheap hedge against a failure whose cause is not established, not a claim that every platform has the same defect. `MdnsResolver::browseService()` selects the system Bonjour backend on Apple platforms and the mjansson backend elsewhere.

The reconnect browse SHALL be isolated from user-initiated scanning:

- It SHALL NOT cancel, supersede or shorten a browse belonging to a user-initiated scan.
- It SHALL NOT set the scanning indicator, and SHALL NOT make the "Scan for Devices" control read "Scanning…".
- It SHALL NOT clear the discovered-devices list that a user scan populated.

The reconnect browse SHALL use a deadline shorter than the user-scan browse, because it repeats on the reconnect ladder rather than running once per user action.

The existing anti-substitution guarantee is unchanged: a browsed instance is auto-connected only when its address matches the saved primary.

A connect to the saved WiFi primary SHALL disarm the reconnect browse, whatever route produced that connect. In particular a connect the BROWSE itself produced SHALL disarm it, even though the WiFi→BLE fallback scan the browse was racing may still be running at that moment. Deciding this from whether a fallback was in flight, rather than from what connected, leaves the browse armed for the remainder of the session after its own successful recovery — which is the same condition "Browse is not run when the direct attempt succeeds" already forbids, reached by a different route.

While the saved WiFi primary is the connected scale, a browse or probe hit matching it SHALL be a no-op: no switch-back request, and no log line asserting that a backup scale is connected. There is no backup in that state, and the switch-back is declined one layer up regardless; the log line is the part a user sees, and it reads as the app dropping their scale.

An address obtained from a browse SHALL NOT outlive the request it was obtained for. A switch-back request may be declined (mid-shot, or already on the primary) and a declined request consumes nothing, so any later reachability check SHALL discard a previously browsed address rather than let it outrank the address that check just verified.

#### Scenario: Stale cached IP is recovered without user action
- **WHEN** a WiFi scale is the saved primary, its cached IP no longer reaches it, and a direct hostname lookup returns no records
- **THEN** the reconnect ladder runs a service browse, the scale's current address is resolved from the browse, and the app connects to it without the user opening the Connections page

#### Scenario: Two scales on one LAN answer one device differently
- **WHEN** a device has recently exchanged traffic with one scale and never with a second scale on the same LAN
- **THEN** a per-peer difference in mDNS answers between the two is an expected observation, not evidence of a fault in the device's resolver

#### Scenario: Reconnect browse does not disturb a user scan
- **WHEN** the user has a scan in progress and a reconnect tick fires
- **THEN** the user's browse continues to its own deadline, its discovered rows are not cleared, and the scanning indicator reflects only the user's scan

#### Scenario: Reconnect browse is invisible in the UI
- **WHEN** a reconnect browse is running and no user scan is in progress
- **THEN** the "Scan for Devices" control remains idle and enabled, and no scanning indicator is shown

#### Scenario: A different scale on the LAN is not substituted
- **WHEN** a reconnect browse resolves a scale whose address does not match the saved primary
- **THEN** the app does not auto-connect to it

#### Scenario: No browse when no WiFi scale is saved
- **WHEN** the saved primary scale address does not begin with `"wifi:"`
- **THEN** no reconnect browse is started, and no WiFi discovery traffic is generated outside a user-initiated scan

#### Scenario: Browse is not run when the direct attempt succeeds
- **WHEN** a reconnect attempt connects successfully via the cached IP
- **THEN** no reconnect browse is started for that attempt

#### Scenario: The browse's own success disarms the browse
- **WHEN** a direct attempt times out, the reconnect browse and the WiFi→BLE fallback scan both start, and the browse resolves the primary and reconnects it while that fallback scan is still running
- **THEN** the reconnect browse is disarmed by that connect, and no further browse runs while the primary stays connected

#### Scenario: Re-finding the primary while it is connected is silent
- **WHEN** the saved WiFi primary is the connected scale and a browse or A-record hit matches it — including on a user-initiated scan
- **THEN** no switch-back is requested and nothing is logged claiming a backup scale is connected

#### Scenario: A declined switch-back leaves no stale address behind
- **WHEN** a browse-driven switch-back request is declined, and later a reachability check verifies the primary at its cached address
- **THEN** the switch-back that follows dials the address that check verified, not the earlier browsed one

### Requirement: The development platform runs the shipped platforms' discovery path

On macOS the default browse backend SHALL be the mjansson implementation, not Bonjour, while BOTH backends remain compiled into the binary and selectable at runtime.

This inverts "ship what the platform prefers", deliberately. Bonjour is measurably better on macOS — 66-113 ms to a first row against mjansson's 160-270 ms, because the system daemon is always listening and its cache is warm. But macOS is the development platform rather than a shipped one: about two installs, both developers, against hundreds of Android users. Defaulting to Bonjour meant the browse path three shipped platforms use was never exercised by the only machine anyone develops on, which is how a multi-hour Android discovery outage reached users and survived review.

Bonjour SHALL remain selectable on macOS, and its compile guards SHALL NOT be narrowed to exclude either backend. The default is the only thing that moves.

Bonjour keeps its field coverage from iOS, which ships it. What this default gives up is EARLY warning: an iOS release build is compiled only by CI, so macOS was the one place a Bonjour regression would surface before users saw it. A Bonjour browse SHALL be run on macOS before an iOS release.

The hostname-resolver default SHALL NOT follow the browse backend. The two are not symmetrical: the mjansson browse is what three shipped platforms use, while the mjansson resolver is Android-only and QHostInfo is what iOS ships. Flipping both would leave neither iOS path with development coverage.

#### Scenario: A developer's ordinary scan exercises the Android browse
- **WHEN** a scan runs on macOS with no backend explicitly selected
- **THEN** the mjansson browse runs, and the log names it as the backend that ran

#### Scenario: Bonjour is still reachable on macOS
- **WHEN** a browse explicitly selects the Bonjour backend on macOS
- **THEN** Bonjour runs and is reported as the backend that ran

#### Scenario: iOS is unaffected
- **WHEN** a browse runs on iOS with no backend selected
- **THEN** Bonjour runs, because mjansson is not compiled there at all

#### Scenario: The resolver default is independent
- **WHEN** a hostname lookup runs on macOS with no resolver selected
- **THEN** the system resolver runs, not mjansson, even though the browse defaults to mjansson
