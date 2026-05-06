# Tasks: Upgrade Qt from 6.10.3 to 6.11.1

## Prerequisites
- [ ] Install Qt 6.11.1 on the Windows dev machine (MSVC 2022 x64 target, same Qt Maintenance Tool procedure as 6.10.3)
- [ ] Install Qt 6.11.1 on Jeff's Mac (macOS + iOS targets; run Qt Maintenance Tool)

## Source Changes

- [ ] Update `CMakeLists.txt`:
  - Update the comment on the `CMAKE_OSX_DEPLOYMENT_TARGET` line from "Qt 6.10.3 was built for iOS 17.0" to "Qt 6.11.1 was built for iOS 17.0" (verify iOS minimum still 17.0)
  - After first Windows build, check for any new QTP policy warnings in the CMake configure output; add any new `qt_policy(SET QTPxxxx NEW)` entries inside the existing `VERSION_GREATER_EQUAL "6.5.0"` guard
- [ ] Update `CLAUDE.md`:
  - `**Qt version**: 6.10.3` → `6.11.1`
  - `**Qt path**: C:/Qt/6.10.3/msvc2022_64` → `C:/Qt/6.11.1/msvc2022_64`
  - All three command-line build commands referencing `C:/Qt/6.10.3/msvc2022_64`
  - macOS `qt-cmake` paths (`/Users/mic/Qt/6.10.3/ios/bin/qt-cmake` and `macos/bin/qt-cmake`)
  - macOS build directory names (`build/Qt_6_10_3_for_iOS` → `build/Qt_6_11_1_for_iOS`, etc.)
- [ ] Update `openspec/project.md`:
  - Tech stack line: `Qt 6.10.3` → `Qt 6.11.1`
  - Windows path: `C:/Qt/6.10.3/msvc2022_64` → `C:/Qt/6.11.1/msvc2022_64`

## CI/CD Workflow Updates

- [ ] Update `.github/workflows/windows-release.yml`:
  - `version: '6.10.3'` → `version: '6.11.1'`
  - `key: sccache-windows-x64-qt6.10.3` → `key: sccache-windows-x64-qt6.11.1`
  - Update in-comment reference "Qt 6.10.3 for Windows" / "ABI-incompatible with Qt 6.10.3" to 6.11.1
- [ ] Update `.github/workflows/macos-release.yml`: `version: '6.10.3'` → `version: '6.11.1'`
- [ ] Update `.github/workflows/ios-release.yml`: `version: '6.10.3'` → `version: '6.11.1'`
- [ ] Update `.github/workflows/android-release.yml`:
  - `version: '6.10.3'` → `version: '6.11.1'`
  - Update "Fallback to known-good version for Qt 6.10.x" comment to 6.11.x
- [ ] Update `.github/workflows/linux-release.yml`: `version: '6.10.3'` → `version: '6.11.1'`
- [ ] Update `.github/workflows/linux-arm64-release.yml`: `version: '6.10.3'` → `version: '6.11.1'`

## Build Validation

- [ ] Build on Windows (Qt Creator) — confirm zero errors, note any new QTP warnings and resolve per CMakeLists.txt task above
- [ ] Build on macOS (Qt Creator) — confirm zero errors
- [ ] Push a tag to trigger CI; confirm all 6 platform builds pass:
  - Windows build passes
  - macOS build passes
  - iOS build passes
  - Android build passes
  - Linux x64 build passes
  - Linux arm64 build passes

## Smoke Testing

- [ ] Smoke test on Windows: launch app, connect to DE1 (or simulator), pull a shot, verify graphs render
- [ ] Smoke test on macOS: launch app, connect to DE1 (or simulator), verify BLE and graphs
- [ ] Smoke test on Android (Decent tablet or phone): verify BLE scanning, scale connection, live shot graph
- [ ] Smoke test on iOS (device): verify BLE, shot history, graphs

## Qt Canvas Painter — CupFillView GPU acceleration (C++ wrapper)

> **Note on approach.** Qt 6.11's Canvas Painter ships only the C++ side (`QCanvasPainter`, `QCanvasPainterItem`, `QCanvasLinearGradient`, etc.); there is no QML-importable `CanvasPainterItem`. We build a small wrapper that exposes a `Canvas`-like JS surface so CupFillView's existing `onPaint:` body can stay almost as-is. See proposal.md "Qt Canvas Painter — CupFillView GPU acceleration" for the full architecture.

### CMake wiring

- [ ] Add `find_package(Qt6 REQUIRED COMPONENTS CanvasPainter)` to `CMakeLists.txt` (after the ShaderTools block). Required, not QUIET, because the wrapper sources won't compile without it. Add `Qt6::CanvasPainter` to the `target_link_libraries(Decenza ...)` block.
- [ ] Add the two new source files (`src/ui/jscanvaspainteritem.{h,cpp}`, `src/ui/jscanvascontext.{h,cpp}`) to the executable's `SOURCES` list in `CMakeLists.txt`.

### `JsCanvasContext` — JS-callable 2D context (`src/ui/jscanvascontext.{h,cpp}`)

- [ ] Define `DrawCmd` POD struct: tag enum + small fixed-size payload union (floats, color RGBA8, brush-id int). Fixed size keeps `QVector<DrawCmd>` allocation-free per call.
- [ ] Define `BrushSpec` (color | linear gradient | radial gradient with stops) stored in a parallel `QVector<BrushSpec>` indexed by id; ids reset per frame to keep storage flat.
- [ ] `JsCanvasContext : public QObject` — Q_INVOKABLE methods (record into the buffer):
  - Path: `beginPath()`, `closePath()`, `moveTo(x,y)`, `lineTo(x,y)`, `arc(cx,cy,r,a0,a1, anticw=false)`, `ellipse(cx,cy,rx,ry, rot=0, a0=0, a1=2π, anticw=false)`
  - Fill/stroke: `fill()`, `stroke()`, `fillRect(x,y,w,h)`, `clearRect(x,y,w,h)`, `strokeRect(x,y,w,h)`
  - State: `save()`, `restore()`, `reset()`
  - Properties exposed via `Q_PROPERTY` writes that record `SetX` commands: `fillStyle`, `strokeStyle` (accept `QColor` *or* `JsCanvasGradient*` via `QVariant`), `lineWidth` (float), `lineCap` (string `"butt"`/`"round"`/`"square"` mapped to enum), `globalAlpha` (float)
  - Gradient factories: `Q_INVOKABLE QObject* createLinearGradient(x0,y0,x1,y1)`, `createRadialGradient(x0,y0,r0,x1,y1,r1)` — returns a `JsCanvasGradient` parented to the ctx (lifetime-tied to one frame)
- [ ] `JsCanvasGradient : public QObject` — `Q_INVOKABLE addColorStop(float pos, QColor color)`; holds `BrushSpec` in-place; assigned-as-fillStyle records its id.
- [ ] Provide `void resetForNextFrame()` that truncates the command + brush vectors (keeps capacity, no reallocs).

### `JsCanvasPainterItem` — `QCanvasPainterItem` subclass (`src/ui/jscanvaspainteritem.{h,cpp}`)

- [ ] `class JsCanvasPainterItem : public QCanvasPainterItem` with:
  - `QML_NAMED_ELEMENT(JsCanvasPainterItem)` so QML can use it without explicit registration
  - `Q_INVOKABLE void requestPaint()` — sets a dirty flag and schedules `update()`
  - `Q_SIGNAL void paint(QObject *ctx)` — emitted on the **main thread** to let QML record draw commands
- [ ] On dirty + before `update()`: clear the ctx, emit `paint(ctx)` (so QML's `onPaint:` runs and records into the buffer), then call `update()`.
- [ ] Override `createItemRenderer()` to return a `JsCanvasPainterItemRenderer`.
- [ ] Implement `JsCanvasPainterItemRenderer : public QCanvasPainterItemRenderer`:
  - `synchronize(QCanvasPainterItem*)` — called on render thread with main blocked. Atomic-swap the recorded `QVector<DrawCmd>` and `QVector<BrushSpec>` into the renderer (use `std::swap` on the vectors held by the item and the renderer).
  - `paint(QCanvasPainter*)` — replay loop: switch on `DrawCmd::tag`, call the matching `painter->...()`. For `SetFillStyle`/`SetStrokeStyle` resolve the brush id and translate `BrushSpec` → `QColor`/`QCanvasLinearGradient`/`QCanvasRadialGradient` (constructed in-place; cheap).
- [ ] Register the QML element. If we use `QML_NAMED_ELEMENT` + the existing `qt_add_qml_module(Decenza)` setup, registration is automatic — confirm by reading the current `qt_add_qml_module` block in `CMakeLists.txt` and adding the new headers to its `SOURCES` if needed.

### CupFillView migration (`qml/components/CupFillView.qml`)

- [ ] Replace both `Canvas { id: liquidCanvas; ...; renderStrategy: Canvas.Threaded; onPaint: { ... } }` with `JsCanvasPainterItem { id: liquidCanvas; ...; onPaint: function(ctx) { ... } }`. Note `onPaint` becomes a signal handler with an explicit `ctx` parameter (the QObject we emit), instead of a Canvas-style scope where `getContext("2d")` returns the context.
- [ ] Same swap for `effectsCanvas`.
- [ ] Remove `var ctx = getContext("2d")` lines — `ctx` arrives as a signal parameter.
- [ ] Audit the existing drawing code for any Canvas-2D feature outside our wrapper's scope. Known items used today: `createLinearGradient`/`createRadialGradient`, `addColorStop`, `beginPath`/`closePath`, `moveTo`/`lineTo`, `fill`/`stroke`, `fillRect`/`clearRect`, `arc`, `ellipse`, `reset`, `clearRect`, `fillStyle`/`strokeStyle`/`lineWidth`/`lineCap`/`globalAlpha`. **Not** in scope: `shadowColor`/`shadowBlur` (commented out today — keep commented), `bezierCurveTo` (used inside steam-wisp `function stmX/stmY` with manual cubic eval — already manual, no change needed). If the audit finds anything else, add it to `JsCanvasContext` before swapping.

### Validation

- [ ] Build on Windows (Qt Creator) — confirm wrapper compiles; CupFillView renders.
- [ ] Build on macOS (Qt Creator) — same.
- [ ] Build on iOS — confirm Tech Preview module is available for iOS targets; if not, fall back to `Canvas` for `IOS` only (a `Loader` with a platform check).
- [ ] Build on Android (Decent tablet) — same iOS check.
- [ ] Visual match: side-by-side screenshot comparison (idle + mid-shot + completion) on Windows, macOS, Android. Note any rendering deltas; gradients and antialiasing are the most likely diff points.
- [ ] Performance: on the Decent tablet during a live extraction, capture before/after CPU usage for the QtQuick render thread (Qt Creator profiler) and main thread. Goal: measurable reduction; no main-thread regression from command recording.
- [ ] Frame budget check: log `JsCanvasContext::resetForNextFrame()` timings and command count; at 30 fps the recording cost per frame should be sub-millisecond on the tablet.

### Decision gate

- [ ] If visual + perf gates pass on all four platforms: ship.
- [ ] If perf is neutral or worse on the tablet (recording overhead eats the GPU win): revert CupFillView to `Canvas`, leave wrapper sources behind a `BUILD_CANVASPAINTER_WRAPPER` CMake option for later revisit.
- [ ] If only one platform fails (most likely iOS / Tech Preview missing): use a QML `Loader` with platform check to keep `Canvas` on the failing platform.

## Follow-up

- [ ] Update `migrate-charting-to-qt-graphs/proposal.md`: mark gate condition #4 (Qt 6.11.1 upgrade) as ✅ satisfied
- [ ] Update `migrate-charting-to-qt-graphs/tasks.md`: set Stage 0 status to ready-to-start
