# wifi-scale-discovery Specification

## Purpose
Covers on-demand mDNS discovery of the Half Decent Scale (HDS) over WiFi alongside the existing BLE scan, the WebSocket-based `DecentScaleWifi` driver that implements the `ScaleDevice` interface (weight, tare, timer, LED, status/battery/charging, button and power events), the `"wifi:<hostname>"` saved-address scheme that lets the same physical scale be selected on either transport, and the cached-IP-with-hostname-fallback and app-owned reconnect behavior that keep the WiFi connection resilient.
## Requirements
### Requirement: On-demand WiFi scale discovery

The app SHALL probe for the Half Decent Scale (HDS) on the local network only when the user initiates a device scan from the Connections page. No background, idle, or app-start probing of WiFi scales SHALL occur unless a WiFi scale is saved as the primary scale.

#### Scenario: User taps Scan for devices
- **WHEN** the user taps "Scan for devices" on the Connections page
- **THEN** the app performs an mDNS lookup of `hds.local` in parallel with the BLE scan, with a timeout of approximately 2 seconds

#### Scenario: App starts with no saved WiFi scale
- **WHEN** the app starts and the saved primary scale address does not begin with `"wifi:"`
- **THEN** the app does not perform any WiFi scale discovery

#### Scenario: App starts with a saved WiFi scale
- **WHEN** the app starts and the saved primary scale address begins with `"wifi:"`
- **THEN** the app performs a single mDNS lookup of the saved hostname with a timeout of approximately 5 seconds, and on success opens a WebSocket to the scale

#### Scenario: Saved WiFi scale unreachable on app start
- **WHEN** the app starts with a saved WiFi scale address but the mDNS lookup fails or the WebSocket fails to connect
- **THEN** the app does not auto-connect to any other scale (mirroring current BLE behavior for an unreachable saved BLE scale per #440); the user must manually rescan to recover

#### Scenario: mDNS lookup fails
- **WHEN** the mDNS lookup for `hds.local` times out or returns `HostNotFound`
- **THEN** no WiFi scale entry appears in the discovered-scales list and no error toast is shown (an offline WiFi scale is not an error)

### Requirement: WiFi scale appears in the unified scale list

The discovered-scales list SHALL contain WiFi scale entries alongside BLE scale entries, with a display-name suffix and a transport tag.

#### Scenario: WiFi scale discovered
- **WHEN** mDNS resolves `hds.local` during a user-initiated scan
- **THEN** the discovered-scales list contains an entry with `name = "Decent Scale (WiFi)"`, `address = "wifi:hds.local"`, `type = "decent-wifi"`, and `transport = "wifi"`

#### Scenario: BLE scale entries are unchanged
- **WHEN** the BLE scan discovers a Decent Scale at a MAC/UUID address
- **THEN** the discovered-scales entry retains its existing shape, with `transport = "ble"` added; the `name` field has no suffix

#### Scenario: Same physical scale on both transports
- **WHEN** the user runs a scan and both BLE and WiFi paths resolve the same physical HDS unit
- **THEN** the discovered-scales list contains two distinct rows — one BLE, one WiFi — that can each be selected independently

### Requirement: WiFi scale driver delivers weight and tare

When the user selects a WiFi scale entry, the app SHALL open a WebSocket to the scale, parse incoming JSON weight frames, and forward weight samples and tare commands through the existing `ScaleDevice` interface.

#### Scenario: Weight frame received
- **WHEN** the WiFi scale driver receives a text frame containing valid JSON `{"grams": <number>, "ms": <number>}`
- **THEN** the driver calls `setWeight(grams)`, which causes the existing weight pipeline to receive the sample with no transport-specific branching

#### Scenario: Malformed frame received
- **WHEN** the WiFi scale driver receives a text frame that is not valid JSON or lacks a numeric `grams` field
- **THEN** the driver silently ignores the frame and does not call `setWeight`

#### Scenario: Tare requested
- **WHEN** the app calls `tare()` on the WiFi scale driver
- **THEN** the driver sends the text frame `"tare"` over the WebSocket

#### Scenario: Timer commands match BLE behavior
- **WHEN** the app calls `startTimer()`, `stopTimer()`, or `resetTimer()` on the WiFi scale driver
- **THEN** the driver sends the text frame `"timer start"`, `"timer stop"`, or `"timer reset"` respectively over the WebSocket

#### Scenario: Display commands match BLE behavior
- **WHEN** the app calls `disableLcd()` on the WiFi scale driver
- **THEN** the driver sends the text frame `"display off"` over the WebSocket

- **WHEN** the app calls `wake()` on the WiFi scale driver
- **THEN** the driver sends `"soft_sleep off"` followed by `"display on"`, in that order

#### Scenario: Sleep command matches BLE intent (reversible scale-side sleep)
- **WHEN** the app calls `sleep()` on the WiFi scale driver
- **THEN** the driver sends `"soft_sleep on"` and emits `sleepCompleted` once after `sendTextMessage` returns; the driver does NOT send `"power off"` (which would be irreversible)

#### Scenario: LED control matches BLE behavior with clamping
- **WHEN** the app calls `setLed(r, g, b)` on the WiFi scale driver with each channel in 0-255
- **THEN** the driver sends `"led <r> <g> <b>"` with the integer values

- **WHEN** the app calls `setLed(r, g, b)` with channels outside 0-255
- **THEN** each channel is clamped to 0-255 before formatting the text command

#### Scenario: sendKeepAlive is a no-op
- **WHEN** the base-class 30 s keepalive timer fires on the WiFi scale driver
- **THEN** the driver does nothing (the 5 s status heartbeat from the scale plus TCP keepalive provide liveness)

### Requirement: Transport switching for the same physical scale

The user SHALL be able to switch between BLE and WiFi transports for the same physical scale by selecting the alternative entry in the discovered-scales list.

#### Scenario: User switches from BLE to WiFi
- **WHEN** the BLE Decent Scale is currently connected, and the user selects the "Decent Scale (WiFi)" row
- **THEN** the BLE connection is disconnected, the WiFi WebSocket opens, and weight samples begin arriving from the WiFi transport

#### Scenario: User switches from WiFi to BLE
- **WHEN** the WiFi Decent Scale is currently connected, and the user selects the BLE Decent Scale row
- **THEN** the WiFi WebSocket is closed (so the scale becomes available to other clients), the BLE connection opens, and weight samples begin arriving from the BLE transport

### Requirement: Saved-scale address scheme distinguishes transports

The saved primary scale address SHALL encode the transport so that BLE and WiFi entries for the same physical scale do not collide on the saved-address comparison.

#### Scenario: WiFi scale is saved
- **WHEN** the user connects to a WiFi scale entry
- **THEN** the saved primary scale address is `"wifi:<hostname>"` (e.g., `"wifi:hds.local"`)

#### Scenario: BLE scale is saved
- **WHEN** the user connects to a BLE scale entry
- **THEN** the saved primary scale address is the BLE MAC or UUID, unchanged from current behavior (no `"ble:"` prefix is added — bare addresses denote BLE for backwards compatibility)

#### Scenario: Primary-only auto-reconnect honors transport
- **WHEN** the saved primary scale address is `"wifi:hds.local"` and a BLE scan discovers a Decent Scale at a MAC address
- **THEN** the BLE entry does not auto-connect (the saved address does not match)

### Requirement: Concurrent-client-cap server-busy condition surfaces clearly

When the scale's firmware refuses a WebSocket upgrade because the concurrent-client cap is exceeded (HTTP 503 per the firmware behavior in `webserver.h` — current cap is 5 clients), the app SHALL surface a specific, user-readable error and NOT loop retry attempts.

#### Scenario: Scale is at the concurrent-client cap
- **WHEN** the WiFi scale driver attempts to open the WebSocket and the server returns 503
- **THEN** the driver emits the error message "Another client is connected to the scale" and stops attempting to connect

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

### Requirement: Weight pipeline auto-adapts to WiFi cadence

The app SHALL NOT introduce transport-specific cadence thresholds, capability handshakes, or version negotiation. The existing per-connection inter-arrival EMA in `WeightProcessor` provides cadence adaptation for any rate within the existing stall thresholds (`kScaleStaleMs = 2000`, `kScaleStallConfirmMs = 6000`).

#### Scenario: Firmware emits at 2 Hz
- **WHEN** the WiFi scale firmware emits weight frames every 500 ms
- **THEN** the weight pipeline absorbs the cadence without stalling and computes flow-rate / SAW signals using the measured interval

#### Scenario: Firmware emits at 10 Hz
- **WHEN** the WiFi scale firmware emits weight frames every 100 ms
- **THEN** the weight pipeline absorbs the cadence without code changes, with the inter-arrival EMA tracking the new rate within ~3-5 samples

#### Scenario: Firmware cadence changes mid-session
- **WHEN** the firmware is updated to a different cadence while a session is in progress (e.g., via OTA — not in scope here but the pipeline must tolerate it)
- **THEN** the inter-arrival EMA reconverges to the new rate without requiring a reconnect or any user action

### Requirement: Connect-time setup sends `rate 10k` and `events on`

When the WebSocket connection establishes, the driver SHALL request the highest supported cadence and opt in to event frames. No retries are needed; if the firmware does not understand a command it ignores it.

#### Scenario: Driver connects to WiFi scale
- **WHEN** the WebSocket `connected` signal fires
- **THEN** the driver sends `"rate 10k"` and then `"events on"` as two text frames, in that order

#### Scenario: Driver seeds initial status
- **WHEN** the driver has sent `"rate 10k"` and `"events on"` on connect
- **THEN** the driver also sends `"status"` to seed an immediate status frame so battery, charging, and firmware_version populate without waiting for the 5 s periodic heartbeat

### Requirement: Status frame readbacks

The driver SHALL parse incoming `type: "status"` frames and surface their readback fields through the existing `ScaleDevice` interface plus the new `charging` property.

#### Scenario: Battery percent updates
- **WHEN** a status frame arrives with `battery_percent: <0..100>`
- **THEN** the driver calls `setBatteryLevel(<value>)`

#### Scenario: Charging state updates
- **WHEN** a status frame arrives with `charging: true` (or `false`)
- **THEN** the driver calls `setCharging(true)` (or `false`); the `chargingChanged` signal fires only when the value flips

#### Scenario: Firmware version logged on first capture
- **WHEN** a status frame arrives with `firmware_version: "<string>"` for the first time on a given connection
- **THEN** the driver logs the version once via `logMessage`; subsequent status frames with the same value do NOT re-log

#### Scenario: Firmware version change mid-connect
- **WHEN** a status frame arrives with a `firmware_version` value different from one previously captured on the same connection
- **THEN** the driver warn-logs the transition (matches BLE driver behavior)

#### Scenario: Firmware version cleared on disconnect
- **WHEN** the WebSocket disconnects
- **THEN** the captured firmware_version is cleared so the next connect re-logs it fresh

#### Scenario: Unknown or renamed status fields are tolerated
- **WHEN** a status frame contains fields the driver does not consume (e.g., `timer_seconds`, `timer_ms`, `led`, `display_on`, `low_power`, `soft_sleep`, `events_enabled`, `rate_hz`, `interval_ms`, or future additive fields)
- **THEN** the driver ignores them without error; missing fields the driver might expect (e.g., a status frame from a transitional firmware that lacks `firmware_version`) are also tolerated — the driver does not crash, log loudly, or skip the otherwise-valid frame

### Requirement: Charging state is a first-class ScaleDevice property

The `ScaleDevice` base class SHALL expose `charging` as a `Q_PROPERTY` independent of battery level so the WiFi driver's explicit boolean is not downgraded into the lossy "battery == 100" overload that BLE has historically used.

#### Scenario: WiFi reports charging explicitly
- **WHEN** the WiFi scale driver receives a status frame with `battery_percent: 82, charging: true`
- **THEN** `setBatteryLevel(82)` and `setCharging(true)` are called as independent updates; consumers of the `charging` property see `true` while `batteryLevel` reads `82`

#### Scenario: BLE driver also surfaces charging correctly
- **WHEN** the BLE Decent Scale driver receives an LED-response packet with battery byte `0xFF`
- **THEN** the driver calls `setCharging(true)` AND `setBatteryLevel(100)` — both, not just the battery level — so the new `charging` property correctly reflects charging state on BLE-connected scales

### Requirement: Button events emit `buttonPressed` with a non-colliding encoding

The WiFi driver SHALL emit `buttonPressed(int)` for scale-side button events, using an encoding that cannot collide with the 0..255 range BLE uses for its single-byte button id.

#### Scenario: Circle short-press over WiFi
- **WHEN** a button frame arrives with `button_number: 1, press_code: 1` after `events on`
- **THEN** the driver emits `buttonPressed(0x1101)`

#### Scenario: Square long-press over WiFi
- **WHEN** a button frame arrives with `button_number: 2, press_code: 2` after `events on`
- **THEN** the driver emits `buttonPressed(0x1202)`

#### Scenario: BLE button values are not affected
- **WHEN** the BLE driver receives a button command (`0xAA`) with byte `[2]` in the 0..255 range
- **THEN** the BLE driver continues to emit `buttonPressed(<raw byte>)` with values in 0..255 — no encoding change on the BLE side

### Requirement: Power events are surfaced and suppress reconnect

The WiFi driver SHALL handle `type: "power"` events as expected disconnects, log the human-readable reason, and SHALL NOT trigger the 3 s reconnect attempt that applies to unexpected disconnects.

#### Scenario: Scale powers off due to low battery
- **WHEN** a power event arrives with `event: "power_off", reason: "low_battery", reason_code: 3` shortly followed by the WebSocket disconnecting
- **THEN** the driver logs the reason ("Scale shut down: low battery"), does NOT schedule a reconnect, and the subsequent `disconnected()` is treated as expected

#### Scenario: Unexpected disconnect (no preceding power event)
- **WHEN** the WebSocket disconnects without a preceding power event
- **THEN** the driver schedules exactly one reconnect attempt approximately 3 seconds later

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

### Requirement: Rate command behavior under weak WiFi is opaque to the driver

The driver SHALL request `rate 10k` on connect and SHALL NOT attempt to downgrade the requested rate if frames arrive less frequently than expected. The firmware drops frames it cannot send rather than queueing them, so the weight pipeline's existing inter-arrival EMA absorbs any variance.

#### Scenario: WiFi link is weak
- **WHEN** the firmware skips a write tick because the socket is not writable
- **THEN** the driver receives a frame later than the requested interval; the weight pipeline's EMA tracks the actual cadence and does not trigger spurious stalls so long as gaps stay under `kScaleStaleMs` (2 s)


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
