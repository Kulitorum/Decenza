# Tasks: Upgrade Qt from 6.10.3 to 6.11.1

## Prerequisites
- [ ] Install Qt 6.11.1 on the Windows dev machine (MSVC 2022 x64 target, same Qt Maintenance Tool procedure as 6.10.3). Include the **Qt Canvas Painter** addon.
- [x] Install Qt 6.11.1 on Jeff's Mac (macOS + iOS targets; run Qt Maintenance Tool). Include the **Qt Canvas Painter** addon.

## Source Changes

- [x] Update `CMakeLists.txt`:
  - Update the comment on the `CMAKE_OSX_DEPLOYMENT_TARGET` line to "Qt 6.11.1 was built for iOS 17.0" (verify iOS minimum still 17.0)
  - Add `CanvasPainter` to the required `find_package(Qt6 REQUIRED COMPONENTS ...)` block; link `Qt6::CanvasPainter` on the executable target
  - Add the four new wrapper sources/headers (`src/ui/jscanvas{painteritem,context}.{h,cpp}`) to the executable's sources
  - After first Windows build, check for any new QTP policy warnings in the CMake configure output; add any new `qt_policy(SET QTPxxxx NEW)` entries inside the existing `VERSION_GREATER_EQUAL "6.5.0"` guard
- [x] Update `CLAUDE.md`:
  - `**Qt version**: 6.10.3` → `6.11.1`
  - `**Qt path**: C:/Qt/6.10.3/msvc2022_64` → `C:/Qt/6.11.1/msvc2022_64`
  - All command-line build commands referencing `C:/Qt/6.10.3/msvc2022_64`
  - macOS `qt-cmake` paths (`/Users/mic/Qt/6.10.3/ios/bin/qt-cmake` and `macos/bin/qt-cmake`)
  - macOS build directory names (`build/Qt_6_10_3_for_iOS` → `build/Qt_6_11_1_for_iOS`, etc.) in `docs/CLAUDE_MD/PLATFORM_BUILD.md`
- [x] Update `README.md`:
  - Bump the Qt badge to `6.11.1+`
  - Raise the install-step minimum from "Qt 6.8 or newer" to "Qt 6.11.1 or newer" (required — `QCanvasPainter` is a 6.11+ module)
  - Add "Qt Canvas Painter" to the required-modules list
- [x] Update `openspec/config.yaml`:
  - Tech stack line: `Qt 6.10.3` → `Qt 6.11.1`
- [x] Update `docs/CLAUDE_MD/TESTING.md`, `docs/CLAUDE_MD/PLATFORM_BUILD.md`, `docs/IOS_CI_SETUP.md`, `docs/IOS_CI_FOR_CLAUDE.md`: Qt version and example build-directory paths.

## CI/CD Workflow Updates

- [x] Update `.github/workflows/windows-release.yml`:
  - `version: '6.10.3'` → `version: '6.11.1'`
  - Add `qtcanvaspainter` to the `modules:` list (separate Qt 6.11 module repo, not in qtbase)
  - `key: sccache-windows-x64-qt6.10.3` → `key: sccache-windows-x64-qt6.11.1`
  - Update in-comment reference "Qt 6.10.3 for Windows" / "ABI-incompatible with Qt 6.10.3" to 6.11.1
  - Pin `aqtsource` to the miurahr/aqtinstall master commit that knows the Qt 6.11 split-arch online-installer layout (PyPI 3.3.0 doesn't). Drop when aqtinstall ≥ 3.4.0 ships — TODO marker `TODO(qt-6.11)` left in place.
- [x] Update `.github/workflows/macos-release.yml`: `version: '6.10.3'` → `version: '6.11.1'`; add `qtcanvaspainter` module.
- [x] Update `.github/workflows/ios-release.yml`:
  - `version: '6.10.3'` → `version: '6.11.1'`; add `qtcanvaspainter` module.
  - Pin Xcode 26.4.1: macos-26 runners' default Xcode 26.2 ships a libc++ missing the `[abi:ue170006]` unstable-ABI symbols Qt 6.11.x prebuilt iOS binaries reference. 26.4.1 ships the matching libc++.
- [x] Update `.github/workflows/android-release.yml`:
  - `version: '6.10.3'` → `version: '6.11.1'`; add `qtcanvaspainter` module.
- [x] Update `.github/workflows/linux-release.yml`: `version: '6.10.3'` → `version: '6.11.1'`; add `qtcanvaspainter` module.
- [x] Update `.github/workflows/linux-arm64-release.yml`: `version: '6.10.3'` → `version: '6.11.1'`; add `qtcanvaspainter` module.

## Build Validation

- [ ] Build on Windows (Qt Creator) — confirm zero errors, note any new QTP warnings and resolve per CMakeLists.txt task above
- [x] Build on macOS (Qt Creator) — zero errors on Qt 6.11.1 (Metal RHI)
- [x] Trigger all 6 release workflows via `workflow_dispatch` against this branch (no tag push needed for the upgrade itself); confirm all 6 platform builds pass:
  - Windows build passes
  - macOS build passes
  - iOS build passes
  - Android build passes
  - Linux x64 build passes
  - Linux arm64 build passes

## Smoke Testing

- [ ] Smoke test on Windows: launch app, connect to DE1 (or simulator), pull a shot, verify graphs render
- [x] Smoke test on macOS: simulator extraction end-to-end on the rebased post-6.11.1 build (shot saved to history, visualizer upload succeeded, JsCanvasPainterItem initialised on Metal with no slow-record warnings)
- [x] Smoke test on Android (Decent tablet): visual parity vs. Canvas baseline confirmed; steady-state perf ~3.5 ms recording + ~230 µs replay per frame, ~11 % of one core (well within 30 fps budget); old Canvas software-rasterization thread gone
- [ ] Smoke test on iOS (device): verify BLE, shot history, graphs

## Qt Canvas Painter — CupFillView GPU acceleration (C++ wrapper)

> **Note on approach.** Qt 6.11's Canvas Painter ships only the C++ side (`QCanvasPainter`, `QCanvasPainterItem`, `QCanvasLinearGradient`, etc.); there is no QML-importable `CanvasPainterItem`. We build a small wrapper that exposes a `Canvas`-like JS surface so CupFillView's existing `onPaint:` body can stay almost as-is. See proposal.md "Qt Canvas Painter — CupFillView GPU acceleration" for the full architecture.

### CMake wiring

- [x] Add `find_package(Qt6 REQUIRED COMPONENTS CanvasPainter)` to `CMakeLists.txt` (merged into the existing required-components block). Required, not QUIET, because the wrapper sources won't compile without it. Add `Qt6::CanvasPainter` to the `target_link_libraries(Decenza ...)` block.
- [x] Add the two new source files (`src/ui/jscanvaspainteritem.{h,cpp}`, `src/ui/jscanvascontext.{h,cpp}`) to the executable's sources in `CMakeLists.txt`.

### `JsCanvasContext` — JS-callable 2D context (`src/ui/jscanvascontext.{h,cpp}`)

- [x] Define `DrawCmd` POD struct: tag enum + small fixed-size payload (floats, color RGBA, `qsizetype brushId`). Fixed-size POD keeps `QVector<DrawCmd>` allocation-free per call.
- [x] Define `BrushSpec` (color | linear gradient | radial gradient with stops) stored in a parallel `QVector<BrushSpec>` indexed by id; ids reset per frame to keep storage flat.
- [x] `JsCanvasContext : public QObject` — Q_INVOKABLE methods (record into the buffer):
  - Path: `beginPath()`, `closePath()`, `moveTo(x,y)`, `lineTo(x,y)`, `arc(cx,cy,r,a0,a1, anticw=false)`, `ellipse(x,y,w,h)` (QML-Canvas-style bounding box)
  - Fill/stroke: `fill()`, `stroke()`, `fillRect(x,y,w,h)`, `clearRect(x,y,w,h)`, `strokeRect(x,y,w,h)`
  - State: `save()`, `restore()`, `reset()`
  - Properties exposed via `Q_PROPERTY` writes that record `SetX` commands: `fillStyle`, `strokeStyle` (accept `QColor` *or* `JsCanvasGradient*` via `QVariant`), `lineWidth` (float), `lineCap` (accepts the existing Canvas 2D string literals `"butt"`/`"round"`/`"square"`, no QML-side enum), `globalAlpha` (float)
  - Gradient factories: `Q_INVOKABLE QObject* createLinearGradient(x0,y0,x1,y1)`, `createRadialGradient(x0,y0,r0,x1,y1,r1)` — returns a `JsCanvasGradient` parented to the ctx (lifetime-tied to one frame)
- [x] `JsCanvasGradient : public QObject` — `Q_INVOKABLE addColorStop(float pos, QVariant color)`; writes stops back into the parent ctx's `BrushSpec` via `brushAt(id)`; assigned-as-fillStyle records its id.
- [x] Provide `void resetForNextFrame()` that truncates the command + brush vectors (keeps capacity, no reallocs) and `qDeleteAll`s the gradient children.

### `JsCanvasPainterItem` — `QCanvasPainterItem` subclass (`src/ui/jscanvaspainteritem.{h,cpp}`)

- [x] `class JsCanvasPainterItem : public QCanvasPainterItem` with:
  - `Q_INVOKABLE void requestPaint()` — runs the QML `onPaint` handler synchronously on the main thread, then schedules `update()`
  - `Q_SIGNAL void paint(QObject *ctx)` — emitted on the **main thread** to let QML record draw commands into the JsCanvasContext
- [x] On `requestPaint()`: call `m_ctx.resetForNextFrame()`, emit `paint(&m_ctx)`, then `update()`. Multiple calls per frame: each clears the prior recording and Qt's `update()` coalesces the paint scheduling, so only the last recording before vsync survives.
- [x] Override `createItemRenderer()` to return a `JsCanvasPainterItemRenderer`.
- [x] Implement `JsCanvasPainterItemRenderer : public QCanvasPainterItemRenderer`:
  - `synchronizeData(QCanvasPainterItem*)` — Qt 6.11.1 rename (QTBUG-145406). Called on render thread with main blocked; atomic-swap the recorded `QVector<DrawCmd>` and `QVector<BrushSpec>` between item and renderer.
  - `paint(QCanvasPainter*)` — call `p->reset()` defensively (defense-in-depth; QML handlers also record `ctx.reset()` as their first command), then replay loop: switch on `DrawCmd::Op`, call the matching `painter->...()`. For `SetFillBrush`/`SetStrokeBrush` resolve the brush id and translate `BrushSpec` → `QColor`/`QCanvasLinearGradient`/`QCanvasRadialGradient` (constructed in-place; cheap). Radial gradients with offset Canvas-2D inner/outer circle centers collapse to a single-center `QCanvasRadialGradient` anchored at the inner circle — the inner circle is the focal point in Canvas 2D, so this preserves "lit-from-this-side" asymmetric highlights (e.g. CupFillView's crema). Mild visual approximation, not a pixel-exact match for offset-center radials.
- [x] Register the QML element via `qmlRegisterType<JsCanvasPainterItem>("Decenza", 1, 0, "JsCanvasPainterItem")` in `src/main.cpp`. The `QML_NAMED_ELEMENT` auto-registration path was attempted first but qmltyperegistrar can't resolve QtCanvasPainter / QtQuick parent-type headers from the Decenza QML module's generated typemap, so we fall back to the imperative registration the rest of `main.cpp` already uses for custom types.

### CupFillView migration (`qml/components/CupFillView.qml`)

- [x] Replace both `Canvas { id: liquidCanvas; ...; renderStrategy: Canvas.Threaded; onPaint: { ... } }` with `JsCanvasPainterItem { id: liquidCanvas; ...; onPaint: function(ctx) { ... } }`. Note `onPaint` becomes a signal handler with an explicit `ctx` parameter (the QObject we emit), instead of a Canvas-style scope where `getContext("2d")` returns the context.
- [x] Same swap for `effectsCanvas`.
- [x] Remove `var ctx = getContext("2d")` lines — `ctx` arrives as a signal parameter.
- [x] Audit the existing drawing code for any Canvas-2D feature outside our wrapper's scope. Used today: `createLinearGradient`/`createRadialGradient`, `addColorStop`, `beginPath`/`closePath`, `moveTo`/`lineTo`, `fill`/`stroke`, `fillRect`/`clearRect`, `arc`, `ellipse`, `reset`, `fillStyle`/`strokeStyle`/`lineWidth`/`lineCap`/`globalAlpha`. **Not** in scope: `shadowColor`/`shadowBlur` (commented out today — kept commented), `bezierCurveTo` (used inside steam-wisp `function stmX/stmY` with manual cubic eval — already manual, no change needed).

### Validation

- [ ] Build on Windows (Qt Creator) — confirm wrapper compiles; CupFillView renders.
- [x] Build on macOS (Qt Creator) — confirmed; CupFillView renders on Metal RHI.
- [x] Build on iOS — passed in CI (`ios-release.yml` against this branch; Xcode 26.4.1 pin required for libc++ ABI).
- [x] Build on Android (Decent tablet) — passed in CI and confirmed visually on the tablet.
- [x] Visual match: side-by-side screenshot comparison on macOS + Android. No noticeable rendering deltas; crema radial-gradient asymmetry preserved via the inner-center anchoring approximation.
- [x] Performance: Decent tablet during live extraction — steady-state ~3.5 ms recording + ~230 µs replay per frame, ~11 % of one core (well within 30 fps budget). Old Canvas software-rasterization thread is gone.
- [x] Frame budget check: outlier-only `[CupFill] slow record` warning at `> ~16 ms` recording — not seen in the post-rebase macOS smoke session.

### Decision gate

- [x] Visual + perf gates passed on the platforms exercised so far (macOS + Android tablet + CI builds on Windows/iOS/Linux). Shipping.
- [ ] Fallback (not exercised): if perf is neutral or worse on a future platform, revert CupFillView to `Canvas` and leave wrapper sources behind a `BUILD_CANVASPAINTER_WRAPPER` CMake option.
- [ ] Fallback (not exercised): if a single platform regresses, use a QML `Loader` with platform check to keep `Canvas` on the failing platform.

## Follow-up

- [ ] Update `migrate-charting-to-qt-graphs/proposal.md`: mark gate condition #4 (Qt 6.11.1 upgrade) as ✅ satisfied
- [ ] Update `migrate-charting-to-qt-graphs/tasks.md`: set Stage 0 status to ready-to-start
