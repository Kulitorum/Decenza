## MODIFIED Requirements

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
