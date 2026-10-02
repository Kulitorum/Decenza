## Why

Decenza draws through several pieces of its own code that stock Qt now covers:

- **The cup.** CupFillView drew through our `JsCanvasPainterItem` / `JsCanvasContext` wrapper (~620 lines) over Qt Canvas Painter. Qt 6.12 ships the same thing as `Canvas2D`. It records once per frame from `updatePolish()` through native engine functions, where ours recorded on every `requestPaint()` through the meta-object system. A Mac Time Profiler run during a simulated shot put cup drawing at ~23% of main-thread time (#1976).
- **Dashed lines and right-axis ranges.** `DashedLineSeries.qml` existed because Qt Graphs lines were solid only, and it mapped every point from data to pixels in JS on every data or axis change. The live shot graph also rebuilt its goal-curve overlays on every flush, because their Repeater model was a list that grew each sample. Qt Graphs has had per-series axes since 6.10 and dashed `LineSeries` since 6.11; 6.12 adds `XYSeries.values` for binding a point list directly.

## What Changes

- Both CupFillView canvases become `Canvas2D` (`import QtCanvas2D`); `ellipse(x, y, w, h)` calls become `ellipseRect`.
- Delete `src/ui/jscanvas{painteritem,context}.{h,cpp}` and the `Qt6::CanvasPainter` link. The RHI-backend log line the wrapper wrote moves to `main.cpp`.
- Weight changes no longer request a cup repaint while the animation timer is running.
- Every `DashedLineSeries` (19 uses across six graphs) becomes a native `LineSeries`, dashed or solid. Range holders become hidden `ValueAxis` objects that the series map through. The history graph's flow traces map through the flow axis instead of being copied and scaled. A shared `GraphSeriesInstantiator` adds one series per entry for variable-count lines (history goal segments and markers, comparison traces, Portal segments).
- Each live goal is one series with a NaN point between its segments, the live markers are three series, and the goal lists drop mid-run points before reaching QML (display only).
- Delete `DashedLineSeries.qml`.

`Canvas2D` is preliminary in 6.12, so its API may change in a minor release. `FastLineRenderer` stays: 6.12's `LineSeries` still rebuilds its whole path on every change (`pointrenderer.cpp:535`), so it is no substitute for the live traces.

## Impact

- Affected specs: `cup-fill-view`, `charting`.
- Affected code: `qml/components/CupFillView.qml`, the six graph components, `src/models/{shotdatamodel,shotcomparisonmodel,steamdatamodel}`, `src/main.cpp`, `CMakeLists.txt`.
