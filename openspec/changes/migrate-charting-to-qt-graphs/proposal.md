# Change: Migrate Charting from Qt Charts to Qt Graphs

## Status: STAGE 0 + STAGE 1 RECOMMENDED NOW (accelerated path)

**Gates 1, 3, 4 are satisfied.** QTBUG-142046 is closed and fixed in Qt 6.11.0 (released 2026-03-23) via new `visualMin`/`visualMax` read-only properties on `QValueAxis` and `QDateTimeAxis`. `QXYSeries::replace(QList<QPointF>)` is `Q_INVOKABLE` from QML in 6.11.1 with docs explicitly noting it is "much faster than replacing data points one by one." Decenza upgraded to Qt 6.11.1 (released 2026-05-12) via the `upgrade-qt-6-11-1` change.

**Gate 2 is open but the gaps are known and the Stage 0 bridge plan presumes they exist** — see "Re-evaluation against the released 6.11.1" below.

**Recommendation (revised 2026-05-13): start Stage 0 + Stage 1 on 6.11.1 now, do not wait for Qt 6.12 GA.** The primary motivation is **older-tablet CPU headroom**. Qt Charts is software-rasterized via the Graphics View Framework — every chart pixel is drawn by CPU. Qt Graphs on today's Quick Shapes backend (6.11.1) does CPU-side path triangulation then submits triangle meshes to the GPU via RHI for actual rasterization — meaningfully less CPU than Charts on its own. The 6.12 `QCanvasPainter` backend pushes more work to the GPU still, but enabling it on already-migrated graphs is a one-line `useCanvasPainter: true` flip per `GraphsView`, not a re-migration. The bridge components (`AutoRangingAxis`, `CustomLegend`, `DashedLineSeries`) are renderer-agnostic and outlive the backend choice. Stage 1 (`FlowCalibrationPage`) is the smallest graph in the app and is the cheap experiment that decides whether to charge ahead to Stages 2–3 or pause for 6.12 — see "Timing recommendation" in the Qt 6.12 Roadmap section.

**Note on crosshair logic**: the fix is a new API (`visualMin`/`visualMax`) rather than a change to `min`/`max` behavior. `ShotGraph.qml`'s crosshair pixel↔data mapping must be updated to use `visualMin`/`visualMax` instead of `min`/`max`.

## Why

Qt Charts was deprecated in Qt 6.10 (the version Decenza targets) and is stuck on the Qt 4-era Graphics View Framework — software-rendered, Widgets-coupled, and not GPU-accelerated. Qt's successor is **Qt Graphs**, which renders via Qt Quick Shapes + Qt RHI, using each platform's native backend (Metal on macOS/iOS, Direct3D on Windows, Vulkan/OpenGL on Linux). Shot graphs are the most GPU-intensive surface in Decenza — running them on the modern pipeline is the right long-term direction.

Qt Charts will not be removed imminently: per the Qt forum consensus it remains available for **the entire lifetime of Qt 6** (shipped late 2020, still the current major version), and is only removed in **Qt 7** (no announced release date). Qt staff have publicly stated *"for new projects it's recommended to start with Qt Graphs. But there's nothing wrong with using Qt Charts"* and *"if you can (i.e. no feature is missing) use Qt Graphs as Qt Charts will be phased out eventually."* The migration is a *when*, not *if* — but the "when" is gated on Qt Graphs closing its current feature gaps, not on internal Decenza timing.

## Gate Conditions (required before Stage 0 starts)

Stage 0 SHALL NOT begin until **all** of the following are true. Each condition is verifiable without ambiguity.

1. ✅ **[QTBUG-142046](https://qt-project.atlassian.net/browse/QTBUG-142046)** — *Axis range properties on `GraphsView` return stale/constant values regardless of zoom/pan* — **CLOSED. Fixed in Qt 6.11.0** (commit `12e2be474191195448d3a0ca73d14e0543a266a4` in `qt/qtgraphs`). Fix adds `visualMin`/`visualMax` read-only properties to `QValueAxis` and `QDateTimeAxis`. Not backported to 6.10.x. Crosshair logic in `ShotGraph.qml` must use `visualMin`/`visualMax` instead of `min`/`max`.

2. **Qt Graphs 2D migration guide** ([doc.qt.io/qt-6/qtgraphs-migration-guide-2d.html](https://doc.qt.io/qt-6/qtgraphs-migration-guide-2d.html)) no longer contains a "Missing features" or equivalent caveat section for the features Decenza uses: built-in legend (or a sanctioned bridge pattern), axis auto-ranging (or sanctioned bridge pattern), dash/dot line strokes, and an approved pixel↔data coordinate mapping API.

3. **`QXYSeries::replace(QList<QPointF>)` or equivalent bulk-update API** is officially supported in Qt Graphs and documented. Decenza's C++ data models (`ShotDataModel`, `SteamDataModel`, `ShotComparisonModel`) rely on this for efficient series population at ~5 Hz during live extraction; per-point `append()` is not performant enough.

4. ✅ **Qt 6.11.1 is released and Decenza upgrades to it** — **SATISFIED**. Qt 6.11.1 released 2026-05-12; Decenza upgraded via the `upgrade-qt-6-11-1` change (archived 2026-05-13).

### Re-evaluation against the released 6.11.1 (2026-05-13)

The 6.11.1 audit of [the Qt Graphs 2D migration guide](https://doc.qt.io/qt-6.11/qtgraphs-migration-guide-2d.html) confirms the "Features missing in Qt Graphs" list still includes:

- **Titles and legends** — explicitly listed
- **Axis auto-ranging** — implicit: the guide's migration examples state verbatim "*Graphs don't calculate a visible range for axes. You should define the visible range explicitly*"
- **Dashed/dotted line strokes** — not called out in the guide but not documented as supported either
- **Pixel↔data coordinate mapping** — not called out; `GraphsView.plotArea` continues to be the only documented hook

Qt has not published sanctioned bridge patterns for legends or auto-ranging. Gate 2 as originally written is therefore **not** met. However, Stage 0 of this proposal was always designed to **build** those bridges in-tree (`AutoRangingAxis`, `CustomLegend`, `DashedLineSeries`) because the proposal-authors did not assume Qt would ship them upstream. The gate language is stricter than the implementation strategy needs.

**Pragmatic reading:** the four gates effectively reduce to "Qt 6.11.x + bulk-replace + visualMin/Max" — all satisfied. The Stage 0 bridges remain Decenza's responsibility regardless of when the migration starts.

### Re-evaluation cadence (between now and Qt 6.12 GA)

- Check Qt release notes at every Qt minor release: search for "Graphs", "QTBUG-142046", and any mention of the missing features.
- Skim the Qt forum Graphs category monthly for new gap reports or workaround patterns.
- Monitor the Qt 6.12 dev branch through the 2026-05-29 feature freeze — `qt/qtgraphs.git` on `dev` is the source of truth for what 6.12 actually ships.
- If Qt 7 gets an announced release date with a Charts-removal timeline, escalate priority even if gates aren't fully met.

### If gates pass but new blockers emerge

This proposal's Stage 0 includes a technical spike (`tasks.md` §0.2) that validates the gates on the actual Decenza codebase before any user-visible work begins. If the spike reveals new blockers (e.g., the bulk `replace()` API exists but has a performance regression at our data rates), those blockers go into a new "Gate Conditions" update to this proposal, and Stage 0 pauses until they clear too.

## Qt 6.12 Roadmap (informational — release 2026-09-22)

Feature freeze 2026-05-29; what's currently on `qt/qtgraphs.git` `dev` is essentially what 6.12 will ship. Two landings change the cost-benefit:

### 1. `QCanvasPainter` rendering backend for Qt Graphs — [QTBUG-140734](https://qt-project.atlassian.net/browse/QTBUG-140734)

qtgraphs commit `03e677e1` adds a second renderer that draws via `QCanvasPainter` — the same module Decenza adopted for `CupFillView` in the `upgrade-qt-6-11-1` change. Selected via two CMake feature flags:
- `high_performance_backend` → enables the `QCanvasPainter` path
- `high_quality_backend` → enables the existing Quick Shapes path (today's only path)

Both flags can be on; a new `useCanvasPainter` property on `GraphsView` chooses at runtime. **Off by default in 6.12.** If Decenza migrates after 6.12 ships, our graph rendering can use the exact same RHI path as the cup-fill — Metal / D3D12 / Vulkan / OpenGL — with one rendering stack across the most expensive UI surfaces.

### 2. Declarative XYSeries data API — [QTBUG-134005](https://qt-project.atlassian.net/browse/QTBUG-134005), [QTBUG-141139](https://qt-project.atlassian.net/browse/QTBUG-141139)

qtgraphs commit `2e6e74c9` adds a declarative `data` property on `XYSeries` (and subclasses like `LineSeries`, `ScatterSeries`). QML can populate point arrays without round-tripping through `Q_INVOKABLE` C++ calls. Decenza's data models will continue to use the C++ `replace()` path for live ~5 Hz updates, but static/computed series (goal curves, profile-editor previews) can be wired up declaratively.

### 3. Minor wins in 6.12

- `labelPostFormat` on `(Log)ValueAxis` — axis-label format string (e.g. `"%.1f bar"`). Replaces bespoke `labelFormat` callback plumbing.
- Multi-axis margin fix — "Count unique axes when more than one series shares the X and/or Y axis". Decenza always shares axes across pressure/flow/temperature/weight series; this corrects current margin miscalculation.
- "Fix crash when axis is deleted" — stability.

### What 6.12 does NOT add

The four Stage 0 bridge components remain Decenza's responsibility:

| Gap | Status on dev (16 days from feature freeze) |
|---|---|
| Built-in legend | Still missing |
| Axis auto-ranging | Still missing — `min`/`max` still required explicitly |
| Dashed/dotted line strokes | No sanctioned API |
| Pixel↔data coordinate mapping | `GraphsView.plotArea` remains the only hook |

These are not on the 6.12 dev log; do not plan around them landing in 6.12.

### Timing recommendation (revised 2026-05-13)

**Start Stage 0 + Stage 1 now on 6.11.1.** The cup-fill GPU migration that landed in `upgrade-qt-6-11-1` removed roughly 11 % of one core on the Decent tablet, but graph repainting is the larger remaining CPU cost on extraction-time UI — Qt Charts is fully software-rasterized. Today's Qt Graphs Quick Shapes backend already does GPU rasterization (CPU triangulates paths to meshes, GPU rasterizes via RHI); the 6.12 `QCanvasPainter` backend reduces the CPU prep further. Net: even pre-6.12 there is a real, measurable CPU win for slower tablets, and the 6.12 backend flip is a one-line follow-up per `GraphsView`.

**Why Stage 1 is the cheap experiment, not Stage 0:**

Stage 0 ships infrastructure only (`Qt6::Graphs` linked, three bridge components, no user-visible change). Stage 1 migrates `FlowCalibrationPage` (the smallest graph in the app, ~272 lines) — that is the first place a real before/after CPU/FPS measurement is possible. The decision tree at the end of Stage 1:

- **If Stage 1 measurement shows measurable CPU drop on the Decent tablet** → continue immediately to Stages 2–3.
- **If Stage 1 is neutral or worse** → pause, wait for 6.12 GA (2026-09-22), flip `useCanvasPainter: true`, re-measure. Stage 0 + 1 work is not wasted — the bridges are reusable and the migration pattern is validated.

**Estimated cost of Stage 0 + Stage 1: 4–6 focused days, worst case ~1.5 weeks** if `GraphsView.plotArea` has edge cases during axis animation. Breakdown:

- `AutoRangingAxis.qml` — ~50 lines, logic-only, ½ day.
- `CustomLegend.qml` — ~100 lines themed via `Theme.qml` (will visually match the rest of the app better than Qt Charts' Widget-styled built-in legend), ~1 day.
- `DashedLineSeries.qml` — ~150 lines; `ShapePath` overlay with data→pixel mapping via `GraphsView.plotArea`. Trickiest piece; 1–2 days. `FlowCalibrationPage` doesn't zoom, so any `plotArea` edge cases discovered here are bounded.
- Stage 0 build wiring + 0.2 spike — ½–1 day.
- Stage 1 `FlowCalibrationPage` migration + measurement — ~1 day, mostly mechanical (`ChartView` → `GraphsView`, swap axis/legend types, replace dashed goal-curve with the new overlay).

**Visual risk areas (small):**

- Axis label text moves from `QPainter::drawText` to `QQuickText` scene-graph rendering — usually sharper, but subtly different. On the tablet this typically reads as a slight improvement.
- 1px-wide series at very thin widths may antialias differently between CPU and GPU. Goal curves (2px+) are unaffected.

The bigger items (Stages 2–3, the espresso graph family) only get scheduled after the Stage 1 measurement is in hand. So the worst case for accelerating is: 4–6 days produces a real measurement and three reusable bridge components, then a defensible pause.

## What Changes

**Staged migration** across five phases. Each phase is independently shippable; the app remains fully functional at every stage boundary, with no flag-day cutover.

- **Stage 0 — Foundation**: Add `Qt6::Graphs` to the build alongside `Qt6::Charts` (both import paths coexist). Build reusable QML components (`AutoRangingAxis`, `CustomLegend`, `DashedLineSeries`) that close the remaining feature gaps between Charts and Graphs.
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
