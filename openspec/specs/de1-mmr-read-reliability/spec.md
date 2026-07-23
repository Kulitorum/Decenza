# de1-mmr-read-reliability Specification

## Purpose
TBD - created by archiving change harden-de1-ble-reliability. Update Purpose after archive.
## Requirements
### Requirement: DE1 connection readiness is gated on confirmed notification subscription
The system SHALL NOT signal that the DE1 connection is ready (emit `connected()`)
until every required characteristic notification subscription (CCCD descriptor
write) has either completed or individually timed out. The system SHALL NOT rely
on a fixed delay in place of this confirmation.

#### Scenario: Normal connect waits for subscriptions before signaling ready
- **WHEN** the DE1 reaches `RemoteServiceDiscovered` and notification subscriptions
  are requested for STATE_INFO, SHOT_SAMPLE, WATER_LEVELS, READ_FROM_MMR, and
  TEMPERATURES
- **THEN** the system does not emit `connected()` until all five subscriptions
  have completed (or individually timed out)

#### Scenario: A stuck subscription does not hang the connection
- **WHEN** one notification subscription does not complete within its timeout
- **THEN** the system logs the failure, proceeds without blocking indefinitely,
  and still emits `connected()` once the remaining subscriptions resolve or
  time out

### Requirement: Post-connect one-shot MMR reads wait for confirmed subscription
The system SHALL NOT issue the post-connect one-shot MMR reads (GHC_INFO,
CPU_BOARD_MODEL, MACHINE_MODEL, FIRMWARE_VERSION build number, HEATER_VOLTAGE,
refill-kit status) until the `READ_FROM_MMR` notification subscription is
confirmed, so a read's response is never sent by the DE1 before the app can
receive it.

#### Scenario: GHC and identity reads are issued only after subscription confirmation
- **WHEN** the DE1 connection reaches the ready state
- **THEN** the GHC_INFO, CPU_BOARD_MODEL, MACHINE_MODEL, FIRMWARE_VERSION, and
  HEATER_VOLTAGE read requests are sent only after the `READ_FROM_MMR`
  notification subscription has been confirmed

### Requirement: One-shot MMR reads retry on a missing response
The system SHALL retry a one-shot MMR read a bounded number of times if no
matching response arrives within a timeout, before giving up and leaving the
associated value at its existing safe/permissive default.

#### Scenario: A dropped GHC_INFO response is retried and recovered
- **WHEN** the GHC_INFO read request is sent but no response arrives within the
  configured timeout
- **THEN** the system re-sends the GHC_INFO read request
- **AND** if a response arrives on a retry, `DE1Device::isHeadless` reflects the
  actual GHC status reported by that response

#### Scenario: Retries are exhausted without any response
- **WHEN** the GHC_INFO (or another one-shot MMR read) receives no response after
  the maximum number of retries
- **THEN** the system logs a warning identifying which read failed
- **AND** the associated value keeps its existing safe/permissive default (e.g.
  `isHeadless` remains `true`) rather than staying in an unknown or stale state
  silently

### Requirement: MMR write verification retries a missing read-back, not just a mismatched one
The system SHALL retry the read-back issued by write verification
(`writeMMRVerified`) when no response arrives within a timeout, in addition to
its existing retry-on-mismatch behavior, so a dropped verification-read
notification does not leave the verification pending indefinitely.

#### Scenario: A dropped verification read-back is retried
- **WHEN** `writeMMRVerified` writes a value and the subsequent read-back
  request receives no response within the configured timeout
- **THEN** the system re-issues the read-back request
- **AND** logs a warning if retries are exhausted without any response, instead
  of leaving the verification pending with no record of failure

#### Scenario: A mismatched read-back still retries as before
- **WHEN** the read-back response arrives but its value does not match the
  written value
- **THEN** the system retries as it does today, unaffected by the new
  missing-response handling

### Requirement: GHC status is logged exactly once per successful connect
The system SHALL log the parsed GHC status exactly once per DE1 connection in
which the GHC_INFO read succeeds (directly or via retry), so the absence of this
log line reliably indicates the read never got a response rather than being
ambiguous with "value unchanged, so not logged."

#### Scenario: GHC status log line appears on every successful connect
- **WHEN** a DE1 connection completes and the GHC_INFO read succeeds (on the
  first attempt or after retry)
- **THEN** a `"GHC status: ..."` log line is recorded for that connection
- **AND** its absence from a connection's log indicates the read failed even
  after retries, not that the status was unchanged

