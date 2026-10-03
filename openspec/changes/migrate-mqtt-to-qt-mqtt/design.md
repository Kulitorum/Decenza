## Context

See proposal.md for motivation. Constraints that shape the approach:

- `MqttClient` (`src/network/mqttclient.{h,cpp}`, ~1,700 lines) wraps Paho's async C API. Paho calls back on its own threads, so every callback is re-emitted as an `internal*` signal and queued to the main thread. The reconnect state machine, status text, Home Assistant discovery and state publishing are Decenza's own code and survive the swap.
- Qt publishes Qt MQTT binaries only to commercial licensees: `qtmqtt` (with `qtcoap` and `qtopcua`) is absent from the open-source 6.12.0 online repository on every platform, and from the source tarballs. The `v6.12.0` git tag exists on code.qt.io and on Qt's GitHub mirror as the same tag object (`7d70078f`, commit `f74f931f`).
- Qt MQTT's module build uses `qt_internal_add_module()`, which needs Qt's internal build machinery and so cannot be `add_subdirectory()`'d into an application. The library proper is 13 `.cpp` files (~6,100 lines) depending on `Qt::Core`, `Qt::CorePrivate` and `Qt::Network`, plus one generated header (`qtmqttexports.h`).
- `.local` brokers are resolved by Decenza's own mDNS lookup on Android. The client therefore connects to an IP while the user configured a name, which matters once TLS verifies certificates.

## Goals / Non-Goals

**Goals:**
- Existing MQTT setups keep working with no user action: same client ID, topics, entities and settings.
- Qt MQTT always matches the Qt version being built against, with no version written in the repository.
- The client survives indefinite reconnecting without growing descriptors.

**Non-Goals:**
- MQTT 5 features, WebSocket transport, mutual TLS (client certificates).
- de1app wire compatibility (single JSON state document), which would be its own change.
- Making two tablets on one broker coexist. The default base topic and discovery config topics would collide, but changing them would break existing setups.
- Remote start of any operation. On a DE1 with a group head controller the firmware still wants physical confirmation (see peterthepeter/decaid-mqtt-bridge 0.2.6 changelog).

## Decisions

### 1. Build Qt MQTT as a narrow static library from a version-derived git tag
`cmake/qtmqtt.cmake` declares the source with FetchContent, `GIT_REPOSITORY https://github.com/qt/qtmqtt.git`, `GIT_TAG v${Qt6_VERSION}`, `GIT_SHALLOW TRUE`, and `SOURCE_SUBDIR` pointed at a path that does not exist, so FetchContent populates the source without running upstream's CMakeLists. A `decenza_qtmqtt` static library then compiles the `src/mqtt/*.cpp` list directly:
- a build-dir include tree `QtMqtt/` holding the public headers (copied with `configure_file(COPYONLY)`) plus a generated `qtmqttexports.h` that defines `Q_MQTT_EXPORT` empty, for a static build;
- `AUTOMOC` on, and the defines upstream's `.cmake.conf` adds (`QT_NO_QASCONST`, `QT_NO_FOREACH`, `QT_NO_SINGLE_ARGUMENT_QHASH_OVERLOAD`);
- no WebSocket transport sources and no `QT_MQTT_WITH_WEBSOCKETS`, so a later `find_package(WebSockets)` cannot change what is compiled.

**Version guard:** after population, read `QT_REPO_MODULE_VERSION` from the fetched `.cmake.conf` and `FATAL_ERROR` unless it equals `Qt6_VERSION`. Only that field is compared: upstream's `QT_REPO_MODULE_PRERELEASE_VERSION_SEGMENT` still reads `alpha1` on the `v6.12.0` release tag. A tag that does not exist fails the FetchContent clone, and a wrapper message names the Qt version and the module. A stale `_deps` checkout after a Qt bump is re-fetched, because the tag changed; a `FETCHCONTENT_SOURCE_DIR_QTMQTT` override pointing at an old tree is caught by the guard.

*Alternatives:* `install-qt-action` `source: true` (downloads all of Qt's sources, gigabytes per job); a tarball (none is published for qtmqtt); `qt-configure-module` against each installed Qt (a separate build-and-install step per platform, including cross-compiled Android and iOS, and an installed Qt tree modified, which `build-config` forbids); vendoring the sources into the repo (a version written down that a Qt bump must remember to move). The GitHub mirror is chosen over code.qt.io because CI already depends on GitHub, and the tag is identical on both.

### 2. One long-lived `QMqttClient`; Decenza owns the socket
`MqttClient` holds one `QMqttClient` for its lifetime. Each attempt creates a fresh `QTcpSocket` or `QSslSocket`, owned by `MqttClient`, and connects it:
- plain: `connectToHost(address, port)`;
- TLS: `connectToHostEncrypted(address, port, configuredHostName)`, so the certificate is checked against the name the user entered even when `address` came from mDNS.

On `connected()` / `encrypted()` the socket is handed over with `QMqttClient::setTransport(socket, AbstractSocket|SecureSocket)` followed by `connectToHost()`. Qt MQTT sees an already-connected transport and only sends CONNECT (`qmqttconnection.cpp:128-142, 205-212, 271-308`). Three Qt MQTT behaviours shape the ownership:
- A handed-over socket is wired only to `aboutToClose`/`readyRead`, and a remote close emits `disconnected()` but not `aboutToClose()` (`qabstractsocket.cpp`, `disconnectFromHost()`). Decenza therefore `close()`s the socket on `disconnected()`/`errorOccurred()`, which is what tells Qt MQTT the session is gone.
- `setTransport()` disconnects from the previous socket by pointer (`qmqttconnection.cpp:102-126`), and `~QMqttConnection` writes DISCONNECT to the transport (`:85-90`). A handed-over socket is therefore kept until the next hand-over replaces it, and the client is deleted before the sockets. A closed socket holds no descriptor, so this keeps at most one closed `QTcpSocket` object alive.
- `closeConnection()` calls `m_transport->disconnect()` (`:870`), dropping Decenza's own connections on the socket, and sets the error before the state (`qmqttclient.cpp:1103-1110`). The outcome is read from `error()` when `stateChanged(Disconnected)` arrives.

Why not let Qt MQTT open the socket? `QMqttClient::connectToHostEncrypted()` passes an empty peer name, and `QSslSocket::connectToHostEncrypted()` stores it as the verification name (`qmqttclient.cpp:594-600`, `qsslsocket.cpp:543`), so TLS to an mDNS-resolved IP would verify against the IP. Qt MQTT also collapses every socket error to `TransportInvalid` (`qmqttconnection.cpp:824-828`). Owning the socket keeps the `QAbstractSocket::SocketError` and the TLS error list for the status text.

`connectToHost()` with an IP literal can emit `errorOccurred` synchronously on Unix (`qnativesocketengine_unix.cpp`, `qabstractsocket.cpp`), so the error handler must not assume the attempt has returned. It sets state and schedules the retry through the existing timer path, never re-entering `connectToBroker()`.

This also removes the leak by construction: no global init/terminate cycle, and every socket Decenza creates is one it closes. The spec's descriptor requirement is verified on device with `debug_get_fds` inode diffing, as in the investigation.

### 3. Protocol fallback
Connect with `MQTT_3_1_1`. If `errorChanged(InvalidProtocolVersion)` arrives on the first attempt to a host, set `MQTT_3_1` and reconnect once immediately, without spending a retry. This matches Paho's `MQTTVERSION_DEFAULT`.

### 4. Status text
One function maps (`QMqttClient::ClientError`, `QAbstractSocket::SocketError`, `QList<QSslError>`) to the user-facing reason, replacing `connackReasonText()`. The CONNACK wordings already shown to users ("bad username or password", …) are kept verbatim. "Connected" is set only after both command subscriptions report `QMqttSubscription::Subscribed`. A subscription that reports `Error` (SUBACK 0x80) sets "Connected, but the broker refused <topic> — check this account's permissions". This is event-based, with no verification timer. A publish acknowledgement is deliberately not used as proof: Mosquitto acknowledges ACL-denied publishes under 3.1.1.

### 5. Clean exit, and Qt MQTT's blocking disconnect
Qt MQTT's graceful disconnect writes DISCONNECT and then blocks in `waitForBytesWritten(30000)` (`qmqttconnection.cpp:716-741`). That is instant while the link moves and up to 30 s of frozen UI when it does not. So on destruction, and on a user or settings disconnect, Decenza takes the graceful path (publish `offline` retained at QoS 1, then `disconnectFromHost()`) only when the socket has no write backlog (`bytesToWrite() == 0`). Otherwise it `abort()`s, and the broker's last will reports `offline` after the keepalive.

### 6. Home Assistant additions
All built by the existing `publishDiscoveryConfig()` path, in one table so discovery cleanup can enumerate it:
- **Availability:** machine entities carry `availability: [base/availability, base/connected]` and scale entities `[base/availability, base/scale_connected]`, with `availability_mode: all` and `payload_available: "true"` for the connection topics. Connection binary sensors keep the single availability topic.
- **Profile select:** `select` with `options` = `ProfileManager::installedProfileTitles()`, `state_topic` = `base/profile`, and a new `command_topic` = `base/profile/select`. The existing `profile/set` takes a filename, but a select sends the title it shows, so `MainController` resolves it with `findProfileByTitle()`. Re-published on `profilesChanged`, and omitted while the list is empty (Home Assistant rejects a select with no options).
`MqttClient` takes plain data from `MainController` (`setProfileTitles()`, `setLastShot()`, `onEspressoCycleStarted(bool maintenance)`, and the `profileTitleSelectRequested`/`lastShotRequested` signals) instead of reaching into `ProfileManager` and `ShotHistoryStorage`, so `tst_mqttclient` links neither.
- **Last shot:** retained JSON on `base/last_shot` with sensors using `value_template`. On `shotPersisted(id, …)`, `MainController` reads the row back with `ShotHistoryStorage::requestShot(id)` for dose, profile and time. On connect, the client asks for the most recent shot the same way. Both answers are filtered by id, because `shotReady` and `mostRecentShotIdReady` are broadcasts. Missing fields are omitted.
- **Shot events:** non-retained JSON on `base/event/shot`, with an `event` entity whose `event_types` are `started`, `finished` and `aborted`. Sources: `MachineState::espressoCycleStarted` → `started`; `shotPersisted` → `finished`; `shotDiscarded` / `shotAbortedNoScale` → `aborted`. Runs whose profile is `Profile::isMaintenanceBeverageType()` emit nothing. A per-cycle flag ensures at most one terminal event.
- **Stop:** `button` with `payload_press: stop` on `base/command`. `handleCommand("stop")` emits `stopRequested()` only in the espresso (including preheating), steam, hot-water and flush phases. `MainController` then makes the same calls as the app's own Stop buttons: `requestIdle()` for steam (stop and purge, the single-tap `SteamPage` path), otherwise `stopOperation()`, recording `"manual"` as the stop reason for an espresso as `EspressoPage.stopAndGoBack()` does. The phase set is narrower than MCP `machine_stop`, which also stops descale and clean: those are unattended maintenance cycles a dashboard tap should not interrupt.
- **Restart recovery:** subscribe to `homeassistant/status`; on `online`, re-publish discovery and force-publish all state, bypassing the last-published dedupe.
- **Cleanup:** the published discovery topic list is persisted in `SettingsMqtt` as an internal property, not a user setting. Disabling discovery, or a topic dropping out of the set, publishes an empty retained payload to each removed topic and rewrites the list.

### 7. Settings and surfaces
`SettingsMqtt` gains `mqttUseTls` (bool, default false) and `mqttCaCertificate` (PEM text, default empty). In both the app's Home Automation tab and the ShotServer settings page: a TLS switch (when turned on with port 1883, the UI offers 8883), and a CA certificate field (paste PEM, or pick a file in the app or upload one on the web). The PEM is validated with `QSslCertificate::fromData()` before saving, and an unparseable certificate is rejected with a message. Settings changes still reconnect via `onSettingsChanged()`. With one long-lived client, repeated setter signals in a single save no longer cost anything beyond a reconnect.

## Risks / Trade-offs

- [Qt MQTT uses Qt private headers] → It is always compiled against the exact Qt being built (Decision 1), so private-ABI drift cannot occur. A Qt version whose private API breaks Qt MQTT is a Qt MQTT bug fixed at the same tag.
- [Configure needs network to fetch qtmqtt] → The same as Paho today. Offline developers can set `FETCHCONTENT_SOURCE_DIR_QTMQTT` to `~/Qt/<ver>/Src/qtmqtt`, which the version guard checks.
- [Machine and scale entities become unavailable when the device disconnects] → Intended, and stated in the proposal. A Home Assistant automation that read stale temperatures now sees `unavailable`. Called out in the release notes.
- [Stop during a shot with a GHC-equipped DE1] → the app's own Stop buttons use the same calls, but a remote stop with an active GHC is unverified. Verified on Jeff's DE1 (GHC installed) before merge.
- [Larger discovery payload] → Adds 1 select, 6 last-shot sensors, 1 event and 1 button to the existing 21 entities. Discovery is retained and sent once per connect, so the cost is negligible.

## Migration Plan

No user migration: settings keys, client ID and topics are unchanged, and the new settings default to today's behaviour. Rollback is a revert. Paho and Qt MQTT produce the same broker-visible session, so a downgraded app reconnects as the same client with the same entities. Entities added by the new version stay retained in Home Assistant after a downgrade until the user deletes them, since the old version has no cleanup.
