# Tasks: Qt 6.12 polish for charts migration

**Precondition**: `upgrade-qt-6-12` change archived and Decenza building cleanly on Qt 6.12 GA.

Each numbered section is a candidate PR. Items can ship independently; only the precondition above is shared.

## 0. Pre-Qt 6.12 GA monitoring (active until 2026-09-22)

Moved from `migrate-charting-to-qt-graphs` Pre-Stage 0 §P.4 when that change archived. These items run before the precondition above is met.

- [ ] Watch `qt/qtgraphs.git` `dev` branch for landings up to feature freeze 2026-05-29. Two major items already landed: `QCanvasPainter` backend ([QTBUG-140734](https://qt-project.atlassian.net/browse/QTBUG-140734)) and declarative XYSeries data API (QTBUG-134005, QTBUG-141139).
- [ ] At each 6.12 beta (2026-06-11 / 2026-07-16 / 2026-08-18), re-check `dev` log for any new legend / auto-ranging / dashed-stroke / coord-mapping landings. None present as of 2026-05-13; do not plan around them appearing.
- [ ] If a new property or API lands that closes an item in §1–§6 below, annotate that section's checklist with the property name before 6.12 GA so the polish PRs can pick it up immediately.

## 1. Re-check tick-mark length API

- [ ] Open Qt 6.12 GA docs for `GraphsTheme` and `GraphsLine` ([qt-project.org doc.qt.io/qt-6.12/qml-qtgraphs-graphstheme.html](https://doc.qt.io/qt-6.12/qml-qtgraphs-graphstheme.html) once GA)
- [ ] Search `qt/qtgraphs.git` `dev` log for: `tickLength`, `tickVisible`, `labelsMargin`, `clipGridToPlotArea`, `tickWidth`
- [ ] If a new property exists: set it in `qml/components/graphs/DecenzaGraphsTheme.qml`, build, on-device verify against the Stage 0 baseline screenshot
- [ ] If no new property: file an upstream Qt suggestion citing the four levers tried (`subWidth: 0`, `mainWidth: 1`, `subTickCount: 0`, no custom overlay) and link this change

## 2. Re-check leftmost label alignment

- [ ] Search Qt 6.12 docs and `qt/qtgraphs.git` log for `labelsAnchor`, `labelsAlignment`, `firstLabelAnchor`
- [ ] If found: apply in `DecenzaGraphsTheme.qml` or per-axis; on-device verify the "0" label sits flush under the Y axis spine
- [ ] If not: close this item as "accept gap"

## 3. Flip `useCanvasPainter: true` on every migrated `GraphsView`

- [ ] `qml/pages/FlowCalibrationPage.qml`
- [ ] `qml/components/SteamGraph.qml` (post-Stage 2)
- [ ] `qml/components/ShotGraph.qml` (post-Stage 3a)
- [ ] `qml/components/HistoryShotGraph.qml` (post-Stage 3b)
- [ ] `qml/components/ComparisonGraph.qml` (post-Stage 3c)
- [ ] `qml/components/ProfileGraph.qml` (post-Stage 3d)
- [ ] Re-measure FPS on Decent tablet (Samsung SM-X210) at each flip; record in `docs/CLAUDE_MD/PERFORMANCE_BASELINE.md`
- [ ] Single PR titled `feat(charts): switch all GraphsView to QCanvasPainter (Qt 6.12)`

## 4. Adopt declarative `XYSeries.data` for static/computed series

- [ ] Audit each migrated graph for static / computed series (goal curves, preview lines)
- [ ] Replace `Component.onCompleted: series.replace(...)` with `data: ...` binding where the data is QML-bindable
- [ ] Keep `QXYSeries::replace()` from C++ for live ~5 Hz extraction series
- [ ] Single PR per graph or one bundled PR if changes are small

## 5. Adopt `ValueAxis.labelPostFormat`

- [ ] Verify the property exists on Qt 6.12 GA `ValueAxis` / `LogValueAxis`
- [ ] Replace `labelFormat: "%.1f"` + `titleText: "<unit>"` with `labelPostFormat: "%.1f <unit>"` where it improves readability
- [ ] One PR

## 6. Verify multi-axis margin fix

- [ ] Take pre-upgrade screenshots of `ShotGraph`, `SteamGraph`, `ComparisonGraph` on Decent tablet (these share axes across multiple series)
- [ ] After Qt 6.12 upgrade, take new screenshots
- [ ] Document any visual margin change in the PR description for the `upgrade-qt-6-12` change (this verification doesn't need a separate PR)

## Cross-stage acceptance criteria

- [ ] All PRs include before/after screenshots for any visible change
- [ ] No FPS regression on Decent tablet at each stage
- [ ] `docs/CLAUDE_MD/PERFORMANCE_BASELINE.md` updated when item 3 flips a backend
- [ ] After all items resolved or formally closed, archive this change: `openspec archive charts-qt-6-12-polish --yes`
