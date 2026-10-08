# Design

## Context

See proposal.md. Decaid at local revision 94501c455d81fa9a31b4ce165bb0b0777e7beec6 calls _prepareColdMaintenanceWorkaround before requested-state writes in lib/src/models/device/impl/de1/unified_de1/unified_de1.dart:339-400. It prepares cold GHC maintenance below build 1356, including an unknown build read as 0. Decenza already has the equivalent requestMaintenanceState/applyColdMaintenanceWorkaround path from PR #1876.

## Goals / Non-Goals

**Goals:** let the existing cold-maintenance path be reached from Transport's page and restore the brew profile after using its temporary preparation profile.

**Non-Goals:** a new reactive firmware capability, another firmware policy, rewriting cold preparation, or rewriting drain/completion behavior.

## Decisions

- Treat unconfirmed GHC hardware conservatively for cold preparation; keep the existing permissive in-app controls and confirmed no-GHC direct path. Hardware confirmation resets on disconnect.
- Keep the temporary Transport profile guarded until the selected-profile upload is acknowledged. Refuse app espresso starts, urgently stop physical espresso starts, and reuse the existing restoration path once idle; require an explicit retry.
- Gate the page on connection and idle/heating/ready phases, not temperature or firmware. Firmware policy belongs to the shared maintenance request, as it does in Decaid; repeating it in the page is what prevented the existing workaround from helping Transport.
- Keep the existing event-driven preparation wait. Decaid's one-second delay is not copied because repository rules require events rather than timing guards.
- Cancel deferred AirPurge synchronously before explicit exits and on StackView deactivation; destruction also cancels as a fallback. Cancel only AirPurge so a replacing Descale/Clean request retains its ownership. Disconnect discards all deferred requests from that connection.
- Cancel deferred AirPurge in the device state-notification path before flushing it or notifying observers when an operation, sleep or error replaces idle preparation.
- Request profile restoration on deactivation (including covering) and destruction, once per start/exit cycle. ProfileManager.restoreCurrentProfile defers quietly until both the device is idle and the phase is idle/heating/ready; Steam warm-up reports Heating but remains an active device state. The existing uploader still owns actual writes and in-flight upload serialization.
- Test actual requested-state writes with MockTransport for unknown/1352/1356/1358 GHC firmware and no-GHC hardware. A deferred request must wait until the state notification leaves heating; acknowledge the preparation profile so fixture teardown does not simulate a failed upload.

## Risks / Trade-offs

- Simulator tests do not establish that a real machine drains: retain that distinction in evidence.
- Completion cannot prove emptiness after a physical stop: preserve existing conditional guidance and abort/disconnection handling.

## Migration Plan

No migration. Rollback restores the Ready page gate; no stored firmware or settings change.
