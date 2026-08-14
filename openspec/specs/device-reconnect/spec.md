# device-reconnect Specification

## Purpose
Defines the BLE reconnect strategy for scales and the DE1: background scale reconnect uses passive scanning rather than a parked `connectToDevice()`, a bounded direct-connect fast path is reserved for foreground-triggered scale connects (aborting the controller on timeout) with duplicate-attempt protection, while the DE1 retains sustained direct-connect for sleep-wake and has its own recovery path for a connect attempt that hangs in `Connecting`.
## Requirements
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

When a scale connect attempt times out, the system SHALL abort it by calling `disconnectFromDevice()` and destroying the `QLowEnergyController`, not merely clearing an in-progress flag. The radio MUST NOT be left to reach Android's own ~30-second supervision timeout.

This SHALL apply to every scale connect attempt still held in `Connecting` at the timeout, regardless of how the attempt was initiated. A connect started from scan discovery is covered identically to a foreground direct-connect: a teardown gated on the attempt having been a direct-connect, or on the transport reporting itself connected, leaves a scan-initiated attempt pending, and while it is pending every retry is rejected as a duplicate connect to a busy transport.

Destroying a controller that is still in `Connecting` SHALL first request its disconnection, so the platform can release the underlying connection resource. Deleting such a controller outright leaves the platform connection attempt outstanding.

#### Scenario: Timeout tears down the connecting controller
- **WHEN** a scale direct-connect attempt reaches its timeout deadline without connecting
- **THEN** the system closes/deletes the connecting `QLowEnergyController`
- **AND** no `Connecting` controller for the scale remains pending afterward

#### Scenario: Timeout tears down a scan-initiated connect
- **WHEN** a scale connect that was started from scan discovery is still in `Connecting` at the connection timeout
- **THEN** it is torn down on the same terms as a direct-connect
- **AND** the next retry is able to start a fresh connect rather than being rejected as a duplicate

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

The system SHALL NOT attempt to power-cycle the Bluetooth adapter as an automatic remedy on platforms where a non-privileged application cannot control adapter power. On those platforms the call has no effect, and the recovery completes through its own fallback path, which is indistinguishable from a real recovery unless the outcome is checked.

Any automatic remedy SHALL report its true outcome. It SHALL NOT report that the stack recovered when the state it acted on did not change.

#### Scenario: DE1 connect wedges with no error
- **WHEN** a DE1 connect attempt remains in `Connecting` past the bounded deadline without connecting or erroring
- **THEN** the system aborts and recreates the controller and re-attempts the connection
- **AND** the DE1 reconnect loop does not give up permanently while in this state

#### Scenario: Link recovers without an app restart
- **WHEN** the DE1 link has wedged after a controller error and subsequent connects hang in `Connecting`
- **THEN** the recovery path restores the DE1 connection without requiring the user to restart the app

#### Scenario: Adapter power control is unavailable to the app
- **WHEN** the platform does not permit a non-privileged application to power the Bluetooth adapter off and on
- **THEN** no adapter power-cycle is attempted, and no time is spent waiting for a state change that cannot occur

#### Scenario: A remedy that did not change anything is not reported as recovery
- **WHEN** an automatic recovery attempt completes without the state it acted on having changed
- **THEN** the outcome recorded is that the attempt did not take effect, not that the stack recovered

### Requirement: Retiring the adapter remedy preserves the reconnect-ladder re-arm

Removing the adapter power-cycle SHALL NOT remove the reconnect-ladder reset that accompanied it. The system SHALL retain an explicit path that, on concluding a recovery attempt while the DE1 is disconnected, resets the DE1 reconnect attempt counter and promptly re-attempts.

On platforms where the adapter power-cycle was already ineffective, this reset is the only effect the recovery mechanism had; losing it would move affected devices from the fast retry tier onto the slow one. Because the existing reset is reached only through the adapter-power state transitions being removed, retiring the remedy SHALL include naming the path that reaches it instead.

#### Scenario: Recovery attempt concludes and the ladder is re-armed
- **WHEN** an automatic BLE recovery attempt concludes while the DE1 is disconnected
- **THEN** the DE1 reconnect attempt counter is reset and a reconnect is attempted promptly
- **AND** the device is not left waiting on the slow retry tier

#### Scenario: Re-arm does not fire against a healthy link
- **WHEN** a recovery attempt concludes and the DE1 is already connected or connecting
- **THEN** no reconnect is forced

#### Scenario: The re-arm path is reachable after the remedy is retired
- **WHEN** the adapter power-cycle no longer runs, so no adapter state transition occurs
- **THEN** the re-arm still occurs, driven by a path that does not depend on those transitions

