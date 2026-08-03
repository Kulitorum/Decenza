# graph-flow-scale Specification

## Purpose
TBD - created by archiving change add-graph-flow-scale-menu. Update Purpose after archive.
## Requirements
### Requirement: Flow Scale Multiplier

Decenza graphs SHALL offer a flow scale multiplier with the values 1x, 2x and 3x, applied to
flow-family series before they are plotted against the shared pressure/flow axis. The
multiplier is a display transform only and SHALL NOT alter stored, exported, or transmitted
values.

#### Scenario: Multiplier scales the plotted flow trace
- **WHEN** the flow scale is 2x and a sample carries a group flow of 2.4 mL/s
- **THEN** the flow trace SHALL be plotted at 4.8 on the shared axis
- **AND** the shared axis `max` SHALL be unchanged by the multiplier

#### Scenario: Default is 2x
- **WHEN** no flow scale has ever been chosen on this device
- **THEN** the flow scale SHALL be 2x

#### Scenario: Allowed values only
- **WHEN** the stored flow scale is absent, zero, negative, non-numeric, or any value other
  than 1, 2 or 3
- **THEN** the flow scale SHALL resolve to 2x

#### Scenario: Stored shot data is untouched
- **WHEN** any flow scale is active and a shot is saved
- **THEN** the samples written to `shot_samples` SHALL carry true mL/s and g/s values
- **AND** re-opening that shot under a different flow scale SHALL render the same underlying
  data at the newly selected scale

#### Scenario: Visualizer export is untouched
- **WHEN** a shot is uploaded to Visualizer under any flow scale
- **THEN** the exported flow and weight-flow series SHALL carry true mL/s and g/s values

### Requirement: Scaled Series Set

The flow scale SHALL be applied to every series that shares the flow unit family, so that a
scaled graph remains internally consistent.

#### Scenario: Flow-family series are scaled together
- **WHEN** a flow scale other than 1x is active
- **THEN** the group flow trace SHALL be scaled
- **AND** the weight flow rate (g/s) trace SHALL be scaled by the same multiplier
- **AND** the dashed flow goal SHALL be scaled by the same multiplier

#### Scenario: Non-flow series are not scaled
- **WHEN** a flow scale other than 1x is active
- **THEN** pressure, the pressure goal, temperature, the temperature goals, cumulative weight,
  resistance, conductance, Darcy resistance, and the conductance derivative SHALL be plotted
  at their true values

#### Scenario: Flow and weight flow remain comparable
- **WHEN** a flow scale other than 1x is active and both the flow and weight flow rate traces
  are visible
- **THEN** their vertical relationship SHALL be identical to their relationship at 1x

### Requirement: Flow Scale Persistence

The flow scale SHALL persist across sessions and SHALL change only in response to a user
selecting a different value.

#### Scenario: Selection persists across restart
- **WHEN** the user selects 3x and the app is restarted
- **THEN** the flow scale SHALL still be 3x

#### Scenario: Nothing else changes the value
- **WHEN** a shot starts or ends, a profile is loaded, a different graph surface is opened, or
  the right-axis mode is changed
- **THEN** the flow scale SHALL be unchanged

#### Scenario: One scale across all graphs
- **WHEN** the flow scale is changed on any surface that offers it
- **THEN** the live shot graph, the history/shot-detail graph, the post-shot review graph and
  the comparison graph SHALL all render at the new scale

### Requirement: Axis Titling Under A Multiplier

The shared left axis title SHALL state only the units it actually reads.

#### Scenario: Title at 1x
- **WHEN** the flow scale is 1x
- **THEN** the shared axis title SHALL name both the pressure and flow units, as it does today

#### Scenario: Title at 2x or 3x
- **WHEN** the flow scale is 2x or 3x
- **THEN** the shared axis title SHALL name the pressure unit only

### Requirement: Flow Right-Axis Mode

The right axis SHALL offer a third mode, flow, in addition to weight and temperature, so that
a multiplied flow trace remains numerically readable.

#### Scenario: Flow mode labels the true flow scale
- **WHEN** the right axis is in flow mode, the shared axis max is 12 and the flow scale is 3x
- **THEN** the right label column SHALL run from 0 to 4.0
- **AND** its unit suffix SHALL name the flow unit

#### Scenario: Flow mode at 1x
- **WHEN** the right axis is in flow mode and the flow scale is 1x
- **THEN** the right label column SHALL match the shared axis exactly

#### Scenario: Tapping cycles all three modes
- **WHEN** the user taps the right-axis label column
- **THEN** the mode SHALL advance to the next of weight, temperature and flow, wrapping around
- **AND** the new mode SHALL persist across sessions

#### Scenario: Mode and multiplier are independent
- **WHEN** the user changes the flow scale
- **THEN** the right-axis mode SHALL NOT change
- **AND** when the user changes the right-axis mode the flow scale SHALL NOT change

#### Scenario: Accessible name reports the current mode
- **WHEN** a screen reader focuses the right-axis label column
- **THEN** its accessible name SHALL state the current mode and that tapping changes it

### Requirement: Readouts Report True Values

Every numeric readout of a scaled series SHALL report the true measured value, never the
plotted value.

#### Scenario: Inspect bar un-scales
- **WHEN** a flow scale other than 1x is active and the user inspects a point whose true group
  flow is 2.4 mL/s
- **THEN** the inspect bar SHALL report 2.4 mL/s

#### Scenario: Weight flow readout un-scales
- **WHEN** a flow scale other than 1x is active and the user inspects a point whose true weight
  flow rate is 1.9 g/s
- **THEN** the readout SHALL report 1.9 g/s

#### Scenario: Every surface un-scales
- **WHEN** a flow-family value is shown in a crosshair label, a comparison inspect bar, a phase
  summary, a stat tile, or any other numeric readout
- **THEN** it SHALL report the true value regardless of the active flow scale

### Requirement: Right-Axis Mode Setting Migration

The existing two-state right-axis boolean SHALL be replaced by a three-state mode without
resetting any user's choice.

#### Scenario: Existing weight preference migrates
- **WHEN** a device holds `graph/showWeightAxis` set to true and no `graph/rightAxisMode`
- **THEN** the right-axis mode SHALL resolve to weight

#### Scenario: Existing temperature preference migrates
- **WHEN** a device holds `graph/showWeightAxis` set to false and no `graph/rightAxisMode`
- **THEN** the right-axis mode SHALL resolve to temperature

#### Scenario: Unrecognised stored mode
- **WHEN** the stored right-axis mode is absent, empty, or not one of weight, temperature or
  flow
- **THEN** the right-axis mode SHALL resolve to weight

### Requirement: Home Screen Widget Reflects The Scale

Graph-bearing widgets SHALL re-render when the flow scale or right-axis mode changes.

#### Scenario: Widget observes the new settings
- **WHEN** the flow scale or the right-axis mode changes
- **THEN** any chart widget that renders flow SHALL re-render at the new setting rather than
  from a cached earlier value

