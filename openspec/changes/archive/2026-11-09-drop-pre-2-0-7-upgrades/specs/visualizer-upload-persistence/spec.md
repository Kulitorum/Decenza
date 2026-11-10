# Spec Delta

## REMOVED Requirements

### Requirement: A one-time bounded reconciliation SHALL relink already-uploaded-but-unrecorded shots and correct stale cloud ratings
**Reason**: It repairs uploads made before 1.7.5 recorded the returned Visualizer id. Every installation at or above the upgrade floor (2.0.7) with Visualizer credentials has already run it. An installation that never had credentials made no uploads to repair.
**Migration**: None.

### Requirement: Reconciliation scope and matching
**Reason**: It is part of the removed reconciliation pass.
**Migration**: None.

### Requirement: Reconciled rows get their id and a rating push
**Reason**: It is part of the removed reconciliation pass.
**Migration**: None.

### Requirement: Reconciliation runs off the main thread and needs no migration
**Reason**: It is part of the removed reconciliation pass.
**Migration**: None.

### Requirement: An aborted pass keeps its links idempotent
**Reason**: It is part of the removed reconciliation pass.
**Migration**: None.
