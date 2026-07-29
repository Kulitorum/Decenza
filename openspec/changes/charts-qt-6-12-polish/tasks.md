# Tasks: Qt 6.12 polish for charts migration

**Precondition**: `upgrade-qt-6-12` change archived and Decenza building cleanly on Qt 6.12 GA
(2026-09-22). That change does not exist yet.

Each numbered section is a candidate PR. Items can ship independently; only the precondition above is
shared. §1 and §2 are already resolved by the 2026-07-29 source verification — see the proposal's
"Verification against Qt 6.12" table for what was checked and how.

## 0. Pre-Qt 6.12 GA monitoring

Moved from `migrate-charting-to-qt-graphs` Pre-Stage 0 §P.4 when that change archived.

- [x] Watch `qt/qtgraphs.git` for landings up to feature freeze (actual freeze **2026-06-02**, not
      05-29). Done 2026-07-29 against the `6.12` branch: `useCanvasPainter` + `dynamicLabelMargins`
      on `GraphsView`, `values` / `valueMapping` / `valueMin` / `stepSize` on `XYSeries`,
      `labelPostFormat` on `ValueAxis`, logarithmic axis support. Nothing for ticks or label anchors.
- [x] Beta re-check for the legend / auto-ranging / dashed-stroke / coord-mapping gaps. Still absent
      on the `6.12` branch — all four Stage 0 bridges stay ours. Do not re-check per beta; the branch
      is feature-frozen, only bug fixes land now.
- [ ] At RC (**2026-09-08**) confirm nothing in §3–§6 regressed, then close this section. Remaining
      beta dates for reference: Beta 3 **2026-08-18**.

## 1. Tick-mark length — CLOSED, upstream suggestion only

No property exists and the geometry is not parameterized: `tickershader.frag` draws major ticks
unconditionally over the full ticker item, which `axisrenderer.cpp` sizes to the axis rect. Details
and line citations in the proposal §1.

- [x] Verify 6.12 `GraphsTheme` / `GraphsLine` property set — unchanged (five members)
- [x] Search the tree for `tickLength`, `tickVisible`, `labelsMargin`, `clipGridToPlotArea`,
      `tickWidth` — only private/3D/bar-series hits
- [ ] File the upstream Qt suggestion: expose a `tickLength` uniform + `GraphsLine` property
      mirroring the existing private `AxisTicker.subTickLength`. Cite `tickershader.frag` `main()`
      (major-line branch has no length gate), the four levers already tried (`subWidth: 0`,
      `mainWidth: 1`, `subTickCount: 0`, no custom overlay), and this change. Gerrit/JIRA per
      `reference_qt_gerrit` conventions — a JIRA suggestion, not a patch, unless we choose to write it
- [ ] Independent of 6.12: test whether the residual strokes are gridlines by toggling
      `clipPlotArea` (since 6.10, so testable on 6.11 today). If they vanish, item 1 was never a tick
      problem and the upstream ask changes

## 2. Leftmost label alignment — CLOSED, accept gap

- [x] Confirm no `labelsAnchor` / `labelsAlignment` / `firstLabelAnchor` on 6.12 `ValueAxis` or
      `GraphsTheme`
- [ ] After §6 enables `dynamicLabelMargins`, look once more at the "0" label position; if the reflow
      fixed it, note that in the §6 PR and delete this section

## 3. `useCanvasPainter` — resolve the build feature FIRST

The property is `REVISION(6, 12)` on `GraphsView`, but both accessors are `#ifdef
USE_PAINTER_BACKEND` and the setter is a silent no-op otherwise. The define comes from CMake feature
`graphs-2d-high-performance-backend`, which is `AUTODETECT OFF`. Proposal §3 has the citations.

- [ ] **Gate**: on the installed Qt 6.12, check whether the feature is on. Read
      `<QtDir>/lib/cmake/Qt6Graphs/*Config*.cmake` (or `qtgraphs` `qconfig`-style feature header) for
      `graphs_2d_high_performance_backend`; alternatively confirm `Qt6::CanvasPainter` is a link
      dependency of `Qt6::Graphs`
- [ ] If OFF: **do not write the flip PR.** Record the finding here, and raise the "build qtgraphs
      from source with `-DFEATURE_graphs_2d_high_performance_backend=ON`" question in
      `upgrade-qt-6-12` — it is a shipping-a-non-stock-Qt-module decision across Android/iOS/desktop,
      not a polish PR
- [ ] If ON: verify the property is actually live before measuring — set `useCanvasPainter: true` on
      one graph and confirm the `useCanvasPainterChanged` signal fires (a no-op build emits nothing).
      Only then trust any FPS number
- [ ] Flip, one line per file: `qml/pages/FlowCalibrationPage.qml`,
      `qml/components/SteamGraph.qml`, `ShotGraph.qml`, `HistoryShotGraph.qml`,
      `ComparisonGraph.qml`, `ProfileGraph.qml` (all six are migrated; no stage gating left)
- [ ] Re-measure FPS on Decent tablet (Samsung SM-X210) at each flip; record in
      `docs/CLAUDE_MD/PERFORMANCE_BASELINE.md`
- [ ] Single PR titled `feat(charts): switch all GraphsView to QCanvasPainter (Qt 6.12)`

## 4. Adopt declarative `XYSeries.values` for one-shot series

Property is **`values`** (`QVariantList`, `REVISION(6, 12)`), not `data`. `setValues()` converts once
and calls `replace()`, so it costs the same as the C++ path.

- [ ] `qml/components/HistoryShotGraph.qml:143-158` — replace the per-point `append()` loop over the
      six series with one `values` assignment each. Highest-value site
- [ ] `qml/pages/FlowCalibrationPage.qml:264-272` — same pattern, two series
- [ ] `qml/components/ProfileGraph.qml:342-490` (`updateCurves()`) — build arrays, assign once. Do
      last; largest rewrite
- [ ] Check whether any of the above is evenly sampled in X; if so evaluate `valueMapping` +
      `valueMin` + `stepSize` (numbers-only list) instead of point pairs
- [ ] Keep `QXYSeries::replace()` from C++ for live ~5 Hz extraction series — do not touch
- [ ] Do **not** touch `qml/components/graphs/DashedLineSeries.qml`; it is a `ShapePath` overlay, not
      an `XYSeries`
- [ ] One bundled PR if the diffs stay small; split `ProfileGraph` out if it grows

## 5. Adopt `ValueAxis.labelPostFormat`

- [x] Property confirmed present on 6.12 `ValueAxis`
- [ ] Adopt only where one axis carries one unit — `FlowCalibrationPage.qml:50-51` (`"s"`) first
- [ ] Leave the shared multi-unit axes (`"bar / mL/s"`, `"bar / mL·g/s"`: `ShotGraph.qml:147-148`,
      `HistoryShotGraph.qml:446-448`, `SteamGraph.qml:135-136`, `ComparisonGraph.qml:373-374`) with
      the unit in `titleText`
- [ ] Any suffix that replaces a translated `titleText` (`ShotGraph.qml:136`,
      `HistoryShotGraph.qml:437`, `SteamGraph.qml:124`) must stay a `TranslationManager.translate`
      binding — never a hardcoded string
- [ ] One PR

## 6. Verify multi-axis margin fix + try `dynamicLabelMargins`

- [ ] Take pre-upgrade screenshots of `ShotGraph`, `SteamGraph`, `ComparisonGraph`,
      `HistoryShotGraph` on Decent tablet (these share axes across multiple series)
- [ ] After the Qt 6.12 upgrade, take new screenshots; document any margin change in the
      `upgrade-qt-6-12` PR description
- [ ] Try `dynamicLabelMargins: true` (new in 6.12, default `false`) on the dual-unit graphs; check
      it does not fight the hand-tuned `marginLeft` / `marginRight` values from the migration. Remove
      any margin override it makes redundant
- [ ] Re-check §2's leftmost-label complaint with the new margins in place

## Cross-stage acceptance criteria

- [ ] All PRs include before/after screenshots for any visible change
- [ ] No FPS regression on Decent tablet at each stage
- [ ] `docs/CLAUDE_MD/PERFORMANCE_BASELINE.md` updated when item 3 flips a backend
- [ ] After all items resolved or formally closed, archive this change: `openspec archive charts-qt-6-12-polish --yes`
