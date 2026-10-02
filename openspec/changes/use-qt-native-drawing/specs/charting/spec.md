## MODIFIED Requirements

### Requirement: Dashed Line Overlays

Goal curves, frame-boundary markers, and phase-transition indicators SHALL support dashed and dotted stroke patterns.

#### Scenario: Dashed goal curve
- **WHEN** a goal curve, frame-boundary marker or phase marker is drawn
- **THEN** it SHALL be a native Qt Graphs `LineSeries` with `strokeStyle: LineSeries.DashLine` and a `dashPattern`
- **AND** a series read against a range other than the graph's own Y axis SHALL map through a hidden `ValueAxis` set as its `axisY`
- **AND** Qt Graphs SHALL do the data-to-pixel mapping, so an axis range change, data change or view resize SHALL NOT remap points in JavaScript

### Requirement: Live Series Rendering

The existing `FastLineRenderer` pattern (custom `QSGGeometryNode` subclass for high-frequency live data) SHALL continue to function as a direct scene-graph renderer, bypassing the Qt Graphs series abstraction.

#### Scenario: FastLineRenderer integration survives migration
- **WHEN** the app is built and run post-migration
- **THEN** live pressure/flow/temperature/weight traces on `ShotGraph` and `SteamGraph` SHALL render via `FastLineRenderer`
- **AND** no measurable performance change SHALL occur in live-trace rendering vs the pre-migration state
- **AND** goal curves and historical traces SHALL render via standard `QtGraphs.LineSeries`

### Requirement: Bridge Components Location

Reusable QML components that close feature gaps in Qt Graphs SHALL live under `qml/components/graphs/` and be registered in `CMakeLists.txt`'s `qt_add_qml_module` file list.

#### Scenario: Bridge components discoverable
- **WHEN** a developer opens `qml/components/graphs/`
- **THEN** they SHALL find at minimum: `AutoRangingAxis.qml`, `CustomLegend.qml`, `GraphSeriesInstantiator.qml`
- **AND** each SHALL be documented at the top of the file with what it supplies that Qt Graphs does not
