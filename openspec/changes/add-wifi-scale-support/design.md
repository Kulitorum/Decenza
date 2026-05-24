# Design: WiFi support for the Half Decent Scale

## Context

Decenza's scale subsystem is conceptually transport-agnostic at the `ScaleDevice` boundary (see [scaledevice.h](src/ble/scaledevice.h)) but is BLE-shaped throughout in practice: the base class owns `QLowEnergyController*`/`QLowEnergyService*` members, `BLEManager` stores discovered scales as `QList<QPair<QBluetoothDeviceInfo, QString>>`, and `scanForDevices()` is bound to `QBluetoothDeviceDiscoveryAgent`. There is one existing non-BLE `ScaleDevice` subclass — `FlowScale`, which derives weight from DE1 flow telemetry as a fallback when no physical scale is connected — so the base class already tolerates "no BLE underneath."

The HDS firmware (`Kulitorum/openscale`) exposes:

- mDNS hostname `hds.local` once joined to the network.
- WebSocket at `/snapshot` pushing JSON `{"grams": <float>, "ms": <millis>}`. Current firmware throttles to 2 Hz (`hds.ino:1411` — `current - lastUpdate > 500`); the same code path can do 10+ Hz by lowering the threshold.
- Single text command `"tare"`. No timer, LED, sleep, battery, button-event, or firmware-version surface over WS.
- Up to 5 concurrent WebSocket clients (server returns HTTP 503 only past that cap). Earlier firmware revisions enforced a 1-client limit; the cap was raised so Decenza, a browser tab, and other consumers can run simultaneously.
- AP-fallback for credential provisioning (SSID `DecentScale` / pass `12345678`, web form at `192.168.1.1`). Out of scope for this change.

## Goals / Non-Goals

- **Goals**
  - User-initiated "Scan for devices" lists the HDS as a WiFi entry when reachable on the LAN.
  - Connecting to the WiFi entry delivers weight + tare with the same `ScaleDevice` API the rest of the app already uses, so MachineState/WeightProcessor/SAW/etc. work without branching.
  - User can switch back and forth between BLE and WiFi rows for the same physical scale at will.
  - No automatic WiFi work on app start when no WiFi scale is saved (zero idle cost).
- **Non-Goals**
  - Pluggable polymorphic transport on the BLE scale base class. Not refactoring `ScaleBleTransport` into a generic transport now — sibling driver is sufficient.
  - Credential provisioning UI inside Decenza.
  - Multi-WiFi-scale support. Only HDS today.
  - Anything that requires the scale to declare its cadence or capabilities.

## Decisions

### 1. Sibling driver, not polymorphic transport

`DecentScaleWifi` is a new `ScaleDevice` subclass next to `DecentScale`, sharing nothing in code with it. Rationale:

- The two protocols share **zero** wire bytes. BLE: XOR-checksummed 7-byte binary, heartbeat, watchdog, LED commands. WiFi: JSON text frames, one text command. Polymorphism over `ScaleBleTransport` would buy nothing because there's nothing to share.
- The BLE driver's behavior (`m_watchdogTimer`, checksum disable thresholds for original Decent Scale, LED responses carrying battery/firmware) is meaningless on WiFi.
- A polymorphic-transport refactor would touch every scale driver. We'd be paying that refactor cost for one feature.
- `FlowScale` already establishes the pattern: a `ScaleDevice` subclass with no BLE underneath and a transport-specific data source.

The two drivers report the same `type() == "decent"` so any code branching on scale type (e.g., scale-specific UI hints, telemetry tags) treats them as the same product, which is true — they're the same physical device on different transports.

### 2. Discovery lives in `BLEManager`, not a parallel manager

The user's mental model is "one list of scales, pick one." Splitting WiFi discovery into a parallel `WifiScaleManager` would force QML to dedupe and merge two property streams, and would push the connection-routing decision up into QML. Keeping a single `discoveredScales` property with a `transport` field on each entry preserves the single-list mental model and keeps routing in C++.

The new `WifiScaleDiscovery` class is just the mDNS-probe helper — `BLEManager` owns it, calls it from `scanForDevices()`, and folds its emission into the same `m_scales` list that BLE discoveries populate. The discovery class itself has no opinion about the discovered-scales model.

### 3. On-demand discovery, no idle work

`WifiScaleDiscovery` performs an mDNS lookup **only** when `BLEManager::scanForDevices()` fires (the user tapped "Scan for devices"). It does not:

- Probe at app startup.
- Watch the network for the scale appearing.
- Retry in the background.

This matches the explicit user requirement and means an offline scale costs nothing. Auto-reconnect to a previously-saved WiFi scale on app start is a separate question (see decision 5).

### 4. Address prefix scheme for transport routing

Saved-scale addresses gain a transport prefix to disambiguate the same physical scale across transports:

```
   BLE saved:  "A4:C1:38:XX:XX:XX"          (current — bare MAC/UUID)
   WiFi saved: "wifi:hds.local"             (new — prefix + hostname)
```

Why a prefix rather than a separate setting field:

- `m_savedScaleAddress` remains a single `QString` — no settings-shape change, no migration.
- The #440 "primary-scale-only auto-reconnect" logic already compares against `m_savedScaleAddress`; a prefixed string is unambiguous, so the BLE row and WiFi row for the same physical scale never collide on the saved-address compare.
- `connectToScale(address)` dispatches on the prefix: `wifi:` → construct `DecentScaleWifi`; otherwise → existing BLE path. The branch lives in one place.

### 5. Auto-reconnect on app start

If `m_savedScaleAddress` starts with `wifi:`, the WiFi auto-reconnect on app start performs:

1. `QHostInfo::lookupHost(<hostname>)` with a ~5 s timeout.
2. On success → `DecentScaleWifi.connectToDevice()` → WebSocket connect.
3. On failure → no fallback to a different scale; no auto-attempt at the BLE entry of the same physical scale. The user can manually scan to retry.

This mirrors the existing BLE behavior exactly: if the saved BLE scale isn't advertising, Decenza doesn't try a different scale either (per #440's primary-only auto-reconnect). The "primary scale" intent is preserved across transports — saved-WiFi and saved-BLE behave symmetrically.

### 6. No cadence handshake — pipeline already auto-calibrates

[weightprocessor.cpp:65-66](src/machine/weightprocessor.cpp:65) maintains an EMA of inter-arrival intervals per connection, with the comment explicitly naming 2 Hz / 5 Hz / 10 Hz as supported rates. Stall thresholds are absolute (`kScaleStaleMs = 2000`, `kScaleStallConfirmMs = 6000`), comfortably above any rate we'd actually see. Therefore:

- The driver does not need to know the firmware's current send rate.
- No transport-specific stall or SAW thresholds.
- A firmware upgrade from 2 Hz to 10 Hz requires no Decenza coordination; the EMA tracks the new rate within ~3-5 samples.
- A WiFi network burp (200-400 ms gap) is well inside the absolute stall thresholds and self-recovers when the next sample arrives.

### 7. Protocol parity with BLE is complete in v1

The HDS WebSocket protocol (`Kulitorum/openscale` README) exposes every command and every readback the BLE protocol does. `DecentScaleWifi` implements the full surface from day one — no TODO comments, no future-firmware staging. Concrete command mapping:

| BLE driver method | BLE packet | WiFi command | Ack semantics |
|---|---|---|---|
| `tare()` | `03 0F 01 00` | `"tare"` (text) | Silent (BLE write fire-and-forget; WiFi text-form intentionally silent for v1 backwards compat) |
| `startTimer()` | `03 0B 03 00` | `"timer start"` (text) | Silent |
| `stopTimer()` | `03 0B 00 00` | `"timer stop"` (text) | Silent |
| `resetTimer()` | `03 0B 02 00` | `"timer reset"` (text) | Silent |
| `wake()` | `03 0A 01 01 00 01` | `"display on"` (and `"soft_sleep off"` if currently soft-sleeping) | Silent |
| `disableLcd()` | `03 0A 00 00` | `"display off"` (text) | Silent |
| `sleep()` → emits `sleepCompleted` | `03 0A 02 00`, waits for `characteristicWritten` ack | `{"command":"power","action":"off"}` (text), emit `sleepCompleted` immediately after the send call returns (write-succeeded analog) | See decision 12 |
| `setLed(r,g,b)` | `03 0A r g b` | `"led <r> <g> <b>"` (text) | Silent |
| Battery (0..100%) | LED-response packet byte `[4]` | `status` frame `battery_percent` | Pull at connect (request `status`) + on every 5 s heartbeat |
| Charging | LED-response packet `[4] == 0xFF` (currently encoded lossily as battery=100) | `status` frame `charging` (explicit bool) | See decision 13 |
| Firmware version | LED-response bytes `[5-6]` (BCD-packed) | `status` frame `firmware_version` (e.g. `"FW: 3.0.9"`) | Log once per connect on first `status` frame; warn-log on subsequent change (mirrors BLE behavior in [decentscale.cpp:230-242](src/ble/scales/decentscale.cpp:230)) |
| `buttonPressed(int)` | Command `0xAA` byte `[2]` (single int) | `events on` then parse `{type:"button", button_number, press_code}` | See decision 11 |
| _(BLE has no equivalent)_ | — | `type:"power"` event with `reason_code` | Log + remember most recent reason; surface on subsequent `disconnected()` so the user sees "scale shut down: low battery" instead of bare "disconnected" |

The `(WiFi)` suffix on the row label is still the only visible marker that transport differs — no other UI hint is added. Per project rule "prefer fewer settings" ([feedback_minimize_settings](memory)).

### 8. mDNS platform reality

`QHostInfo::lookupHost()` resolves `.local` names via the OS resolver, which means:

| Platform | mDNS path | Likely works? |
|---|---|---|
| macOS | mDNSResponder (Bonjour) | Yes |
| iOS | mDNSResponder, **requires Info.plist entries** | Yes, with plist work |
| Windows 10+ | Native mDNS resolver | Yes |
| Linux (most distros) | Avahi via NSS | Yes if Avahi running |
| Android | Not via libc resolver — needs `NsdManager` | **Spike** |

iOS Info.plist needs:

```xml
<key>NSLocalNetworkUsageDescription</key>
<string>Decenza discovers the Half Decent Scale on your local network.</string>
<key>NSBonjourServices</key>
<array>
    <string>_http._tcp</string>
</array>
```

Without `NSLocalNetworkUsageDescription`, the lookup silently fails on iOS 14+.

The Android spike is the single largest unknown. Three outcomes:

- **A. Qt resolves `.local` on Android** (possible on some OEM builds) → no extra work.
- **B. Need a JNI bridge to `NsdManager`** → a small Kotlin/JNI helper in `android/` that resolves the hostname and returns an IP, plus a Qt wrapper. Manageable but adds ~100 lines of platform glue.
- **C. mDNS unreliable; fall back to user-typed IP** → would require a Settings UI for "WiFi scale address." Conflicts with project rule "prefer fewer settings." We'd push back on this outcome and try harder on option B before adding a setting.

Spike result determines task 6.

### 9. WebSocket lifecycle

`QWebSocket` is already a dependency (verified in `CMakeLists.txt`: `Qt6::WebSockets`). The driver wires:

- `connected` → `setConnected(true)` and `emit weightSampleReceived` once we start receiving frames (the existing scaledevice contract uses sample arrival as the liveness signal, not "TCP open").
- `textMessageReceived` → parse JSON (via `QJsonDocument`, not regex — frames are small and well-formed), pull `grams`, call `setWeight()`. Reject non-conforming frames silently.
- `disconnected` → `setConnected(false)`. The hot-swap path in `main.cpp` already handles reconnection via the normal scale-swap machinery.
- `error` → log via `logMessage`, surface via `errorOccurred`.

Reconnect strategy: the driver itself does **not** schedule any reconnect — on `disconnected` it just calls `setConnected(false)` and exits. Reconnect is owned by `main.cpp`'s `scaleReconnectTimer` (a 5 s / 30 s / 60 s exponential-backoff loop, gated on `settings.scaleAddress()` being non-empty), which re-fires `BLEManager::tryDirectConnectToScale()` → `DecentScaleWifi::connectToHost()`. Keeping the reconnect path inside the driver would have required a debounce timer (forbidden by the project's "no timer-as-guard" rule) and would race with the main.cpp retry loop. The earlier in-driver 3 s reconnect was removed for these reasons.

Server-busy handling: if the WS upgrade fails with HTTP 503 (firmware-side 5-client cap exceeded, see `webserver.h`), emit a specific error message ("Another client is connected to the scale") so the toast is clear. With the 5-client cap this should be very rare in practice; the path is kept as defensive code.

### 10. What about the dual-HIGH-priority / scale-stall backoff?

The connection-priority / scale-feed-stall machinery in `BLEManager` and `qtscalebletransport.cpp` is BLE-specific (it manages GATT connection intervals and reconnects). It has no analogue and no purpose on WiFi:

- `setSkipHighPriority(bool)` — default no-op on the base class; `DecentScaleWifi` doesn't override.
- `setConnectionPriorityManaged(bool)` — same.
- `onScaleFeedStallConfirmed(qint64)` — the WeightProcessor still emits stall signals on a real feed gap, but the WiFi driver doesn't act on them (no equivalent to dropping connection priority). If the WS link is actually dead the TCP layer disconnects, the WS emits `disconnected`, and the normal reconnect path runs.

So `DecentScaleWifi` inherits the no-op defaults on all of this machinery, and the BLE-specific logic stays cleanly scoped to the BLE path. No conditional checks needed.

### 11. BLE-specific machinery that has NO equivalent on WiFi

The BLE driver carries machinery that exists to work around GATT-layer quirks and Decent Scale firmware behaviors specific to the BLE link. These are deliberately NOT replicated on WiFi:

| BLE machinery | Why BLE needs it | WiFi equivalent |
|---|---|---|
| 1 Hz heartbeat (`03 0A 03 FF FF`) [decentscale.cpp:394-406](src/ble/scales/decentscale.cpp:394) | Decent Scale firmware disconnects a silent BLE link | WiFi's 5 s `status` heartbeat (after `events on`) + TCP keepalive; no app-level ping needed |
| 1 s/2 s watchdog with re-enable-notifications retry [decentscale.cpp:261-322](src/ble/scales/decentscale.cpp:261) | Android Qt can silently fail to actually enable GATT notifications; the only fix is to re-write the CCCD | None — WS surfaces dead links directly via `disconnected()`. The pipeline-level `kScaleStaleMs = 2000` / `kScaleStallConfirmMs = 6000` thresholds in `WeightProcessor` still apply and are sufficient |
| XOR checksum validation + auto-disable after N failures [decentscale.cpp:185-202](src/ble/scales/decentscale.cpp:185) | Original Decent Scale (v1) has buggy checksums; HDS has correct ones — driver auto-detects | None — TCP guarantees integrity at the link layer; JSON parse failure is the analog ("if parse fails, drop frame silently") |
| 6-step wake sequence over 2 s on connect [decentscale.cpp:115-162](src/ble/scales/decentscale.cpp:115) | GATT writes can be silently dropped on Android, so the driver retries notifications enable + LCD enable; sequence comes from de1app | Two text writes: `"rate 10k"` then `"events on"`. TCP guarantees ordering and delivery; no retries needed |
| Service / characteristic discovery handshake | GATT model | None — `QWebSocket::open()` is the entire connection setup |
| `enableNotifications` (CCCD write) | GATT model | None — server pushes frames as soon as WS handshake completes |

**Decision: `DecentScaleWifi` has no heartbeat timer, no watchdog timer, no checksum logic, and no on-connect wake sequence beyond the two-text-write `rate` + `events on` setup.** Each of these would be cargo-culted BLE complexity solving problems WiFi doesn't have.

### 12. `sleep()` ↔ `sleepCompleted` semantics on WiFi

`ScaleDevice::sleep()` is documented as "Put scale to sleep (battery power saving - full power off)" and the BLE driver's `03 0A 02 00` is in fact the firmware-level power-off — the scale's radio drops and the scale requires a physical button press to wake again. The earlier reading of `0A 02 00` as a reversible LCD-off-plus-pause was wrong: that's what `disableLcd()`'s `0A 00 00` does, which is explicitly contrasted with `sleep()` in [decentscale.cpp:383-388](src/ble/scales/decentscale.cpp:383) ("This is different from sleep() which powers off the scale completely"). The same logical "sleep" gesture should produce the same outcome on the scale regardless of transport.

Decision:

- WiFi `sleep()` → `send("{\"command\":\"power\",\"action\":\"off\"}")`, the HDS firmware's equivalent of BT `0A 02 00`. Emits `sleepCompleted` unconditionally after the send attempt returns, so callers (app-exit waitLoop, DE1-sleep handler) don't hang waiting for an ack that won't come.
- The BLE driver waits for `characteristicWritten` before emitting `sleepCompleted` ([decentscale.cpp:362-364](src/ble/scales/decentscale.cpp:362)). The semantic intent of that wait is "we confirmed the bytes left our radio." On WiFi, `send()` returning success (socket was in `ConnectedState`) is the precise analog (the TCP write entered the kernel buffer). We do not wait for the firmware's status-ack JSON form — that would change the latency profile vs BLE.
- If the socket is not in `ConnectedState` (e.g., `ClosingState` from a prior graceful close, or never-opened), `send()` returns false. `sleep()` emits `sleepCompleted` anyway — matching the BLE driver's "transport unavailable → emit immediately" early-return at [decentscale.cpp:358-361](src/ble/scales/decentscale.cpp:358) — and logs a WARN so the dropped power-off command is diagnosable rather than hidden behind a misleading "drained successfully" downstream log.
- WiFi `wake()` sends `"soft_sleep off"` then `"display on"` — the reversible-park complement (`soft_sleep on/off`), useful between explicit user actions but **not** what `sleep()` issues.
- `"soft_sleep on"` remains a lighter, reversible park state on the firmware side. It is intentionally NOT what `sleep()` sends — it does not match BT's `0A 02 00` semantics and leaves the ESP32 radio active, draining a battery-only HDS while the DE1 is asleep.

### 13. Charging state — small `ScaleDevice` interface extension

WiFi reports `charging` as an explicit boolean in the `status` frame. BLE encodes charging as `battery_byte == 0xFF` and the current driver translates that to `setBatteryLevel(100)`, **losing the charging signal entirely** ([decentscale.cpp:213-218](src/ble/scales/decentscale.cpp:213)).

To not downgrade WiFi's cleaner data, extend the base `ScaleDevice` interface:

```cpp
// ScaleDevice (base) — additions:
Q_PROPERTY(bool charging READ charging NOTIFY chargingChanged)
bool charging() const { return m_charging; }
signals:
    void chargingChanged(bool charging);
protected:
    void setCharging(bool charging);  // emits signal on value flip
private:
    bool m_charging = false;
```

Driver behavior:

- **WiFi**: parse `status.charging` → `setCharging(...)`; parse `status.battery_percent` → `setBatteryLevel(...)`. Independent fields.
- **BLE** (incidental improvement, no behavior regression): on LED-response packet byte `[4]`:
  ```cpp
  if (battByte <= 100)        { setCharging(false); setBatteryLevel(battByte); }
  else if (battByte == 0xFF)  { setCharging(true);  setBatteryLevel(100); }
  ```
  The "battery=100" reporting stays the same so any UI that currently checks `battery == 100` for "full" keeps working; the new `charging` signal additionally surfaces the charging fact. UI that wants to display a charging icon should bind to `charging` directly going forward.

This is the only base-class change. Everything else is encapsulated in `DecentScaleWifi`.

### 14. mDNS resilience: IP cache with hostname fallback

mDNS resolution of `.local` names is unreliable in practice — Android's libc resolver doesn't handle it, multicast traffic is filtered on some APs, and the scale's mDNS responder may not answer when waking from power-save. To make WiFi reconnect robust:

- **The driver caches the WebSocket peer IP** after each successful connection (read via `QWebSocket::peerAddress()`). Cache is keyed by hostname and persisted to Settings (`wifiScaleIp(hostname)` accessor pair). One entry per WiFi scale.
- **Connect attempts try the cached IP first**, then fall back to the hostname. If both fail validation, give up; the user can rescan.
- **Validation**: a "recognizable HDS frame" must arrive within 5 s of the WS upgrade — either a snapshot (`contains("grams")`) or a typed `status` frame. Anything else (no frame, malformed frames, WS server that isn't an HDS) trips the recognition timeout and the driver closes the socket. This guards against DHCP reassignment putting a non-HDS WebSocket server at the cached IP.
- **Cache invalidation**: failure to validate via cached IP triggers fallback to hostname; the next successful connect (via hostname) writes a fresh IP into the cache. So the cache is self-healing — there's no explicit TTL or manual clear.
- **No on-disk schema migration**: the cache is a `QMap<QString,QString>` in Settings, additive only. Absence of an entry means "no cache yet, go straight to hostname."

The orchestration lives **inside `DecentScaleWifi`** rather than in BLEManager — the driver knows the hostname, owns the socket, sees the recognition signal, and reads `peerAddress()` after connect. BLEManager just hands it a hostname as before. The driver takes two test-friendly callbacks (`m_ipResolver`, `m_ipCacheUpdate`) so tests inject mocks and production code wires to Settings.

```
   connectToHost("hds.local")
     │
     ├─ cachedIp = m_ipResolver("hds.local")
     ├─ if cachedIp:  attemptTarget(cachedIp,  isHostname=false)
     ├─ else:         attemptTarget("hds.local", isHostname=true)
     │
   attemptTarget(target, isHostname)
     │
     ├─ open ws://<target>/snapshot
     ├─ start 5 s recognition timer
     │
     ├─ first snapshot/status frame arrives:
     │    cancel timer; if isHostname: cache m_socket->peerAddress()
     │
     └─ recognition timer fires:
          close socket
          if cached-IP attempt:    attemptTarget(hostname, isHostname=true)
          else (hostname failed):  emit errorOccurred("WiFi scale did not respond as HDS")
```

### 15. Button event encoding

BLE `buttonPressed(int button)` emits a single int — the byte from `command 0xAA` byte `[2]` ([decentscale.cpp:243-247](src/ble/scales/decentscale.cpp:243)). The valid BLE values are in `0..0xFF`.

WiFi delivers `{button_number, press_code}` as a pair. To keep the `buttonPressed(int)` signal signature unchanged (so all downstream consumers work for both transports), `DecentScaleWifi` encodes the pair into a single int with a bit pattern that cannot collide with any BLE-side value:

```cpp
// WiFi encoding: 0x1000 | (button_number << 8) | press_code
// e.g. circle-short = 0x1101, square-long = 0x1202
constexpr int kWifiButtonFlag = 0x1000;
int encoded = kWifiButtonFlag | (buttonNumber << 8) | pressCode;
emit buttonPressed(encoded);
```

The high bit (`0x1000`) flags the value as a WiFi-encoded button. Downstream consumers that want to handle WiFi-specific press kinds (e.g., differentiate long-press) can do so via:

```cpp
if (button & 0x1000) {
    int buttonNum  = (button >> 8) & 0xF;
    int pressCode  = button & 0xFF;
    // ...
}
```

Downstream consumers that don't care just see a non-colliding int. The BLE driver continues to emit raw `0..0xFF` values unchanged.

**Open question worth confirming during implementation:** does any current consumer of `buttonPressed` switch on specific BLE button values? If so, the encoding scheme needs to preserve them. A quick grep for `buttonPressed` consumers will resolve this before locking the encoding.

## Risks / Trade-offs

- **Android mDNS** is the spike-load-bearing risk. If outcome C (need a user-typed-IP setting) is forced on us, the project-rule conflict with "prefer fewer settings" forces a design conversation. Mitigation: spike before locking the design; outcome B (JNI bridge) is the planned fallback.
- **One-client-only firmware limit** means a user with a browser scale-dashboard open will fail to connect from Decenza, and vice versa. This is a property of the device. Mitigation: clear error message; don't fight for the slot.
- **WiFi modem sleep on ESP32**: if the firmware enables WiFi power-save, weight frames can stall briefly. Verify firmware default; if enabled, request a firmware change to disable it on the always-streaming code path.
- **Same-LAN assumption**: WiFi discovery only works if the device running Decenza and the scale are on the same broadcast domain. VLAN'd or mesh-routed setups may not see mDNS. Not in scope to fix.
- **No transport-version handshake** means we can't tell a 2 Hz firmware from a 10 Hz firmware. If users complain about laggy weight on old firmware, the answer is "update the scale firmware," not a Decenza-side workaround.

## Suffix surfaces

The `(WiFi)` suffix follows the scale name **everywhere it is printed** — discovered-scales list, "Connecting to …" / "Connected to …" toasts, connection-status badge, diagnostics/share-log output, MCP tool responses that include scale name, and any future per-shot "scale used" annotation. This falls out for free because the suffix lives in the synthesized `name` field on the discovered entry and flows through every consumer unchanged; no surface needs a special branch. Rationale: transport-at-a-glance is useful for troubleshooting (e.g., "scale lagging?" → look at status badge → see WiFi → infer LAN issue), and it's cheaper to let the name flow than to strip it conditionally.
