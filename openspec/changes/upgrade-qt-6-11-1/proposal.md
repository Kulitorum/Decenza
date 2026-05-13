# Change: Upgrade Qt from 6.10.3 to 6.11.1

## Why

Qt 6.11.1 is the next minor release in the Qt 6 series and is a prerequisite for the approved `migrate-charting-to-qt-graphs` change (gate condition #4: "Qt 6.11.1 is released and Decenza upgrades to it"). Qt 6.11 also promotes Qt Graphs to full production readiness with new APIs (`visualMin`/`visualMax`, `QCustomSeries`, multi-axis support) that the charting migration depends on. Qt 6.10.x continues to receive security patches but Qt 6.11 is the forward-looking release.

## What Changes

- **CI/CD workflows** (6 files): Bump `version: '6.10.3'` → `version: '6.11.1'` in all `jurplel/install-qt-action` steps. Adds `qtcanvaspainter` to every workflow's `modules:` list (separate Qt 6.11 module repo, not in qtbase).
- **Windows sccache cache key**: Rename `sccache-windows-x64-qt6.10.3` → `sccache-windows-x64-qt6.11.1` to avoid stale-cache hits.
- **Windows workflow OpenSSL comment**: Update in-code comment referencing Qt 6.10.3 ABI; OpenSSL 3.x requirement is unchanged.
- **Windows workflow aqtinstall pin** (transitive workaround): Pin `aqtsource` to the miurahr/aqtinstall master commit that knows the new per-arch Windows online-installer layout. aqtinstall 3.3.0 (still PyPI head as of 2026-05-13) fails on 6.11.x with "Failed to locate XML data". Drop the pin when aqtinstall ≥ 3.4.0 ships (greppable via `TODO(qt-6.11)`).
- **iOS workflow Xcode pin** (transitive workaround): Pin Xcode 26.4.1 because macos-26 runners' default Xcode 26.2 ships a libc++ revision missing the `[abi:ue170006]` unstable-ABI symbols Qt 6.11.x's prebuilt iOS binaries reference. 26.4.1 ships the matching libc++.
- **`CMakeLists.txt`**: Update the comment on line 135 that says "Qt 6.10.3 was built for iOS 17.0"; add `Qt6::CanvasPainter` to required components; verify and add any new `qt_policy()` entries introduced in 6.11 (QTP0001 and QTP0004 are already set for Qt ≥ 6.5).
- **`CLAUDE.md`**: Update Qt version, dev-machine paths (`C:/Qt/6.10.3/` → `C:/Qt/6.11.1/`, `~/Qt/6.10.3/` → `~/Qt/6.11.1/`), and iOS build path in build commands.
- **`README.md`**: Bump Qt badge to 6.11.1+, raise the install-step minimum from 6.8 to 6.11.1 (`QCanvasPainter` is 6.11+ only), and list Qt Canvas Painter in the required-modules block.
- **`openspec/config.yaml`**: Update the tech stack entry to Qt 6.11.1.
- **`docs/CLAUDE_MD/TESTING.md`, `docs/CLAUDE_MD/PLATFORM_BUILD.md`, `docs/IOS_CI_SETUP.md`, `docs/IOS_CI_FOR_CLAUDE.md`**: Update Qt version references and example build-directory names.
- **Local developer installs**: Qt 6.11.1 must be installed locally on the Windows dev machine and Jeff's Mac (macOS + iOS targets) before building. The **Qt Canvas Painter** module is a separate component in the Qt Maintenance Tool — must be selected when installing 6.11.1.

### Qt Canvas Painter — CupFillView GPU acceleration

`CupFillView.qml` renders the liquid fill, crema, waves, ripples, and steam wisps using two `Canvas` items with `renderStrategy: Canvas.Threaded`. This is software-rasterized on a CPU background thread and repaints at ~30fps via a 33ms `Timer` — the Decent tablet is already busy processing BLE data at 5Hz and updating the shot graph during extraction, so offloading this to the GPU is a meaningful win.

Qt 6.11 ships the **Qt Canvas Painter** module (Technology Preview): a GPU-accelerated 2D painting toolkit (`QCanvasPainter`) whose C++ API is named to match HTML Canvas 2D (`beginPath`, `moveTo`, `fill`, `setFillStyle`, `arc`, `ellipse`, `QCanvasLinearGradient`, `QCanvasRadialGradient`, etc.). Rendering goes through Qt's RHI (Vulkan/Metal/D3D12/OpenGL) on the scene-graph render thread.

**The module ships only the C++ side.** There is no QML-importable `CanvasPainterItem` element — `qtcanvaspainter`'s sources contain no `QML_NAMED_ELEMENT` registration and the install lays down no `qml/QtCanvasPainter/` directory. The official example (`Src/qtcanvaspainter/examples/canvaspainter/gallery/`) subclasses `QCanvasPainterItem` in C++ and registers its own QML element. So the migration is **not** a drop-in QML swap; it requires a small C++ wrapper that bridges QML/JS callbacks to `QCanvasPainter` calls on the render thread.

**Approach**: Build a reusable `JsCanvasPainterItem` C++ wrapper that exposes a `Canvas`-like QML surface (`onPaint`, `requestPaint()`, a `ctx`-shaped object). The wrapper:

1. Subclasses `QCanvasPainterItem` and registers itself to QML as `JsCanvasPainterItem` in the Decenza module.
2. On `requestPaint()` (or property change), schedules a main-thread callback that invokes the `paint(ctx)` signal with a `JsCanvasContext` proxy QObject.
3. `JsCanvasContext` exposes `Q_INVOKABLE` methods mirroring the Canvas 2D API used by CupFillView. Each call **records a typed POD command** into a flat `QVector<DrawCmd>` (no per-call allocation; ~16–32 bytes per command).
4. Gradients (`createLinearGradient`/`createRadialGradient`) return small `JsCanvasGradient` QObjects with `addColorStop()`. Setting `fillStyle = grad` records a "set fill brush #N" command and copies stops into the buffer.
5. On the next render cycle, `synchronizeData(QCanvasPainterItem*)` (renamed from `synchronize()` in Qt 6.11.1 per QTBUG-145406; called on the render thread while main is blocked) atomic-swaps the recorded buffer into the renderer.
6. `paint(QCanvasPainter*)` replays the buffer using `QCanvasPainter` calls — `MoveTo` → `painter->moveTo()`, `SetFillStyle` → `painter->setFillStyle()`, etc.

The wrapper is **isolated to two new files** (`src/ui/jscanvaspainteritem.{h,cpp}` and `src/ui/jscanvascontext.{h,cpp}`) and `CupFillView.qml` swaps `Canvas { ... }` → `JsCanvasPainterItem { ... }` with the existing `onPaint:` body intact. String-keyed Canvas 2D properties like `lineCap = "round"` are preserved as-is — `JsCanvasContext::setLineCap(const QString&)` accepts the existing literals (`"butt"`/`"round"`/`"square"`) and maps them to `QCanvasPainter::LineCap` internally, so no QML-side enum-switch is needed. All other QML files using `Canvas` continue to use the built-in type.

**Scope of the JS API surface**: only what CupFillView actually uses today — `beginPath`/`closePath`, `moveTo`/`lineTo`, `fill`/`stroke`, `fillRect`/`clearRect`, `arc`/`ellipse`, `reset`; properties `fillStyle`/`strokeStyle`/`lineWidth`/`lineCap`/`globalAlpha`; and the two gradient types with `addColorStop()`. Roughly 15 ctx methods + 6 properties + 2 gradient classes. We are **not** rebuilding the full HTML Canvas 2D API — anything we don't use today is out of scope.

**Acceptance gate**: visual output matches the Canvas baseline side-by-side on Windows, macOS, and Android; animation feels at least as smooth on the Decent tablet during a live extraction; CPU usage (sampled via Qt Creator profiler or Android Studio CPU profiler) measurably drops on the tablet during a live shot.

**Rollback**: if the Tech Preview misbehaves on any platform, revert `CupFillView.qml` to `Canvas` (one-line type change per item) and leave the wrapper in tree behind a guard for future revival.

### Impact on `migrate-charting-to-qt-graphs`

Once this upgrade lands, gate condition #4 of `migrate-charting-to-qt-graphs` is met. That proposal's `tasks.md` should be updated to mark gate condition #4 as satisfied and set Stage 0 as ready to start.

### Qt 6.10 → 6.11 delta relevant to Decenza

This is a **minor version upgrade** within Qt 6; binary compatibility is maintained. No source-breaking API removals affect Decenza's codebase based on code audit:

- **Qt Charts**: Still present and compilable in 6.11; deprecation warnings are at build-time noise level only (same as 6.10). No migration required in this change — that is `migrate-charting-to-qt-graphs`.
- **Qt Bluetooth**: `serviceUuids()` used in `blemanager.cpp:485` continues to work; the `setServiceUuids()`/`DataCompleteness` removal was a Qt 5→6 change already behind us.
- **QML signal handlers**: Already use explicit `function(mouse)` / `function(event)` syntax throughout the codebase — no changes needed.
- **Qt Positioning**: `QGeoPositionInfoSource::errorOccurred` connected in `locationprovider.cpp` is the correct Qt 6 API — no changes needed.
- **New Qt policies**: Qt 6.11 may introduce new `QTP` policy IDs. The `CMakeLists.txt` guard (`Qt6_VERSION VERSION_GREATER_EQUAL "6.5.0"`) currently sets QTP0001 + QTP0004; any new policies emitting warnings must be identified during the build step and added.

## Impact

- **Affected specs**: `build-config` (new capability — records Qt version constraints and platform targets); `cup-fill-view` (new capability — GPU-accelerated cup animation requirements).
- **Affected code**:
  - `.github/workflows/*.yml` — all 6 workflow files (`version:` bump + `qtcanvaspainter` module + sccache key + aqtinstall master pin on Windows + Xcode 26.4.1 pin on iOS)
  - `CMakeLists.txt` — `find_package(Qt6 ... CanvasPainter)`, iOS-deployment comment, new sources/headers
  - `CLAUDE.md` — Qt version and path references
  - `README.md` — Qt badge, install-step minimum, required-modules list
  - `openspec/config.yaml` — tech stack entry
  - `docs/CLAUDE_MD/PLATFORM_BUILD.md`, `docs/CLAUDE_MD/TESTING.md`, `docs/IOS_CI_SETUP.md`, `docs/IOS_CI_FOR_CLAUDE.md` — Qt version + build path references
  - `src/ui/jscanvaspainteritem.{h,cpp}` — **new** — `QCanvasPainterItem` subclass + QML registration
  - `src/ui/jscanvascontext.{h,cpp}` — **new** — JS-callable Canvas 2D ctx proxy + command buffer
  - `qml/components/CupFillView.qml` — swap `Canvas` → `JsCanvasPainterItem` (two instances); `lineCap` string literals preserved
  - `src/main.cpp` — `qmlRegisterType<JsCanvasPainterItem>` in the "Decenza" module
- **Risk**: Low for version bump; Medium for the JS↔QCanvasPainter wrapper (Tech Preview C++ API may shift in 6.12 — the `synchronize()` → `synchronizeData()` rename in 6.11.1 is one example, already absorbed). Wrapper code is isolated to two new files and one QML file — revert path is a one-line QML change per item.
- **Unblocks**: `migrate-charting-to-qt-graphs` Stage 0 (gate condition #4 satisfied)
