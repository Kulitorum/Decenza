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

### Requirement: One MMR register is written at one assurance level

The system SHALL NOT write a given MMR register through the verified path at one call site and the unverified path at another. Mixed assurance on one register means whether the setting reaches the machine depends on which code path last wrote it, which is not diagnosable from the machine's behaviour and not reproducible from a log.

Choosing which level a register uses is a per-register decision. Where a register is left unverified, the reason SHALL be stated at the call site, so the choice is visible as a decision rather than an oversight.

No MMR write SHALL be verified by reading the register back. Unverified is not merely the default side of that split — after this change it is the only side, and the verification mechanism is removed rather than left available. Read-back verification is not free on this protocol: an MMR read is itself a write to the read-request characteristic, so verifying a write issues a second write, plus a retry ladder of further writes, onto the same link whose write failures prompted the verification. Neither reference implementation verifies an MMR write at all, while both retry MMR *reads* — which is the asymmetry the protocol actually justifies. Reintroducing read-back verification SHALL require stating the consequence of the write silently not landing, for the specific register, and SHALL account for that cost.

#### Scenario: A register written from several call sites

- **WHEN** an MMR register is written from more than one call site
- **THEN** all of those call sites use the same assurance level

#### Scenario: A register deliberately left unverified

- **WHEN** a register is written unverified
- **THEN** the reason is recorded at the call site

