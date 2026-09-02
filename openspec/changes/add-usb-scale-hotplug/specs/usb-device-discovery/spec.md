## Purpose

Covers how the app notices a supported USB device — the DE1 or a scale — being attached and detached: the Android hotplug broadcast that reports both events immediately for both device kinds, the slow background scan that remains only as a recovery path for a missed broadcast, and the single user-facing setting that governs periodic scanning and nothing else about USB support.

## ADDED Requirements

### Requirement: One setting governs USB scanning, and only scanning

A single user-facing setting SHALL govern whether the app periodically SCANS for USB devices, covering both the DE1 and the scale. It SHALL default to OFF. Its label, description and accessibility name SHALL say that it controls scanning, and SHALL cover USB devices generally rather than the DE1 alone — a user reading the control must not conclude that turning it off disables USB support.

While the setting is OFF, the app SHALL NOT run any periodic USB scanning for either device. Turning it ON SHALL begin USB scanning without requiring a restart; turning it OFF SHALL stop it, likewise without a restart.

The setting SHALL NOT gate any other USB behavior: not hotplug, not the on-demand probe run by "Scan for Devices", and not an already-connected device.

On a platform WITHOUT hotplug, a USB scale attached while the setting is off is no longer detected automatically, where previously it was. The affected population is narrow — a desktop user, with a USB scale, who has the setting off — and the alternative is a periodic scan on every machine that never attaches anything over USB.

#### Scenario: Setting is off by default on a fresh install
- **WHEN** the app starts for the first time
- **THEN** no periodic USB scanning runs for the DE1 or the scale

#### Scenario: An already-connected device is unaffected by turning scanning off
- **WHEN** a USB device is connected and the user turns the scanning setting off
- **THEN** the device stays connected and usable; only periodic scanning stops

#### Scenario: Unplugging is still noticed with scanning off
- **WHEN** a connected USB device is unplugged while the scanning setting is off
- **THEN** the app reports it as no longer connected, because losing an open port is
  observed by the port itself rather than by the scan
- **AND** it releases that device and falls back to its default weight source at that
  moment, not at the next scan — a device the app has stopped talking to SHALL NOT be
  held as connected while waiting for a scan that may never be run
- **AND** on a platform without hotplug it is not reconnected automatically when
  plugged back in; "Scan for Devices" is the way back

#### Scenario: Enabling the setting starts scanning immediately
- **WHEN** the user turns the USB setting on
- **THEN** USB scanning begins without an app restart

#### Scenario: Disabling the setting stops scanning immediately
- **WHEN** the user turns the USB setting off while a USB scan is running
- **THEN** the periodic scanning stops without an app restart

#### Scenario: A USB device is still reachable while the setting is off
- **WHEN** a USB device is attached, the setting is off, and the user presses "Scan for Devices"
- **THEN** the on-demand USB probe still finds it, because that probe is not gated by the scanning setting

#### Scenario: The setting never blocks USB support itself
- **WHEN** the setting is off on a platform with hotplug
- **THEN** an attached device is still detected and usable; the setting has suppressed periodic scanning only

### Requirement: Android reports USB attach and detach immediately, for every supported device

On Android, the app SHALL learn of a supported USB device being attached or detached from a system broadcast rather than by waiting for a scan tick, so both events are observed within the time the platform takes to deliver the broadcast.

This SHALL apply to **both** supported device kinds — the DE1 and the scale — not the scale alone. The two kinds are distinguished by their vendor and product identifiers and routed to their respective handling; the app SHALL NOT maintain a separate subscription per device kind, so the two cannot drift apart in what they detect or how they filter.

Detection SHALL be limited to the USB devices the app supports, matching the vendor and product identifiers the app already declares for USB device filtering. Those identifiers SHALL have a single definition; the hotplug filter SHALL NOT restate them. A broadcast for any other device SHALL be ignored.

The subscription SHALL follow the app's lifecycle: established while the app is running and released on teardown, leaving no registration behind.

Hotplug SHALL NOT be subject to the USB scanning setting. The setting exists to stop periodic scanning, which has a recurring cost; an idle broadcast subscription has none. A supported USB device that is plugged in SHALL therefore be detected and usable regardless of the setting, on any platform that provides hotplug.

#### Scenario: Scale attached while the app is running
- **WHEN** a supported USB scale is plugged in
- **THEN** the app observes the attach from the broadcast, without waiting for the next scan tick

#### Scenario: DE1 attached while the app is running
- **WHEN** a supported USB DE1 is plugged in
- **THEN** the app observes the attach from the broadcast and begins connecting to it, without waiting for the next scan tick

#### Scenario: Device unplugged while connected
- **WHEN** a connected USB device is unplugged
- **THEN** the app observes the detach from the broadcast and tears that device's connection down, rather than waiting for a scan tick to notice it is gone

#### Scenario: Each device kind is routed to its own handling
- **WHEN** a DE1 and a scale are attached in either order
- **THEN** each is recognized as its own kind and handled by the path for that kind, with no crossover

#### Scenario: An unrelated USB device is ignored
- **WHEN** a USB device that is not a supported scale or DE1 is attached
- **THEN** the app takes no action

#### Scenario: A plugged-in device works with the setting off
- **WHEN** a supported USB device is plugged in while the USB scanning setting is off
- **THEN** the app still detects it from the broadcast and makes it available, because the setting governs scanning rather than USB support

#### Scenario: Detach is observed with the setting off
- **WHEN** a USB device connected via hotplug with the setting off is unplugged
- **THEN** the app observes the detach and reports that device as no longer connected

### Requirement: Scanning remains as a recovery path, not the mechanism

Periodic USB scanning SHALL remain available while the USB scanning setting is on, as recovery for a hotplug event the app did not receive — a broadcast missed while backgrounded, or a platform that does not deliver one.

On a platform with hotplug, the scan interval SHALL be long enough that it is a background safety net rather than a detection mechanism, since detection latency is the broadcast's responsibility. On a platform without hotplug, scanning is the only detection path and SHALL retain a responsive interval.

The interval for each case SHALL be a single named value per platform, so the hotplug fallback can be lengthened or removed once field evidence shows the broadcast is reliable, without hunting for scattered literals.

#### Scenario: A missed attach broadcast is still recovered
- **WHEN** a USB device is attached but no hotplug broadcast reaches the app, and the USB setting is on
- **THEN** the next scan tick discovers it

#### Scenario: Hotplug platform does not scan aggressively
- **WHEN** the USB setting is on and hotplug is available
- **THEN** the periodic probe runs on the long fallback interval, not the responsive one

#### Scenario: Platform without hotplug keeps a responsive scan
- **WHEN** the USB setting is on and the platform provides no hotplug signal
- **THEN** the periodic probe runs on the responsive interval, because it is the only way an attach is noticed

### Requirement: Detection paths agree on connection state

A device discovered by hotplug and a device discovered by the scan SHALL produce the same observable result: the same availability state, the same connection behavior, and no duplicate connection when both paths observe the same device.

A detach observed by either path SHALL leave the app reporting that device as not connected, and SHALL NOT leave a stale connected state behind.

#### Scenario: Both paths see the same attach
- **WHEN** a hotplug attach is received and a scan tick runs before the resulting connection completes
- **THEN** the app connects to the device once, not twice

#### Scenario: Detach leaves no stale state
- **WHEN** a connected USB device is detached
- **THEN** the app reports it as not connected, by whichever path observed the detach
