# de1-connection-health Specification

## Purpose
TBD - created by archiving change harden-de1-ble-reliability. Update Purpose after archive.

## Requirements

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

### Requirement: A DE1 link whose required notification subscriptions failed is not reported connected

The system SHALL NOT report the DE1 as connected when any notification stream required for normal operation failed to be enabled. `STATE_INFO` and `SHOT_SAMPLE` are required: without them the app cannot observe a shot starting, cannot chart it, and cannot stop at weight, so a link missing either is not usable for making coffee regardless of what other traffic still succeeds.

On such a failure the system SHALL tear the link down and re-enter its existing reconnect path, so subscription is retried against a freshly established connection rather than against the connection that just failed it.

Retry of a failed notification-enable SHALL be bounded by the same policy as any other GATT write on that link, and SHALL NOT be unbounded or pinned at the head of the queue past that bound. On exhaustion the link SHALL be torn down rather than retried further in place.

A failure that is a permanent fact about the connection — the characteristic absent from the discovered map, or no CCCD descriptor on it — SHALL NOT be retried at all, and SHALL fail the connection at once. Retrying five times could only delay saying so.

(This requirement previously read "SHALL NOT retry a failed notification-enable in place against the same connection", which contradicted both `design.md` and the shipped code, where a rejected CCCD write takes the ordinary write policy. Corrected rather than quietly reworded.)

#### Scenario: A notification-enable is rejected during connection setup

- **WHEN** the DE1's notification-enable for a required stream is rejected by the platform, or is not confirmed within its bound
- **THEN** the DE1 is not reported as connected
- **AND** the link is torn down and a fresh connect is attempted
- **AND** the reconnected link performs its notification subscription again from the start

#### Scenario: An optional stream fails but the required streams succeed

- **WHEN** a notification-enable fails for a stream that is not required for shot observation, and every required stream was enabled
- **THEN** the DE1 is reported as connected
- **AND** the failure of the optional stream is recorded

#### Scenario: Reconnect also fails to subscribe

- **WHEN** the fresh connect's notification subscription fails in the same way
- **THEN** the system continues through its existing reconnect ladder rather than reporting a connected DE1
- **AND** the user is not shown a machine that appears ready while its telemetry is dead

### Requirement: The outcome of DE1 notification subscription is observable to the user

The system SHALL record the outcome of DE1 notification subscription at the tier the in-app connection views display, and SHALL identify which stream each outcome refers to. A subscription failure recorded only at a tier those views filter out is not observable to the person holding the machine, nor to a support reader working from a submitted log.

Where a failure is recorded, the record SHALL name the affected characteristic so the failed stream can be identified directly rather than inferred from the order in which subscriptions were attempted.

#### Scenario: A subscription fails

- **WHEN** a DE1 notification-enable fails or times out
- **THEN** the failure appears in the in-app connection views without changing any log-level setting
- **AND** the record identifies which characteristic failed

#### Scenario: Connection setup completes

- **WHEN** the DE1 finishes its notification subscription sequence
- **THEN** the record states which streams are live, so a reader can tell a fully-subscribed link from a partially-subscribed one without reconstructing it from earlier lines

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
