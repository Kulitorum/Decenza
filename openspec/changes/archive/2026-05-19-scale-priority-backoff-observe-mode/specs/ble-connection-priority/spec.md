## ADDED Requirements

### Requirement: Backoff Policy Mode

The scale connection-priority dual-HIGH backoff SHALL have a persistent policy mode with exactly two values: `enforce` (default) and `observe`. The mode SHALL be readable and settable via MCP. When the persisted mode is absent or unrecognized, the system MUST treat it as `enforce`.

#### Scenario: Default mode on a fresh install
- **WHEN** the app starts with no persisted `connectionPriority/policyMode` key
- **THEN** the active backoff mode is `enforce`
- **AND** the detector, latch, disconnect/reconnect, and structural-confirmation behavior are byte-identical to the pre-change behavior

#### Scenario: Mode persists until explicitly changed
- **WHEN** the operator sets the mode to `observe`
- **AND** the app is restarted, or upgraded to a new build
- **THEN** the active mode is still `observe` (the mode is NOT build-scoped, unlike the latch)
- **AND** the mode only returns to `enforce` when explicitly set back via MCP

#### Scenario: Enforce mode is unchanged
- **WHEN** the mode is `enforce` and a DE1-fault cluster or scale-feed stall is detected
- **THEN** the system latches skip-HIGH app-run-wide, disconnects, and reconnects the scale at BALANCED exactly as before this change

### Requirement: Observe Mode Detects Without Acting

In `observe` mode the detector SHALL arm and evaluate the identical DE1-fault-cluster and scale-feed-stall trigger conditions used in `enforce` mode, but on a would-fire it MUST take no action: it MUST NOT latch skip-HIGH, MUST NOT disconnect or reconnect the scale, and MUST keep the link at HIGH. Observe detection MUST NOT be fire-once — after a would-fire it MUST continue detecting subsequent episodes for the remainder of the run.

#### Scenario: Would-fire in observe takes no action
- **WHEN** the mode is `observe` and the scale-feed-stall (or DE1-fault-cluster) condition is met
- **THEN** a WARN log line is emitted clearly marked as observe / no-action (e.g. "WOULD back off (trigger=<kind>) — observe mode, no action; link stays HIGH")
- **AND** the skip-HIGH latch is NOT set, the scale is NOT disconnected, and the link remains HIGH

#### Scenario: Observe keeps detecting after a would-fire
- **WHEN** a would-fire has already been logged in observe mode this run
- **AND** a later, independent stall or fault cluster occurs
- **THEN** the later episode is also detected and logged (the detector re-arms / resets its cluster window rather than latching off)

#### Scenario: Trigger conditions identical to enforce
- **WHEN** the same sequence of DE1 faults / scale-stall inputs is replayed in `enforce` and in `observe`
- **THEN** both reach the trigger point on exactly the same input; only the consequence differs (act vs log-only)

### Requirement: Observe Mode Forces HIGH

In `observe` mode the scale link SHALL be forced to HIGH on connect. Entering observe MUST override any pre-existing persisted BALANCED latch so detection runs against the real at-risk path, but MUST NOT erase the latch value; switching back to `enforce` MUST restore honoring of the prior latch state.

#### Scenario: Observe overrides an existing BALANCED latch
- **WHEN** a build-scoped skip-HIGH latch is persisted (scale would normally reconnect at BALANCED)
- **AND** the mode is set to `observe` and the scale reconnects
- **THEN** the scale link is requested at HIGH (the latch is not consulted for the priority decision)
- **AND** the persisted latch value is left intact on disk and in memory

#### Scenario: Switching back to enforce restores the latch
- **WHEN** the mode is changed from `observe` back to `enforce` and the scale reconnects
- **AND** a skip-HIGH latch was still persisted
- **THEN** the scale reconnects at BALANCED, honoring that latch exactly as before observe was entered

### Requirement: Scale-Feed Recovery Is Observable

The system SHALL emit an event-based recovery signal when a stalled scale feed resumes: after `WeightProcessor` has signalled an in-cycle scale-feed stall, the first subsequent genuine weight sample MUST emit a resume event carrying the stall gap duration. A DE1-fault-cluster window that elapses without reaching the fire threshold MUST be logged (observe mode) as the cluster subsiding. No timer may be used to detect recovery — it MUST be driven by the sample/window edge.

#### Scenario: Stalled feed recovers on its own
- **WHEN** a scale-feed stall has been signalled during an extraction/preheat cycle
- **AND** a genuine (non-spike) weight sample subsequently arrives
- **THEN** a resume event is emitted exactly once on that stall→sample edge, carrying the elapsed gap
- **AND** in observe mode a WARN line records the recovery (e.g. "feed RESUMED after X.X s — would-have-been-backoff recovered at HIGH")

#### Scenario: Fault cluster subsides without escalation
- **WHEN** in observe mode a DE1-fault-cluster window elapses without reaching the ≥2/20 s fire threshold
- **THEN** a log line records that the cluster subsided without escalation

#### Scenario: Recovery signal does not alter SAW
- **WHEN** the resume signal is emitted
- **THEN** stop-at-weight, flow-rate, and per-frame-exit decisions are byte-identical to a run without the signal (recovery is observation only)

### Requirement: Latch Clear Preserves The Mode

Clearing the connection-priority latch SHALL remove only the latch record (the `latched`, `triggerKind`, `setTimeIso`, and `buildCode` keys) and MUST NOT remove the persisted `policyMode`.

#### Scenario: Mode survives a latch reset
- **WHEN** the mode is `observe` and the latch is cleared (via the MCP reset tool or a new-build re-detect path)
- **THEN** the latch record is gone
- **AND** the persisted mode is still `observe`
