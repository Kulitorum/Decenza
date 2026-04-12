# Tasks: Migrate Charting to Qt Graphs

Each stage below is independently shippable as its own PR. Do not start a stage until the previous stage has been merged, tested on hardware, and its performance measurements recorded.

**Stage 0 is currently GATED on upstream Qt work.** Complete the monitoring tasks in the "Pre-Stage 0 — Upstream Monitoring" section below before beginning any implementation work.

## Pre-Stage 0 — Upstream Monitoring (active now)

This section's tasks run until the gate conditions in `proposal.md` are satisfied. They are intentionally lightweight so they don't compete with other active work.

### P.1 Set up monitoring
- [ ] Subscribe to [QTBUG-142046](https://bugreports.qt.io/browse/QTBUG-142046) (click "Watch") to get notified on status changes
- [ ] Bookmark the [Qt Graphs 2D migration guide](https://doc.qt.io/qt-6/qtgraphs-migration-guide-2d.html) for periodic re-read
- [ ] Add a recurring reminder (calendar or task tracker) to re-check gates at each Qt release

### P.2 Quarterly gate check (or at every Qt release, whichever comes first)
- [ ] Read the Qt release notes for each new minor or patch version (`6.11.0`, `6.11.1`, `6.12.0`, …). Search for: `Graphs`, `QTBUG-142046`, `LineSeries`, `ValueAxis`
- [ ] Review the migration guide for changes to its "Missing features" section
- [ ] Scan the Qt forum's Graphs category for new gap reports or sanctioned workarounds
- [ ] If any gate condition flips from open → closed, update this file, then notify whoever owns the migration decision

### P.3 Earliest checkpoint: Qt 6.11.1 release
- [ ] When Qt 6.11.1 ships, do a full gate-condition audit. If all four gates pass, write up findings and request approval to begin Stage 0. If gates remain open, document which ones and revise the "earliest re-evaluation" date in `proposal.md`

## Stage 0 — Foundation

**Precondition**: All four gate conditions in `proposal.md` satisfied and documented in a recent Pre-Stage 0 gate-check entry.

### 0.1 Build system
- [ ] Install Qt Graphs component via Qt Maintenance Tool (dev machines only at this stage)
- [ ] Add `Graphs` to the `find_package(Qt6 REQUIRED COMPONENTS ...)` list in `CMakeLists.txt`
- [ ] Add `Qt6::Graphs` to `target_link_libraries(Decenza PRIVATE ...)`
- [ ] Verify clean build on Windows, macOS, iOS, Android with both `Charts` and `Graphs` linked
- [ ] Update `openspec/project.md` Tech Stack section to list `Graphs` alongside `Charts`

### 0.2 Spike — verify gate conditions on real Decenza code
- [ ] **Re-verify QTBUG-142046 is fixed**: write a 10-line QML reproducer that programmatically changes an axis range and reads it back. If the readback lies, halt Stage 0 and reopen Pre-Stage 0 monitoring.
- [ ] Confirm the released Qt version's `XYSeries::replace()` signature and data format; document in `design.md`. If QML `LineSeries.replace()` is still not exposed as a slot, prototype the 30-line `SeriesUpdater` bridge described in `design.md` §Upstream Dependencies.
- [ ] Verify `GraphsView.plotArea` / margin properties are sufficient for crosshair pixel↔data mapping; if not, file a Qt upstream bug and document workaround
- [ ] Test `GraphsView` clipping behavior with overlays drawn outside the plot area
- [ ] Decide `GraphsTheme` integration strategy with Decenza's `Theme.qml` singleton

### 0.3 Bridge components
- [ ] Create `qml/components/graphs/AutoRangingAxis.qml` — wraps `ValueAxis` with auto-range behavior from attached series; supports `padding`, `minFloor`, `maxCeiling`
- [ ] Create `qml/components/graphs/CustomLegend.qml` — themed horizontal legend with tap-to-toggle visibility; driven by `{ name, color, visible }` model
- [ ] Create `qml/components/graphs/DashedLineSeries.qml` — `ShapePath`-based dashed-stroke overlay aligned to `axisX`/`axisY`; data→pixel mapping via `GraphsView.plotArea`
- [ ] Register each component in `CMakeLists.txt` `qt_add_qml_module` file list

### 0.4 Performance baseline
- [ ] Create `docs/CLAUDE_MD/PERFORMANCE_BASELINE.md` documenting the measurement protocol
- [ ] Record baseline metrics on Decent tablet (Samsung SM-X210):
  - Live shot FPS during a 30-second pour (`QSG_RENDER_TIMING=1`)
  - Shot history scroll FPS with 200+ entries
  - Graph first-paint latency
  - Memory footprint after opening history with 50 shots
- [ ] Record baseline on Windows dev machine and macOS for comparison

### 0.5 Stage 0 PR
- [ ] Open PR with title `feat: add Qt Graphs alongside Qt Charts (migration Stage 0)`
- [ ] PR description lists bridge components added, spike findings, baseline measurements
- [ ] Merge after review; do not remove any Qt Charts usage yet

## Stage 1 — Pilot: `FlowCalibrationPage.qml`

### 1.1 Migrate QML
- [ ] Replace `import QtCharts` with `import QtGraphs` in `qml/pages/FlowCalibrationPage.qml`
- [ ] Replace `ChartView` with `GraphsView`
- [ ] Replace built-in `ValueAxis` usages with `AutoRangingAxis` (or explicit `min`/`max` where appropriate)
- [ ] Replace dashed goal-curve `LineSeries` with `DashedLineSeries` overlay
- [ ] Replace `.legend` references with `CustomLegend` instance
- [ ] Hook plot-area geometry to any overlay that used `plotArea`

### 1.2 Validate
- [ ] Side-by-side screenshot comparison with pre-migration state (Windows, macOS, Android)
- [ ] Re-run all Flow Calibration user flows (run a calibration, view results, adjust settings)
- [ ] Measure live FPS during calibration pour — must match or beat Stage 0 baseline
- [ ] Verify crosshair/inspect behavior if present

### 1.3 Stage 1 PR
- [ ] Open PR: `feat: migrate FlowCalibrationPage to Qt Graphs (migration Stage 1)`
- [ ] Include before/after screenshots and performance diff
- [ ] Merge only after on-device sign-off by hardware owner

## Stage 2 — Steam Graph

### 2.1 Migrate C++ model
- [ ] `src/models/steamdatamodel.h`: change `#include <QtCharts/QLineSeries>` to `#include <QtGraphs/QLineSeries>`
- [ ] Update any `QtCharts::` namespace qualifications to `QtGraphs::`
- [ ] Adjust `registerGoalSeries()` / `registerSeries()` to use the Graphs `replace()` API confirmed in Stage 0.2
- [ ] Verify `FastLineRenderer` (live series) still registers and renders — it bypasses Charts anyway

### 2.2 Migrate QML
- [ ] `qml/components/SteamGraph.qml`: `import QtCharts` → `import QtGraphs`, `ChartView` → `GraphsView`
- [ ] Replace axes with `AutoRangingAxis` or explicit ranges
- [ ] Replace legend with `CustomLegend`
- [ ] Convert dashed goal-temperature overlay to `DashedLineSeries`

### 2.3 Validate
- [ ] Live steam session on hardware — goal curve aligned, live temperature/pressure traces smooth
- [ ] Steam history view — historical sessions render correctly
- [ ] FPS meets baseline

### 2.4 Stage 2 PR
- [ ] Open PR: `feat: migrate SteamGraph and SteamDataModel to Qt Graphs (migration Stage 2)`
- [ ] Merge after on-device sign-off

## Stage 3 — Espresso Graphs

Stage 3 is the largest. Split into sub-stages 3a–3d to keep PRs reviewable (one graph family per PR).

### 3.1 Stage 3a — `ShotGraph.qml` + `ShotDataModel` (live extraction view)
- [ ] C++: migrate `ShotDataModel` includes + namespaces; update series-registration API
- [ ] QML: migrate `ShotGraph.qml` — GraphsView, AutoRangingAxis, CustomLegend, DashedLineSeries for goal curves, DashedLineSeries for frame markers
- [ ] Preserve `FastLineRenderer` integration (live pressure/flow/temperature/weight traces)
- [ ] Validate crosshair + inspect-point feature still works
- [ ] Validate series visibility toggling via legend
- [ ] Stage 3a PR: `feat: migrate ShotGraph to Qt Graphs (migration Stage 3a)`

### 3.2 Stage 3b — `HistoryShotGraph.qml` (history detail view)
- [ ] QML migration following Stage 3a pattern
- [ ] Ensure scroll performance of the history list is not degraded (each list row contains a HistoryShotGraph)
- [ ] Stage 3b PR: `feat: migrate HistoryShotGraph to Qt Graphs (migration Stage 3b)`

### 3.3 Stage 3c — `ComparisonGraph.qml` + `ShotComparisonModel`
- [ ] C++: migrate `ShotComparisonModel` includes + namespaces
- [ ] QML: migrate; pay special attention to Canvas-based phase markers — these are Charts-independent and must continue to overlay correctly on GraphsView
- [ ] Validate 2-shot and 3-shot comparisons render identically
- [ ] Stage 3c PR: `feat: migrate ComparisonGraph to Qt Graphs (migration Stage 3c)`

### 3.4 Stage 3d — `ProfileGraph.qml` (profile editor preview)
- [ ] QML migration; the graph previews simulated extraction from profile frames
- [ ] Verify profile-editing responsiveness (graph re-renders on frame edit)
- [ ] Stage 3d PR: `feat: migrate ProfileGraph to Qt Graphs (migration Stage 3d)`

## Stage 4 — Cleanup

### 4.1 Remove Qt Charts
- [ ] Audit codebase: `grep -r "QtCharts" src/ qml/` must return zero matches
- [ ] Remove `Charts` from `find_package(Qt6 REQUIRED COMPONENTS ...)` in `CMakeLists.txt`
- [ ] Remove any remaining `Qt6::Charts` link references
- [ ] Build clean on all platforms; confirm app launches and all graphs render

### 4.2 Bridge-component retention decision
- [ ] Decide whether `AutoRangingAxis`, `CustomLegend`, and `DashedLineSeries` stay as project-owned components (likely yes — they're useful and Qt Graphs' gaps are real)
- [ ] Update `CLAUDE.md` graph/QML conventions section to mention the bridge components as canonical

### 4.3 Documentation and cleanup
- [ ] Update `openspec/project.md` Tech Stack: remove `Charts`
- [ ] Update `CLAUDE.md` if it mentions Qt Charts anywhere
- [ ] Uninstall Qt Charts from dev machines' Qt installations
- [ ] Uninstall Qt Data Visualization (unrelated to this migration but should be cleaned up at the same time — Decenza does not use it)

### 4.4 Final PR and archive
- [ ] Open PR: `chore: remove Qt Charts dependency (migration Stage 4)`
- [ ] After merge, archive this change: `openspec archive migrate-charting-to-qt-graphs --yes`
- [ ] Update `openspec/specs/charting/spec.md` with the final capability definition

## Cross-stage acceptance criteria

At each stage PR before merge:
- [ ] Clean build on Windows, macOS, iOS, Android
- [ ] On-device smoke test on Decent tablet (Samsung SM-X210)
- [ ] Live FPS meets or beats Stage 0 baseline (documented in PR)
- [ ] No new Qt log warnings/errors introduced
- [ ] Side-by-side screenshots attached for any graph touched in that stage
