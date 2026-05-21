# Change: Add WiFi support for the Half Decent Scale

## Why

The Half Decent Scale (HDS) — the official Decent v2 scale — exposes a WiFi WebSocket interface in addition to its BLE interface (firmware: `Kulitorum/openscale`). For users on tablets or desktops where the BLE radio is contended (the dual-HIGH-priority problem we already model with `ScaleSkipHighLatch`), or where the device is physically distant from the machine, a WiFi connection is a strictly better link option. Today Decenza only knows how to talk to the HDS over BLE.

The scale broadcasts an mDNS hostname (`hds.local`) when joined to the user's network, exposes a WebSocket at `/snapshot` that pushes JSON weight samples (`{"grams": <float>, "ms": <uint>}`) and accepts a single text command `"tare"`. There's no auth, no provisioning UX on the app side (the firmware ships an AP-fallback web form for credentials), and the device caps concurrent WebSocket clients at 5 (server replies 503 only past that ceiling, which is well above the realistic concurrent-consumer count of 1-3: Decenza + an optional browser tab + maybe a debug client).

The shot-tracking pipeline already auto-calibrates to the scale's actual sample cadence — see [weightprocessor.cpp:65-66](src/machine/weightprocessor.cpp:65) ("EMA … adapts to any scale rate (10Hz, 5Hz, 2Hz)"). So no downstream tuning is required: whatever cadence the WiFi link delivers (current firmware emits at 2 Hz; a one-line firmware change to ~10 Hz is a natural follow-up, with no Decenza-side coordination needed because the pipeline absorbs the rate change automatically).

## What Changes

### mDNS resilience

- **NEW** `Settings::wifiScaleIp(hostname)` / `setWifiScaleIp(hostname, ip)` accessor pair backed by a persisted `QMap<QString,QString>` — hostname → last-known-good IP. Additive; no schema migration.
- **`DecentScaleWifi` connects by cached IP first, falls back to hostname.** A 5 s recognition timer guards the WebSocket: the first snapshot or `status` frame must arrive within the window or the driver closes the socket and retries via hostname. After a successful hostname connect, the driver caches `QWebSocket::peerAddress()` so the next connect skips mDNS entirely. Self-healing: a stale cached IP that fails validation is replaced by the next successful hostname resolve.

### Discovery

- **NEW** `WifiScaleDiscovery` service (`src/network/wifiscalediscovery.{h,cpp}`) — performs an on-demand `QHostInfo::lookupHost("hds.local")` with a short timeout (~2 s) when triggered. If the host resolves to an address, emits a synthetic scale entry to be merged into `BLEManager::discoveredScales`. Does nothing on app startup; runs only when the user-initiated scan fires.
- **MODIFIED** `BLEManager::scanForDevices()` — kicks off the WiFi probe in parallel with the existing BLE scan. The discovered-scales list gains a `transport` field (`"ble"` or `"wifi"`) so QML and the connection code can route correctly.
- **MODIFIED** `BLEManager::discoveredScales` shape — entries gain `transport: "ble"|"wifi"`. WiFi entries use the synthesized display name `"Decent Scale (WiFi)"` (suffix only on WiFi; BLE rows stay unchanged). The list still has a single shape; QML doesn't branch.

### Driver

- **NEW** `DecentScaleWifi : public ScaleDevice` (`src/ble/scales/decentscalewifi.{h,cpp}`) — wraps `QWebSocket` connected to `ws://hds.local/snapshot`. **Implements the full BLE-parity command surface from day one** against the HDS WebSocket protocol documented in `Kulitorum/openscale` README: `tare`, `timer start/stop/reset`, `display on/off`, `soft_sleep on/off`, `led <r> <g> <b>`, `events on`, `rate <2k|5k|10k>`, plus reading `status` frames for battery, charging, firmware version, button events, and power-off events. Returns `type() == "decent"` so downstream code that branches on scale type treats it identically to BLE HDS.
- **NEW** `ScaleDevice` base-class extension — adds `Q_PROPERTY(bool charging READ charging NOTIFY chargingChanged)` + `setCharging(bool)` protected setter. Currently the BLE driver loses the charging signal by encoding it as `battery == 100` (battery byte `0xFF`). WiFi reports `charging` as an explicit boolean. Extending the base interface preserves WiFi's cleaner data and, as an incidental improvement, lets the BLE driver also report charging correctly (set `charging=true` AND `batteryLevel=100` on `0xFF`).
- **MODIFIED** `ScaleFactory::createScale(device, typeName, parent)` — when `typeName == "decent-wifi"`, constructs `DecentScaleWifi` instead of `DecentScale`. The `device` argument is unused on the WiFi path (no `QBluetoothDeviceInfo` exists for a WiFi scale).

### Address scheme & saved-scale handling

- **MODIFIED** `BLEManager` saved-scale handling — saved scale addresses gain a transport prefix: `wifi:hds.local` for WiFi entries; BLE addresses remain bare (current MAC/UUID format). `connectToScale()` routes by prefix. The #440 "primary-scale-only auto-reconnect" logic continues to work because the prefixed handle is unambiguous: a user with a saved BLE HDS doesn't auto-connect to the same physical scale's WiFi entry, and vice versa.

### Platform plumbing

- **MODIFIED** iOS `Info.plist` — add `NSLocalNetworkUsageDescription` and a Bonjour service declaration for `_http._tcp`. iOS 14+ silently blocks mDNS lookups without this.
- **SPIKE** Android mDNS — `QHostInfo::lookupHost("hds.local")` on Android does not reliably resolve `.local` names. The spike confirms which of these is needed: (a) Qt-only resolution works (do nothing), (b) a JNI shim to `NsdManager` is needed, (c) fall back to letting the user type an IP. Result decides task 6.x.

### UI

- **MODIFIED** `qml/pages/settings/SettingsConnectionsTab.qml` (and any other scale-list views) — surface the `(WiFi)` suffix already present in the synthesized name; no transport-specific UI branching needed beyond that.

### Firmware (separate repo, separate PR)

- **MODIFIED** `Kulitorum/openscale` `src/hds.ino:1411` — change `current - lastUpdate > 500` to `> 100`. Trivial one-line change; not part of this Decenza change but coordinated so the Decenza release notes can reference a firmware version that delivers a useful cadence. **The Decenza side does not require this firmware change** — the discovery and driver work identically against 2 Hz firmware; users on old firmware get a working but low-cadence link.

### Tests

- **NEW** `tests/tst_decentscalewifi.cpp` — frames a fake WebSocket server, asserts that JSON frames produce `setWeight()` calls with the correct values, and that `tare()` writes the expected `"tare"` text frame.
- **NEW** `tests/tst_wifiscalediscovery.cpp` — uses a mock `QHostInfo` resolver to verify the discovery service emits a synthetic entry on successful lookup and nothing on failure/timeout.
- **MODIFIED** `tests/tst_blemanager.cpp` (if exists; otherwise new coverage in an existing test file) — verifies `discoveredScales` entries have `transport` populated, prefix-routed saved-scale rehydration works, and the WiFi scan probe is only triggered on user-initiated scan.

### NOT in scope

- **WiFi credential provisioning from inside Decenza.** Users provision via the firmware's existing AP-fallback web form (`DecentScale` SSID / `12345678`, web form at `192.168.1.1`). Building a captive-portal flow inside Decenza is a separate, larger feature.
- **Multi-scale WiFi discovery.** Only `hds.local` is probed. Other WiFi scales, if any appear later, are a separate change.
- **Subnet scanning / user-typed IP fallback.** If mDNS fails, the WiFi scale doesn't appear in the list. Revisit only if real users hit it.
- **Transport-specific stall thresholds / SAW tuning.** The weight pipeline's existing per-scale interval EMA already absorbs cadence variation; no transport-aware knobs are introduced.
- **Capability negotiation / version handshake.** The scale doesn't advertise its rate; the pipeline measures it. No protocol versioning is added.
- **Heartbeat ping from Decenza to scale.** WebSocket-level keepalives (`QWebSocket::ping`) handle dead-link detection. App-level pings are not required at v1; revisit if real-world WiFi drops aren't caught fast enough.

## Impact

- **Affected specs**: `wifi-scale-discovery` (NEW capability).
- **Affected code**:
  - `src/network/wifiscalediscovery.{h,cpp}` — new discovery service.
  - `src/ble/blemanager.{h,cpp}` — integrate WiFi probe into `scanForDevices()`; add `transport` field to `discoveredScales`; route `connectToScale()` by transport prefix; saved-scale prefix handling.
  - `src/ble/scales/decentscalewifi.{h,cpp}` — new driver (full BLE-parity command surface).
  - `src/ble/scales/scalefactory.{h,cpp}` — recognise `"decent-wifi"` typeName.
  - `src/ble/scaledevice.{h,cpp}` — add `charging` Q_PROPERTY + signal + protected setter.
  - `src/ble/scales/decentscale.cpp` — update LED-response parse to call `setCharging(true)` on `battByte == 0xFF` (incidental improvement, no behavior regression).
  - `src/main.cpp` — register `DecentScaleWifi` in the scale-swap hot-path next to `DecentScale`.
  - `qml/pages/settings/SettingsConnectionsTab.qml` — passive (the `(WiFi)` suffix is already in the model). If any UI currently binds to "battery == 100 ⇒ charging icon", that should bind to the new `charging` property instead.
  - `CMakeLists.txt` — already links `Qt6::WebSockets` (verified) and `Qt6::Network`. No new dependencies.
  - `android/AndroidManifest.xml.in` — confirm `INTERNET` permission (already granted by other features); no NSD changes pending the spike result.
  - iOS Info.plist (generated/templated) — add `NSLocalNetworkUsageDescription` and Bonjour service declaration.
- **Behavior change**: Tapping "Scan for devices" now lists a `Decent Scale (WiFi)` row when the scale is reachable on the LAN. Users can select it; weight, tare, timer, LED, sleep, display, battery, charging, button events, and power-off events all work identically to BLE. The `(WiFi)` suffix follows the scale name everywhere it is printed (status badge, toasts, diagnostics, MCP tool output). Users can switch between BLE and WiFi by disconnecting and selecting the other row; main.cpp's existing scale-type-change path handles the swap. Auto-reconnect targets whichever transport was last saved; if the saved scale is unreachable, behavior mirrors current BLE — user has to manually rescan, no fallback to a different scale.
- **Risk**: mDNS reliability on Android (mitigated by spike → fallback choice). 503 server-busy past the 5-client firmware cap (mitigated by the 503-detection path in `DecentScaleWifi::onError` — surfaces a clear toast and suppresses reconnect; very unlikely to trigger in practice given the 5-client ceiling). Power-save behavior on the ESP32 if WiFi modem sleep is enabled (mitigated by firmware default; verify before release).
