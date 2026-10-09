## MODIFIED Requirements

### Requirement: Stale queued connection-error popups are dropped
When a queued `bleError` popup is about to be shown (e.g. after the screensaver deactivates) and the error is a generic connection error rather than a permission call-to-action (Location / Bluetooth permission), the system SHALL skip it if the DE1 is connected at display time. An error that was raised while the machine was running an operation SHALL NOT be skipped: the DE1 dropping ends the operation, so such an error comes from a scale or refractometer and is not stale.

#### Scenario: Error queued during screensaver, link healed before wake
- **WHEN** a generic DE1 connection error is queued while the screensaver is active and the DE1 has reconnected by the time the queue is shown
- **THEN** the stale popup is skipped and the next pending popup (if any) is shown instead

#### Scenario: Permission errors are never dropped
- **WHEN** a queued `bleError` is a Location or Bluetooth-permission error
- **THEN** it is shown regardless of DE1 connectivity, since the required user action is still outstanding

#### Scenario: Error raised during an operation is shown after it
- **WHEN** a scale or refractometer error is raised while a shot is running and the DE1 stays connected
- **THEN** it is queued, and shown when the operation ends, not skipped as stale
