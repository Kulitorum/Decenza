## ADDED Requirements

### Requirement: Background scale reconnect uses passive scanning, not parked direct-connect

When a saved BT scale is not connected and no intentional foreground trigger is active, the system SHALL pursue it with a passive BLE scan and SHALL NOT hold an open `connectToDevice()` attempt against the saved address. A direct `connectToDevice()` to the scale SHALL be issued only after the scale is observed advertising in a scan.

#### Scenario: Saved scale is powered off
- **WHEN** a saved BT scale is disconnected and powered off, and the periodic reconnect tick fires
- **THEN** the system starts (or continues) a passive scan for the scale
- **AND** does NOT open a direct `connectToDevice()` against the saved scale address
- **AND** the BLE radio is not held in a `Connecting` state waiting on the absent scale

#### Scenario: Saved scale powers back on
- **WHEN** the saved scale begins advertising and is seen in a scan
- **THEN** the system issues a direct `connectToDevice()` to the discovered device and connects

#### Scenario: DE1 link is unaffected by an absent scale
- **WHEN** a saved scale remains absent for an extended period while the DE1 is connected
- **THEN** the DE1 link experiences no reconnect-induced write failures or controller errors attributable to scale reconnect activity

### Requirement: Bounded foreground direct-connect fast-path for scales

At intentional foreground triggers — selecting/switching to a BT scale in the device picker, and app startup (and optionally DE1 wake / app resume) — the system MAY attempt a single direct `connectToDevice()` to the saved scale address to connect faster, bounded to approximately 4 seconds. This fast-path SHALL NOT be used for the periodic background reconnect.

#### Scenario: User switches to a BT scale that is present
- **WHEN** the user selects a saved BT scale from the device picker and the scale is advertising
- **THEN** the system issues one direct `connectToDevice()` and connects within the bound, without waiting for a full scan cycle

#### Scenario: Foreground direct-connect to an absent scale
- **WHEN** a foreground direct-connect is attempted but the scale does not connect within ~4 seconds
- **THEN** the system aborts the attempt and falls back to passive scanning (it does not retry the direct-connect on the background timer)

### Requirement: Direct-connect timeout aborts the controller

When a scale direct-connect attempt times out, the system SHALL abort it by calling `disconnectFromDevice()` and destroying the `QLowEnergyController`, not merely clearing an in-progress flag. The radio MUST NOT be left to reach Android's own ~30-second supervision timeout.

#### Scenario: Timeout tears down the connecting controller
- **WHEN** a scale direct-connect attempt reaches its timeout deadline without connecting
- **THEN** the system closes/deletes the connecting `QLowEnergyController`
- **AND** no `Connecting` controller for the scale remains pending afterward

### Requirement: Concurrent connect attempts to the same scale are prevented

The system SHALL NOT have more than one outstanding connect attempt to the same scale at a time. A scan-discovery-triggered connect and a foreground direct-connect SHALL be deduplicated so a second connect is not issued while one is already in progress.

#### Scenario: Scan finds the scale during a direct-connect
- **WHEN** a foreground direct-connect is in progress and the same scale is also discovered by a scan
- **THEN** the system does not start a second connect to that scale

### Requirement: DE1 retains sustained direct-connect for sleep-wake

The DE1 SHALL continue to be pursued by direct `connectToDevice()` while disconnected, because a sleeping DE1 does not advertise but remains connectable. This sustained direct-connect applies to the DE1 only and SHALL NOT be applied to scales.

#### Scenario: Sleeping DE1 is reconnected
- **WHEN** the DE1 is asleep (not advertising) and the DE1 reconnect tick fires
- **THEN** the system issues a direct `connectToDevice()` to the saved DE1 address to wake and reconnect it

### Requirement: Recovery from a DE1 connect that hangs in Connecting

If a DE1 connect attempt enters `Connecting` and neither reaches a connected state nor reports an error within a bounded time, the system SHALL tear down the connection attempt (close and recreate the controller) and continue retrying. The reconnect logic SHALL NOT permanently stop retrying solely because the controller reports `isConnecting()`.

#### Scenario: DE1 connect wedges with no error
- **WHEN** a DE1 connect attempt remains in `Connecting` past the bounded deadline without connecting or erroring
- **THEN** the system aborts and recreates the controller and re-attempts the connection
- **AND** the DE1 reconnect loop does not give up permanently while in this state

#### Scenario: Link recovers without an app restart
- **WHEN** the DE1 link has wedged after a controller error and subsequent connects hang in `Connecting`
- **THEN** the recovery path restores the DE1 connection without requiring the user to restart the app
