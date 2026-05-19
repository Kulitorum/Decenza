## ADDED Requirements

### Requirement: Classification Is Epoch-Scoped, Not Build-Scoped

The persisted dual-HIGH-incapable classification SHALL be scoped to a deliberate detection epoch, not to the application build/version code. The system MUST rehydrate the persisted latch when the stored epoch equals the current `kBleDetectionEpoch` source constant, and MUST discard it and re-detect when they differ. `kBleDetectionEpoch` MUST NOT be derived from or coupled to the build/version code and MUST change only by a deliberate source edit. The build/version code at set-time MAY still be persisted but only as a diagnostic and MUST NOT gate rehydration.

#### Scenario: Latch survives an ordinary app update
- **WHEN** a device is latched, then the app is updated to a build with the same `kBleDetectionEpoch`
- **THEN** the latch is rehydrated on startup (the scale starts at BALANCED with no detection window)
- **AND** no re-detection fault is incurred

#### Scenario: Epoch bump re-classifies every device once
- **WHEN** a build ships with an incremented `kBleDetectionEpoch` and a device has a latch stored under the previous epoch
- **THEN** that latch is discarded and the device re-detects from scratch on that build
- **AND** a device latched again under the new epoch then persists across subsequent same-epoch builds

#### Scenario: Build code does not gate
- **WHEN** the stored epoch matches but the stored build code differs from the current version code
- **THEN** the latch is still rehydrated (build code is diagnostic only, not a gate)

### Requirement: Legacy Records Migrate Forward Without Re-Detection

A persisted record that is latched and carries a build code but has no stored detection epoch (a pre-epoch / legacy record) SHALL be honored once and migrated forward: the in-memory latch MUST be rehydrated AND the current `kBleDetectionEpoch` MUST be written to the stored record. A user already classified on the pre-epoch release MUST NOT incur any additional detection across the upgrade. The migration MUST be logged.

#### Scenario: Pre-epoch latch is preserved across the upgrade
- **WHEN** the first epoch-aware build starts and finds a latched legacy record (build code present, no epoch key)
- **THEN** the latch is rehydrated (scale starts BALANCED, no detection window)
- **AND** the stored record is stamped with the current epoch
- **AND** the one-time migration is logged

#### Scenario: Migrated record then behaves epoch-scoped
- **WHEN** a migrated record is read again on a later same-epoch build
- **THEN** it rehydrates normally (it is now an ordinary epoch-stamped record, not re-migrated)

### Requirement: Scale-Feed Stall Must Be Confirmed Before Latching

The scale-feed-stall backstop SHALL distinguish a *suspected* stall from a *confirmed* stall. A gap exceeding the existing suspected threshold MUST still emit the existing suspected-stall signal (unchanged — observe logging and diagnostics rely on it) but MUST NOT by itself cause a backoff latch. The stall MUST only become confirmed — and thus eligible to latch in enforce mode and to be reported as the observe "would back off" — if the feed remains stalled past a larger persistence threshold AND no scale-feed recovery occurred since the suspected edge. A stall that recovers before confirmation MUST NOT latch. Confirmation MUST be evaluated event/edge-based on the existing DE1 shot-sample cadence and the recovery edge, with no timer. The DE1-fault-cluster trigger MUST be unchanged.

#### Scenario: Transient stall self-recovers and never latches
- **WHEN** the feed stalls past the suspected threshold and then a recovery occurs before the confirmation threshold
- **THEN** the suspected-stall signal fired (observable) but no confirmed-stall signal fires
- **AND** in enforce mode no latch / disconnect / reconnect occurs
- **AND** in observe mode the suspected stall and the recovery are logged but no "would back off" is recorded

#### Scenario: Sustained stall confirms and latches in enforce
- **WHEN** the feed stalls past the suspected threshold and remains stalled past the confirmation threshold with no recovery
- **THEN** a confirmed-stall signal fires
- **AND** in enforce mode the backoff latches (skip-HIGH + reconnect at BALANCED) exactly as the prior single-stall path did, only later
- **AND** in observe mode the suspected stall and then the confirmed "would back off" are recorded

#### Scenario: DE1-fault cluster path unchanged
- **WHEN** DE1-link faults cluster at the existing rate/window
- **THEN** the existing cluster trigger fires unchanged (it is not subject to scale-feed-stall confirmation)

#### Scenario: Capable hardware unaffected
- **WHEN** the device never produces a sustained stall or a fault cluster
- **THEN** it never latches, never re-detects, and its behavior is byte-identical to before this change regardless of epoch or confirmation
