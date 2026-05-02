# Change: Add Decenza Scale Wi-Fi Transport, Provisioning, and Calibration GUI

## Why

The Decenza Scale (an ESP32-based scale that emulates the Decent Scale protocol) currently talks to Decenza only over BLE. BLE works but has three real-world drawbacks for live shots:

1. **Discovery is flaky.** Android scan results vary across devices and OS versions; users routinely report "the scale isn't found" until the second or third attempt.
2. **MTU is 23 bytes.** Fine for the 7-byte Decent Scale frames today, but it limits future enhancements and makes link-level retries more expensive.
3. **NOTIFY rate is bounded.** BLE NOTIFY at the connection interval gives ~10 Hz on most stacks but with non-trivial jitter; a TCP socket on local Wi-Fi delivers the same 10 Hz with sub-ms variance.

The DecenzaScale firmware (companion repo at `C:\CODE\DecenzaScale`) has just shipped Wi-Fi support: after a one-time BLE provisioning step, the scale joins the user's home Wi-Fi as an STA and streams the **identical** 7-byte Decent Scale weight frames over a TCP socket on port 8765 at 10 Hz. BLE remains available — both as the provisioning channel and as a runtime fallback.

This change adds the Decenza-side counterpart: a Wi-Fi transport, a BLE-driven provisioning UI, transport-selection logic that prefers Wi-Fi when available, a `SettingsConnections` domain to persist scale↔IP pairings, and a calibration GUI that uses a user-supplied known weight to command the scale to recalibrate (over whichever transport is active). Tare-over-Wi-Fi is wired through the same path.

The `scalefactory.cpp::isDecentScale()` name match for "decenza" already shipped in commit `a0993dd3` and is **not** part of this proposal.

## What Changes

1. **New `WifiScaleTransport`** that adapts a `QTcpSocket` to the existing `ScaleBleTransport` interface. The adapter approach (vs. refactoring `DecentScale` to a smaller transport interface) was chosen because the protocol consumer's signal contract is already transport-agnostic in practice — the methods leak BLE concepts in their *names*, but their *data flow* is opaque byte arrays delivered via `characteristicChanged`. The adapter:
   - Opens TCP to `(ip, 8765)` with a 2 s connect timeout.
   - Buffers incoming bytes and emits one `characteristicChanged(Scale::Decent::READ, frame)` per complete 7-byte frame, **resyncing on the `0x03` header byte** if alignment is ever lost (defensive: TCP is in-order so this only matters under partial-write edge cases).
   - Forwards `writeCharacteristic` payloads to the TCP socket. Firmware ignores unknown command bytes today — heartbeats and LCD writes flow harmlessly until firmware wires the command path.
   - Synthesizes `connectToDevice` / `discoverServices` / `discoverCharacteristics` / `enableNotifications` as no-ops (or as immediate signal emissions) so `DecentScale` can drive the wake sequence unchanged.

2. **New BLE provisioning flow** for the Decenza-vendor service `0000feed-decc-1000-8000-00805f9b34fb` (4 characteristics: SSID write, passphrase write, control byte write, status notify). The provisioning client SHALL spin up a dedicated short-lived `QLowEnergyController` separate from any runtime scale connection — keeps the runtime transport's hot path free of provisioning code and lets the user provision a scale that is currently not the active one.

3. **Transport selection at scale-creation time.** When `ScaleFactory` decides to connect to a Decenza Scale, it consults `Settings.connections.scaleWifiPairings` for a stored `(mac → last_known_ip)`. If present, it constructs a `WifiScaleTransport` and attempts `connectToHost(ip, 8765)` with a 2 s timeout. On success the runtime path is Wi-Fi. On failure (timeout, refused, host unreachable) the factory falls back to BLE. After a successful BLE connect, the runtime path opportunistically reads the provisioning service's STATUS characteristic (`0xfee4`) once, and, if state == Connected with a fresh IP, updates `Settings.connections.scaleWifiPairings` so the next launch self-heals around DHCP shuffles.

4. **New `SettingsConnections` domain sub-object** under `Settings.connections` per CLAUDE.md's settings architecture. Initial surface is just `scaleWifiPairings` — a `QVariantMap` keyed by lowercase MAC address with `{ ip, port, lastSeenIso }` values. The 13th domain sub-object; requires the `qmlRegisterUncreatableType` registration in `main.cpp` per the existing pattern.

5. **Provisioning UI** in `qml/pages/settings/SettingsConnectionsTab.qml`: "Set up Decenza Scale Wi-Fi" entry → BLE scan filtered to `name.toLower().contains("decenza")` → SSID/passphrase form → status display tied to fee4 STATUS notifications → "Connected — 192.168.x.x" or error state. A per-pairing "Forget Wi-Fi" button writes `0x02` to fee3 to clear NVS on the scale and removes the pairing from `Settings.connections`.

6. **Calibration GUI** for the Decenza Scale, accessible from the same `SettingsConnectionsTab.qml` once a scale is paired. Two-step wizard: (a) user enters their known reference weight in grams (e.g. 100.0 g, 132.0 g — value is stored only for the duration of the wizard, not persisted), (b) user places the weight on the scale and confirms. Decenza emits a calibration command frame (a Decent-Scale-protocol-compliant write, with the known-weight encoded in the payload — exact wire bytes are coordinated with the firmware repo and tracked in `design.md`) over whichever transport is active. The same calibration path works over BLE *and* Wi-Fi because the underlying transport interface is identical.

7. **Tare-over-Wi-Fi.** Tare is already a Decent Scale write frame (`0x03 0x0F 0x01 0x00 ... + xor`). Sending it via `WifiScaleTransport::writeCharacteristic` is a one-line ride along the existing path — but firmware does not currently parse incoming TCP commands. The Decenza-side write path is wired now; the firmware change to honor it is a follow-up tracked by the companion repo.

8. **Documentation.** A new `docs/CLAUDE_MD/DECENZA_SCALE_WIFI.md` covering the wire protocol, the transport-selection logic, the IP refresh behavior, and the calibration command coordination point. Linked from the main `CLAUDE.md` index table.

## Impact

**Affected specs:**
- `decenza-scale-connectivity` — **new capability**. All of the runtime transport, provisioning, transport selection, calibration GUI, and command-emission requirements live here.
- `settings-architecture` — **modified**. The 12-domain set grows to 13 with `SettingsConnections`. The "Settings Domain Decomposition" requirement is updated to add `SettingsConnections` to the canonical domain list.

**Affected code:**
- New: `src/ble/transport/wifiscaletransport.{h,cpp}` — the TCP adapter.
- New: `src/ble/scales/decenzaprovisioningclient.{h,cpp}` — short-lived `QLowEnergyController` that handles fee1/fee2/fee3/fee4 writes and STATUS notifications.
- New: `src/core/settings_connections.{h,cpp}` — the 13th domain sub-object.
- Touched: `src/ble/scales/scalefactory.{h,cpp}` — Wi-Fi-first transport selection for Decenza scales; opportunistic STATUS read after BLE connect.
- Touched: `src/core/settings.{h,cpp}` — adds the `connections()` accessor + `Q_PROPERTY`. No properties move TO `Settings`.
- Touched: `src/main.cpp` — `qmlRegisterUncreatableType<SettingsConnections>(...)` registration.
- Touched: `qml/pages/settings/SettingsConnectionsTab.qml` — provisioning entry point, calibration entry point, "Forget Wi-Fi" button.
- New QML: `qml/pages/settings/DecenzaWifiSetupDialog.qml`, `qml/pages/settings/DecenzaCalibrationDialog.qml`.
- Touched: `CMakeLists.txt` — register all new sources + QML files.
- New: `tests/tst_wifiscaletransport.cpp` — unit tests against an in-memory `QTcpServer` covering aligned frames, mid-stream resync, partial trailing frames, and command write-through.
- Touched: `CLAUDE.md` — add the new doc to the reference-document index.
- New: `docs/CLAUDE_MD/DECENZA_SCALE_WIFI.md`.

**User-visible behavior:**
- Decenza Scale users who provision Wi-Fi once get faster, more reliable connection on every subsequent app launch — no BLE pairing dance during the time-critical pre-shot moment.
- BLE-only users see no change.
- Adds a per-scale calibration button in Settings → Connections.

**Migration:**
- New `SettingsConnections` domain reads from a fresh `QSettings` group (`connections/...`); no existing settings to migrate.
- No database schema change.
- No version bump beyond the standard CI version-code bump.

**Risk:** medium-low. The runtime path is gated by "Wi-Fi pairing exists for this MAC?" — users who never provision are unaffected. The fallback-to-BLE-on-Wi-Fi-failure path means a stale pairing produces a 2 s connect-timeout penalty on the next app launch (then BLE works as before, and the IP gets refreshed). The riskiest piece is the calibration command, which requires firmware-side coordination — that's tracked in `design.md` as an explicit blocker for the calibration sub-feature only; everything else can ship without it.

**What this proposal does not address:**
- Captive portal or AP-mode provisioning — firmware doesn't support it.
- Encryption of stored Wi-Fi credentials beyond what `QSettings` already provides on the host platform. The firmware stores them in NVS in the clear; we are not pretending this is bank-grade.
- BLE bonding for the provisioning writes — the firmware currently accepts cleartext writes. If Qt's BLE stack triggers pairing automatically on a particular OS, fine; we don't force it.
- Multi-scale Wi-Fi (one user with two Decenza scales paired simultaneously). The data model supports it (the map is MAC-keyed) but the UI doesn't go out of its way to expose it.
- Tare-over-Wi-Fi being honored end-to-end — that requires a firmware-side change that lives in the DecenzaScale repo, not this one. Decenza wires the write path; firmware will land the receiver in a follow-up.
