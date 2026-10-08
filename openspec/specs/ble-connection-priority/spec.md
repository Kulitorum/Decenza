# ble-connection-priority Specification

## Purpose
Defines the scale BLE connection-priority behavior: the dual-HIGH backoff policy mode (enforce/observe), epoch-scoped persistence of a dual-HIGH-incapable classification, scale-feed stall suspect/confirm detection, the weight-sample delivery contract, and the BALANCED latch. #1176 has two confirmed root causes (SM-X200 / Tab A8 logs): (1) the `ScaleDevice::setWeight()` value-dedup starving the weight pipeline during static windows — fixed in PR Kulitorum/Decenza#1224, priority-independent; and (2) genuine dual-HIGH BLE radio contention on weak hardware dropping changing weight samples mid-shot — mitigated by the skip-HIGH → BALANCED latch (PR Kulitorum/Decenza#1224 is necessary but not sufficient for this case). The latch decision is runtime self-identifying (no device-model list) and must never disconnect the scale mid-shot (PR Kulitorum/Decenza#1226).
## Requirements
### Requirement: Backoff Policy Mode

The dual-HIGH backoff SHALL have a persistent policy mode with exactly two
values: `enforce` (default) and `observe`. The mode SHALL be readable and
settable via MCP. An absent or unrecognized persisted mode MUST be treated as
`enforce`. In `enforce` mode a confirmed trigger MUST always latch skip-HIGH,
subject to "Backoff Does Not Disconnect The Scale During A Shot". In `observe`
mode the backoff SHALL never latch or reconnect.

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

The system SHALL emit a resume event when a stalled scale feed resumes: the
first genuine weight sample after an in-cycle scale-feed stall signal MUST emit
it, carrying the stall gap duration. Recovery SHALL be detected from the sample
and window edges, never from a timer.

#### Scenario: Stalled feed recovers on its own
- **WHEN** a scale-feed stall has been signalled during an extraction/preheat cycle
- **AND** a genuine (non-spike) weight sample subsequently arrives
- **THEN** a resume event is emitted exactly once on that stall→sample edge, carrying the elapsed gap
- **AND** in observe mode a WARN line records the recovery (e.g. "feed RESUMED after X.X s — would-have-been-backoff recovered at HIGH")

#### Scenario: Fault cluster subsides without escalation
- **WHEN** in observe mode a DE1-fault-cluster window elapses without reaching the fire threshold
- **THEN** a log line records that the cluster subsided without escalation

#### Scenario: Recovery signal does not alter SAW
- **WHEN** the resume signal is emitted
- **THEN** stop-at-weight, flow-rate, and per-frame-exit decisions are byte-identical to a run without the signal (recovery is observation only)

#### Scenario: The cascade weighting matches the retry budget in force
- **WHEN** the per-write retry budget changes
- **THEN** the fault weight assigned to a write-failed cascade is re-derived from the starvation that cascade now represents
- **AND** the rate at which the fire threshold is reached on a given real-world fault pattern is not increased merely because cascades became shorter and more frequent

### Requirement: Observe logs a subsiding fault cluster

In `observe` mode, a DE1-fault-cluster window that elapses without reaching the
fire threshold MUST be logged as the cluster subsiding.

#### Scenario: Subsiding cluster is logged in observe

- **WHEN** in observe mode a DE1-fault-cluster window elapses below the fire threshold
- **THEN** a log line records that the cluster subsided

### Requirement: Write-failure cascades are weighted against the retry budget

The weight a write-failed cascade contributes to the DE1-fault-cluster threshold
MUST be derived from the starvation it represents under the current retry
budget, and MUST NOT be a constant tied to a budget that has changed. A single
write failure MUST NOT reach the fire threshold on its own unless it genuinely
represents sustained starvation.

#### Scenario: A lone write failure does not fire

- **WHEN** a single write failure occurs and no sustained starvation follows
- **THEN** the fault cluster does not reach the fire threshold on that failure alone

### Requirement: Latch Clear Preserves The Mode

Clearing the connection-priority latch SHALL remove only the latch record (the `latched`, `triggerKind`, `setTimeIso`, and `buildCode` keys) and MUST NOT remove the persisted `policyMode`.

#### Scenario: Mode survives a latch reset
- **WHEN** the mode is `observe` and the latch is cleared (via the MCP reset tool or a new-build re-detect path)
- **THEN** the latch record is gone
- **AND** the persisted mode is still `observe`

### Requirement: Classification Is Epoch-Scoped, Not Build-Scoped

The persisted dual-HIGH-incapable classification SHALL be scoped to
`kBleDetectionEpoch`, not to the application build or version. The latch MUST be
rehydrated when the stored epoch equals `kBleDetectionEpoch`, and discarded when
they differ. `kBleDetectionEpoch` MUST change only by a deliberate source edit
and MUST NOT be derived from build or version code. A stored build code MAY be
kept for diagnostics but MUST NOT gate rehydration.

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

### Requirement: Android SDK<30 Devices Are Seeded on First Launch

On Android devices whose runtime `Build$VERSION.SDK_INT` is below 30, the first
launch with no persisted connection-priority record under the current epoch MUST
pre-seed the dual-HIGH-incapable classification with trigger kind `seed:sdk<30`.
The seed MUST be written through the normal persisted-latch path so later
launches rehydrate it identically. The seed MUST NOT fire on non-Android
platforms or when the SDK query returns a non-positive value.

#### Scenario: First launch on Android SDK<30 with no record
- **WHEN** the app starts on Android with `SDK_INT < 30` and no persisted connection-priority record exists under the current epoch
- **THEN** the latch is seeded with trigger kind `seed:sdk<30` and persisted under the current epoch
- **AND** both BLE links start at BALANCED on this run with no detection window
- **AND** a WARN line records the seed action with the observed SDK level

#### Scenario: Seed survives across launches without re-evaluating SDK
- **WHEN** a device has a `seed:sdk<30` record from a prior launch and the app starts again
- **THEN** the record is rehydrated via the normal latch path (the SDK is not consulted again on this launch)
- **AND** both BLE links start at BALANCED with no detection window

#### Scenario: MCP clear of a seeded record re-arms detection
- **WHEN** a user clears a `seed:sdk<30` record via the MCP reset
- **THEN** the persisted record is wiped and the in-memory latch is cleared
- **AND** the next scale connect requests HIGH and the runtime detector arms (re-seeding does NOT occur within the same app run)

#### Scenario: Non-Android platforms are unaffected
- **WHEN** the app starts on iOS / macOS / Windows / Linux with no persisted connection-priority record
- **THEN** no seed is written and the runtime detector arms on the next scale connect as before

### Requirement: The seed never overrides an existing record

Once a persisted record exists, the seed MUST NOT fire and MUST NOT re-evaluate
the SDK predicate; the record alone determines behaviour. A deliberate epoch
bump MUST discard the seed with all other prior-epoch records and re-evaluate
from scratch.

#### Scenario: Existing record suppresses the seed

- **WHEN** a persisted connection-priority record exists under the current epoch
- **THEN** no seed is written and the SDK predicate is not consulted

### Requirement: An MCP clear re-arms detection until the next launch

A manual MCP clear MUST wipe the seed record and re-arm the runtime detector for
the rest of the current app run. The seed MUST re-apply on the next launch while
`SDK_INT` stays below 30, because `SDK_INT` is a permanent OS property.

#### Scenario: Next launch re-seeds after a clear

- **WHEN** a user has cleared the seed record and the app restarts on SDK below 30
- **THEN** the seed is written again with trigger kind `seed:sdk<30`

### Requirement: The seed does not gate the runtime detector

The seed bypasses the first-launch detection window only for the SDK below 30
cohort. It is not a gate: the runtime detector MUST continue to handle SDK 30
and later devices on weak chipsets unchanged.

#### Scenario: SDK 30 device still uses the detector

- **WHEN** the app runs on Android SDK 30 or later on weak hardware
- **THEN** the runtime detector arms as it did before the seed existed

### Requirement: Scale-Feed Stall Must Be Confirmed Before Latching

The scale-feed-stall backstop SHALL distinguish a suspected stall from a
confirmed stall. A gap past the existing suspected threshold MUST still emit the
existing suspected-stall signal, but MUST NOT by itself cause a backoff latch.
Confirmation MUST be evaluated on the existing DE1 shot-sample cadence and the
recovery edge, with no timer.

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

### Requirement: A stall is confirmed only when it persists

A stall MUST become confirmed, and so be eligible to latch in enforce mode or to
be reported as observe's would-back-off, only if the feed remains stalled past a
larger persistence threshold and no scale-feed recovery occurred since the
suspected edge. A stall that recovers before confirmation MUST NOT latch.

#### Scenario: Recovery before confirmation blocks the latch

- **WHEN** the feed recovers after the suspected edge but before the persistence threshold
- **THEN** the stall is not confirmed and no latch occurs

### Requirement: DE1-fault cluster trigger is independent of stall confirmation

The DE1-fault-cluster trigger MUST be unchanged and MUST NOT be subject to
scale-feed-stall confirmation.

#### Scenario: Fault cluster fires without a scale stall

- **WHEN** DE1-link faults cluster at the existing rate and window with no scale-feed stall
- **THEN** the existing cluster trigger fires unchanged

### Requirement: Weight-Sample Delivery Drives The Pipeline, Not Value Change

The weight pipeline (per-frame weight-exit, stop-at-weight and the scale-feed
stall detector) SHALL be driven by scale-sample arrival, not value change.
`ScaleDevice` MUST expose an unconditional per-sample signal that fires for
every accepted sample, including repeats of the previous value, and
`WeightProcessor::processWeight` and the stall detector MUST be fed from it.

#### Scenario: Constant weight during preheat does not blind the pipeline
- **WHEN** the scale reports a static (unchanging) weight through an EspressoPreheating / early-extraction window
- **THEN** every such sample still reaches `processWeight` and the stall detector via the unconditional signal
- **AND** the weight-gated frame-exit and SAW evaluate on the real sample stream (no multi-second "missing head", no blow-through on a first delayed reading)

#### Scenario: Deduped signal still serves UI/MQTT
- **WHEN** the weight value is unchanged sample-to-sample
- **THEN** the value-change signal stays deduped (the `weight` Q_PROPERTY / QML / MQTT do not churn)
- **AND** only the unconditional per-sample signal feeds the processing pipeline

### Requirement: The deduplicated value signal stays out of the pipeline

The deduplicated value-change signal, which backs the `weight` property, QML
bindings and MQTT, MUST NOT be the pipeline's input. The synthetic
`setSimulationMode` reset MUST also emit the unconditional signal, so the
contract has no bypass.

#### Scenario: Simulation reset emits the per-sample signal

- **WHEN** simulation mode resets the weight
- **THEN** the unconditional per-sample signal is emitted

### Requirement: Genuine Dual-HIGH Contention On Weak Hardware Is Mitigated By The BALANCED Latch

The skip-HIGH → BALANCED latch SHALL be the mitigation for root cause 2 of
#1176: genuine dual-HIGH BLE radio contention on weak hardware, which drops
changing weight samples mid-shot. The weight-sample delivery fix is necessary
but not sufficient on such hardware, so the latch remains required. The latch
decision MUST be made by runtime self-identification: a genuine sustained stall
or DE1-fault cluster while at HIGH.

#### Scenario: Specs attribute #1176 to two causes
- **WHEN** the `ble-connection-priority` capability is read
- **THEN** #1176 is attributed to two independent root causes: (1) weight-sample dedup (fixed by PR Kulitorum/Decenza#1224, priority-independent) and (2) genuine dual-HIGH radio contention on weak hardware (mitigated by the BALANCED latch)
- **AND** the spec states PR Kulitorum/Decenza#1224 is necessary but not sufficient on weak hardware

#### Scenario: Genuine contention is treated as confirmed, not unproven
- **WHEN** the latch's justification is described
- **THEN** it cites the confirmed SM-X200 evidence (changing-weight loss the dedup cannot cause; clean delivery once at BALANCED)
- **AND** the latch is NOT described as merely defensive/telemetry or as unproven

#### Scenario: No device blocklist
- **WHEN** deciding scale connection priority for any device
- **THEN** the decision MUST NOT consult a hardcoded device-model allow/block list (runtime self-identification only)

### Requirement: Connection priority never consults a device-model list

Connection priority MUST NOT consult a hardcoded device-model allow or block
list for any device.

#### Scenario: Decision uses only runtime signals

- **WHEN** deciding scale connection priority for any device
- **THEN** the decision uses runtime detection only and no model list is read

### Requirement: Backoff Does Not Disconnect The Scale During A Shot

A triggered backoff SHALL always latch skip-HIGH. During an espresso cycle (from
EspressoPreheating through shot end) it MUST NOT disconnect or reconnect the
scale: the latch takes effect at the next natural (re)connect, and a scale-feed-
stall trigger MUST always defer. Only when no shot or preheat is in progress MAY
the backoff disconnect and reconnect immediately, so the next shot starts at
BALANCED.

#### Scenario: Stall confirmed during a shot — latch only, no bounce
- **WHEN** a confirmed scale-feed stall (or a fault cluster) triggers the backoff while a shot/preheat is in progress
- **THEN** skip-HIGH is latched and BALANCED applies at the next natural scale (re)connect
- **AND** the scale is NOT disconnected/reconnected mid-shot

#### Scenario: Fault cluster at idle — reconnect now
- **WHEN** the backoff triggers while no espresso cycle is in progress
- **THEN** the scale is disconnected and reconnected at BALANCED immediately so the upcoming shot starts at BALANCED

#### Scenario: A mid-shot teardown would lose weight

- **WHEN** a stall confirms while a shot or preheat is in progress
- **THEN** the scale is not torn down, because a teardown loses several seconds of weight and cannot rescue the shot


### Requirement: The backoff trigger is calibrated against observed fault patterns, not restated constants

Any constant governing when the connection-priority backoff fires SHALL be traceable to an observed fault pattern rather than to another constant. A weighting expressed in terms of a retry count is coupled to that retry count, and the coupling SHALL be recorded where the weighting is defined so a later change to either is not made in ignorance of the other.

#### Scenario: A latch that demotes every scale for the session

- **WHEN** a single DE1 write failure would be sufficient to latch skip-HIGH app-run-wide
- **THEN** that sufficiency is justified against the fault pattern it is meant to detect, and recorded where the weighting is defined

