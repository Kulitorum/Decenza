# mqtt-home-automation Specification

## Purpose
Defines what Decenza's MQTT integration guarantees to a user's broker and to Home Assistant (client and device identity, device-based discovery, availability, message delivery, reconnect behaviour, status reporting, encryption, and the entities and controls it offers) and that it can run indefinitely without consuming system resources.

## Requirements

### Requirement: Unique Client Identity
The app SHALL connect with a client ID that belongs to this install alone: generated from a random value, saved in the MQTT settings and reused across restarts. Backup, restore and device-to-device migration SHALL NOT export or import it. An install upgrading from a version before this change SHALL generate a fresh client ID once. The user MAY set a client ID by hand in the app or on the web settings page.

#### Scenario: Backup restored onto a second device
- **WHEN** a backup from one tablet is restored onto another device and both connect to the same broker
- **THEN** the two SHALL connect with different client IDs, and neither SHALL be disconnected by the other

#### Scenario: Two installs that already share an ID are upgraded
- **WHEN** two installs that were given the same client ID by an earlier backup both upgrade to this version
- **THEN** each SHALL generate its own client ID and stop disconnecting the other

### Requirement: Stable Home Assistant Identity
Every Home Assistant entity `unique_id` and the device identifier SHALL be built from a Home Assistant device ID, not from the client ID. The device ID SHALL be generated once, saved, carried by backup, restore and device migration, and reused across restarts and updates. An install upgrading from a version before this change SHALL take its device ID from its previous client ID, so its `unique_id`s and device identifier do not change. The user SHALL be able to replace the device ID with a new random one from the app and from the web settings page, connected or not. Doing so SHALL NOT clear any discovery message published under the previous device ID, because another install restored from the same backup may share it; a previous device used by this install alone stays in Home Assistant until the user deletes it there, and while it shares this install's base topic it keeps showing this machine's state. A new device ID has no earlier per-entity topics, so the move to device-based discovery SHALL NOT run for it. A backup made before this change carries only a client ID; restoring it SHALL use that client ID as the device ID, because it built that install's `unique_id`s, and SHALL NOT use it as the client ID.

#### Scenario: Upgrading an existing install
- **WHEN** a user who already uses MQTT discovery installs this version
- **THEN** Home Assistant SHALL show the same device with the same entities and entity IDs, with no duplicate device or entity created

#### Scenario: Replacing a tablet
- **WHEN** a user migrates settings to a new tablet and retires the old one
- **THEN** Home Assistant SHALL keep showing the same device and entities, now fed by the new tablet

#### Scenario: Second live install from the same backup
- **WHEN** a second install restored from the same backup chooses "New device ID", before or after connecting
- **THEN** it SHALL appear in Home Assistant as a separate device, and the first install's device and entities SHALL be unaffected

#### Scenario: Replacing a tablet from a backup made before this change
- **WHEN** a backup made by an earlier version is restored onto a new tablet that replaces the old one
- **THEN** the new tablet SHALL take the backup's client ID as its device ID, keep its own client ID, and Home Assistant SHALL keep showing the same device and entities

### Requirement: Broker Message Contract
The app SHALL publish and subscribe as follows:
- State values are published at QoS 0 under the configured base topic, retained only when the user has enabled retained messages. The topics entity availability reads (`availability`, `connected`, `scale_connected`) are always retained, at QoS 1, like the last-will message; otherwise Home Assistant reads the retained `offline` when it subscribes.
- The command and profile-select topics are subscribed at QoS 1.
- The availability topic carries `online` after connecting.
- A last-will message of `offline` is registered on the availability topic, at QoS 1 and retained.
- Home Assistant discovery is published as one device-based discovery message, retained when the user has enabled retained messages (as before; otherwise it is re-sent when Home Assistant restarts), at `homeassistant/device/<device ID>/config` when discovery is enabled. It carries the device and origin information and one component per entity, each with its platform and `unique_id`. This requires Home Assistant 2024.11 or newer.

The connection SHALL use a clean session and a 60-second keepalive. It SHALL use MQTT 3.1.1, and SHALL retry once with MQTT 3.1 if the broker rejects the protocol version.

#### Scenario: Home Assistant sees the app go online
- **WHEN** the app connects to the broker
- **THEN** the availability topic SHALL read `online`
- **AND** commands published to the command topic SHALL reach the app

#### Scenario: The app disappears without disconnecting
- **WHEN** the app loses its connection without sending a disconnect (crash, power loss, network drop)
- **THEN** the broker SHALL publish the retained `offline` last-will message on the availability topic

#### Scenario: Broker only speaks MQTT 3.1
- **WHEN** the broker rejects the connection because it does not support MQTT 3.1.1
- **THEN** the app SHALL connect with MQTT 3.1 instead of reporting a failure

### Requirement: Availability On Clean Exit
On a normal app exit while connected, the app SHALL publish a retained `offline` message on the availability topic before disconnecting.

#### Scenario: User quits the app
- **WHEN** the app quits while connected to the broker
- **THEN** Home Assistant SHALL show the device as unavailable without waiting for the keepalive to expire

### Requirement: Reconnect Cadence
After a lost or failed connection, the app SHALL retry on its own:
- first after 5 seconds, then doubling the delay each attempt, capped at 60 seconds;
- after 10 attempts, every 15 minutes, indefinitely.

While the device has no network, attempts SHALL be deferred and SHALL NOT count toward the 10. A disconnect the user requested SHALL stop retrying.

#### Scenario: Broker restarts briefly
- **WHEN** the broker becomes unreachable and returns within the fast-retry window
- **THEN** the app SHALL reconnect without user action

#### Scenario: Broker is gone for days
- **WHEN** the broker stays unreachable past 10 attempts
- **THEN** the app SHALL keep retrying every 15 minutes until it connects

### Requirement: Reconnecting Does Not Consume System Resources
Repeated connection attempts, whether successful or failed, SHALL NOT grow the app's count of open file descriptors. The count after any number of attempts SHALL return to the count before them, within the noise of unrelated activity.

#### Scenario: Broker unreachable for many retries
- **WHEN** the broker is unreachable and the app makes repeated failed attempts
- **THEN** a file-descriptor census taken after the attempts SHALL show no sockets added that are still open after the attempts, relative to a census taken before them

#### Scenario: Settings saved while connected
- **WHEN** the user saves MQTT settings several times, each save reconnecting the client
- **THEN** the open socket count SHALL NOT increase with each save

### Requirement: Connection Status Is Verified And Explained
The status shown in the app and on the web settings page SHALL read connected only after the broker has accepted the login AND acknowledged the command and profile-select subscriptions. If the broker refuses a subscription, the status SHALL name the refused topic and say that the broker account lacks permission for it.

When a connection attempt fails, the status SHALL state the reason in plain words. A broker rejection SHALL name its cause: unsupported protocol version, client ID rejected, broker unavailable, bad username or password, or not authorized. A transport failure SHALL say which kind it was: host unreachable, connection refused, timed out, name not resolved, or certificate rejected. A bare numeric code SHALL NOT be the only reason shown.

#### Scenario: Account cannot subscribe to commands
- **WHEN** the broker accepts the login but refuses the subscription to the command topic
- **THEN** the status SHALL NOT read connected, and SHALL name the command topic as refused

#### Scenario: Wrong password
- **WHEN** the broker rejects the connection for bad credentials
- **THEN** the status SHALL read that the username or password is wrong

#### Scenario: Broker host unreachable
- **WHEN** the broker host cannot be reached
- **THEN** the status SHALL say the broker could not be reached, and SHALL show the retry schedule

### Requirement: Local Broker Names Resolve On Android
On Android, a broker host ending in `.local` SHALL be resolved through the app's own mDNS lookup before connecting, because the platform resolver does not reliably answer `.local` names. If the lookup finds no address, the app SHALL still try the name as given.

#### Scenario: Home Assistant addressed by its .local name
- **WHEN** the broker host is set to a `.local` name that answers mDNS on the LAN
- **THEN** the app SHALL connect to the address that name resolves to

### Requirement: Encrypted Connection
The user SHALL be able to turn on an encrypted (TLS) connection to the broker. It SHALL be off by default and existing setups SHALL remain unencrypted until the user turns it on. With TLS on:
- the broker's certificate SHALL always be verified, and there SHALL be no option to skip verification;
- verification SHALL use the platform's trusted certificates, plus a CA certificate the user supplies, if any;
- the certificate SHALL be checked against the broker host name the user entered, even when that name was resolved to an address by the app's own lookup.

The TLS setting and CA certificate SHALL be editable both in the app and on the web settings page.

#### Scenario: Cloud broker with a public certificate
- **WHEN** TLS is on and the broker presents a certificate from a publicly trusted authority matching its host name
- **THEN** the app SHALL connect

#### Scenario: Home broker with a self-signed certificate
- **WHEN** TLS is on, the broker's certificate is signed by a private CA, and the user has supplied that CA certificate
- **THEN** the app SHALL connect

#### Scenario: Certificate does not verify
- **WHEN** TLS is on and the broker's certificate cannot be verified
- **THEN** the app SHALL NOT connect, and the status SHALL say the certificate was rejected and why

#### Scenario: TLS to a .local broker on Android
- **WHEN** TLS is on and the broker host is a `.local` name resolved by the app's mDNS lookup
- **THEN** the certificate SHALL be verified against the `.local` name, not the resolved address

### Requirement: Changes That Would Expose The Stored Password Need It Again
While a broker password is stored, a change to the broker host, the port, turning TLS off, or a different CA certificate SHALL NOT be applied from the web settings page unless the password is entered again in the same request, and SHALL NOT be applied through MCP, which cannot carry the password. The app's own settings tab, used by someone at the machine, is not restricted. Each refusal SHALL say which change was refused and where it can be made.

#### Scenario: LAN client turns TLS off through MCP
- **WHEN** an MCP client calls settings_set with mqttUseTls false while a password is stored
- **THEN** the setting SHALL NOT change, and the error SHALL name mqttUseTls and point to the app or the web settings page

#### Scenario: Web page changes the CA with the password re-entered
- **WHEN** the web settings page posts a new CA certificate together with the re-entered password
- **THEN** the change SHALL be applied

### Requirement: Home Assistant Restart Recovery
The app SHALL subscribe to Home Assistant's status topic (`homeassistant/status`). When Home Assistant publishes `online` on it, and discovery is enabled, the app SHALL re-publish its discovery configuration and its current state values.

#### Scenario: Home Assistant restarts with retained messages off
- **WHEN** Home Assistant restarts while the app stays connected and retained messages are off
- **THEN** the DE1 device and its entities SHALL reappear in Home Assistant with current values, without the app reconnecting

### Requirement: Existing Entities Move To Device Discovery
An install that published per-entity discovery topics (`homeassistant/<component>/de1_<object>/config`) under an earlier version SHALL move to device-based discovery once, following Home Assistant's documented procedure: publish `{"migrate_discovery": true}` to each earlier topic, then publish the device-based discovery message, then publish an empty retained payload to each earlier topic. Entity IDs, names and user customizations SHALL survive the move. Once it has completed, the app SHALL NOT publish to the per-entity topics again.

#### Scenario: Upgrade with a customised entity
- **WHEN** a user who renamed a DE1 entity in Home Assistant and uses it in a dashboard upgrades to this version
- **THEN** after the move the entity SHALL keep its entity ID, name and dashboard use, and no per-entity discovery topic SHALL remain retained on the broker

### Requirement: Discovery Cleanup
The app SHALL only ever clear discovery messages belonging to its own device ID. When the user turns discovery off, it SHALL publish an empty retained payload to its device-based discovery topic so Home Assistant removes the device. When an entity leaves the device's set (for example, the profile select when no profiles remain), the app SHALL publish that component once with only its platform, as Home Assistant's procedure requires, before leaving it out.

#### Scenario: User turns discovery off
- **WHEN** the user disables Home Assistant discovery while connected
- **THEN** this install's device and entities SHALL disappear from Home Assistant, and SHALL NOT come back after a broker or Home Assistant restart

#### Scenario: Another install shares the broker
- **WHEN** a second install with a different device ID is connected to the same broker and this install turns discovery off
- **THEN** the second install's device and entities SHALL be unaffected

### Requirement: Entity Availability Follows Device Connections
Home Assistant entities whose values come from the DE1 SHALL be available only while the app is online AND the DE1 is connected. Entities whose values come from the scale SHALL be available only while the app is online AND the scale is connected. Entities that report the connections themselves SHALL depend only on the app being online.

#### Scenario: DE1 switched off
- **WHEN** the DE1 disconnects while the app stays connected to the broker
- **THEN** machine entities such as temperatures and pressure SHALL show as unavailable rather than their last value
- **AND** the DE1 connected entity SHALL show disconnected

### Requirement: Profile Selection From Home Assistant
When discovery is enabled, the app SHALL publish a Home Assistant select entity whose options are the titles of the installed profiles, in the profile page's default order (the current profile, then most recently used, then never-used profiles alphabetically), and whose state is the active profile. Choosing an option SHALL activate that profile, exactly as the existing profile-select topic does. The options SHALL be re-published whenever profiles are added, removed or renamed. The existing profile text entity SHALL remain, still working for anyone who has it, but SHALL be marked disabled by default, so a device Home Assistant creates from now on shows the dropdown as its profile control and the text entity only if the user enables it.

#### Scenario: Pick a profile from a dashboard
- **WHEN** the user picks a profile in the Home Assistant dropdown while the machine is idle
- **THEN** the app SHALL activate that profile and the dropdown SHALL show it as current

#### Scenario: Text entity on an existing and a new device
- **WHEN** an existing user upgrades, and separately a new user sets up discovery for the first time
- **THEN** the existing user's profile text entity SHALL stay enabled and keep working, and the new user's device SHALL show only the dropdown until the text entity is enabled in Home Assistant

#### Scenario: New profile added
- **WHEN** the user adds a profile in the app
- **THEN** the dropdown SHALL list it without the app reconnecting

### Requirement: Recipe Selection From Home Assistant
When discovery is enabled and at least one recipe exists, the app SHALL publish a Home Assistant select entity whose options are the names of the non-archived recipes, and whose state is the active recipe's name, or unknown when no recipe is active. Choosing an option SHALL activate that recipe through the app's own recipe activation. The options SHALL be re-published whenever recipes are added, renamed, archived or restored.

#### Scenario: Pick a recipe from a dashboard
- **WHEN** the user picks a recipe in the Home Assistant dropdown
- **THEN** the app SHALL activate that recipe, exactly as tapping it in the app does, and the dropdown SHALL show it as current

#### Scenario: No recipe active
- **WHEN** the user switches to a profile directly, so no recipe is active
- **THEN** the dropdown SHALL show no current recipe, and its options SHALL still be the non-archived recipes

#### Scenario: Recipe archived
- **WHEN** the user archives a recipe
- **THEN** it SHALL disappear from the dropdown without the app reconnecting

### Requirement: Last Shot Summary
After each espresso shot is saved to history, the app SHALL publish a retained summary of that shot: finish time (ISO 8601 with time zone), duration in seconds, dose in grams, yield in grams, ratio, and the profile it was pulled with. Fields the shot does not have SHALL be omitted, never sent as zero. When discovery is enabled, each field SHALL have a Home Assistant sensor. After connecting, the summary SHALL reflect the most recent saved shot.

#### Scenario: Shot finishes
- **WHEN** an espresso shot is saved to history
- **THEN** the summary SHALL show that shot's duration, dose, yield, ratio and profile

#### Scenario: Shot without a scale
- **WHEN** a shot is saved without a yield weight
- **THEN** the summary SHALL omit yield and ratio, rather than reporting 0

### Requirement: Shot Events
When discovery is enabled, the app SHALL publish a Home Assistant event entity for espresso shots, and SHALL publish non-retained shot events to it:
- `started` when an espresso begins;
- `finished` when the shot is saved to history, carrying its duration, yield and profile;
- `aborted` when an espresso ends without being saved.

Each espresso SHALL produce exactly one `started` and at most one `finished` or `aborted`. An espresso run with a cleaning, descale or calibration profile, which history never saves, SHALL produce no shot events at all.

#### Scenario: Automation on shot finished
- **WHEN** a shot completes and is saved
- **THEN** Home Assistant SHALL receive one `finished` event for it

#### Scenario: Shot that never started
- **WHEN** an espresso is stopped before it produced a shot and is not saved
- **THEN** Home Assistant SHALL receive an `aborted` event and no `finished` event

#### Scenario: Backflush with a cleaning profile
- **WHEN** the user runs a cleaning profile
- **THEN** Home Assistant SHALL receive no shot events

### Requirement: Remote Stop
The command topic SHALL accept `stop`, and when discovery is enabled the app SHALL publish a Home Assistant button that sends it. `stop` SHALL end the current operation when the machine is making espresso (including preheating), steaming, dispensing hot water or flushing. In any other state, including cleaning, descaling and idle, and while the DE1 is disconnected, it SHALL do nothing and log why. The app SHALL NOT accept any remote command that starts an operation.

#### Scenario: Stop a shot from Home Assistant
- **WHEN** the user presses the Stop button during an espresso shot
- **THEN** the machine SHALL stop the shot as if stopped from the app

#### Scenario: Stop during cleaning
- **WHEN** `stop` arrives while the machine is cleaning or descaling
- **THEN** the app SHALL ignore it
