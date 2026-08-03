# Add a flow scale menu to the graph Advanced button

## Why

Flow is drawn against the same 0–12 axis as pressure, so the part of the curve a user
actually dials on sits crushed against the floor of the chart.

Measured over this machine's own history (1,068 espresso shots with flow curves, `shots.db`):

| | mL/s |
|---|---|
| pour sample, median | 1.81 |
| pour sample, p90 | 2.20 |
| pour sample, p99 | 3.98 |
| per-shot pour peak, p90 | 3.42 |
| per-shot pour peak, p99 | 5.47 |

The median pour therefore occupies 15% of chart height. The numbers that *do* reach the top
of the axis are the puck-fill spike — peak flow p50 8.01 mL/s at t≈2.5 s, over within ~4 s,
and not something anyone dials on. The axis is scaled for the one part of the curve that
carries no information.

Every other DE1 app has already solved this and none of them solved it the same way: de1app's
Insight skin gives flow a separate chart with its own 0–8 axis, DSx2 pre-multiplies flow by 2
and labels a second axis at half scale, Decaid does neither and leaves flow on a fixed 0–11.
DSx2's approach is the one that fits a single-chart layout, which is ours.

The graph's Advanced button is presently a bare on/off toggle. Turning it into a menu — the
shape `ExtractionViewSelector` already established on the live espresso screen — gives the
flow scale a home without adding another button to the chrome.

## What Changes

- Add a **flow scale** of 1x, 2x, or 3x. The selected multiplier is applied to the flow trace
  before it is plotted against the existing left axis, so 2x makes the axis read 0–6 mL/s for
  flow and 3x makes it read 0–4.
- Apply the same multiplier to the **weight flow rate (g/s)** trace and to the **dashed flow
  goal**, so all three stay mutually readable. This follows de1app, which multiplies
  `espresso_flow_2x`, `espresso_flow_weight_2x` and `espresso_flow_goal_2x` by the same 2.0
  (`gui.tcl:3426`, `:3451`, `:3551`).
- Default to **2x**. It raises the median pour to 45% of chart height while its 6.0 mL/s
  ceiling still clears the p99 pour peak of 5.47, so no pour detail is clipped at the default.
  3x is available for those who want the resolution and accept clipping above 4.0.
- The multiplier **persists** and changes only when the user changes it.
- Add **flow** as a third right-axis mode, joining weight and temperature. In flow mode the
  right label column reads the true flow scale (axis max ÷ multiplier). The right-axis mode
  and the multiplier stay **independent** — selecting 2x/3x does not move the right axis.
- **Replace the Advanced toggle button with a menu** on the shot detail and post-shot review
  graphs. The menu carries the Advanced Curves toggle (moved out of the button) and the flow
  scale selector. The live espresso screen gets the flow scale added to its existing
  `ExtractionViewSelector`.
- The left axis title drops from `bar / mL·g/s` to `bar` whenever a multiplier other than 1x
  is active, because at 2x/3x it no longer reads true for flow.
- **BREAKING (settings only, migrated):** the two-state `graph/showWeightAxis` boolean becomes
  a three-state `graph/rightAxisMode` string. Existing values migrate; no user-visible reset.

**Included refactor — `SettingsGraph` and the deduplication it enables.**

The 13 `graph/*` keys had no domain sub-object; they were read through the generic
`Settings.boolValue()`, which is a plain `Q_INVOKABLE`. A property initialised from it records
no binding dependency, so every graph was reading a one-shot snapshot taken at construction,
and three separate mechanisms had grown to compensate: `GraphLegend` wrote the setting *and*
poked the graph's property directly, `HistoryShotGraph` hand-maintained a 12-case string
switch over `valueChanged`, and `LastShotChartSource` hand-maintained a parallel key list that
had already shipped one entry short.

This change would have added two more keys to that scheme. Instead the keys move into a
`SettingsGraph` domain with real `Q_PROPERTY`/`NOTIFY`, and the compensations delete. What
that removed, all of it duplication that predates this feature:

- 59 mirror property declarations across 5 QML files, replaced by direct bindings
- the 12-case refresh switch and the hand-maintained watch list
- 4 copies of the series list (legend model, comparison map, watch list, mirror blocks) →
  one `GraphSeries` singleton
- 2 verbatim copies of the right-axis label column → `GraphRightAxisLabels`
- 4 copies of the Advanced button → `GraphOptionsButton`
- 3 copies of the checkbox card in `ExtractionViewSelector` alone → `GraphOptionToggleCard`
  (386 → 214 lines in that file)
- identical `_niceTimeAxisStep` / `_timeAtPixel` helpers in two graphs each → `GraphUtils.js`
- `shotReview/advancedMode` mirrored in 4 pages with 4 `Connections` blocks → one property

## Capabilities

### New Capabilities

- `graph-flow-scale`: the flow multiplier — its allowed values, which series it applies to,
  its persistence, its effect on axis titling, and the third right-axis mode that makes a
  multiplied trace readable.
- `graph-options-menu`: the menu that replaces the graph Advanced toggle button — what it
  contains, which surfaces present it, and how it relates to the live screen's existing
  extraction-view selector.
- `graph-settings-domain`: the `SettingsGraph` domain sub-object the graphs bind to, and the
  single-source-of-truth rule for the series list. Folded into this change rather than
  deferred, because it removes the defect class the flow scale would otherwise extend.

### Modified Capabilities

- `charting`: auto-ranging must be computed from **unscaled** flow. Under the present
  requirement the axis max tracks the maximum y-value across attached series, which would
  grow the axis in proportion to the multiplier and cancel the zoom exactly.

## Impact

**QML**
- `qml/components/ShotGraph.qml` — live graph: multiplier on flow / weight-flow-rate / flow
  goal series, left axis title, third right-axis mode.
- `qml/components/HistoryShotGraph.qml` — same, plus `pressureAxisMax` must exclude the
  multiplier, plus the right-axis label column at `:724-750` gains a third state.
- `qml/components/ComparisonGraph.qml` — same treatment so the three graphs agree.
- `qml/components/ExtractionViewSelector.qml` — gains the flow scale selector.
- New menu component for the shot-detail / post-shot-review Advanced button.
- `qml/pages/ShotDetailPage.qml:517-547`, `qml/pages/PostShotReviewPage.qml:1279+` — the
  Advanced toggle button becomes a menu opener.
- `qml/components/GraphInspectBar.qml`, `ComparisonInspectBar.qml`, and any crosshair or
  tooltip readout — values must be **un-scaled** before display. Decaid ships this bug today:
  its weight trace is plotted at ÷10 and the tooltip prints the plotted number, so a 36 g shot
  reads "Weight: 3.6 g" (`lib/src/util/shot_chart.dart:105`, `:359`).
- `qml/components/LastShotChartSource.qml:102` — the widget's settings watch-list currently
  names `graph/showWeightAxis`; the new keys must be added or the home-screen widget renders
  from stale values. The file's own comment records that this list has already failed once
  this way.

**C++**
- `SettingsGraph` (or the matching domain sub-object) for `graph/flowScale` and
  `graph/rightAxisMode`, plus the one-time migration off `graph/showWeightAxis`.

**Docs**
- Wiki manual page for the shot graph — new menu, the multiplier, the third axis mode.

**Not affected**
- Stored shot data. The multiplier is a display transform applied at plot time; nothing is
  written to `shot_samples` and no existing shot is rewritten.
- Visualizer export, which carries true mL/s and g/s regardless of display scale.
