# Change: Upgrade Qt from 6.10.3 to 6.11.1

## Why

Qt 6.11.1 is the next minor release in the Qt 6 series and is a prerequisite for the approved `migrate-charting-to-qt-graphs` change (gate condition #4: "Qt 6.11.1 is released and Decenza upgrades to it"). Qt 6.11 also promotes Qt Graphs to full production readiness with new APIs (`visualMin`/`visualMax`, `QCustomSeries`, multi-axis support) that the charting migration depends on. Qt 6.10.x continues to receive security patches but Qt 6.11 is the forward-looking release.

## What Changes

- **CI/CD workflows** (6 files): Bump `version: '6.10.3'` → `version: '6.11.1'` in all `jurplel/install-qt-action` steps.
- **Windows sccache cache key**: Rename `sccache-windows-x64-qt6.10.3` → `sccache-windows-x64-qt6.11.1` to avoid stale-cache hits.
- **Windows workflow OpenSSL comment**: Update in-code comment referencing Qt 6.10.3 ABI; OpenSSL 3.x requirement is unchanged.
- **`CMakeLists.txt`**: Update the comment on line 134 that says "Qt 6.10.3 was built for iOS 17.0"; verify and add any new `qt_policy()` entries introduced in 6.11 (QTP0001 and QTP0004 are already set for Qt ≥ 6.5).
- **`CLAUDE.md`**: Update Qt version, dev-machine paths (`C:/Qt/6.10.3/` → `C:/Qt/6.11.1/`, `~/Qt/6.10.3/` → `~/Qt/6.11.1/`), and iOS build path in build commands.
- **`openspec/project.md`**: Update the tech stack entry to Qt 6.11.1.
- **Local developer installs**: Qt 6.11.1 must be installed locally on the Windows dev machine and Jeff's Mac (macOS + iOS targets) before building.

### Qt Canvas Painter — CupFillView GPU acceleration

`CupFillView.qml` renders the liquid fill, crema, waves, ripples, and steam wisps using two `Canvas` items with `renderStrategy: Canvas.Threaded`. This is software-rasterized on a CPU background thread and repaints at ~30fps via a 33ms `Timer` — the Decent tablet is already busy processing BLE data at 5Hz and updating the shot graph during extraction, so offloading this to the GPU is a meaningful win.

Qt 6.11 ships **Qt Canvas Painter** (Technology Preview) — a GPU-accelerated `CanvasPainterItem` that exposes the same HTML Canvas 2D context API (`ctx.beginPath`, `ctx.fill`, `ctx.createLinearGradient`, etc.). Because CupFillView's drawing code uses only standard Canvas 2D calls, the migration is a drop-in swap of the QML type; the drawing logic itself is unchanged.

**Approach**: Replace both `Canvas` items in `CupFillView.qml` with `CanvasPainterItem`, add `CanvasPainter` as an optional `find_package` in `CMakeLists.txt` (quiet, like `ShaderTools`), and evaluate visually and by feel on the Decent tablet. If the Tech Preview causes any rendering issues on any platform, revert to `Canvas` — the diff is small and localised to `CupFillView.qml`.

**Acceptance gate**: visual output matches the Canvas baseline side-by-side on Windows, macOS, and Android; animation feels at least as smooth on the Decent tablet during a live extraction.

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

- **Affected specs**: `build-config` (new capability — records Qt version constraints and platform targets)
- **Affected code**:
  - `.github/workflows/*.yml` — all 6 workflow files
  - `CMakeLists.txt` — comment + policy block + optional CanvasPainter find_package
  - `CLAUDE.md` — Qt version and path references
  - `openspec/project.md` — tech stack entry
  - `qml/components/CupFillView.qml` — swap `Canvas` → `CanvasPainterItem` (two instances)
- **Risk**: Low for version bump; Low-Medium for Canvas Painter (Tech Preview). Canvas Painter is isolated to `CupFillView.qml`; a revert is a small, localised change if the Tech Preview causes issues on any platform.
- **Unblocks**: `migrate-charting-to-qt-graphs` Stage 0 (gate condition #4 satisfied)
