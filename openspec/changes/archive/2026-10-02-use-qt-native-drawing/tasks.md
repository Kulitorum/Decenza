## 1. Cup fill (Canvas2D)

- [x] 1.1 Port both CupFillView canvases to `Canvas2D` (`contextType: "2d"`, transparent fill, alpha blending; `ellipseRect` for the bounding-box ellipses)
- [x] 1.2 Delete the `JsCanvas*` wrapper, its CMake entries and the `Qt6::CanvasPainter` link
- [x] 1.3 Log the RHI backend from `main.cpp` once the scene graph is ready
- [x] 1.4 Skip weight-triggered repaints while the animation timer runs

## 2. Graphs (native series)

- [x] 2.1 Replace every `DashedLineSeries` with a native `LineSeries`; range holders become hidden `ValueAxis` objects
- [x] 2.2 `GraphSeriesInstantiator` for variable-count series; each live goal one NaN-separated series, live markers three series
- [x] 2.3 Goal display lists drop mid-run points (test: `goalDisplayListsDropOnlyMidRunPoints`)
- [x] 2.4 Delete `DashedLineSeries.qml`
- [x] 2.5 Live, steam and Portal overlays position from the plot rect in the outer item's coordinates. They used GraphsView.plotArea, which ignores the view's top margin, so every FastLineRenderer trace, the pump bars and the right-axis labels sat that margin too high. The native goal series exposed it.

## 3. Verification

- [x] 3.1 Build and full test suite green (118/118, macOS, 2026-10-02) — cup change
- [x] 3.2 macOS (Metal): simulated shot — liquid, crema, waves, stream, steam and glow match the previous rendering (2026-10-02)
- [x] 3.3 Build and full test suite green — graph change (118/118, qmllint gate clean, macOS, 2026-10-02)
- [x] 3.4 macOS (2026-10-02): live shot graph (goals, markers, temperature on its goal after the offset fix, pump bars on the axis), shot review (flow multiplier 3x, right-axis weight), comparison (three line styles, temperature axis), steam flow goal, profile editor preview. Portal overlay not checked (no Portal data)
- [x] 3.5 Android tablet (Galaxy Tab A9+, ece12cc0 build, 2026-10-02): cup draws during a shot, live shot graph and shot review graph render; simpleperf captures of idle, navigation, a graph shot and a cup shot. Comparison, steam and profile-editor graphs not checked on Android
