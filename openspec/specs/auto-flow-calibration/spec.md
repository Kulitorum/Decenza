# auto-flow-calibration Specification

## Purpose
Automatically derives a per-profile flow-sensor calibration multiplier from scale data after each
shot, so the DE1's reported flow tracks actual poured weight across both flow-controlled and
pressure-controlled profiles without requiring manual calibration.

## Requirements

### Requirement: Steady-state window selection
The system SHALL scan a completed shot's telemetry for the longest contiguous window meeting
minimum stability, pressure, flow-magnitude, and scale-recency criteria, and SHALL use only that
window as the basis for a calibration ideal.

#### Scenario: No qualifying window
- **WHEN** a shot has no telemetry segment meeting the minimum duration and sample-count
  thresholds
- **THEN** the system SHALL skip calibration for that shot and log that no qualifying window was
  found

### Requirement: Window-level flow/pressure classification
The system SHALL classify each selected window as flow-controlled or pressure-controlled based on
which profile frames were active during the window (via the shot's recorded phase-marker stream),
not on the profile as a whole. A window whose frames are mixed flow- and pressure-controlled SHALL
be skipped as ambiguous.

#### Scenario: Window entirely inside a flow-controlled frame
- **WHEN** every frame touched by the selected window has `pump == flow` and a flow target above
  the minimum threshold
- **THEN** the window is classified flow-controlled and its target flow is the touched frame's
  flow target closest to the window's measured machine flow

#### Scenario: Window entirely inside a pressure-controlled frame
- **WHEN** every frame touched by the selected window has `pump == pressure`
- **THEN** the window is classified pressure-controlled

#### Scenario: Window spans a flow/pressure transition
- **WHEN** the selected window's frames are a mix of flow-controlled and pressure-controlled
- **THEN** the system SHALL skip the window and log that it spans mixed frames

### Requirement: Achieved-flow deviation check for flow-controlled windows
For a window classified flow-controlled, the system SHALL compare the window's measured mean
machine flow against the frame's target flow before computing a calibration ideal. If the measured
flow falls short of (undershoots) target flow by more than the configured threshold, the system
SHALL skip the window entirely — the shot contributes no ideal to the profile's batch — because the
window measured the pump model at an operating point the profile does not pour at, and a single
per-profile multiplier cannot describe two operating points at once. The check SHALL NOT trigger on
overshoot (measured flow above target) regardless of magnitude — a pressure ceiling can only hold
flow below its setpoint, never push it above, so an overshoot reading has no pressure-cap
explanation. The system SHALL log the skip with the measured flow, the target flow and the
deviation, so a shot that produced no ideal is explicable from a submitted debug log.

#### Scenario: Flow-controlled window achieves its target flow
- **WHEN** a window is classified flow-controlled and its measured mean machine flow is within the
  configured deviation threshold of the frame's target flow
- **THEN** the system computes the ideal as
  `currentMultiplier * meanWeightFlow / (meanMachineFlow * density)`

#### Scenario: Flow-controlled window undershoots its target flow (pressure-capped)
- **WHEN** a window is classified flow-controlled but its measured mean machine flow falls short of
  the frame's target flow by more than the configured threshold — for example, because the frame's
  pressure limiter is holding the pump below its flow target
- **THEN** the system skips the window, contributes no ideal for that shot, and logs the measured
  flow, the target flow and the deviation
- **AND** the profile's pending batch and stored multiplier are left unchanged by that shot

#### Scenario: Every window on a profile misses target
- **WHEN** a profile's shots consistently fail to reach the frame's target flow
- **THEN** that profile accumulates no ideals and its stored multiplier does not move, rather than
  being driven by measurements taken at an unreached flow rate

#### Scenario: Flow-controlled window overshoots its target flow
- **WHEN** a window is classified flow-controlled and its measured mean machine flow exceeds the
  frame's target flow by more than the configured threshold
- **THEN** the system does NOT skip the window — it computes the ideal as
  `currentMultiplier * meanWeightFlow / (meanMachineFlow * density)`, the same as a window that
  achieved target

#### Scenario: Pressure-controlled window
- **WHEN** a window is classified pressure-controlled
- **THEN** the system computes the ideal as
  `currentMultiplier * meanWeightFlow / (meanMachineFlow * density)`, unaffected by this requirement

### Requirement: Batched median updates
The system SHALL accumulate per-profile ideals across a fixed batch size, persisted across app
restarts, and SHALL update the profile's calibration multiplier only from the batch median, not
from any single shot's ideal.

#### Scenario: Batch incomplete
- **WHEN** fewer than the configured batch size of ideals have accumulated for a profile
- **THEN** the system SHALL accumulate the new ideal and SHALL NOT change the stored multiplier

#### Scenario: Batch complete with meaningful change
- **WHEN** the batch reaches its configured size and the blended median differs from the current
  multiplier by more than the configured minimum change threshold
- **THEN** the system SHALL update the profile's multiplier from the blend and SHALL reset the
  batch accumulator

#### Scenario: Batch complete with negligible change
- **WHEN** the batch reaches its configured size but the blended median differs from the current
  multiplier by less than the configured minimum change threshold
- **THEN** the system SHALL leave the stored multiplier unchanged and SHALL reset the batch
  accumulator

### Requirement: Sanity bounds on computed multipliers
The system SHALL clamp every computed multiplier to a firmware-dependent range before it can be
stored or applied, so a corrupted or anomalous computation cannot push the machine's flow
calibration to an extreme value.

#### Scenario: Computed multiplier outside firmware-dependent bounds
- **WHEN** a computed multiplier (single-window ideal or batch-median update) falls outside the
  connected DE1's firmware-dependent bounds
- **THEN** the system SHALL clamp the value to those bounds before storing or applying it

### Requirement: Formula-version migration on behavior change
When a change alters how calibration ideals are computed, or which windows produce one at all, the
system SHALL clear all profiles' pending (not-yet-applied) batch accumulators as a one-time
migration under a version key, so no batch median mixes ideals computed under different rules.
Already-applied per-profile multipliers and the global multiplier SHALL NOT be reset by this
migration when the change is per-window rather than systemic, so users whose data does not exhibit
the defect are not forced back through several batches of re-convergence.

#### Scenario: App launches for the first time after the formula-selection logic changes
- **WHEN** the app detects it has not yet run the formula-version migration for the current logic
  version
- **THEN** the system SHALL clear every profile's pending batch accumulator and SHALL leave stored
  per-profile and global multipliers unchanged

#### Scenario: Upgrade to a build that unifies the ideal formula
- **WHEN** the app starts for the first time on a build where flow-controlled windows use the
  pump-model-error formula rather than a target-anchored one
- **THEN** the system SHALL clear every profile's pending batch accumulator, so no median mixes
  ideals from the two formulas
- **AND** stored per-profile and global multipliers SHALL be left unchanged

#### Scenario: Upgrade to a build that skips off-target windows
- **WHEN** the app starts for the first time on a build where off-target flow windows are skipped
  rather than re-routed through the achieved-flow formula
- **THEN** pending per-profile batch accumulators are cleared once and the migration is recorded
- **AND** stored per-profile multipliers and the global multiplier are left unchanged

#### Scenario: Subsequent launches
- **WHEN** the app starts again on the same build
- **THEN** the migration does not run a second time and pending accumulators are preserved

### Requirement: The flow calibration multiplier a shot ran under is recorded on the shot
The system SHALL record, on each saved shot, the effective flow calibration multiplier that was in
force while that shot was pulled — the same value `effectiveFlowCalibration()` resolves for the
shot's profile and that is written to the machine. The recorded value SHALL be the multiplier in
force at shot START, so an auto-calibration update computed from the shot itself never overwrites
the value the shot is recorded under.

#### Scenario: Shot pulled at a per-profile multiplier
- **WHEN** a shot completes on a profile that has a per-profile flow calibration multiplier and auto
  flow calibration is enabled
- **THEN** the saved shot records that per-profile multiplier

#### Scenario: Shot pulled at the global multiplier
- **WHEN** a shot completes on a profile with no per-profile multiplier, or with auto flow
  calibration disabled
- **THEN** the saved shot records the global flow calibration multiplier

#### Scenario: Auto-calibration updates the multiplier on the same shot
- **WHEN** a shot completes and the auto-calibration batch for its profile completes on that shot,
  writing a new per-profile multiplier before the shot is saved
- **THEN** the saved shot records the multiplier that was in force during the pour, not the newly
  written one

### Requirement: An unrecorded multiplier is distinguishable from a recorded one
The system SHALL represent "no multiplier recorded" as an absent value, never as the neutral
multiplier 1.0, and SHALL NOT infer a multiplier for a shot that has none. Shots saved before this
capability existed, and shots imported from a source that carries no multiplier, SHALL read as
unrecorded.

#### Scenario: Shot saved before the field existed
- **WHEN** a shot recorded before this capability is loaded
- **THEN** its flow calibration multiplier reads as unrecorded, and no value is substituted

#### Scenario: Consumer reads a shot with no recorded multiplier
- **WHEN** a shot with no recorded multiplier is projected for a consumer (MCP tool, AI payload,
  export)
- **THEN** the field is omitted rather than emitted as 0 or 1.0

### Requirement: The recorded multiplier survives transfer and import
The system SHALL carry a shot's recorded flow calibration multiplier through device-to-device
transfer and through shot import, and SHALL leave it unrecorded when the source has no such value
rather than substituting one.

#### Scenario: Device-to-device transfer
- **WHEN** shots are transferred from a device whose database records the multiplier
- **THEN** each transferred shot keeps its recorded multiplier

#### Scenario: Transfer from a source predating the field
- **WHEN** shots are transferred from a database that has no flow calibration column
- **THEN** the imported shots read as unrecorded

### Requirement: Single window ratio guard for both control modes
The system SHALL reject a window's ideal — before it is accumulated into any batch — when the
window's density-adjusted machine-flow-to-weight-flow ratio falls outside configured bounds. ONE
guard SHALL apply to both control modes, on the two quantities the ideal divides.

#### Scenario: Any window whose machine and weight flow disagree
- **WHEN** a window's density-adjusted machine-flow-to-weight-flow ratio falls outside the
  configured bounds, in either control mode
- **THEN** the system rejects the window, contributes no ideal, and logs the ratio, the bounds and
  the window's control mode

#### Scenario: Flow-controlled window that overshoots its target
- **WHEN** a window is classified flow-controlled and its measured machine flow runs well above the
  frame's target — which the off-target check does not reject, being undershoot-only
- **THEN** the guard still bounds it, because the guarded quantity is the machine flow the ideal
  divides by rather than the frame's target

### Requirement: One calibration ideal formula for both control modes
The system SHALL compute a window's calibration ideal as
`currentMultiplier * meanWeightFlow / (meanMachineFlow * density)` regardless of whether the window
was flow- or pressure-controlled. The system SHALL NOT anchor a flow-controlled window's ideal to
the frame's target flow. Window classification SHALL continue to select whether the off-target check applies, but SHALL NOT
select a formula or a ratio guard — one ratio guard, on the quantities the formula divides, applies
to both control modes.

#### Scenario: Machine whose reported flow already matches the scale
- **WHEN** a window's mean weight flow equals its mean machine flow times the density constant
- **THEN** the computed ideal equals the current multiplier, so a converged machine's multiplier
  does not move

#### Scenario: Same machine measured at two different multipliers
- **WHEN** the same machine and profile produce two windows at different stored multipliers, and
  the machine holds its reported flow constant by delivering water in inverse proportion to the
  multiplier
- **THEN** both windows produce the same ideal, so the update converges rather than tracking the
  current multiplier

#### Scenario: Flow-controlled window on target
- **WHEN** a window is classified flow-controlled and passes the off-target check
- **THEN** the system computes the ideal with the same formula a pressure-controlled window uses,
  and logs the reported flow, the frame's target flow and the current multiplier
