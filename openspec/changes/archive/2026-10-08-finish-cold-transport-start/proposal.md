# Finish cold Transport starts using the shared maintenance handler

## Why

TransportPage still requires Ready even though DE1Device already supports cold AirPurge like Decaid. The user requested the limited change matching Decaid on October 8, 2026; this supersedes the earlier native-firmware-only gate proposal.

## What Changes

- Permit Start while connected and idle/heating/ready; preserve simulation and block sleep, disconnection and other operations.
- Reuse startAirPurge/requestMaintenanceState: older or unknown GHC firmware gets the existing 1°C group / 0°C tank preparation; newer firmware requests AirPurge directly.
- Retain Decenza's event-driven wait for preheat to end rather than Decaid's fixed one-second delay, per repository timer rules.
- Cancel a deferred AirPurge before leaving/covering Transport, with disconnect cleanup; later ready notifications must not revive it.
- Restore the selected brew profile when leaving or covering Transport; defer until the device is idle if another operation replaces it. Cancel competing-operation deferred AirPurge before state observers run.
- Treat unread GHC status conservatively for cold preparation.
- Add focused regression coverage, update the hint/manual, and reconcile the maintenance spec.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `machine-maintenance`: Transport no longer requires Ready; cold requests use the shared preparation path.

## Impact

TransportPage.qml, the existing DE1 headless test target, the wiki Maintenance entry and main maintenance specs. No new firmware capability property, threshold, assets, settings or cold-preparation implementation. The C++ review correction cancels deferred requests without changing preparation. Physical-device testing remains evidence to record, not a result inferred from simulator checks.
