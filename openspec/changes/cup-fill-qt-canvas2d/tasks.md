## 1. Implementation

- [x] 1.1 Port both CupFillView canvases to `Canvas2D` (`contextType: "2d"`, transparent fill, alpha blending; `ellipseRect` for the bounding-box ellipses)
- [x] 1.2 Delete the `JsCanvas*` wrapper and its CMake entries
- [x] 1.3 Log the RHI backend from `main.cpp` once the scene graph is ready
- [x] 1.4 Skip weight-triggered repaints while the animation timer runs

## 2. Verification

- [x] 2.1 Build and full test suite green (118/118, macOS, 2026-10-02)
- [x] 2.2 macOS (Metal): simulated shot — liquid, crema, waves, stream, steam and glow match the previous rendering (2026-10-02)
- [ ] 2.3 Android tablet (profiling build): same visual check, and a simpleperf capture of a shot to measure the main-thread cost
