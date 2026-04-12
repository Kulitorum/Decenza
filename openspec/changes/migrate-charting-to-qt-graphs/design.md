# Design: Charting Migration from Qt Charts to Qt Graphs

## Upstream Dependencies

This change is blocked on upstream Qt work. The gates are documented in `proposal.md` under "Gate Conditions"; this section documents the engineering impact of each open issue and how it shapes the design.

### QTBUG-142046 — Axis range API returns stale values

**Link**: [bugreports.qt.io/browse/QTBUG-142046](https://bugreports.qt.io/browse/QTBUG-142046)

**Impact on Decenza**: `ShotGraph.qml` and `HistoryShotGraph.qml` implement a tap-to-inspect feature: the user taps the plot area and a crosshair snaps to the nearest data point, displaying the values at that x. Implementation requires reading the current `ValueAxis.min`/`max` to convert the tap's pixel coordinates into data coordinates. With the bug present, those properties return a constant garbage value (one forum user reported `25.626488705368423`) regardless of the actual visible range — meaning the crosshair would snap to wrong points on any zoomed or programmatically-range-set graph.

**Why we can't work around it**: `GraphsView` does not (as of Qt 6.10–6.11) expose an alternative API that reports the effective axis range. We could track every programmatic range change ourselves in QML and mirror it into a side channel, but (a) that duplicates bookkeeping Qt should own, (b) it doesn't help if the range changes via user interaction (zoom/pan), and (c) maintaining it against a Qt version where the official API starts working again would cause divergence.

**Status monitoring**: Watch the ticket's Fix Version field. A "6.11.x" or "6.12" designation opens the gate.

### Missing bulk-update API on `XYSeries`

**Forum reference**: [forum.qt.io/topic/158993/dynamically-add-point-to-lineseries-of-qml-qt-graphs](https://forum.qt.io/topic/158993)

**Impact on Decenza**: `ShotDataModel` registers `QLineSeries` from QML and calls `replace(QList<QPointF>)` at ~5 Hz during live extraction to push the full updated dataset in one call. Per-point `append()` calls QML→C++ hundreds of times per second, which Qt forum users have flagged as a performance bottleneck.

**Forum workarounds reported**:
1. Pre-build data in C++ and call `QXYSeries::replace()` from a DataSource class that receives the `QLineSeries*` as a property. Qt users report this is "fast" — unclear if the QML `LineSeries` type exposes `replace()` via a slot (Qt has TODO comments in the source indicating this is planned but not done).
2. `clear()` + append-in-loop. Forum consensus: works but not performant enough for real-time.

**Design implication**: If by the time gates pass the QML `LineSeries.replace()` slot still isn't exposed, we either (a) keep the current C++-side registration pattern (which should continue to work with Qt Graphs' `QLineSeries` since it inherits from `QXYSeries`), or (b) write a small `SeriesUpdater` C++ class that wraps `QList<QPointF>` replacement. Option (a) is free; option (b) is ~30 lines. Neither is a blocker — just a style choice revealed at spike time.

## Feature-Gap Analysis

Four Qt Charts features used by Decenza have no direct Qt Graphs equivalent. Each needs a bridging strategy before any migration code is written.

### 1. Axis auto-ranging (Charts auto-sizes, Graphs requires explicit `min`/`max`)

**Charts behavior**: `ValueAxis` auto-computes `min`/`max` from attached series data and re-fits on series change.

**Graphs behavior**: Axes have no auto-range; `min`/`max` must be set explicitly. If series data exceeds the range, points clip silently.

**Bridge**: Build `qml/components/graphs/AutoRangingAxis.qml` — a `ValueAxis` subclass that binds `min`/`max` to computed expressions over attached series. Takes:
- `series: var` — one or more `XYSeries` to track
- `padding: real` — fraction above/below the data range (default 0.05)
- `minFloor: real`, `maxCeiling: real` — optional hard clamps (used for temperature axes that should never dip below room temperature)

Recomputes on `series.pointsChanged` and on series list changes. Used everywhere a Qt Charts `ValueAxis` currently relies on auto-range behavior.

### 2. Legend (Charts built-in, Graphs has none)

**Charts behavior**: `ChartView.legend` exposes a fully-themed legend with markers synced to series colors and visibility.

**Graphs behavior**: No built-in legend. Must be built from QML primitives.

**Bridge**: Build `qml/components/graphs/CustomLegend.qml` — a horizontal `Row` of colored-square + label pairs, driven by a list of `{ name, color, visible }` objects. Matches the existing `Theme.qml` styling and supports tap-to-toggle visibility (already used in `ShotGraph` for muting individual series).

### 3. Dash/dot line styles (Charts has `Qt.DashLine`, Graphs does not)

**Charts behavior**: `LineSeries.style: Qt.DashLine | Qt.DotLine | Qt.DashDotLine` renders the stroke pattern in the series itself.

**Graphs behavior**: `LineSeries` is solid-stroke only. Qt Quick Shapes (which Graphs is built on) supports `ShapePath.strokeStyle: ShapePath.DashLine` but not through the `LineSeries` element.

**Bridge**: Build `qml/components/graphs/DashedLineSeries.qml` — a reusable overlay that takes the same `points` binding as a `LineSeries` and draws a `ShapePath` with a `strokeStyle` pattern. Used for goal curves, frame-boundary markers, and phase-transition indicators.

Axis alignment is the tricky part: the `ShapePath` is drawn in plot-area coordinates (pixels), not data coordinates. The component accepts `axisX` and `axisY` references and does the data→pixel mapping using `GraphsView.plotArea` dimensions.

### 4. `plotArea` geometry exposure

**Charts behavior**: `ChartView.plotArea` returns a `rect` usable for pixel↔data coordinate mapping (used by crosshairs and the "inspect this point" feature in `ShotGraph`).

**Graphs behavior**: `GraphsView` exposes plot-area geometry via different properties — need to verify during Stage 0 whether `marginLeft`/`marginRight`/`marginTop`/`marginBottom` are sufficient or if a custom property binding is needed. If inadequate, file an upstream Qt bug; in the interim, compute geometry from `width`/`height` minus axis sizes.

## Staged-Migration Rationale

### Why 5 stages and not a big-bang rewrite?

- **Each graph is isolated.** `ShotGraph` and `SteamGraph` do not share QML code; they can migrate independently.
- **The app remains usable between stages.** At every stage boundary, both Charts and Graphs are linked and in use. A staged rollback of a single graph is cheap — just revert that graph's file.
- **Feedback accumulates.** Lessons from Stage 1 (FlowCalibrationPage) materially shape Stages 2–3 design. A big-bang approach commits to decisions before validating them on even one graph.
- **CI stays green throughout.** No long-lived migration branch; each stage is a regular PR to `main`.

### Stage ordering justification

| Stage | Target | Why this order |
|---|---|---|
| 0 | Infrastructure only | Defer all visible change until bridge components exist |
| 1 | `FlowCalibrationPage` | Smallest surface (~272 lines, 40% chart) — validates the pattern with lowest blast radius |
| 2 | `SteamGraph` + `SteamDataModel` | Simpler than espresso (fewer axes, no goal curves, no interactive crosshairs) — second-easiest win |
| 3 | Espresso graphs (4 QML + 2 C++ models) | Hardest. By this point the bridge components are battle-tested and the migration pattern is well-understood |
| 4 | Remove Charts dependency | Must be last: cannot remove the import until every graph has migrated |

### Why not migrate C++ models first, QML last (or vice-versa)?

Qt Charts and Qt Graphs both use `QLineSeries` at the C++ level but from different namespaces (`QtCharts::QLineSeries` vs `QtGraphs::QLineSeries`). They are not interchangeable — a `replaceSeries()` call into a QML-side `QtGraphs.LineSeries` from a C++ `QtCharts::QLineSeries` would fail at runtime. Each graph's QML and C++ sides must migrate together in a single stage.

## Performance Measurement

Before Stage 1 begins, capture baseline metrics so each stage can be evaluated against them:

1. **Live shot FPS** — run a shot on the Decent tablet (Samsung SM-X210) with `QSG_RENDER_TIMING=1`, log min/median/max frame times during the 30-second pour. Expected baseline: ~16.7 ms median (60 fps).
2. **History scroll smoothness** — scroll the shot history list with 200+ entries; measure scroll FPS via Qt Quick profiler.
3. **Graph first-paint latency** — time from `Component.onCompleted` on a `ShotGraph` to first frame rendered. Baseline is whatever Charts currently delivers.
4. **Memory footprint** — `QProcess::RssUsage` after opening the history view with 50 shots; Graphs is expected to be lower due to dropping the Graphics View framework.

Record baselines in `docs/CLAUDE_MD/PERFORMANCE_BASELINE.md` (new file in Stage 0). Re-measure at each stage boundary and note regressions in the PR description.

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| **QTBUG-142046 regresses after initial fix** (axis range API) | Low | High (breaks crosshairs) | Stage 0 spike re-runs the regression test; hold Stage 1 if regressed |
| Qt Graphs `plotArea` API insufficient for crosshairs | Medium | High (blocks ShotGraph migration) | Prototype in Stage 0 before committing to Stage 3 scope |
| `DashedLineSeries` axis mapping is buggy at axis-range changes | Medium | Medium | Stage 1 pilot stress-tests this on flow-cal goal curves |
| Visual fidelity diverges (anti-aliasing, line thickness) | High | Low | Acceptable — document divergence, don't chase pixel-perfect parity |
| Live shot FPS regresses on lower-end Android tablets | Low | High | Measure in Stage 1 on the oldest supported hardware; abort migration if >10% regression |
| Qt version with fixes has a *different* blocking bug | Medium | Medium | Stage 0 spike catches it before any migration code ships |
| `QXYSeries::replace()` slot not exposed to QML | Medium | Low | C++-side registration continues to work; ~30 lines of bridge code as fallback |
| Qt announces Qt 7 release date sooner than expected, compressing migration timeline | Low | Medium | Gates already include a Qt 7 escalation clause in `proposal.md` |

## Open Questions

- **Does Qt 6.10 Graphs support series `replace()` with a `QList<QPointF>`?** If not, `ShotDataModel::registerSeries()` needs an alternative bulk-update path (possibly `setPoints()` on each update). Must be answered in Stage 0.
- **Does `GraphsView` respect the parent `QQuickItem` clip rect?** Current `ChartView`-based graphs rely on plot-area clipping when overlays draw out of range. Verify before Stage 1.
- **Theme system compatibility.** Qt Graphs uses `GraphsTheme` (unified 2D/3D theme). Does it play nicely with Decenza's `Theme.qml` singleton, or does `Theme.qml` need new properties to feed `GraphsTheme`? Defer decision to Stage 0 spike.

## Rollback Plan

If any stage must be rolled back mid-migration:

1. **Stage 0 rollback**: Remove `Qt6::Graphs` from `CMakeLists.txt`, delete `qml/components/graphs/`. No user impact — nothing was using it.
2. **Stage 1–3 rollback**: `git revert` the stage's PR. Qt Charts path remains intact in every preceding commit. No data migration or setting reset needed.
3. **Stage 4 rollback**: Re-add `Charts` to `CMakeLists.txt`. No QML code changes needed since Stage 4 only removes the dependency.

All rollbacks are clean because Charts and Graphs coexist in the build from Stage 0 through Stage 3.
