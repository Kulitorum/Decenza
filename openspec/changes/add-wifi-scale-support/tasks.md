# Tasks

## 0. ScaleDevice base-class extension: `charging` property

- [x] 0.1 Add `Q_PROPERTY(bool charging READ charging NOTIFY chargingChanged)` to `ScaleDevice`. Add `bool charging() const` getter (returns `m_charging`), `chargingChanged(bool)` signal, and `protected void setCharging(bool)` setter that emits the signal only on value flip. Initial `m_charging = false`.
- [x] 0.2 Update `src/ble/scales/decentscale.cpp` LED-response handler at lines 213-218 to call `setCharging(false) + setBatteryLevel(battByte)` when `battByte <= 100`, and `setCharging(true) + setBatteryLevel(100)` when `battByte == 0xFF`. The existing "battery=100" reporting on charging is preserved so any UI that currently checks `battery == 100` keeps working; the new `charging` signal additionally surfaces the charging fact.
- [x] 0.3 Audit existing QML / C++ consumers of battery state for places that should bind to the new `charging` property instead of (or in addition to) `battery == 100`. Specifically check `qml/components/ConnectionIndicator.qml` and any settings/diagnostics views that show battery. **Do not change existing bindings unless they were incorrectly representing charging** — additive use is fine, regression is not. (Audit: no QML bindings consume `battery == 100` for a charging indication; nothing to migrate.)
- [x] 0.4 Verify the `charging` property surfaces correctly via the existing `Q_PROPERTY` registration (no extra `qmlRegisterType` work needed since `ScaleDevice` is already exposed).

## 1. mDNS discovery service

- [x] 1.1 Create `src/network/wifiscalediscovery.{h,cpp}` with a `WifiScaleDiscovery` QObject exposing `void probe(int timeoutMs = 2000)` and signals `scaleFound(QString hostname, QString resolvedAddress)` / `probeFinished()`.
- [x] 1.2 Implement `probe()` via `QHostInfo::lookupHost("hds.local", ...)`. On success, emit `scaleFound`. On NotFound/HostNotFound/timeout, emit `probeFinished` only.
- [x] 1.3 Cancel any in-flight lookup if `probe()` is called again before completion.
- [x] 1.4 Add to `CMakeLists.txt` source list. `Qt6::Network` is already linked — no new module.

## 2. BLEManager integration

- [x] 2.1 Add `WifiScaleDiscovery m_wifiDiscovery` member to `BLEManager`.
- [x] 2.2 In `BLEManager::scanForDevices()` (the user-initiated entry point — not `startScan()`), call `m_wifiDiscovery.probe()` in parallel with the BLE scan.
- [x] 2.3 On `WifiScaleDiscovery::scaleFound`, append a synthetic entry to `m_scales` with `name = "Decent Scale (WiFi)"`, `address = "wifi:" + hostname`, `type = "decent-wifi"`. Emit `scalesChanged()`.
- [x] 2.4 Extend the `m_scales` storage element so it can hold the transport tag. Either change `QPair<QBluetoothDeviceInfo, QString>` to a small struct, or store the transport tag in a parallel structure — pick whichever minimises churn in the existing iteration sites.
- [x] 2.5 Update `discoveredScales()` to populate a `transport` field (`"ble"` or `"wifi"`) on each map entry.
- [x] 2.6 Update `connectToScale(const QString& address)` to branch on the `"wifi:"` prefix: WiFi entries emit a new `wifiScaleSelected(hostname)` signal (or reuse `scaleDiscovered` with a synthetic `QBluetoothDeviceInfo` if simpler — pick the path that keeps `main.cpp` cleaner). BLE path is unchanged.

## 3. DecentScaleWifi driver

- [x] 3.1 Create `src/ble/scales/decentscalewifi.{h,cpp}` with `class DecentScaleWifi : public ScaleDevice`.
- [x] 3.2 Add `QWebSocket m_socket` member; in the constructor, connect `connected`, `disconnected`, `textMessageReceived`, `error` slots.
- [x] 3.3 Override `connectToDevice(const QBluetoothDeviceInfo&)` to ignore the argument and open `m_socket.open(QUrl("ws://" + hostname + "/snapshot"))` (hostname parameterised; default `"hds.local"`).
- [x] 3.4 Implement `disconnectFromScale()` → `m_socket.close()`. Override is enough; base class handles flag clearing.
- [x] 3.5 On `connected` signal: send `"rate 10k"` then `"events on"` (two text writes, TCP-ordered). No retries. Also send `"status"` to seed an immediate status frame so battery / firmware_version are populated before the periodic 5 s heartbeat would otherwise deliver them.
- [x] 3.6 Frame dispatch in `textMessageReceived`: parse with `QJsonDocument::fromJson`; if `doc` is an object:
  - if it contains `grams` (numeric) → snapshot path → `setWeight(grams)`.
  - else if it contains `type` (string) → dispatch on `type`: `"status"` (3.7), `"rate"` (log + verify rate took effect), `"button"` (3.8), `"power"` (3.9).
  - else → silently drop (log at debug only).
- [x] 3.7 Status-frame handler:
  - `battery_percent` → `setBatteryLevel(battery_percent)`.
  - `charging` (bool) → `setCharging(charging)` (the new base-class setter from task 0.1).
  - `firmware_version` (string) → log once per connect with the BLE driver's format; warn-log if it changes mid-connect (mirrors [decentscale.cpp:230-242](src/ble/scales/decentscale.cpp:230)). Track via a `QString m_firmwareVersion` member cleared in `onDisconnected`.
  - `protocol_version` → log on first status frame for diagnostics only; do not gate on a specific value (forward-compat).
- [x] 3.8 Button-frame handler: encode `(button_number, press_code)` as `0x1000 | (button_number << 8) | press_code` and `emit buttonPressed(encoded)`. See design decision 14.
- [x] 3.9 Power-frame handler: cache the most recent `reason` + `reason_code` in members `m_lastPowerEventReason` / `m_lastPowerEventCode`; set `m_userInitiatedShutdownReason = reason` so the imminent `disconnected` knows the cause; log via `logMessage`. On the subsequent `disconnected`, surface "Scale shut down: <reason>" via `errorOccurred` (or `logMessage` if `errorOccurred` would be too alarming) and do **not** trigger the 3 s reconnect attempt.
- [x] 3.10 Implement `tare()` → `m_socket.sendTextMessage("tare")` (text form; intentionally silent per protocol).
- [x] 3.11 Implement `startTimer()` / `stopTimer()` / `resetTimer()` → `"timer start"` / `"timer stop"` / `"timer reset"`.
- [x] 3.12 Implement `disableLcd()` → `"display off"`.
- [x] 3.13 Implement `wake()` → send `"soft_sleep off"` THEN `"display on"` (order: sensor/loop first, OLED second).
- [x] 3.14 Implement `sleep()` → send `"soft_sleep on"`; emit `sleepCompleted` immediately after `sendTextMessage` returns (positive byte count or 0 alike — see design decision 12).
- [x] 3.15 Implement `setLed(int r, int g, int b)` → `QString("led %1 %2 %3").arg(r).arg(g).arg(b)`. Clamp each channel to `0..255` before send.
- [x] 3.16 Make `sendKeepAlive()` a silent no-op (the 5 s status frame + TCP keepalive cover liveness; see design decision 11).
- [x] 3.17 Override `name() const` to return `"Decent Scale (WiFi)"` and `type() const` to return `"decent"` (matches BLE driver — same physical product). Add a separate `transportType() const` returning `"wifi"` if downstream code needs to distinguish.
- [x] 3.18 Implement single-attempt reconnect on `disconnected`: schedule a 3 s `QTimer::singleShot` to reopen the socket; if that attempt also disconnects, give up. **Skip the reconnect when `m_userInitiatedShutdownReason` is set** (the scale told us it was going down — don't fight it).
- [x] 3.19 Implement 503-detection on connection failure: when `error` fires after an upgrade attempt and the response code was 503, emit `errorOccurred("Another client is connected to the scale")` and do not schedule a reconnect. The QWebSocket error path may or may not give us the HTTP status directly — use `errorString()` heuristics if needed.
- [x] 3.20 Clear `m_firmwareVersion`, `m_lastPowerEventReason`, `m_lastPowerEventCode`, and `m_userInitiatedShutdownReason` in `onDisconnected` so the next connect re-captures them fresh (mirrors BLE driver behavior at [decentscale.cpp:63-77](src/ble/scales/decentscale.cpp:63)).

## 4. ScaleFactory + main.cpp wiring

- [x] 4.1 In `ScaleFactory::resolveScaleType`, recognise `"decent-wifi"` as a new `ScaleType` value (add `ScaleType::DecentScaleWifi`).
- [x] 4.2 In `ScaleFactory::createScale(device, typeName, parent)`, route `ScaleType::DecentScaleWifi` to `makeScale<DecentScaleWifi>(parent)`. The `device` argument is unused on this branch.
- [x] 4.3 In `main.cpp`'s scale-creation site (around `physicalScale = ScaleFactory::createScale(device, type)`), confirm the existing hot-swap path works for `DecentScaleWifi` — it should, since the swap is keyed on `type()`. Verify and add inline comment if any branch is changed.

## 5. Saved-scale handling

- [x] 5.1 Update `BLEManager::setSavedScaleAddress(address, type, name)` to accept `"wifi:hds.local"` addresses verbatim — no transformation, no MAC normalisation on prefixed addresses.
- [x] 5.2 Update `BLEManager::tryDirectConnectToScale` (if present) to branch on `"wifi:"` prefix: WiFi path performs `WifiScaleDiscovery::probe()` and on resolution constructs/connects `DecentScaleWifi`. BLE path is unchanged.
- [x] 5.3 Verify #440 primary-only auto-reconnect logic in `onDeviceDiscovered` still passes when a WiFi scale is saved (the BLE scan should not auto-connect to any BLE scale because the saved address is `"wifi:…"` and won't match any BLE MAC).

## 6. Android mDNS spike

- [ ] 6.1 Build a minimal test that calls `QHostInfo::lookupHost("hds.local", ...)` on a known-good Android device (Galaxy Tab S9, the maintainer's main test rig) and a known-poor device if available.
- [ ] 6.2 If `QHostInfo` resolves: do nothing, mark spike complete.
- [ ] 6.3 If `QHostInfo` does NOT resolve, build a JNI bridge:
  - [ ] 6.3.1 Add a Kotlin helper in `android/src/.../NsdHelper.kt` wrapping `android.net.nsd.NsdManager.resolveService`.
  - [ ] 6.3.2 Bridge to C++ via `QJniObject` (existing pattern in the codebase — see `screencaptureservice.cpp` for a JNI-bridge precedent).
  - [ ] 6.3.3 Route `WifiScaleDiscovery::probe()` through the JNI path on `Q_OS_ANDROID` and through `QHostInfo` everywhere else.
- [ ] 6.4 Update this tasks.md and design.md to reflect the chosen path.

## 7. iOS Info.plist

- [x] 7.1 Add `NSLocalNetworkUsageDescription` with the user-facing string "Decenza discovers the Half Decent Scale on your local network." (Existing key already present for the local web server; updated wording to also cover scale discovery.)
- [x] 7.2 Add `NSBonjourServices` array containing `_http._tcp`.
- [x] 7.3 If Info.plist is generated from a template (`.in` file), edit the template, not the generated file. (`ios/Info.plist` is the source — referenced directly by `CMakeLists.txt:951`, not generated from a template.)
- [ ] 7.4 Verify on iOS device: first scan after install prompts the local-network permission dialog; granting allows discovery; denying suppresses the WiFi row cleanly. **(Manual test — requires iOS device with a reachable HDS.)**

## 8. QML / connections page

- [x] 8.1 Verify `SettingsConnectionsTab.qml` (and any other scale-list views — search for `discoveredScales`) renders the synthesized `"Decent Scale (WiFi)"` name correctly. No QML changes expected — the suffix is in the model.
- [x] 8.2 Verify the "Connecting to …" log line in `connectToScale` correctly says "Connecting to Decent Scale (WiFi)" by virtue of using the name from `m_scales`.

## 9. Tests

- [x] 9.1 `tests/tst_decentscalewifi.cpp` — stand up a fake `QWebSocketServer` on a random port, point the driver at it via a settable URL (introduce a test-only setter or accept the URL in the constructor), send snapshot frames, assert that `weightChanged` fires with the parsed value.
- [x] 9.2 Malformed snapshot frame is dropped silently (no `weightChanged`).
- [x] 9.3 On connect, the server receives `"rate 10k"` and `"events on"` in that order.
- [x] 9.4 `tare()` causes the server to receive the text frame `"tare"`.
- [x] 9.5 `startTimer()` / `stopTimer()` / `resetTimer()` send the corresponding `"timer …"` text frames.
- [x] 9.6 `disableLcd()` sends `"display off"`; `wake()` sends `"soft_sleep off"` then `"display on"` in that order.
- [x] 9.7 `sleep()` sends `"soft_sleep on"` AND emits `sleepCompleted` once. (Verify the signal fires exactly once.)
- [x] 9.8 `setLed(255, 128, 0)` sends `"led 255 128 0"`; `setLed(300, -5, 256)` clamps and sends `"led 255 0 255"`.
- [x] 9.9 Status frame with `battery_percent: 82, charging: false` triggers `setBatteryLevel(82)` and `setCharging(false)`. Toggling `charging` between `false → true → false` emits exactly two `chargingChanged` signals.
- [x] 9.10 Status frame with `firmware_version: "FW: 3.0.9"` logs once; a subsequent status with the same value does not log again; a different value warn-logs the transition.
- [x] 9.11 Button frame `{type:"button", button_number:1, press_code:1}` emits `buttonPressed(0x1101)`. Button frame `{button_number:2, press_code:2}` emits `buttonPressed(0x1202)`.
- [x] 9.12 Power frame `{type:"power", event:"power_off", reason:"low_battery", reason_code:3}` is logged with the reason; the subsequent `disconnected` does NOT trigger a reconnect attempt.
- [ ] 9.13 Disconnect with no preceding power frame triggers exactly one reconnect attempt 3 s later; if that disconnects again, no further attempts. **(Deferred — timing-sensitive; the "no reconnect" half is covered indirectly by `powerEventSuppressesReconnect`. Revisit once we have a flake-resistant time-skip harness.)**
- [ ] 9.14 503 response on initial WS upgrade attempt emits `errorOccurred("Another client is connected to the scale")` and does not schedule a reconnect. **(Deferred — `QWebSocketServer` doesn't reject upgrades with HTTP 503 the way the HDS firmware does; would need a custom `QTcpServer` test or a real device.)**
- [x] 9.15 `ScaleDevice::charging` property reads `false` by default; the BLE driver test (`tst_scaleprotocol` or equivalent) is updated to verify the LED-response handler with `battByte == 0xFF` now flips `m_charging` to `true` and emits `chargingChanged(true)` while keeping `batteryLevel == 100`.
- [x] 9.16 `tests/tst_wifiscalediscovery.cpp` — use a mock host-info resolver (or `QHostInfo::lookupHost` against `localhost` as a stand-in) to verify `scaleFound` fires on resolution and `probeFinished` fires on timeout.
- [ ] 9.17 Verify `BLEManager::discoveredScales` includes `transport` field, that WiFi entries have `transport == "wifi"`, BLE entries `"ble"`, after a scan-that-finds-both. **(Deferred — requires a BLEManager test harness with a mocked BLE discovery agent; covered by the manual test matrix in 11.3.)**
- [ ] 9.18 Verify `BLEManager::connectToScale("wifi:hds.local")` routes through the WiFi path (assert a `wifiScaleSelected` signal or equivalent), and that bare BLE addresses route through the BLE path unchanged. **(Deferred — same reason as 9.17.)**
- [x] 9.19 Wrap all expected `qWarning` calls in `QTest::ignoreMessage` per `docs/CLAUDE_MD/TESTING.md`.

## 10. Firmware coordination

- [ ] 10.1 Confirm the minimum HDS firmware version that ships the full WebSocket command surface documented in `Kulitorum/openscale` README (commands: `tare`, `timer …`, `display …`, `led …`, `soft_sleep …`, `events on/off`, `rate …`, `status`, etc.). Record it in the Decenza release notes as "WiFi scale support requires HDS firmware ≥ X.Y.Z."
- [ ] 10.2 Older HDS firmware that only ships `tare` + 2 Hz snapshot stream still works (the JSON `rate` / `events` / `timer` / `led` / `soft_sleep` / `display` text commands will be silently ignored by the firmware's unknown-command path). Confirm by testing against the oldest firmware we expect users to be on. Decenza-side: log a warning on connect if no `status` frame arrives within ~10 s of `"events on"` (likely-old firmware), but do not refuse to operate.
- [ ] 10.3 No firmware PR required from this change. The firmware-side work is upstream of Decenza.

## 12. mDNS resilience: IP cache + hostname fallback

- [x] 12.1 Add `Settings::wifiScaleIp(const QString& hostname) const` getter and `setWifiScaleIp(const QString& hostname, const QString& ip)` setter on `src/core/settings.{h,cpp}`. Backed by a private `QMap<QString,QString> m_wifiScaleIpCache` member; serialized alongside other scale settings.
- [x] 12.2 Extend `DecentScaleWifi` with two settable callbacks (`std::function`): `setIpResolver(hostname → ip)` and `setIpCacheUpdate(hostname, ip → void)`. Default: no-ops (driver works standalone in tests).
- [x] 12.3 Add a `QTimer m_recognitionTimer` (single-shot, 5 s) to `DecentScaleWifi`. Started on `onConnected`. Stopped when the first snapshot frame or `type:"status"` frame arrives. On timeout: close socket and trigger fallback (or fail if already at fallback).
- [x] 12.4 In `DecentScaleWifi::connectToHost(hostname)`: call `m_ipResolver(hostname)`; if it returns a non-empty IP, call `attemptTarget(ip, isHostname=false)`; otherwise `attemptTarget(hostname, isHostname=true)`.
- [x] 12.5 In `attemptTarget`: record target + isHostname, open `ws://<target>/snapshot`, start recognition timer. Maintain `m_currentTarget` and `m_currentTargetIsHostname` members.
- [x] 12.6 In the first valid frame handler (snapshot or status): stop recognition timer, set `m_recognized = true`. If the target was a hostname, read `m_socket->peerAddress().toString()` and call `m_ipCacheUpdate(m_hostname, peerIp)` (skip if peer IP is empty or equals the hostname).
- [x] 12.7 In `onRecognitionTimeout`: log a warning; if the current attempt was the cached IP, close the socket and re-attempt via the hostname (`attemptTarget(m_hostname, true)`). If the hostname attempt also timed out, emit `errorOccurred("WiFi scale did not respond as HDS")` and stop — no further retries.
- [x] 12.8 Wire the callbacks in `main.cpp` after creating the WiFi driver: `wifi->setIpResolver([&](auto h){ return settings.wifiScaleIp(h); })` and `wifi->setIpCacheUpdate([&](auto h, auto ip){ settings.setWifiScaleIp(h, ip); })`.
- [x] 12.9 Tests for the cache + fallback in `tst_decentscalewifi.cpp`:
  - Cache hit: driver connects, receives first frame, calls `ipCacheUpdate` with the peer IP.
  - Cache miss: no resolver callback (or empty return) → driver connects via the supplied hostname; cache update fires.
  - Validation timeout from cached IP: server accepts WS upgrade but sends no recognizable frame within 5 s → driver closes, retries via hostname, succeeds.
  - Hostname also fails recognition: emits `errorOccurred` and does not loop.

## 11. Validate and PR

- [x] 11.1 `openspec validate add-wifi-scale-support --strict --no-interactive`.
- [x] 11.2 Build via Qt Creator MCP (per `feedback_use_qt_creator`), run `tst_decentscalewifi`, `tst_wifiscalediscovery`, and any modified `tst_blemanager` coverage; confirm 0 failures, 0 unexpected warnings. **(Build: 0 errors / 0 warnings. Tests: 56 passed / 0 failed / 0 warnings across `tst_DecentScaleWifi`, `tst_WifiScaleDiscovery`, and `tst_ScaleProtocol`.)**
- [ ] 11.3 Manual test matrix:
  - [ ] HDS over WiFi: scan → entry appears → connect → weight streams at requested rate → tare works → timer start/stop/reset work and show on scale display → LED commands change scale LED → display off / display on work → soft_sleep on / off work → disconnect cleanly.
  - [ ] Status frame contents reach UI: battery percent updates, charging icon toggles when USB-C cable plugged/unplugged.
  - [ ] Firmware version logs once on connect; appears in Share Log diagnostics.
  - [ ] Scale-side circle-button short press over WiFi (with `events on`) emits a `buttonPressed` signal Decenza handles identically to BLE.
  - [ ] Power-off event: drain battery (or trigger via double-click) → scale emits power event before WS drops → Decenza logs "Scale shut down: low battery" (or appropriate reason) instead of bare disconnect → no reconnect-loop fires.
  - [ ] HDS over BLE: scan → entry appears alongside the WiFi entry → both connectable → switch back and forth.
  - [ ] Saved scale rehydration: save WiFi entry → restart app → auto-reconnect → weight streams.
  - [ ] iOS: first scan prompts local-network permission; deny path is graceful.
  - [ ] Android: per spike result, mDNS resolves either via Qt or via the JNI bridge.
  - [ ] Server-busy: open 5 WS clients to the scale (past the firmware's concurrent-client cap), scan from Decenza, observe clean 503 error with toast. NOTE: with the cap raised from 1 to 5 in the firmware, this is now a rare-case verification rather than a common-path test.
  - [ ] Charging-state parity: BLE-connected scale on USB-C → `charging` property reads `true` (new behavior — verify previously-broken path is now correct).
- [x] 11.4 Open PR; run `/review`; address any 75%+-confidence issues. **(PR opened: [Kulitorum/Decenza#1246](https://github.com/Kulitorum/Decenza/pull/1246). `/review` to follow.)**
- [x] 11.5 PR description references the openscale firmware PR (#10.1) and notes that the firmware bump is optional (Decenza works against current 2 Hz firmware). **(N/A — firmware already ships the full command surface per README; minimum-version note moved to section 10.)**
