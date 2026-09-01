## MODIFIED Requirements

### Requirement: A zombie DE1 link is detected via notification liveness
The system SHALL track whether the DE1 is delivering its expected periodic
notifications (e.g. STATE_INFO) while connected, so a link that remains
GATT-connected and continues to acknowledge writes, but has silently stopped
delivering notifications, can be distinguished from a healthy connection.

This tracking SHALL be evaluated on its own, not only when a reconnect is attempted. Nothing
attempts a reconnect while the link reports itself connected, so a check reached only from that
path never runs in the case it exists for.

#### Scenario: Notifications stop while writes keep succeeding
- **WHEN** the DE1 link is connected, characteristic writes continue to be
  acknowledged, but no expected periodic notification has arrived for
  significantly longer than the DE1's normal push interval
- **THEN** the system's liveness tracking reflects that notifications have
  stalled on this connection

#### Scenario: Notifications stop and nothing attempts a reconnect
- **WHEN** the DE1 stops delivering notifications and no other part of the system requests a
  connection
- **THEN** the stall is still observed

## ADDED Requirements

### Requirement: A DE1 link that has gone silent is torn down so the existing reconnect ladder recovers it

When a write to the DE1 has been abandoned after exhausting its retries AND the DE1 has also
delivered nothing for longer than a corroboration threshold, the system SHALL tear the link down
and report it disconnected, so the existing DE1 reconnect ladder reconnects it. It SHALL NOT wait
for the platform to report the disconnect, and SHALL NOT require a user action.

Both signals SHALL be required. An abandoned write alone is not conclusive, since writes fail
transiently on working links; silence alone SHALL NOT trigger recovery, because the device's true
minimum push cadence is unmeasured, so such a rule would rest on an assumption rather than
evidence — and it would be evaluated continuously on every periodic write for the whole life of a
connection.

The corroboration threshold SHALL be short enough that recovery begins sooner than the platform
reports the disconnect on its own; a threshold longer than that offers nothing over waiting. It
SHALL be documented as provisional until the DE1's true minimum push cadence is measured
on-device, and SHALL NOT be derived from the app's own inbound logging, which is de-duplicated.

It also has to be short because the evaluation is infrequent: the app's only guaranteed periodic
write is far apart, so a link that fails between writes is not reconsidered until the next one.

The teardown SHALL NOT be deferred on the machine's reported phase. That state is derived from the
DE1's own notifications — the stream whose absence is being measured — so a deferral keyed to it
can never release: once the link goes quiet mid-operation the phase freezes at its last value and
the deferral holds forever. Any deferral introduced here SHALL be released by evidence that does
not travel over the link being judged.

#### Scenario: A write is abandoned on a link that has also gone quiet

- **WHEN** a write is abandoned and the DE1 has delivered nothing for longer than the
  corroboration threshold
- **THEN** the link is torn down and the existing reconnect ladder reconnects it

#### Scenario: A write is abandoned on a link that is still delivering

- **WHEN** a write is abandoned but inbound data arrived within the corroboration threshold
- **THEN** no teardown occurs, since a transient write failure on a live link is not evidence the
  link is unusable

#### Scenario: The link is quiet but no write has failed

- **WHEN** the DE1 delivers nothing for an extended period and no write has been abandoned
- **THEN** this mechanism takes no action

#### Scenario: Recovery does not wait for the platform

- **WHEN** the link has gone silent and the platform has not reported a disconnect
- **THEN** the teardown proceeds anyway

#### Scenario: Silence confirms during an operation

- **WHEN** the DE1 stops delivering notifications during a shot, whether or not its writes are
  still acknowledged
- **THEN** the link is torn down and recovered, rather than deferred on a phase reading that the
  same silence has already made stale

#### Scenario: A deferral cannot be keyed to the silent link's own telemetry

- **WHEN** a mid-operation deferral is considered
- **THEN** it is not released by the machine phase, since that phase stops updating exactly when
  the deferral would begin

#### Scenario: The platform reports the disconnect first

- **WHEN** the controller reports the link disconnected before the teardown is acted on
- **THEN** the ordinary disconnect path handles it and no second reconnect is started
