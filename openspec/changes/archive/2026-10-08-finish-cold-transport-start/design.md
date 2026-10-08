# Design

## Context

See proposal.md. Decaid at local revision 94501c455d81fa9a31b4ce165bb0b0777e7beec6 calls _prepareColdMaintenanceWorkaround before requested-state writes in lib/src/models/device/impl/de1/unified_de1/unified_de1.dart:339-400. It prepares cold GHC maintenance below build 1356, including an unknown build read as 0. Decenza already has the equivalent requestMaintenanceState/applyColdMaintenanceWorkaround path from PR #1876.

## Goals / Non-Goals

**Goals:** let the existing cold-maintenance path be reached from Transport's page and restore the brew profile after using its temporary preparation profile.

**Non-Goals:** a new reactive firmware capability, another firmware policy, rewriting cold preparation, or rewriting drain/completion behavior.

## Decisions

- Gate the page on connection and idle/heating/ready phases, not temperature or firmware. Firmware policy belongs to the shared maintenance request, as it does in Decaid; repeating it in the page is what prevented the existing workaround from helping Transport.
- Keep the existing event-driven preparation wait. Decaid's one-second delay is not copied because repository rules require events rather than timing guards.
- Cancel deferred AirPurge synchronously before explicit exits and on StackView deactivation; destruction also cancels as a fallback. Cancel only AirPurge so a replacing Descale/Clean request retains its ownership. Disconnect discards all deferred requests from that connection.
- Restore through ProfileManager.uploadCurrentProfile on page destruction only while idle/heating/ready. An operation that replaces Transport must not have its profile overwritten.
- Test actual requested-state writes with MockTransport for unknown/1352/1356/1358 GHC firmware and no-GHC hardware. A deferred request must wait until the state notification leaves heating; acknowledge the preparation profile so fixture teardown does not simulate a failed upload.

## Risks / Trade-offs

- Simulator tests do not establish that a real machine drains: retain that distinction in evidence.
- Completion cannot prove emptiness after a physical stop: preserve existing conditional guidance and abort/disconnection handling.

## Migration Plan

No migration. Rollback restores the Ready page gate; no stored firmware or settings change.
