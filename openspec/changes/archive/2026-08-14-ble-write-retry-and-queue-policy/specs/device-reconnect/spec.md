## MODIFIED Requirements

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

## ADDED Requirements

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
