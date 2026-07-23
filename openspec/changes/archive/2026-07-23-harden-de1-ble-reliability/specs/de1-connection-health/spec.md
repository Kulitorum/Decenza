## ADDED Requirements

### Requirement: A zombie DE1 link is detected via notification liveness
The system SHALL track whether the DE1 is delivering its expected periodic
notifications (e.g. STATE_INFO) while connected, so a link that remains
GATT-connected and continues to acknowledge writes, but has silently stopped
delivering notifications, can be distinguished from a healthy connection.

#### Scenario: Notifications stop while writes keep succeeding
- **WHEN** the DE1 link is connected, characteristic writes continue to be
  acknowledged, but no expected periodic notification has arrived for
  significantly longer than the DE1's normal push interval
- **THEN** the system's liveness tracking reflects that notifications have
  stalled on this connection

### Requirement: A reconnect attempt against a zombie link triggers teardown and reconnect
The system SHALL NOT silently no-op a reconnect attempt made while a zombie link
(GATT-connected, notifications stalled) is in place. It SHALL tear down the
stale link and proceed with a fresh connect and re-subscription.

#### Scenario: Reconnect is attempted while the link is a zombie
- **WHEN** `connectToDevice()` is invoked and the current link is GATT-connected
  but its notification liveness indicates a zombie state
- **THEN** the system tears down the existing controller and connection
- **AND** proceeds with a fresh connect, service discovery, and re-subscription
  rather than returning immediately because a connection nominally already
  exists

#### Scenario: Reconnect is attempted against a genuinely healthy link
- **WHEN** `connectToDevice()` is invoked and the current link is connected with
  recent, healthy notification liveness
- **THEN** the system behaves as it does today (no unnecessary teardown)

### Requirement: Zombie-link detection feeds the existing wedge/link-health signal
The system SHALL surface zombie-link detection as an additional input to its
existing BLE link-health evaluation, alongside controller errors and
write-timeout exhaustion, rather than as an isolated, unused signal.

#### Scenario: Zombie detection contributes to link-health evaluation
- **WHEN** a zombie link is detected
- **THEN** the existing link-health/wedge evaluation is informed by this signal
  in addition to its current controller-error and write-timeout-exhaustion
  signals
