# Spec Delta: charting

## ADDED Requirements

### Requirement: Upstream Qt Graphs Readiness Gates

Implementation of this capability SHALL NOT begin until every one of the following upstream conditions is satisfied and documented in `openspec/changes/migrate-charting-to-qt-graphs/tasks.md` under Pre-Stage 0.

#### Scenario: QTBUG-142046 resolution verified
- **WHEN** an engineer checks the axis-range API readiness gate
- **THEN** [QTBUG-142046](https://bugreports.qt.io/browse/QTBUG-142046) (axis range properties return stale/constant values on `GraphsView`) SHALL be marked "Closed" with a Fix Version corresponding to a released Qt version
- **AND** a 10-line QML reproducer SHALL demonstrate that programmatic axis range changes are readable via `ValueAxis.min`/`max` on that Qt version

#### Scenario: Qt Graphs migration guide signals parity
- **WHEN** an engineer checks the feature-parity gate
- **THEN** Qt's published [Qt Graphs 2D migration guide](https://doc.qt.io/qt-6/qtgraphs-migration-guide-2d.html) SHALL either contain no "Missing features" caveat relevant to Decenza (built-in legend, axis auto-ranging, dashed/dotted line strokes, pixel↔data coordinate mapping), OR document sanctioned bridge patterns for each

#### Scenario: Bulk series update API available
- **WHEN** an engineer checks the bulk-update gate
- **THEN** Qt Graphs SHALL expose a documented, supported bulk-update API equivalent to `QXYSeries::replace(QList<QPointF>)` suitable for ~5 Hz dataset replacement without measurable per-call overhead

#### Scenario: Qt 6.11.1 released
- **WHEN** an engineer checks the minimum-version gate
- **THEN** Qt 6.11.1 or later SHALL have been released and available on all Decenza-supported platforms (Windows MSVC 2022, macOS, iOS, Android)

### Requirement: Rendering Backend

The charting subsystem SHALL use Qt Graphs (GPU-accelerated, Qt Quick Shapes-based) as its sole rendering backend. Qt Charts (Graphics View-based) SHALL NOT be present in the build.

#### Scenario: Build configuration omits Qt Charts
- **WHEN** a developer inspects `CMakeLists.txt`
- **THEN** `find_package(Qt6 ...)` SHALL include `Graphs` but NOT `Charts`
- **AND** no source file SHALL `#include <QtCharts/...>` or reference `QtCharts::` namespaces
- **AND** no QML file SHALL `import QtCharts`

#### Scenario: Live shot graph renders on GPU
- **WHEN** a user starts an espresso shot on the Decent tablet (Samsung SM-X210)
- **THEN** `ShotGraph.qml` SHALL render via `GraphsView`
- **AND** the render loop SHALL maintain ≥60 fps during the densest extraction phase
- **AND** no Qt Charts symbols SHALL be loaded at runtime

### Requirement: Axis Ranging

Axes on any Decenza graph SHALL have a defined visible range at all times, either explicitly configured or automatically computed from attached series data.

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

### Requirement: Legend

Any graph that displays two or more user-distinguishable series SHALL provide a legend identifying each series by name and color.

#### Scenario: Legend displays all visible series
- **WHEN** a graph has multiple visible series
- **THEN** a `CustomLegend` SHALL render one entry per series
- **AND** each entry SHALL show the series color and display name
- **AND** entries SHALL match `Theme.qml`-defined fonts and spacing

#### Scenario: Legend toggles series visibility
- **WHEN** a user taps a legend entry
- **THEN** the corresponding series `visible` property SHALL toggle
- **AND** the legend entry SHALL visually indicate the muted state (reduced opacity)

### Requirement: Dashed Line Overlays

Goal curves, frame-boundary markers, and phase-transition indicators SHALL support dashed and dotted stroke patterns.

#### Scenario: Dashed goal curve
- **WHEN** a `DashedLineSeries` is configured with `stroke: DashedLineSeries.DashLine`, `axisX`, `axisY`, and a `points` array
- **THEN** it SHALL render a `ShapePath` with `strokeStyle: ShapePath.DashLine`
- **AND** each point SHALL map from data space to pixel space using the attached axes' current ranges and the parent `GraphsView.plotArea` geometry
- **AND** the pattern SHALL re-map on axis range changes, series changes, and view resize

### Requirement: Pixel-to-Data Coordinate Mapping

Graphs that implement crosshair, tap-to-inspect, or overlay positioning features SHALL expose a deterministic mapping between pixel coordinates and data coordinates.

#### Scenario: Crosshair aligns with underlying data point
- **WHEN** a user taps a location within the plot area of a graph that supports inspection
- **THEN** the crosshair SHALL position at the nearest data point's screen coordinates
- **AND** the inspected data values SHALL be read directly from the series at that x-value
- **AND** the mapping SHALL remain correct across window resizes and axis range changes

### Requirement: Performance Parity

The migration from Qt Charts to Qt Graphs SHALL NOT regress graph rendering performance.

#### Scenario: Live shot FPS maintained
- **WHEN** a shot is running on the Decent tablet with a physical BLE scale reporting at 20 Hz
- **AND** the `ShotGraph` is displaying live pressure, flow, temperature, and weight traces
- **THEN** the median frame time SHALL be ≤16.7 ms (60 fps)
- **AND** the 99th-percentile frame time SHALL be ≤33.3 ms (30 fps)
- **AND** these metrics SHALL meet or beat the documented Qt Charts baseline in `docs/CLAUDE_MD/PERFORMANCE_BASELINE.md`

#### Scenario: History list scroll smoothness
- **WHEN** a user scrolls the shot history list containing ≥200 entries
- **AND** each row contains a `HistoryShotGraph`
- **THEN** scroll FPS SHALL be ≥60 fps on the Decent tablet
- **AND** SHALL meet or beat the Qt Charts baseline

### Requirement: Live Series Rendering

The existing `FastLineRenderer` pattern (custom `QSGGeometryNode` subclass for high-frequency live data) SHALL continue to function as a direct scene-graph renderer, bypassing the Qt Graphs series abstraction.

#### Scenario: FastLineRenderer integration survives migration
- **WHEN** the app is built and run post-migration
- **THEN** live pressure/flow/temperature/weight traces on `ShotGraph` and `SteamGraph` SHALL render via `FastLineRenderer`
- **AND** no measurable performance change SHALL occur in live-trace rendering vs the pre-migration state
- **AND** goal curves and historical traces SHALL render via standard `QtGraphs.LineSeries` or `DashedLineSeries`

### Requirement: Bridge Components Location

Reusable QML components that close feature gaps between Qt Charts and Qt Graphs SHALL live under `qml/components/graphs/` and be registered in `CMakeLists.txt`'s `qt_add_qml_module` file list.

#### Scenario: Bridge components discoverable
- **WHEN** a developer opens `qml/components/graphs/`
- **THEN** they SHALL find at minimum: `AutoRangingAxis.qml`, `CustomLegend.qml`, `DashedLineSeries.qml`
- **AND** each SHALL be documented at the top of the file with usage notes and the Charts feature it replaces

### Requirement: Rollback Safety Through Stages

Each migration stage SHALL be independently revertable via a single `git revert` without requiring data migration, setting changes, or cross-stage coordination.

#### Scenario: Mid-migration rollback
- **WHEN** a regression is discovered after Stage N (1 ≤ N ≤ 3) has merged
- **THEN** reverting Stage N's merge commit SHALL restore the pre-Stage-N graph behavior
- **AND** all earlier stages' graphs SHALL continue to function
- **AND** Qt Charts SHALL still be present in the build through Stage 3
