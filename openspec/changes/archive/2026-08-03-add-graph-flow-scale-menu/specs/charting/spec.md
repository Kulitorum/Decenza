## MODIFIED Requirements

### Requirement: Axis Ranging

Axes on any Decenza graph SHALL have a defined visible range at all times, either explicitly configured or automatically computed from attached series data. Where a series is plotted through a display transform such as the flow scale multiplier, auto-ranging SHALL be computed from the untransformed values.

#### Scenario: Auto-ranging axis tracks series data
- **WHEN** an `AutoRangingAxis` is bound to one or more `XYSeries` with a non-empty point set
- **THEN** its `min` SHALL equal the minimum y-value across all attached series minus `padding`
- **AND** its `max` SHALL equal the maximum y-value across all attached series plus `padding`
- **AND** it SHALL recompute on any `pointsChanged` signal from an attached series

#### Scenario: Auto-ranging axis respects floor and ceiling clamps
- **WHEN** an `AutoRangingAxis` has a non-null `minFloor` and the data minimum would be below it
- **THEN** the axis `min` SHALL be clamped to `minFloor`
- **AND** the same SHALL apply for `maxCeiling`

#### Scenario: Explicit range overrides
- **WHEN** a graph's axis has an explicit `min` and `max` set directly (not `AutoRangingAxis`)
- **THEN** the axis SHALL use exactly those values regardless of series data

#### Scenario: Auto-ranging ignores the flow scale multiplier
- **WHEN** an auto-ranged shared pressure/flow axis is computed and a flow scale other than 1x is active
- **THEN** the contribution of the flow, weight flow rate and flow goal series SHALL be their unmultiplied values
- **AND** the resulting axis max SHALL be identical to the max computed at 1x for the same shot

#### Scenario: Multiplied trace may exceed the axis
- **WHEN** a flow scale other than 1x is active and a multiplied flow value exceeds the axis max
- **THEN** the trace SHALL clip at the axis boundary
- **AND** the axis SHALL NOT grow to accommodate it
