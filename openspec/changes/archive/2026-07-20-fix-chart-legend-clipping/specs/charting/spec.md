## MODIFIED Requirements

### Requirement: Legend

Any graph that displays two or more user-distinguishable series SHALL provide a legend identifying each series by name and color. The legend SHALL reflow (wrap onto additional lines) when its entries do not fit the available width, and SHALL NOT clip, truncate, or push any entry off-screen at any supported window width.

#### Scenario: Legend displays all visible series
- **WHEN** a graph has multiple visible series
- **THEN** a `CustomLegend` SHALL render one entry per series
- **AND** each entry SHALL show the series color and display name
- **AND** entries SHALL match `Theme.qml`-defined fonts and spacing

#### Scenario: Legend toggles series visibility
- **WHEN** a user taps a legend entry
- **THEN** the corresponding series `visible` property SHALL toggle
- **AND** the legend entry SHALL visually indicate the muted state (reduced opacity)

#### Scenario: Legend wraps instead of clipping at narrow widths
- **WHEN** the total width of all legend entries exceeds the width available to the legend
- **THEN** the legend SHALL wrap its entries onto two or more lines
- **AND** every entry — including the first entry — SHALL be fully visible, with neither its color swatch nor its label clipped at either edge
- **AND** the legend's rendered height SHALL grow to accommodate the wrapped lines rather than the overflow being hidden

#### Scenario: Legend fits on one line at wide widths
- **WHEN** the width available to the legend is sufficient to hold all entries on a single line
- **THEN** the legend SHALL render all entries on one line
- **AND** the entries SHALL remain centered within the available width

#### Scenario: All graph surfaces render through the shared legend component
- **WHEN** a legend is shown on any graph surface — the live shot graph, Shot Detail, post-shot review, the profile-editor graph, the steam graph, or the flow-calibration graph
- **THEN** each SHALL render through the single shared `CustomLegend` component (directly, or via a thin adapter that supplies the entry model)
- **AND** the wrapping-not-clipping behavior SHALL therefore be identical on every surface, defined in exactly one place
- **AND** no per-surface layout override SHALL be required to prevent clipping

#### Scenario: Shared legend preserves each surface's existing behavior
- **WHEN** the shot legend, steam legend, flow-calibration legend, and profile-editor legend all render through the shared component
- **THEN** each SHALL retain its existing behavior: per-series color and name, tap-to-toggle visibility with Settings persistence where present, muted-state opacity, long-press/hover tooltips where present, and the goal-curve line-swatch style where present
- **AND** legends that are display-only (e.g. the profile-editor legend) SHALL render with toggling disabled

#### Scenario: Wrapping does not regress live-graph performance
- **WHEN** the legend is displayed above a live shot graph rendering traces at ~5 Hz
- **THEN** the wrapping layout SHALL NOT introduce a binding loop
- **AND** SHALL NOT trigger a relayout on every incoming data sample
- **AND** the live render loop SHALL continue to meet the frame-time targets in the Performance Parity requirement
