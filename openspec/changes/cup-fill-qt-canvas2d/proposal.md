## Why

CupFillView drew through our own `JsCanvasPainterItem` / `JsCanvasContext` wrapper (~620 lines) over Qt Canvas Painter. Qt 6.12 ships the same thing as `Canvas2D`: a Canvas 2D context recorded on the main thread and replayed by Canvas Painter on the render thread. Its draw calls are native engine functions, where ours went through the meta-object system per call, and it records once per frame from `updatePolish()` where ours recorded synchronously on every `requestPaint()`. A Mac Time Profiler run during a simulated shot put cup drawing at ~23% of main-thread time (#1976).

## What Changes

- Both CupFillView canvases become `Canvas2D` (`import QtCanvas2D`); `ellipse(x, y, w, h)` calls become `ellipseRect`.
- Delete `src/ui/jscanvas{painteritem,context}.{h,cpp}`.
- The RHI-backend log line the wrapper wrote moves to `main.cpp` (scene graph ready).
- Weight changes no longer request a repaint while the animation timer is running; its next tick draws them.

`Canvas2D` is preliminary in 6.12, so its API may change in a minor release.

## Impact

- Affected spec: `cup-fill-view` (the rendering element is named in a requirement).
- Affected code: `qml/components/CupFillView.qml`, `src/main.cpp`, `CMakeLists.txt`.
