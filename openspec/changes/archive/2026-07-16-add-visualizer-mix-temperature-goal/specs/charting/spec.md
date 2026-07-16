## ADDED Requirements

### Requirement: Mix Temperature Goal Series

Decenza graphs SHALL offer the machine's mix temperature setpoint (`SetMixTemp`) as a plottable series, so it can be read against the measured "Mix temp" line. The series SHALL be treated as an advanced series: it is off by default and never rendered outside advanced mode.

#### Scenario: Series is hidden by default
- **WHEN** a user who has never changed graph settings views a shot graph
- **THEN** the mix temperature goal line SHALL NOT be rendered
- **AND** the `graph/showTemperatureMixGoal` setting SHALL default to `false`

#### Scenario: Series requires advanced mode
- **WHEN** `graph/showTemperatureMixGoal` is `true` but `shotReview/advancedMode` is `false`
- **THEN** the mix temperature goal line SHALL NOT be rendered
- **AND** it SHALL NOT appear in the graph legend
- **AND** it SHALL NOT appear in the inspect bar readout

#### Scenario: Series renders when enabled in advanced mode
- **WHEN** both `shotReview/advancedMode` and `graph/showTemperatureMixGoal` are `true`
- **AND** the shot carries mix goal data
- **THEN** the mix temperature goal SHALL render as a dashed goal line on the temperature axis
- **AND** a legend entry SHALL be shown for it, adjacent to the "Mix temp" entry
- **AND** its value SHALL be readable from the inspect bar in the display temperature unit

#### Scenario: Series is offered on live, history, and comparison graphs
- **WHEN** the series is enabled in advanced mode
- **THEN** it SHALL be available on the live shot graph, the history/shot-detail graph, and the comparison graph
- **AND** its enabled state SHALL be shared across all three via the single `graph/showTemperatureMixGoal` setting

#### Scenario: Shots without mix goal data render no line
- **WHEN** the series is enabled in advanced mode
- **AND** the displayed shot has an empty mix goal series (recorded before the series existed, or imported)
- **THEN** no mix temperature goal line SHALL be rendered
- **AND** no line SHALL be drawn at zero

#### Scenario: Series color is themeable
- **WHEN** the mix temperature goal line is rendered
- **THEN** its color SHALL come from a `Theme` property, not a hardcoded literal
- **AND** that property SHALL be overridable through custom theme colors like every other series color
