# Spec Delta: decenza-scale-connectivity

## ADDED Requirements

### Requirement: Wi-Fi Runtime Transport for Decenza Scale

The system SHALL provide a `WifiScaleTransport` that adapts a `QTcpSocket` to the existing `ScaleBleTransport` interface so that the Decent Scale protocol consumer (`DecentScale`) can drive a Decenza Scale over a TCP connection without modification.

#### Scenario: TCP frames are surfaced as BLE-shaped notifications
- **WHEN** the firmware writes a 7-byte Decent Scale frame on the TCP socket
- **THEN** `WifiScaleTransport` SHALL emit `characteristicChanged(Scale::Decent::READ, frame)` exactly once per complete frame
- **AND** the byte payload SHALL be byte-identical to the bytes received on the wire
- **AND** the protocol consumer SHALL parse it via the same `parseWeightData` path used for BLE notifications

#### Scenario: Synthetic BLE-shaped events on connect
- **WHEN** the TCP socket reaches the connected state
- **THEN** the transport SHALL emit `connected()`, then `serviceDiscovered(Scale::Decent::SERVICE)` on `discoverServices()`, then `servicesDiscoveryFinished()`, then `characteristicsDiscoveryFinished(Scale::Decent::SERVICE)` on `discoverCharacteristics()`
- **AND** `enableNotifications()` SHALL synchronously emit `notificationsEnabled(uuid)` and otherwise no-op
- **AND** the consumer's wake sequence (heartbeat, LCD, NOTIFY enable timed singleShots) SHALL execute identically to the BLE path

#### Scenario: Header-byte resync on misalignment
- **WHEN** the receive buffer's first byte is not `0x03` (Decent Scale frame header)
- **THEN** the transport SHALL drop one byte from the front of the buffer and retry
- **AND** the transport SHALL log a single warning per resync occurrence (not per dropped byte)

#### Scenario: Partial frame buffering
- **WHEN** the socket delivers fewer than 7 bytes in a single `readyRead`
- **THEN** the transport SHALL retain the partial frame in its receive buffer
- **AND** SHALL emit `characteristicChanged` only after the remaining bytes arrive

#### Scenario: Connect timeout
- **WHEN** the TCP `connectToHost` does not reach the connected state within 2 seconds
- **THEN** the transport SHALL emit `error("connect timeout")` and abort the connection attempt
- **AND** the consumer SHALL be free to fall back to BLE without leaked socket state

#### Scenario: Write passthrough
- **WHEN** the consumer calls `writeCharacteristic(SERVICE, WRITE, bytes)`
- **THEN** `WifiScaleTransport` SHALL write those exact bytes to the TCP socket
- **AND** synchronously emit `characteristicWritten(WRITE)` so the consumer's write-completion handlers fire

### Requirement: BLE Provisioning Service Client

The system SHALL provide a `DecenzaProvisioningClient` that uses a dedicated short-lived `QLowEnergyController` to write Wi-Fi credentials to the Decenza Scale's provisioning service `0000feed-decc-1000-8000-00805f9b34fb` and to read the resulting connection state.

#### Scenario: Dedicated controller per provisioning session
- **WHEN** the user initiates a provisioning session
- **THEN** the client SHALL construct a fresh `QLowEnergyController` instance
- **AND** SHALL NOT reuse the runtime scale transport's controller
- **AND** SHALL self-delete via `deleteLater()` on completion (success or failure)

#### Scenario: Successful provisioning hand-off
- **WHEN** the user submits an SSID and passphrase via the provisioning dialog
- **THEN** the client SHALL subscribe to characteristic `0000fee4-decc-1000-8000-00805f9b34fb` for STATUS notifications first
- **AND** SHALL write the UTF-8 SSID to `0000fee1-decc-...`
- **AND** SHALL write the UTF-8 passphrase to `0000fee2-decc-...`
- **AND** SHALL write `0x01` to `0000fee3-decc-...` to start STA association
- **AND** on a STATUS notification with `state == 2` (Connected) SHALL emit `provisioningCompleted(QString ip)` carrying the dotted-decimal IP from STATUS bytes 2–5

#### Scenario: Failed provisioning surfaces error code
- **WHEN** STATUS reports `state == 3` (Failed)
- **THEN** the client SHALL emit `provisioningFailed(QString reason)` where `reason` is a translated string derived from the `err` byte at STATUS index 6
- **AND** SHALL self-delete

#### Scenario: Forget Wi-Fi clears NVS on the scale
- **WHEN** the user activates the "Forget Wi-Fi" affordance for a paired scale
- **THEN** a short-lived client SHALL connect, write `0x02` to `0000fee3-decc-...`, and self-delete
- **AND** the matching pairing entry SHALL be removed from `Settings.connections.scaleWifiPairings`

#### Scenario: Opportunistic STATUS read after BLE connect
- **WHEN** a Decenza-named scale connects successfully via the BLE runtime path
- **THEN** the runtime path SHALL invoke `DecenzaProvisioningClient::readWifiStatusOnce(controller, callback)` exactly once
- **AND** if the STATUS read returns `state == 2 (Connected)` with an IP that differs from the stored pairing
- **THEN** the stored pairing SHALL be updated with the fresh IP and `lastSeenIso` SHALL be set to `QDateTime::currentDateTimeUtc().toString(Qt::ISODate)`
- **AND** the runtime transport SHALL NOT switch from BLE to Wi-Fi mid-session — the refresh is for the next launch only

### Requirement: Transport Selection at Scale Creation Time

The system SHALL prefer Wi-Fi as the runtime transport for a Decenza Scale when a stored Wi-Fi pairing exists for that scale's BLE MAC address, and SHALL fall back to BLE on any Wi-Fi failure.

#### Scenario: Wi-Fi-first when pairing is known
- **WHEN** `ScaleFactory` is asked to construct a transport for a device whose name matches `isDecentScale()` AND the lowercase MAC has a non-empty entry in `Settings.connections.scaleWifiPairings`
- **THEN** the factory SHALL construct a `WifiScaleTransport` and attempt to connect to `(stored.ip, stored.port)`
- **AND** SHALL hand the transport to `DecentScale` on connect success

#### Scenario: BLE fallback on Wi-Fi failure
- **WHEN** the Wi-Fi transport emits `error()` (timeout, refused, host unreachable) within the 2-second timeout window
- **THEN** the factory SHALL discard the failed `WifiScaleTransport`
- **AND** SHALL construct the existing BLE transport and hand it to `DecentScale`
- **AND** SHALL log a single warning identifying the MAC, attempted IP, and reason

#### Scenario: BLE-only path is unaffected for unpaired scales
- **WHEN** the device's MAC has no entry in `Settings.connections.scaleWifiPairings`
- **THEN** the factory SHALL skip the Wi-Fi attempt entirely and construct the BLE transport directly
- **AND** the connection latency SHALL be identical to the pre-change BLE-only behavior

### Requirement: Calibration GUI Using User-Defined Reference Weight

The system SHALL provide a calibration wizard accessible from `SettingsConnectionsTab.qml` that prompts the user for a known reference weight and instructs the scale to recalibrate against it via the active transport.

#### Scenario: User enters reference weight
- **WHEN** the user opens the Decenza Scale calibration dialog
- **THEN** the dialog SHALL present a numeric input labelled "Reference weight (g)" with a validator accepting values in `[0.1, 10000.0]`
- **AND** SHALL display localized instructions ("Place the reference weight on the scale and tap Calibrate") via `Tr` / `TranslationManager.translate`
- **AND** the entered weight SHALL NOT be persisted across wizard sessions

#### Scenario: Calibration command emission
- **WHEN** the user confirms the calibration step
- **THEN** the system SHALL emit a Decent-Scale-protocol-compliant calibration write frame on the active transport's `writeCharacteristic`
- **AND** the frame SHALL encode the reference weight in *decigrams* as a big-endian `int16` at packet bytes 2–3
- **AND** the frame SHALL include a valid XOR checksum at byte 6 (`DecentScaleProtocol::calculateXor`)
- **AND** the same code path SHALL operate over both BLE and Wi-Fi without conditional branches

#### Scenario: Calibration is disabled when firmware does not support it
- **WHEN** firmware coordination on the calibration command wire format is incomplete at ship time
- **THEN** the calibration entry point SHALL be hidden or disabled
- **AND** the user SHALL see a localized "Requires DecenzaScale firmware update" message
- **AND** the rest of the provisioning and runtime transport functionality SHALL ship unaffected

### Requirement: Tare Command Over Wi-Fi

The system SHALL emit the existing Decent Scale tare command over whichever transport is active without conditional branching on transport type.

#### Scenario: Tare write reaches the active transport
- **WHEN** `DecentScale::tare()` is called and the runtime transport is `WifiScaleTransport`
- **THEN** the tare frame (`0x03 0x0F 0x01 0x00 0x00 0x00 [xor]`) SHALL be written to the TCP socket
- **AND** if the connected firmware does not yet honor TCP commands, the bytes SHALL be silently discarded by the firmware with no error surfaced to the user

#### Scenario: Tare write reaches the BLE transport unchanged
- **WHEN** `DecentScale::tare()` is called and the runtime transport is the existing BLE transport
- **THEN** the tare frame SHALL be written via `writeCharacteristic` exactly as it is today
- **AND** behavior SHALL be byte-for-byte identical to pre-change

### Requirement: Persisted Scale Wi-Fi Pairings

The system SHALL persist a map of `(scaleMac → { ip, port, lastSeenIso })` so that the runtime path can resolve the Wi-Fi target without user input on subsequent launches.

#### Scenario: Pairing schema
- **WHEN** a pairing is stored
- **THEN** the key SHALL be the lowercase BLE MAC address of the scale (e.g. `"a0:b1:c2:d3:e4:f5"`)
- **AND** the value SHALL be a `QVariantMap` containing `ip` (`QString`, IPv4 dotted-decimal), `port` (`int`, default 8765), and `lastSeenIso` (`QString`, ISO 8601 with timezone offset)

#### Scenario: Pairing survives app restart
- **WHEN** a user provisions a scale and the app is then restarted
- **THEN** the pairing SHALL be readable via `Settings.connections.scaleWifiPairing(mac)` on the next launch
- **AND** the runtime path SHALL prefer the stored pairing without any user prompt

#### Scenario: Pairing survives DHCP shuffle via opportunistic refresh
- **WHEN** the stored IP is stale and Wi-Fi connect fails, then BLE fallback succeeds
- **THEN** the post-BLE-connect STATUS read SHALL refresh the stored IP if the firmware reports a new one
- **AND** the next launch's Wi-Fi-first attempt SHALL succeed against the refreshed IP
