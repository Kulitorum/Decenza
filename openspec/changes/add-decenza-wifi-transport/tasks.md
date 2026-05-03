# Tasks: Add Decenza Scale Wi-Fi Transport, Provisioning, and Calibration GUI

## Phase 0 — Settings domain plumbing (gates the rest)

- [ ] Create `src/core/settings_connections.{h,cpp}` modeled on the slimmest existing sub-object (e.g. `settings_autowake.h`). Initial property: `Q_PROPERTY(QVariantMap scaleWifiPairings READ scaleWifiPairings WRITE setScaleWifiPairings NOTIFY scaleWifiPairingsChanged)` with `Q_INVOKABLE` helpers `setScaleWifiPairing(QString mac, QString ip, int port)`, `clearScaleWifiPairing(QString mac)`, `scaleWifiPairing(QString mac)`. Backed by `QSettings` keys under `connections/scaleWifiPairings/<mac>/{ip,port,lastSeenIso}`.
- [ ] Wire the new domain into `Settings`: `Q_PROPERTY(QObject* connections READ connectionsQObject CONSTANT)` + typed inline `SettingsConnections* connections() const`. Construct `m_connections` in the `Settings::Settings()` constructor as a child.
- [ ] Register in `main.cpp`: `qmlRegisterUncreatableType<SettingsConnections>("Decenza", 1, 0, "SettingsConnectionsType", ...)`. Without this, `Settings.connections.scaleWifiPairings` resolves to `undefined` at runtime.
- [ ] Update `CMakeLists.txt` source list.
- [ ] Smoke test: read/write a pairing from QML in a throwaway test harness; restart the app; confirm the value persists. (Not in CI — manual.)

## Phase 1 — `WifiScaleTransport` (adapter pattern)

- [ ] Create `src/ble/transport/wifiscaletransport.{h,cpp}`. Subclass `ScaleBleTransport`. Internally hold a `QTcpSocket`, a `QByteArray m_rxBuffer`, a `QTimer m_connectTimeout`, and a `QString m_targetIp` / `int m_targetPort`.
- [ ] `connectToDevice(const QBluetoothDeviceInfo& device)` overload: extract the IP and port from a non-BLE device descriptor (we'll use `device.address().toString()` for the IP — chosen because `QBluetoothDeviceInfo` is the existing call surface; alternative is a dedicated overload `connectToHost(QString ip, int port)`). Pick whichever requires fewer changes in `ScaleFactory`.
- [ ] On `QTcpSocket::connected`: emit `connected()`. Start a 2 s `m_connectTimeout` before `connectToHost`; cancel on success, emit `error("connect timeout")` on expiry.
- [ ] On `QTcpSocket::readyRead`: append to `m_rxBuffer`. Drain frames by scanning for the leading `0x03` byte and emitting `characteristicChanged(Scale::Decent::READ, frame)` once a 7-byte frame is fully present. Defensive resync: if `buffer[0] != 0x03`, drop one byte and retry.
- [ ] Implement `discoverServices()` and `discoverCharacteristics()` as immediate emissions (`serviceDiscovered(Scale::Decent::SERVICE)` → `servicesDiscoveryFinished()` → on demand `characteristicsDiscoveryFinished(Scale::Decent::SERVICE)`).
- [ ] `enableNotifications()` is a no-op that synchronously emits `notificationsEnabled(uuid)`.
- [ ] `writeCharacteristic()` writes the bytes to the socket and synchronously emits `characteristicWritten(uuid)`. (Firmware ignores unknown commands today — see proposal.)
- [ ] `readCharacteristic()` is unsupported; emit `error()` if called. (DecentScale doesn't currently call read on the runtime path.)
- [ ] `disconnectFromDevice()` / `isConnected()` map onto `QTcpSocket` state.

## Phase 2 — Provisioning client and STATUS read helper

- [ ] Create `src/ble/scales/decenzaprovisioningclient.{h,cpp}`. Owns its own `QLowEnergyController` constructed from a `QBluetoothDeviceInfo`. Stays alive only for the duration of one provisioning session; emits `provisioningCompleted(QString ip)` / `provisioningFailed(QString reason)` and self-deletes via `deleteLater()` on completion.
- [ ] Service UUID `0000feed-decc-1000-8000-00805f9b34fb`. Characteristic UUIDs from proposal: fee1 SSID write, fee2 passphrase write, fee3 control byte, fee4 STATUS notify+read.
- [ ] Sequence: connect → discover → subscribe to fee4 first → write SSID to fee1 → write passphrase to fee2 → write `0x01` to fee3 → emit STATUS state changes upward → on `state==2 (Connected)` extract IP bytes, emit success → on `state==3 (Failed)` extract `err` byte, emit failure.
- [ ] Add a separate static helper `DecenzaProvisioningClient::readWifiStatusOnce(QLowEnergyController*, callback)` for the opportunistic IP-refresh path used by `ScaleFactory` after a successful BLE connect on a Decenza scale. (Reuses an existing controller rather than spinning up a fresh one — that's the only place the "share" path is OK.)
- [ ] "Forget Wi-Fi" path: dedicated short-lived client that writes `0x02` to fee3 and self-deletes.

## Phase 3 — Transport selection in `ScaleFactory`

- [ ] In `ScaleFactory` (or wherever the Decent Scale transport is currently constructed for the Decenza name match), add the branch: if `Settings.connections.scaleWifiPairing(mac).ip` is non-empty, attempt Wi-Fi first with a 2 s timeout. On success, hand the `WifiScaleTransport` to `DecentScale`. On failure, fall back to constructing the BLE transport.
- [ ] On a successful BLE connect for a Decenza-named device, schedule one fee4 STATUS read via `DecenzaProvisioningClient::readWifiStatusOnce`. If state==Connected and the IP differs from the stored value, update `Settings.connections` with the fresh IP and update `lastSeenIso` to `QDateTime::currentDateTimeUtc().toString(Qt::ISODate)`. Do NOT switch transports mid-session — the refresh is for next launch.

## Phase 4 — Provisioning + calibration UI

- [ ] Add new QML files: `qml/pages/settings/DecenzaWifiSetupDialog.qml` and `qml/pages/settings/DecenzaCalibrationDialog.qml`. Register both in `CMakeLists.txt`.
- [ ] Provisioning dialog: BLE scan filter (`name.toLowerCase().includes("decenza")`), device list, SSID + password fields (use `StyledTextField` per CLAUDE.md), "Connect" button that drives `DecenzaProvisioningClient`. Live status line tied to fee4 state transitions ("Connecting…" → "Connected — 192.168.x.x" / "Failed (wrong password / no AP / timeout)"). All user-visible text via `Tr` or `TranslationManager.translate` per CLAUDE.md.
- [ ] Calibration dialog: numeric input for "Reference weight (g)" (validator: 0.1–10000.0), instructions "Place the reference weight on the scale and tap Calibrate.", a Calibrate button that emits the calibration command via the active transport's `writeCharacteristic`.
- [ ] Wire the new entry points into `qml/pages/settings/SettingsConnectionsTab.qml`. Add a "Forget Wi-Fi" button per pairing (driven by the `0x02 → fee3` short-lived client).
- [ ] All interactive elements get `Accessible.role` / `Accessible.name` / `Accessible.focusable: true` / `Accessible.onPressAction` per CLAUDE.md's accessibility rules. Prefer `AccessibleButton` / `AccessibleMouseArea`.
- [ ] No hardcoded colors / fonts / spacing — `Theme.qml` only.

## Phase 5 — Calibration command coordination

- [ ] Coordinate with the DecenzaScale firmware repo on the calibration command wire format. Tentative proposal (subject to firmware approval, captured in `design.md`): `0x03 0x10 [int16BE knownWeightDecigrams] 0x00 0x00 [xor]`. Command byte `0x10` is currently unused in the Decent Scale protocol per `decentscale.cpp`'s sendCommand consumers (`0x0A` LCD/LED, `0x0B` timer, `0x0F` tare).
- [ ] Once firmware locks the format: implement the command emission in `DecentScale::calibrateToKnownWeight(double grams)` and expose as a `Q_INVOKABLE` slot on `ScaleDevice` (so the UI binds to it without knowing it's a Decent Scale specifically).
- [ ] If firmware coordination slips, the calibration UI ships disabled with a "Coming soon — requires firmware update" message rather than blocking the rest of the change.

## Phase 6 — Tests

- [ ] `tests/tst_wifiscaletransport.cpp` covering:
  - Aligned-frame happy path: server sends two complete 7-byte frames in one write → transport emits two `characteristicChanged` with correct payloads.
  - Split frame: server sends 4 bytes + 3 bytes → transport buffers the partial and emits one `characteristicChanged` after the second write.
  - Mid-stream misalignment: server sends `[0xFF 0xFF 0x03 ...frame... 0x03 ...frame...]` → transport drops the two leading garbage bytes, emits exactly two frames.
  - Connect timeout: point the transport at an unreachable host with a 2 s timeout and assert `error()` fires within ~2.1 s.
  - Write passthrough: `writeCharacteristic` of a tare frame results in those exact bytes arriving on the server side.
- [ ] Run the existing `tests/tst_scaleprotocol.cpp` unchanged — frame parsing must remain identical regardless of transport.
- [ ] Build with `-DBUILD_TESTS=ON`; full ctest suite must pass.

## Phase 7 — Manual smoke

- [ ] On a real provisioned scale: cold-launch Decenza, observe that the live weight appears in < 1 s without any BLE pairing prompt.
- [ ] Block the scale's Wi-Fi (yank power cycle, or use "Forget Wi-Fi"); confirm Decenza falls back to BLE within ~3 s and weight resumes.
- [ ] Re-provision via the new dialog; confirm `Settings.connections.scaleWifiPairings` populates and persists across an app restart.
- [ ] Calibration: place a 100 g reference weight, run the calibration wizard, restart the scale, confirm a 100 g reading is reported as 100 ± 0.1 g.

## Phase 8 — Docs & archive

- [ ] Write `docs/CLAUDE_MD/DECENZA_SCALE_WIFI.md` covering: wire protocol summary (link to firmware repo as authoritative), transport-selection flow chart, persistence schema, fallback semantics, calibration coordination notes.
- [ ] Add it to the `CLAUDE.md` reference-document index table with a one-line "When to read" entry.
- [ ] After deployment is stable for ≥ 1 release cycle:
  - [ ] If `openspec` CLI is installed in CI/dev: run `openspec validate add-decenza-wifi-transport --strict --no-interactive` and resolve any issues.
  - [ ] Update `specs/` to reflect as-shipping behavior.
  - [ ] Move `changes/add-decenza-wifi-transport/` to `changes/archive/<YYYY-MM-DD>-add-decenza-wifi-transport/`.
