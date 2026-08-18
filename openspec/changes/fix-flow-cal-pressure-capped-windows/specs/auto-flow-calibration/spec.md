## Purpose

Automatically derives a per-profile flow-sensor calibration multiplier from scale data after each
shot, so the DE1's reported flow tracks actual poured weight across both flow-controlled and
pressure-controlled profiles without requiring manual calibration.

## ADDED Requirements

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
machine flow against the frame's target flow before choosing a calibration formula. If the
measured flow deviates from target flow by more than the configured threshold, the system SHALL
treat the window as pressure-capped and compute its ideal using the achieved-flow (pressure-branch)
formula instead of the target-flow (flow-branch) formula. The system SHALL log which formula was
used and the measured-vs-target flow values whenever a flow-classified window is re-routed this
way, so the decision is diagnosable from a submitted debug log.

#### Scenario: Flow-controlled window achieves its target flow
- **WHEN** a window is classified flow-controlled and its measured mean machine flow is within the
  configured deviation threshold of the frame's target flow
- **THEN** the system computes the ideal as `meanWeightFlow / (targetFlow * density)`, independent
  of the current calibration multiplier

#### Scenario: Flow-controlled window does not achieve its target flow (pressure-capped)
- **WHEN** a window is classified flow-controlled but its measured mean machine flow deviates from
  the frame's target flow by more than the configured threshold — for example, because the frame's
  pressure limiter is holding the pump below its flow target
- **THEN** the system computes the ideal using the pressure-branch formula
  (`currentMultiplier * meanWeightFlow / (meanMachineFlow * density)`), using the flow actually
  achieved rather than the unattained target

#### Scenario: Pressure-controlled window
- **WHEN** a window is classified pressure-controlled
- **THEN** the system computes the ideal as
  `currentMultiplier * meanWeightFlow / (meanMachineFlow * density)`

### Requirement: Window ratio sanity guard
The system SHALL reject a window's ideal — before it is accumulated into any batch — when the
window's flow ratio falls outside configured bounds, using the same flow basis (target vs.
achieved) that was used to select the window's calibration formula.

#### Scenario: Flow-classified window that achieved target — guard compares against target
- **WHEN** a window is classified flow-controlled and used the target-flow formula
- **THEN** the ratio guard compares weight flow against the frame's target flow (density-adjusted)
  and rejects the window if outside bounds

#### Scenario: Flow-classified window re-routed to achieved-flow formula — guard compares against achieved flow
- **WHEN** a window is classified flow-controlled but re-routed to the achieved-flow formula per
  the deviation check above
- **THEN** the ratio guard compares weight flow against the window's measured machine flow
  (density-adjusted), consistent with the pressure-branch guard, and rejects the window if outside
  bounds

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
When the deviation-check logic that selects between the target-flow and achieved-flow formulas
changes, the system SHALL clear all profiles' pending (not-yet-applied) batch accumulators as a
one-time migration, so no batch median mixes ideals computed under different formula-selection
logic. Already-applied per-profile multipliers SHALL NOT be reset by this migration.

#### Scenario: App launches for the first time after the formula-selection logic changes
- **WHEN** the app detects it has not yet run the formula-version migration for the current logic
  version
- **THEN** the system SHALL clear every profile's pending batch accumulator and SHALL leave stored
  per-profile and global multipliers unchanged
