## Context

See `proposal.md` for the measured failure. The relevant state of the code:

- `BleTransport::m_notificationLiveness` is restarted by every inbound push in
  `onCharacteristicChanged()`. It is read in exactly one place — `connectToDevice()` — where a link
  reporting connected but stale past `NOTIFICATION_STALE_MS` (30 s) is torn down instead of
  early-returning. Nothing calls `connectToDevice()` while the link claims connected.
- The reconnect ladder in `main.cpp` is driven by `disconnected()` and already owns its backoff and
  attempt budget.
- `BleGattQueue` is keyed by `Requester` and already reports each device's write outcomes, so
  "are writes still succeeding" needs no new plumbing.
- `noteWriteAbandoned()` counts consecutive abandoned writes and logs at three, deliberately inert
  ("Reporting only. Nothing is torn down on this signal").

## Goals / Non-Goals

**Goals:** recover a silent DE1 link from the app's own evaluation, sooner than the platform
reports it; stop asking the user to do it.

**Non-Goals:** touching the scale or refractometer paths; re-tuning the retry budget or the
keepalive period; diagnosing why the keepalive write hangs.

## Decisions

### Evaluate the liveness the app already tracks, and shorten the threshold

The tracking exists; only the evaluation and the number are missing. `NOTIFICATION_STALE_MS` is
30 s, longer than the 22.5 s outage, so even polling it would have lost to the platform. The
threshold is set below that and marked provisional: the DE1's true minimum push cadence needs
on-device measurement (already tracked as `harden-de1-ble-reliability` tasks 5.2 / 8.5) and cannot
be derived from this log, whose inbound logging is de-duplicated.

Evaluation rides existing activity rather than a new timer, per the project's design principles —
including the write-failure path, which is exactly the moment the app first has evidence (1929.62
in episode 2, 22.5 s before the platform speaks).

### Tear down only; the existing ladder reconnects

The teardown emits `disconnected()` and stops there. `main.cpp`'s ladder owns the reconnect, its
backoff and its attempt budget. A directly-issued connect would race the ladder that the teardown's
own `disconnected()` starts — the #1309 shape where two schedulers reset each other's counters.
Cost: the ladder's first retry is at 5 s and the log shows connect plus discovery at ~4.9 s, so
recovery lands near 10 s against 22.5 s of silence.

### One mid-operation guard, on writes rather than on the phase

Defer the teardown only while the machine is in an active phase **and** writes are still
succeeding. In that state the app can still stop the machine, and tearing the link down would
remove that ability for the reconnect's duration — sacrificing the control worth protecting in
order to recover telemetry the shot does not depend on. When writes are failing too, nothing is
being preserved and the teardown proceeds.

*Why the guard is not a whole deferral mechanism:* it cannot deadlock, because the writes-failing
arm always terminates. A phase-only deferral would deadlock — `MachineState::updatePhase()` sets
`Disconnected` only when `!isConnected()`, so with a silent link the phase stays frozen at
`Pouring` and the release signal travels over the dead link.

### Nothing else about the mid-shot case needs handling

A teardown produces the same sequence as a spontaneous mid-shot drop: `updatePhase()` sets
`Disconnected` and emits `espressoCycleEnded()`, after which reconnect and the connect-time
settings burst behave exactly as they do for any drop today. That path already exists and is
already exercised, so this change does not need to phase-guard the settings burst, hold a stop
across the outage, or exclude the shot from SAW learning. Earlier drafts specified all three; they
were guarding a path this change does not create.

### Withhold upload retries from a non-writing link

`ProfileManager`'s retry timer checks write health before firing and neither issues the attempt nor
consumes the budget while writes are failing, keeping the upload pending for the reconnect path.
The precedent is already there: `BLE disconnect` is non-retryable because "the reconnect path
re-uploads when the link comes back. Retrying on a timer would race with that." Without this, five
consecutive failures raise `de1CommunicationFailure` and ask the user to power-cycle the DE1 —
wrong for this cause.

## Risks / Trade-offs

- **A too-eager threshold bounces a healthy link.** → Bounded above by having to beat the platform's
  own notice, and the cost of a false positive is one reconnect. Provisional and calibrated the way
  the scale's equivalent was.
- **Detection may not fire mid-shot**, since few writes are in flight and the guard defers while
  writes work. → Accepted; not worked around. Probing a failing link with manufactured writes during
  a shot adds traffic exactly when it is least wanted.
- **The DE1 gains behaviour the scale already has, without sharing its code.** → Accepted. The
  scale's detection is weight-sample driven and entangled with shot gating and tare state; the
  refractometers deliberately have none, and would be harmed by it (they are idle by design). The
  DE1's liveness is its own timestamp, so this is not a copy of the scale's logic.
