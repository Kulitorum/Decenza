## ADDED Requirements

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

## MODIFIED Requirements

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
