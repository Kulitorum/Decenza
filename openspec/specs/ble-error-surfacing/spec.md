# ble-error-surfacing Specification

## Purpose
Governs which DE1 BLE connection failures reach the user: transient write-retry exhaustion is logged and self-heals via the existing reconnect ladder without a popup, persistent reconnect failures are still surfaced (debounced to one message per distinct error), and stale queued connection-error popups are dropped once the link has already recovered, except permission call-to-actions which are always shown.
## Requirements
### Requirement: Transient DE1 write-retry exhaustion is not a user-facing error
When a DE1 BLE write exhausts its retry budget (write timeout or `CharacteristicWriteError`), the system SHALL log a warning and emit the `de1LinkFault("write-failed")` diagnostic signal, and SHALL NOT surface a user-facing error dialog for the failure itself. Recovery SHALL proceed via the existing disconnect detection and automatic reconnect ladder.

#### Scenario: Overnight keepalive write fails and self-heals
- **WHEN** the BLE link dies silently while idle and a periodic MMR keepalive write fails after all retries, and the automatic reconnect subsequently succeeds
- **THEN** no error dialog is shown or queued; the log records the failure and the fault-cluster detector still receives the `de1LinkFault`

#### Scenario: Write failure during an attended session
- **WHEN** a DE1 write exhausts retries while the user is actively using the app
- **THEN** no modal appears for the write failure; the UI reflects the link state (disconnect and reconnect) through the existing machine-status surfaces

### Requirement: Persistent connection failure still reaches the user
If the DE1 cannot be reconnected after a link fault, the system SHALL continue to surface the reconnect path's own connection errors as user-facing messages, debounced so each distinct message is shown at most once until the DE1 reconnects.

#### Scenario: Reconnect ladder keeps failing
- **WHEN** the DE1 link drops and every reconnect attempt fails with a connection error
- **THEN** the user sees the connection error surfaced by the reconnect path (once per distinct message), and the UI shows the machine as disconnected

### Requirement: Stale queued connection-error popups are dropped
When a queued `bleError` popup is about to be shown (e.g. after the screensaver deactivates) and the error is a generic connection error rather than a permission call-to-action (Location / Bluetooth permission), the system SHALL skip it if the DE1 is connected at display time.

#### Scenario: Error queued during screensaver, link healed before wake
- **WHEN** a generic DE1 connection error is queued while the screensaver is active and the DE1 has reconnected by the time the queue is shown
- **THEN** the stale popup is skipped and the next pending popup (if any) is shown instead

#### Scenario: Permission errors are never dropped
- **WHEN** a queued `bleError` is a Location or Bluetooth-permission error
- **THEN** it is shown regardless of DE1 connectivity, since the required user action is still outstanding

