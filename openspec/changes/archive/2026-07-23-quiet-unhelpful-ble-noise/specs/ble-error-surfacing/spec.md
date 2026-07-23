## ADDED Requirements

### Requirement: Transient DE1 authorization/contention teardown is not a user-facing error

When the DE1 BLE controller reports `AuthorizationError`, the system SHALL log a warning and emit the `de1LinkFault` diagnostic signal, and SHALL NOT surface a user-facing error dialog for that error. On these PIN-less devices `AuthorizationError` is never an actionable pairing failure — it is the OS tearing down the encrypted link under BLE contention (the dual-HIGH signature, with the scale left at HIGH priority in observe mode by design) — and the link recovers via the existing reconnect ladder.

#### Scenario: Authorization error under dual-HIGH contention self-heals
- **WHEN** the DE1 link is dropped with `AuthorizationError` while the scale holds HIGH BLE priority, and the DE1 subsequently reconnects
- **THEN** no error dialog is shown or queued; the log records the controller error and the fault-cluster detector still receives the `de1LinkFault`

#### Scenario: Authorization error does not dialog even when it recurs
- **WHEN** the DE1 repeatedly drops with `AuthorizationError` over a session
- **THEN** none of those `AuthorizationError` occurrences raise a dialog
- **AND** other controller-error types on the reconnect path (e.g. a generic connection error) are still surfaced as governed by the persistent-connection-failure requirement
