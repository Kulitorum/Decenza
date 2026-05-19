# ble-connection-priority Specification

## Purpose
Defines the scale BLE connection-priority behavior: the dual-HIGH backoff policy mode (enforce/observe), epoch-scoped persistence of a dual-HIGH-incapable classification, scale-feed stall suspect/confirm detection, the weight-sample delivery contract, and the BALANCED latch. #1176 has two confirmed root causes (SM-X200 / Tab A8 logs): (1) the `ScaleDevice::setWeight()` value-dedup starving the weight pipeline during static windows — fixed in #1224, priority-independent; and (2) genuine dual-HIGH BLE radio contention on weak hardware dropping changing weight samples mid-shot — mitigated by the skip-HIGH → BALANCED latch (#1224 is necessary but not sufficient for this case). The latch decision is runtime self-identifying (no device-model list) and must never disconnect the scale mid-shot (#1226).
## Requirements
### Requirement: Backoff Policy Mode

The scale connection-priority dual-HIGH backoff SHALL have a persistent policy mode with exactly two values: `enforce` (default) and `observe`. The mode SHALL be readable and settable via MCP. When the persisted mode is absent or unrecognized, the system MUST treat it as `enforce`. In `enforce` mode the backoff MUST always latch skip-HIGH on a confirmed trigger but MUST honor "Backoff Does Not Disconnect The Scale During A Shot" — it does NOT unconditionally disconnect/reconnect the scale mid-shot. `observe` mode runs detection inert (for tuning the engage point on weak hardware) and never latches or reconnects.

#### Scenario: Default mode on a fresh install
- **WHEN** the app starts with no persisted `connectionPriority/policyMode` key
- **THEN** the active backoff mode is `enforce`
- **AND** the detector, latch, and structural-confirmation behavior are as specified by the current requirements (including the no-mid-shot-teardown rule)

#### Scenario: Mode persists until explicitly changed
- **WHEN** the operator sets the mode to `observe`
- **AND** the app is restarted, or upgraded to a new build
- **THEN** the active mode is still `observe` (the mode is NOT build-scoped, unlike the latch)
- **AND** the mode only returns to `enforce` when explicitly set back via MCP

#### Scenario: Enforce mode latches but does not bounce mid-shot
- **WHEN** the mode is `enforce` and a confirmed scale-feed stall or DE1-fault cluster is detected while a shot/preheat is in progress
- **THEN** the system latches skip-HIGH app-run-wide (epoch-persisted)
- **AND** it does NOT disconnect/reconnect the scale until the next natural (re)connect; an idle trigger still reconnects immediately at BALANCED

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

### Requirement: Weight-Sample Delivery Drives The Pipeline, Not Value Change

The weight-processing pipeline — per-frame weight-exit, stop-at-weight (SAW), and the scale-feed stall detector — SHALL be driven by scale-sample *arrival*, not by scale-value *change*. `ScaleDevice` MUST expose an unconditional per-sample signal that fires for every accepted weight sample including ones whose value equals the previous reading; `WeightProcessor::processWeight` and the connection-priority stall detector MUST be fed from that signal. The deduped value-change signal (which backs the `weight` Q_PROPERTY / QML bindings / MQTT) MUST NOT be the pipeline's input. The synthetic `setSimulationMode` reset MUST also emit the unconditional signal so the contract has no bypass.

This is **root cause 1** of #1176: a static or slowly-changing reading during preheat/infuse, deduped away by `ScaleDevice::setWeight()`'s long-standing `if (m_weight != weight)` guard (present since the repository's initial commit), starved the weight-gated frame-exit so it blew through on the first delayed sample (reported "Infusing abandoned at ~6 s / 9.6 g"). Priority-independent. Shipped: #1224.

#### Scenario: Constant weight during preheat does not blind the pipeline
- **WHEN** the scale reports a static (unchanging) weight through an EspressoPreheating / early-extraction window
- **THEN** every such sample still reaches `processWeight` and the stall detector via the unconditional signal
- **AND** the weight-gated frame-exit and SAW evaluate on the real sample stream (no multi-second "missing head", no blow-through on a first delayed reading)

#### Scenario: Deduped signal still serves UI/MQTT
- **WHEN** the weight value is unchanged sample-to-sample
- **THEN** the value-change signal stays deduped (the `weight` Q_PROPERTY / QML / MQTT do not churn)
- **AND** only the unconditional per-sample signal feeds the processing pipeline

### Requirement: Genuine Dual-HIGH Contention On Weak Hardware Is Mitigated By The BALANCED Latch

The connection-priority skip-HIGH → BALANCED latch SHALL be specified as the warranted mitigation for **root cause 2** of #1176: genuine dual-HIGH BLE radio contention on weak hardware, which drops *changing* weight samples mid-shot and is independent of, and not fixed by, root cause 1. This cause is confirmed (not hypothetical) from the reporter's Samsung SM-X200 / Tab A8 logs: on a build with zero backoff code the cup reached 9.6 g by 6.3 s of active extraction with **zero** samples delivered while pressure was building — a monotonic climb of distinct values that the value-dedup provably cannot suppress; and on a later build the same device, once reconnected at BALANCED, delivered changing weight cleanly to shot end. Therefore the spec MUST state that the #1224 weight-sample-delivery fix is **necessary but not sufficient on weak hardware**, and the BALANCED latch remains required for cause 2. The latch decision MUST be made by **runtime self-identification** (a genuine sustained stall / DE1-fault cluster while at HIGH); the system MUST NOT gate connection priority on a device-model allow/block list.

#### Scenario: Specs attribute #1176 to two causes
- **WHEN** the `ble-connection-priority` capability is read
- **THEN** #1176 is attributed to two independent root causes: (1) weight-sample dedup (fixed by #1224, priority-independent) and (2) genuine dual-HIGH radio contention on weak hardware (mitigated by the BALANCED latch)
- **AND** the spec states #1224 is necessary but not sufficient on weak hardware

#### Scenario: Genuine contention is treated as confirmed, not unproven
- **WHEN** the latch's justification is described
- **THEN** it cites the confirmed SM-X200 evidence (changing-weight loss the dedup cannot cause; clean delivery once at BALANCED)
- **AND** the latch is NOT described as merely defensive/telemetry or as unproven

#### Scenario: No device blocklist
- **WHEN** deciding scale connection priority for any device
- **THEN** the decision MUST NOT consult a hardcoded device-model allow/block list (runtime self-identification only)

### Requirement: Backoff Does Not Disconnect The Scale During A Shot

A triggered connection-priority backoff SHALL always latch skip-HIGH (epoch-scoped persistence), but it MUST NOT disconnect/reconnect the scale while an espresso cycle is in progress (from EspressoPreheating through shot end). During a shot the decision MUST be latch-only and take effect at the next natural (re)connect. A mid-shot scale teardown is forbidden because it loses several seconds of weight and cannot rescue the in-progress shot (it caused the user-visible "no scale — estimating" failures); the `scale-feed-stall` trigger is in-shot by construction and therefore MUST always defer. Only when no shot/preheat is in progress (e.g. a DE1-fault cluster shortly after connect at idle) MAY the backoff disconnect and reconnect immediately so the next shot starts at BALANCED. This fixes the remediation *mechanism*, not the *need* for the latch. (Shipped: #1226.)

#### Scenario: Stall confirmed during a shot — latch only, no bounce
- **WHEN** a confirmed scale-feed stall (or a fault cluster) triggers the backoff while a shot/preheat is in progress
- **THEN** skip-HIGH is latched and BALANCED applies at the next natural scale (re)connect
- **AND** the scale is NOT disconnected/reconnected mid-shot

#### Scenario: Fault cluster at idle — reconnect now
- **WHEN** the backoff triggers while no espresso cycle is in progress
- **THEN** the scale is disconnected and reconnected at BALANCED immediately so the upcoming shot starts at BALANCED

