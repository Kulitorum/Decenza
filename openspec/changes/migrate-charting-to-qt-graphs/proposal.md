# Change: Migrate Charting from Qt Charts to Qt Graphs

## Status: READY TO START — PENDING Qt 6.11.1 UPGRADE

**Gates are clear.** QTBUG-142046 is closed and fixed in Qt 6.11.0 (released 2026-03-23) via new `visualMin`/`visualMax` read-only properties on `QValueAxis` and `QDateTimeAxis`. Decenza plans to upgrade to Qt 6.11.1 when it releases. Stage 0 can begin as soon as the upgrade lands.

**Note on crosshair logic**: the fix is a new API (`visualMin`/`visualMax`) rather than a change to `min`/`max` behavior. `ShotGraph.qml`'s crosshair pixel↔data mapping must be updated to use `visualMin`/`visualMax` instead of `min`/`max`.

## Why

Qt Charts was deprecated in Qt 6.10 (the version Decenza targets) and is stuck on the Qt 4-era Graphics View Framework — software-rendered, Widgets-coupled, and not GPU-accelerated. Qt's successor is **Qt Graphs**, which renders via Qt Quick Shapes + Qt RHI, using each platform's native backend (Metal on macOS/iOS, Direct3D on Windows, Vulkan/OpenGL on Linux). Shot graphs are the most GPU-intensive surface in Decenza — running them on the modern pipeline is the right long-term direction.

Qt Charts will not be removed imminently: per the Qt forum consensus it remains available for **the entire lifetime of Qt 6** (shipped late 2020, still the current major version), and is only removed in **Qt 7** (no announced release date). Qt staff have publicly stated *"for new projects it's recommended to start with Qt Graphs. But there's nothing wrong with using Qt Charts"* and *"if you can (i.e. no feature is missing) use Qt Graphs as Qt Charts will be phased out eventually."* The migration is a *when*, not *if* — but the "when" is gated on Qt Graphs closing its current feature gaps, not on internal Decenza timing.

## Gate Conditions (required before Stage 0 starts)

Stage 0 SHALL NOT begin until **all** of the following are true. Each condition is verifiable without ambiguity.

1. ✅ **[QTBUG-142046](https://qt-project.atlassian.net/browse/QTBUG-142046)** — *Axis range properties on `GraphsView` return stale/constant values regardless of zoom/pan* — **CLOSED. Fixed in Qt 6.11.0** (commit `12e2be474191195448d3a0ca73d14e0543a266a4` in `qt/qtgraphs`). Fix adds `visualMin`/`visualMax` read-only properties to `QValueAxis` and `QDateTimeAxis`. Not backported to 6.10.x. Crosshair logic in `ShotGraph.qml` must use `visualMin`/`visualMax` instead of `min`/`max`.

2. **Qt Graphs 2D migration guide** ([doc.qt.io/qt-6/qtgraphs-migration-guide-2d.html](https://doc.qt.io/qt-6/qtgraphs-migration-guide-2d.html)) no longer contains a "Missing features" or equivalent caveat section for the features Decenza uses: built-in legend (or a sanctioned bridge pattern), axis auto-ranging (or sanctioned bridge pattern), dash/dot line strokes, and an approved pixel↔data coordinate mapping API.

3. **`QXYSeries::replace(QList<QPointF>)` or equivalent bulk-update API** is officially supported in Qt Graphs and documented. Decenza's C++ data models (`ShotDataModel`, `SteamDataModel`, `ShotComparisonModel`) rely on this for efficient series population at ~5 Hz during live extraction; per-point `append()` is not performant enough.

4. **Qt 6.11.1 is released and Decenza upgrades to it** (planned). Decenza will upgrade to Qt 6.11.1 when it releases; Stage 0 begins after the upgrade lands.

### Re-evaluation cadence (until all gates pass)

- Check Qt release notes at every Qt minor release (`6.11.0`, `6.11.1`, `6.12.0`, ...): search for "Graphs", "QTBUG-142046", and any mention of the missing features.
- Skim the Qt forum Graphs category monthly for new gap reports or workaround patterns.
- If Qt 7 gets an announced release date with a Charts-removal timeline, escalate priority even if gates aren't fully met.

### If gates pass but new blockers emerge

This proposal's Stage 0 includes a technical spike (`tasks.md` §0.2) that validates the gates on the actual Decenza codebase before any user-visible work begins. If the spike reveals new blockers (e.g., the bulk `replace()` API exists but has a performance regression at our data rates), those blockers go into a new "Gate Conditions" update to this proposal, and Stage 0 pauses until they clear too.

## What Changes

**Staged migration** across five phases. Each phase is independently shippable; the app remains fully functional at every stage boundary, with no flag-day cutover.

- **Stage 0 — Foundation**: Add `Qt6::Graphs` to the build alongside `Qt6::Charts` (both import paths coexist). Build reusable QML components (`AutoRangingAxis`, `CustomLegend`, `DashedLineSeries`) that close the remaining feature gaps between Charts and Graphs.
- **Stage 1 — Pilot**: Migrate `FlowCalibrationPage.qml` (smallest graph, ~40% chart code) as a proof of concept to validate the pattern end-to-end.
- **Stage 2 — Steam graph**: Migrate `SteamGraph.qml` + `SteamDataModel` C++ backing (simpler than espresso graphs — fewer axes, no goal curves).
- **Stage 3 — Espresso graphs**: Migrate the four espresso graph families (`ShotGraph`, `HistoryShotGraph`, `ComparisonGraph`, `ProfileGraph`) and their C++ backing (`ShotDataModel`, `ShotComparisonModel`).
- **Stage 4 — Cleanup**: Remove `Qt6::Charts` from `CMakeLists.txt`, delete migration shim components, uninstall Qt Charts from dev machines, close the migration.

**Intentionally out of scope**: visual redesign of any graph (migration is mechanical fidelity only), migration of `FastLineRenderer` (already bypasses Qt Charts — survives intact), migration of Canvas-based phase markers in `ComparisonGraph` (independent of Charts).

## What Changes

**Staged migration** across five phases. Each phase is independently shippable; the app remains fully functional at every stage boundary, with no flag-day cutover.

- **Stage 0 — Foundation**: Add `Qt6::Graphs` to the build alongside `Qt6::Charts` (both import paths coexist). Build reusable QML components (`AutoRangingAxis`, `CustomLegend`, `DashedLineSeries`) that close the feature gaps between Charts and Graphs.
- **Stage 1 — Pilot**: Migrate `FlowCalibrationPage.qml` (smallest graph, ~40% chart code) as a proof of concept to validate the pattern end-to-end.
- **Stage 2 — Steam graph**: Migrate `SteamGraph.qml` + `SteamDataModel` C++ backing (simpler than espresso graphs — fewer axes, no goal curves).
- **Stage 3 — Espresso graphs**: Migrate the four espresso graph families (`ShotGraph`, `HistoryShotGraph`, `ComparisonGraph`, `ProfileGraph`) and their C++ backing (`ShotDataModel`, `ShotComparisonModel`).
- **Stage 4 — Cleanup**: Remove `Qt6::Charts` from `CMakeLists.txt`, delete migration shim components, uninstall Qt Charts from dev machines, close the migration.

**Intentionally out of scope**: visual redesign of any graph (migration is mechanical fidelity only), migration of `FastLineRenderer` (already bypasses Qt Charts — survives intact), migration of Canvas-based phase markers in `ComparisonGraph` (independent of Charts).

## Impact

- **Affected specs**: `charting` (new capability — codifies Decenza's graphing contract so future rendering-backend swaps are cheaper)
- **Affected code**:
  - `CMakeLists.txt` — add `Graphs` component, drop `Charts` in Stage 4
  - `qml/components/ShotGraph.qml`, `HistoryShotGraph.qml`, `ComparisonGraph.qml`, `SteamGraph.qml`, `ProfileGraph.qml` (~2 350 lines of QML)
  - `qml/pages/FlowCalibrationPage.qml` (~272 lines)
  - `src/models/shotdatamodel.{h,cpp}`, `src/models/steamdatamodel.{h,cpp}`, `src/models/shotcomparisonmodel.{h,cpp}` (~1 460 lines)
  - New QML components under `qml/components/graphs/` — `AutoRangingAxis.qml`, `CustomLegend.qml`, `DashedLineSeries.qml`
- **Performance target**: graphs render at ≥60 fps during live extraction on Decent tablet (Samsung SM-X210) — measured via `Qt Quick Scene Graph` profiler; no regression on existing frame-drop tests (currently frame drops <1% of extraction time).
- **Risk**: mid-migration regressions are the largest hazard. Mitigation is the staged approach — each stage ships to `main` independently with the full test protocol (see `design.md`).

## Success Criteria

- Every graph in the app renders with visual fidelity matching the pre-migration baseline (side-by-side screenshot comparison on Windows, macOS, Android).
- No user-visible feature regressions (crosshairs, axis toggling, dash-line goal curves, phase markers, zoom/pan if present, legend).
- Shot-history list with 100+ entries opens and scrolls at the same speed or faster than the Qt Charts baseline.
- Live shot graphing on the Decent tablet maintains ≥60 fps during the densest extraction phase (high-flow pour with scale updates at 20 Hz).
- `CMakeLists.txt` contains no reference to `Qt6::Charts`; the Qt Charts library can be uninstalled with no build or runtime breakage.

## Non-Goals

- **No new graphing features** as part of the migration. Adding zoom, new series types, or theming changes is a separate future change after Stage 4 archives.
- **No 3D graphs.** Qt Graphs supports 3D; Decenza has no 3D graph use case today.
- **No QSGGeometryNode rewrite.** The existing `FastLineRenderer` pattern is faster than either Charts or Graphs for live data and stays as-is.
