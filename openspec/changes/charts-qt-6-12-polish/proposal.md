# Change: Qt 6.12 polish for charts migration

## Status: DEFERRED — depends on Decenza upgrading to Qt 6.12 (GA 2026-09-22)

This change collects every visual / API / performance item that was deferred during `migrate-charting-to-qt-graphs` (Stages 0–4) because the underlying Qt 6.11 `Graphs` module did not expose the property or backend we needed. Each item is a one-line or one-property follow-up — none of them require re-migrating any graph.

## Why

`migrate-charting-to-qt-graphs` had to ship on Qt 6.11.1 (the version Decenza upgraded to in `upgrade-qt-6-11-1`) because Qt Charts is deprecated and the GPU-rasterisation win on the Decent tablet was worth taking now. Stage 1 (`FlowCalibrationPage`) confirmed the approach works, but a handful of visual fidelity gaps vs. the Qt Charts baseline could not be closed with Qt 6.11's API surface — and complicating the bridges to work around them is not worth the maintenance cost when the same items are likely addressable cleanly on Qt 6.12.

Decenza will upgrade to Qt 6.12 in a separate `upgrade-qt-6-12` change after 6.12 GA (2026-09-22). This change runs after that upgrade lands.

## Deferred items

### 1. X / Y axis tick-mark length

**Symptom**: Qt Graphs draws ~6–10 px vertical strokes between the X axis spine and the tick labels (and equivalent horizontal strokes off the Y axis). Qt Charts drew them at ~2–3 px or omitted them entirely. The Decenza eye-test calls them "still too long" after every Qt 6.11 lever was tried.

**Levers attempted in Stage 0 (all kept in code, none sufficient)**:
- `axisX.subWidth: 0` / `axisY.subWidth: 0` — shortened them noticeably but not enough
- `axisX.mainWidth: 1` / `axisY.mainWidth: 1` — additional reduction
- `subTickCount: 0` on each `ValueAxis` — disabled minor ticks only

**Qt 6.11 limitation**: `GraphsTheme.axisX` (type `GraphsLine`) exposes only `mainColor`, `subColor`, `mainWidth`, `subWidth`, `labelTextColor`. No `tickLength`, `tickVisible`, or `labelsMargin` property exists. The remaining strokes may also be vertical gridlines spilling past the plot-area clip, which Qt 6.11 also gives no way to suppress.

**6.12 check**: re-read the `GraphsTheme` and `GraphsLine` doc pages. Search the `qt/qtgraphs.git` `dev`-branch log for `tickLength`, `labelsMargin`, `tickVisible`, and `clipToPlotArea`. If a new property exists, set it in `DecenzaGraphsTheme.qml`; if not, file an upstream Qt suggestion citing this list of attempts and note the workaround (custom overlay) here.

### 2. Leftmost X tick label alignment

**Symptom**: The "0" label on the X axis sits noticeably right of where the Y axis spine is. Qt Charts kept the leftmost label flush with the axis edge; Qt Graphs centres each label on its tick and shifts the leftmost label inward to keep it inside plot bounds.

**6.12 check**: look for any new `labelsAnchor`, `labelsAlignment`, or `firstLabelAnchor` property on `ValueAxis` or `GraphsTheme`. If absent, leave the alignment as-is — it's cosmetic and a custom overlay is more code than the polish is worth.

### 3. Switch each migrated `GraphsView` to the `QCanvasPainter` backend

Per Pre-Stage 0 task §11 in `../archive/2026-05-15-migrate-charting-to-qt-graphs/tasks.md`. Qt 6.12 lands a second rendering backend ([QTBUG-140734](https://qt-project.atlassian.net/browse/QTBUG-140734)) selected via `useCanvasPainter: true`. Off by default in 6.12.

**Files to flip**:
- `qml/pages/FlowCalibrationPage.qml`
- `qml/components/SteamGraph.qml` (after Stage 2 migration)
- `qml/components/ShotGraph.qml` (after Stage 3a)
- `qml/components/HistoryShotGraph.qml` (after Stage 3b)
- `qml/components/ComparisonGraph.qml` (after Stage 3c)
- `qml/components/ProfileGraph.qml` (after Stage 3d)

One line per file. Re-measure FPS on the Decent tablet against the Stage 0 baseline (`docs/CLAUDE_MD/PERFORMANCE_BASELINE.md`).

### 4. Adopt the declarative `XYSeries.data` property where the C++ round-trip is unnecessary

Qt 6.12 adds a declarative `data` property on `XYSeries` and subclasses ([QTBUG-134005](https://qt-project.atlassian.net/browse/QTBUG-134005), [QTBUG-141139](https://qt-project.atlassian.net/browse/QTBUG-141139)). Live ~5 Hz extraction series keep using the C++ `QXYSeries::replace()` pattern (still faster). But static / computed series — goal curves, profile-editor previews — can be populated declaratively:

- `qml/components/ProfileGraph.qml` — pure preview, no live data
- `qml/components/SteamGraph.qml` — goal-temperature curve only
- Any `DashedLineSeries` bridge instance whose `points` is data-bound

This is a code-clarity win, not a perf change. Optional.

### 5. `ValueAxis.labelPostFormat`

Qt 6.12 adds a `labelPostFormat` property (e.g. `"%.1f bar"`) on `ValueAxis` / `LogValueAxis`. Replaces the bespoke `labelFormat` + unit-suffix plumbing scattered across the migrated graphs. Verify the property exists on 6.12 GA, then adopt in:

- `qml/pages/FlowCalibrationPage.qml` (currently `titleText: "mL/s · g/s"` on the Y axis — could move the unit into the labels)
- `qml/components/SteamGraph.qml`, `ShotGraph.qml`, etc.

### 6. Multi-axis margin fix

Qt 6.12 fixes a `GraphsView` margin miscalculation when multiple series share the X and/or Y axis ([qt/qtgraphs commit on dev branch, see migrate-charting proposal §"Qt 6.12 Roadmap"](../archive/2026-05-15-migrate-charting-to-qt-graphs/proposal.md)). Decenza always shares axes across pressure/flow/temperature/weight series in the espresso graphs. Verify visual change after the upgrade — pre-migration screenshots may shift slightly.

## What Changes

Pure follow-up: one PR per item (or a small batch of related items), no architectural change. The `qml/components/graphs/` bridges stay. The `DecenzaGraphsTheme.qml` may gain or lose a property line per item. No new modules.

## Impact

- **Affected specs**: `charting` — the `Performance Parity` and `Rendering Backend` scenarios may tighten once `useCanvasPainter: true` ships
- **Affected code**: incremental edits to the six migrated graph files and `DecenzaGraphsTheme.qml`
- **Performance target**: re-measure on Decent tablet at each item-3 flip, document deltas in `docs/CLAUDE_MD/PERFORMANCE_BASELINE.md`

## Success Criteria

- All deferred items either landed (item 3, 5, 6 likely) or formally closed as "not addressable in 6.12, accept gap or escalate to upstream"
- No new bridge components added (bridges from Stage 0 are still the contract)
- No FPS regression on Decent tablet

## Non-Goals

- No re-migration of any graph. Items 1–2 do not warrant a custom-overlay reimplementation just to match Charts pixel-for-pixel.
- No new graphing features. That belongs in a separate change after this archives.
