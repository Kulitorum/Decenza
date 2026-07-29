# charting Specification (delta)

## MODIFIED Requirements

### Requirement: Rendering Backend

The charting subsystem SHALL use Qt Graphs (GPU-accelerated) as its sole rendering backend. Qt Charts (Graphics View-based) SHALL NOT be present in the build. Qt Graphs offers two 2D rendering backends — Quick Shapes (`USE_SHAPE_BACKEND`) and Canvas Painter (`USE_PAINTER_BACKEND`, Qt 6.12+) — and Decenza SHALL treat Quick Shapes as the backend it is guaranteed to have, selecting Canvas Painter only where the installed Qt was built with that feature enabled.

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

#### Scenario: Canvas Painter backend is only selected when the Qt build provides it
- **WHEN** a developer intends to set `GraphsView.useCanvasPainter: true`
- **THEN** they SHALL first confirm the installed Qt has the `graphs-2d-high-performance-backend` feature enabled (that feature is `AUTODETECT OFF` upstream, so a stock Qt may not have it)
- **AND** a `GraphsView` SHALL NOT be left with `useCanvasPainter: true` in a build where the feature is absent, because `QGraphsView::setUseCanvasPainter()` is compiled out there and the assignment is a silent no-op
- **AND** every graph SHALL continue to render correctly on the Quick Shapes backend regardless of the flag's value

### Requirement: Performance Parity

The migration from Qt Charts to Qt Graphs SHALL NOT regress graph rendering performance. Any performance claim attributed to a rendering-backend change SHALL be supported by evidence that the backend actually changed.

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

#### Scenario: A backend-attributed measurement proves the backend took effect
- **WHEN** an FPS measurement is recorded in `docs/CLAUDE_MD/PERFORMANCE_BASELINE.md` and attributed to the Canvas Painter backend
- **THEN** the recording SHALL state how the backend was confirmed active (for example: `useCanvasPainterChanged` observed to fire, or the Qt build's feature state inspected)
- **AND** a measurement that cannot show this SHALL be recorded as "backend unconfirmed" rather than attributed to Canvas Painter
