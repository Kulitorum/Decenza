## ADDED Requirements

### Requirement: The recognition window is armed before the socket is opened

`attemptTarget` SHALL start the recognition timer BEFORE calling `open()`, never after. Opening a WebSocket against an address the operating system rejects at the routing layer fails synchronously, inside the `open()` call, so an error handler that stops the recognition timer runs before a timer started after `open()` exists.

#### Scenario: Synchronous connect failure cancels the recognition window
- **WHEN** `open()` fails synchronously and the error is classified as a transient transport failure
- **THEN** the recognition timer is not running afterwards, and no recognition timeout fires for that attempt

#### Scenario: A transiently-failed attempt does not evict the cache 5 seconds later
- **WHEN** a cached-IP attempt fails transiently and no further attempt is made for longer than the recognition window
- **THEN** no cache eviction occurs at any point after the failure — the decision made by the error handler is final for that attempt

### Requirement: DE1 simulation does not gate scale connectivity

The DE1 simulator setting means "no DE1 machine is attached" and SHALL gate DE1 BLE operations only. It SHALL NOT block connecting to, switching to, or reconnecting a real scale. Real-scale connection paths SHALL instead be gated on the scale simulator setting, which is the switch that means a simulated scale owns the weight stream.

This matters most for the WiFi scale, which reaches the scale over the network and shares no radio with the DE1.

#### Scenario: DE1 simulator on, scale simulator off
- **WHEN** the DE1 simulator is enabled and the scale simulator is disabled, and a saved scale exists
- **THEN** scale scanning, scale switching, and the app-level scale reconnect loop all operate normally against the real scale

#### Scenario: Scale simulator on
- **WHEN** the scale simulator is enabled
- **THEN** real-scale connect attempts are refused and any connected physical scale is stood down, so the simulated and real scales do not both drive weight

#### Scenario: Enabling DE1 simulation does not drop a connected scale
- **WHEN** DE1 simulation mode is switched on while a real scale is connected
- **THEN** the scale stays connected; only DE1 BLE operations are disabled

## MODIFIED Requirements

### Requirement: WiFi connect tries cached IP first with hostname fallback

To make WiFi reconnect robust against unreliable mDNS resolution, the WiFi driver SHALL cache the resolved peer IP after each successful connection, attempt the cached IP on subsequent connects, and fall back to the hostname when the cached IP does not deliver a recognizable HDS frame within a short window.

A failed attempt SHALL be classified by what the failure proves about the cached IP:

- A **wrong-host failure** is one where a peer answered but is not an HDS — a refused connection (TCP RST, proving a host is up at that address), a completed TCP connection that fails the WebSocket handshake (HTTP 403/404, protocol error), or one that upgrades but delivers no recognizable HDS frame within the recognition window. This is evidence the cached IP now belongs to a different device, so the driver SHALL evict the cached IP and fall back to the hostname.
- A **transient transport failure** is one where nothing answered at all — host unreachable, network unreachable, or connect timeout before the WebSocket upgrade. This is NOT evidence about which device owns the IP, so the driver SHALL preserve the cached IP, SHALL NOT consume the hostname fallback, and SHALL end the attempt so the app-level reconnect loop owns the retry.

#### Scenario: First connect by hostname caches the peer IP
- **WHEN** the driver opens a WebSocket using the hostname (no cached IP available) and receives the first snapshot or status frame within the recognition window
- **THEN** the driver writes the WebSocket peer address into the IP cache, keyed by hostname, so subsequent connects can skip mDNS

#### Scenario: Subsequent connect tries the cached IP first
- **WHEN** a previously cached IP exists for the saved hostname and the driver is asked to connect
- **THEN** the driver opens the WebSocket against `ws://<cached-ip>/snapshot` first, without performing a hostname resolution

#### Scenario: Cached IP succeeds — no mDNS lookup performed
- **WHEN** the cached IP responds with a recognizable HDS frame (snapshot or `type:"status"`) within the 5 s recognition window
- **THEN** the driver treats the connection as established and does not attempt to resolve the hostname

#### Scenario: Cached IP fails recognition — fall back to hostname
- **WHEN** the cached IP attempt opens the WebSocket but no recognizable HDS frame arrives within 5 s (e.g., DHCP reassigned the IP to a different host running a WebSocket server)
- **THEN** the driver closes the cached-IP socket and retries via `ws://<hostname>/snapshot`; on success, the cache is overwritten with the new peer IP

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

### Requirement: Reconnect on transient drop is owned by the app-level reconnect loop

The WiFi driver SHALL NOT schedule reconnect attempts internally. On WebSocket `disconnected`, and on a connect attempt that fails transiently before ever reaching the connected state, the driver calls `setConnected(false)` and returns. The app-level reconnect loop in `main.cpp` (the `scaleReconnectTimer`, gated on `settings.scaleAddress()` being non-empty) owns retry orchestration via `BLEManager::tryDirectConnectToScale()` → `DecentScaleWifi::connectToHost()`.

After an attempt has ended with a transient transport failure, the next attempt SHALL re-resolve the hostname rather than re-dial the remembered address, so that an address which has changed is picked up. This is required because a transient classification retains the cached IP: when DHCP moves the scale the address it vacated usually goes dark, which itself classifies as transient, and without a re-resolve the driver would re-dial that dead address indefinitely with no other path to correction.

The justification is address freshness only. No claim is made that the mDNS exchange clears operating-system state for an unreachable peer — for a `.local` name the responder is the scale itself, so during an unreachability window the resolution typically fails too. Recovery in that case comes from the backoff delay.

If that resolution fails, the driver SHALL still dial the remembered address rather than dial nothing, so a device whose mDNS is unreliable attempts a connection on every cycle.

#### Scenario: WebSocket disconnects mid-stream
- **WHEN** the WiFi WebSocket emits `disconnected` after having been connected
- **THEN** the driver propagates the disconnect via `setConnected(false)` and does NOT schedule its own reconnect; the app-level `scaleReconnectTimer` schedules the retry

#### Scenario: Connect attempt fails transiently before connecting
- **WHEN** a connect attempt fails with a transient socket-layer error before the WebSocket upgrade completes
- **THEN** the driver ends the attempt without an internal retry, and the app-level `scaleReconnectTimer` schedules the next attempt on its existing backoff schedule

#### Scenario: Retry re-resolves the hostname
- **WHEN** the app-level reconnect loop retries a saved `.local` WiFi scale hostname after a transient failure
- **THEN** an mDNS resolution for that hostname is performed as part of the attempt, and the freshly resolved address is the one dialled

#### Scenario: Scale moved to a new address and the old one went dark
- **WHEN** DHCP has reassigned the scale to a different address, the address it vacated answers nothing, and the driver holds the vacated address in its cache
- **THEN** the failure classifies as transient and the cache is retained, and the following attempt re-resolves and dials the scale's new address rather than re-dialling the dead one indefinitely

#### Scenario: Resolution fails on the retry — dial the remembered address anyway
- **WHEN** the retry's hostname resolution fails (an unreliable or unavailable mDNS responder) and a cached IP is held for that hostname
- **THEN** the driver dials the cached IP rather than ending the attempt without opening a socket, so a connection is attempted on every cycle

#### Scenario: Reconnect also fails
- **WHEN** the app-level reconnect re-fires `connectToHost()` and that attempt also disconnects
- **THEN** the driver again propagates `setConnected(false)`; the app-level retry loop applies its exponential-backoff schedule (5 s / 30 s / 60 s) before the next attempt
