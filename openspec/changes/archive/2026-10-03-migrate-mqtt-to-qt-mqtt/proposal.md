## Why

Decenza's MQTT client leaks two file descriptors every time it reconnects. On 2026-09-30 the Home Assistant broker went away, and the tablet's open socket count climbed by exactly 2 per 15-minute retry (244 → 290 sockets) until the app was restarted. On-device inode diffing traced it to Paho C v1.3.16: `Socket_outInitialize()` opens a `socketpair()` that `Socket_outTerminate()` never closes, and `MqttClient::connectWithHost()` destroys and recreates the only client on every attempt, which re-runs that init. Upstream has the bug open twice ([eclipse-paho/paho.mqtt.c#1676](https://github.com/eclipse-paho/paho.mqtt.c/issues/1676), [#1705](https://github.com/eclipse-paho/paho.mqtt.c/issues/1705)) with no maintainer response. One person maintains Paho, no issue has been closed since April, and no fork is active.

Qt MQTT is maintained by The Qt Company, runs on the Qt event loop Decenza already uses, and is GPL-3.0 like Decenza. Moving to it fixes the leak and drops a C library that delivers its callbacks on its own threads. Testing the move also showed that Decenza's MQTT identity is not standard. The client ID is copied by backup and migration, so a restored backup gives a second install the same ID, and the broker then disconnects one client each time the other connects (MQTT 3.1.1 §3.1.4; Home Assistant's docs: "the client ID must be unique"). The discovery topics (`homeassistant/sensor/de1_state/config`) carry no device identity, so two installs on one broker overwrite each other's entities. Home Assistant's documented practice is a unique client ID per connection and device-based discovery for devices with many entities.

The rewrite is also the moment to close gaps that two Decaid MQTT plugins ([meldavy/decaid-mqtt-plugin](https://github.com/meldavy/decaid-mqtt-plugin), [peterthepeter/decaid-mqtt-bridge](https://github.com/peterthepeter/decaid-mqtt-bridge)) have already shown users want: encrypted connections, a profile picker, shot results, shot events, and a remote stop.

## What Changes

**Library swap:**
- Replace the Paho C client inside `MqttClient` with Qt MQTT's `QMqttClient`. State and command topics, payloads, last-will message, QoS, retain flags, keepalive, clean session and reconnect cadence stay the same. If a broker rejects MQTT 3.1.1, the client falls back to 3.1, as Paho did.
- Compile Qt MQTT in-tree from its unmodified upstream source. Qt publishes Qt MQTT binaries only to commercial licensees, so the open-source installer and `install-qt-action` cannot provide it. CMake fetches the source at the `v<version>` tag matching the Qt version found at configure time (`Qt6_VERSION`), so every machine and CI job builds the Qt MQTT that matches its own Qt, and a Qt bump moves it with no edit. If no tag matches, or the fetched source declares a different version, configure fails.
- Remove the Paho FetchContent block, its SSL configuration, and every `paho-mqtt3a(s)-static` link. That includes the test targets that link Paho only because `mqttclient.h` includes `<MQTTAsync.h>` (through `maincontroller.h`). After the change only targets that compile `mqttclient.cpp` link MQTT.
- Keep Decenza's own mDNS lookup for `.local` brokers on Android.

**Identity and discovery (the standard design; requires Home Assistant 2024.11 or newer):**
- **Client ID:** generated per install from a random value and never exported by backup or device migration. An install upgrading from an earlier version gets a fresh one, which also separates installs that already share an ID.
- **Home Assistant device ID:** a new stable identifier that builds every entity `unique_id` and the device identifier, and travels with backup and migration so a replacement tablet stays the same Home Assistant device. On upgrade it is seeded from the old client ID, so `unique_id`s, entity IDs and the device do not change. A "New device ID" action covers a second live install restored from the same backup.
- **Device-based discovery:** one retained message at `homeassistant/device/<deviceId>/config` replaces the per-entity topics. Existing installs move over once using Home Assistant's `migrate_discovery` procedure, which keeps entity IDs, names and customizations. Two installs on one broker no longer disconnect each other or overwrite each other's entities.

**Additions (all additive or opt-in):**
- **Encrypted connection (TLS):** an opt-in toggle, with certificate checking always on and an optional custom CA certificate for home brokers that use self-signed certificates. Available on every platform; Paho was built without TLS on Android and iOS.
- **Home Assistant restart recovery:** when Home Assistant announces `homeassistant/status = online`, re-send discovery and current state.
- **Profile dropdown:** a Home Assistant `select` entity listing installed profiles, kept current as profiles are added or removed. The existing text entity stays.
- **Last-shot summary:** after each saved shot, a retained summary (finish time, duration, dose, yield, ratio, profile) with matching sensors.
- **Shot events:** a Home Assistant `event` entity firing `started`, `finished` and `aborted`.
- **Remote stop:** a `stop` command and Home Assistant button that end an espresso, steam, hot-water or flush operation, and are ignored in any other state. No remote start is offered.
- **Discovery cleanup:** turning discovery off removes this install's entities from Home Assistant instead of leaving stale ones, and touches no other install's.
- **Verified connection status:** report "connected" only after the broker has accepted the command subscriptions, and name a refused subscription.
- **Entities follow device connections:** machine entities become unavailable while the DE1 is disconnected, and scale entities while the scale is. This is a deliberate change for existing entities, which today keep showing their last value.

## Capabilities

### New Capabilities
- `mqtt-home-automation`: the broker-facing contract of Decenza's MQTT integration (client and device identity, device-based discovery and its migration, availability, message delivery, reconnect behaviour, status reporting, TLS), the Home Assistant entities and controls listed above, and a guarantee that reconnecting does not consume file descriptors.

### Modified Capabilities
- `build-config`: "Decenza Ships Stock Qt Runtime Binaries" gains a clause permitting an unmodified Qt add-on module that the upstream installer does not provide, compiled from source at the tag matching the Qt version found at configure time, with a configure-time version check, and never placed in the installed Qt tree.

## Impact

- **Code:** `src/network/mqttclient.{h,cpp}` is rewritten onto `QMqttClient`, with Decenza owning the TCP/TLS socket. The Paho static callbacks, the `internal*` cross-thread signals and the Paho failure-code decoding are removed. `MainController` wires the new stop command and the shot lifecycle signals (`shotPersisted`, `shotDiscarded`, espresso cycle start) to the client. A Paho reference in `src/network/shotserver_settings.cpp` is updated.
- **Settings:** new `SettingsMqtt` properties for TLS, the CA certificate and the Home Assistant device ID, plus internal bookkeeping (published discovery components, a discovery-migrated marker). `SettingsSerializer` stops exporting and importing the client ID and carries the device ID instead. TLS defaults off, so existing setups stay unencrypted until the user turns it on.
- **UI:** the Home Automation tab in the app and the MQTT section of the ShotServer settings page gain the TLS toggle, the CA certificate input, the client ID (the app tab never had it) and the device ID with its "New device ID" action. Both surfaces change together.
- **MCP:** `settings_set`/`settings_get` gain the new MQTT settings, so `McpSurfaceVersion` is bumped.
- **Build:** a new narrow static library target built from the Qt MQTT sources, linking `Qt6::Core`, `Qt6::CorePrivate` and `Qt6::Network`. Paho is removed from `CMakeLists.txt` and `tests/CMakeLists.txt`.
- **Tests:** `tst_mqttclient` keeps its reconnect state-machine coverage, its Paho-callback cases are re-expressed against Qt MQTT errors, and it gains coverage for command gating, discovery payloads and status text.
- **CI:** the six release workflows and the nightly sanitizers job need network access to fetch the Qt MQTT source at configure time (the same as Paho today). No Qt module list changes.
- **Docs:** `openspec/config.yaml` (tech stack line), `docs/CPP_COMPLIANCE_AUDIT.md` (MQTT conventions line), and a short wiki manual update for the new user-visible features and the Home Assistant 2024.11 minimum, which the release notes also state.
- **Licensing:** Qt MQTT is GPL-3.0-only, which is compatible with Decenza's GPL-3.0.
