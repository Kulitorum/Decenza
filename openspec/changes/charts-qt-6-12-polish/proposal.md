# Change: Qt 6.12 polish for charts migration

## Status: DEFERRED — depends on Decenza upgrading to Qt 6.12 (GA 2026-09-22)

This change collects every visual / API / performance item that was deferred during `migrate-charting-to-qt-graphs` (Stages 0–4) because the underlying Qt 6.11 `Graphs` module did not expose the property or backend we needed. Each item is a one-line or one-property follow-up — none of them require re-migrating any graph.

## Why

`migrate-charting-to-qt-graphs` had to ship on Qt 6.11.1 (the version Decenza upgraded to in `upgrade-qt-6-11-1`) because Qt Charts is deprecated and the GPU-rasterisation win on the Decent tablet was worth taking now. Stage 1 (`FlowCalibrationPage`) confirmed the approach works, but a handful of visual fidelity gaps vs. the Qt Charts baseline could not be closed with Qt 6.11's API surface — and complicating the bridges to work around them is not worth the maintenance cost when the same items are likely addressable cleanly on Qt 6.12.

Decenza will upgrade to Qt 6.12 in a separate `upgrade-qt-6-12` change after 6.12 GA (2026-09-22). This change runs after that upgrade lands.

**`upgrade-qt-6-12` now exists** (`../upgrade-qt-6-12/`) and is ready to run at GA. It raised one
product question — Qt 6.12 lifts the iOS minimum from 17.0 to iOS 18, dropping pre-A12 devices —
which was decided on 2026-07-29: take iOS 18, one Qt version on every platform, and explain the
inherited floor in the release notes. So the only thing gating this change is GA itself.

## Verification against Qt 6.12 (done 2026-07-29, Beta 2 tree)

Every item below was checked against the actual `qt/qtgraphs` **`6.12` branch** (feature freeze
was 2026-06-02, so this is what GA ships barring bug fixes) and the 6.12 doc snapshots. Three of
the six assumptions in the original proposal were wrong; the sections carry the corrected API
names and file:line citations. Fetch commands used (GitHub mirror of `code.qt.io`):

```
gh api "repos/qt/qtgraphs/contents/<path>?ref=6.12" -H "Accept: application/vnd.github.raw"
gh api "search/code?q=<symbol>+repo:qt/qtgraphs"
```

Summary of the correction:

| # | Original assumption | Verified reality on the 6.12 branch |
|---|---|---|
| 1 | Maybe a new `tickLength` / `labelsMargin` | **No.** Main-tick length is not parameterized at all — see §1 |
| 2 | Maybe a new `labelsAnchor` | **No.** Accept gap — see §2 |
| 3 | `useCanvasPainter: true`, one-line flip | Property exists but is **compile-gated behind an off-by-default Qt feature** — see §3 |
| 4 | New `XYSeries.data` | Property is named **`values`**, plus three siblings the proposal missed — see §4 |
| 5 | `ValueAxis.labelPostFormat` | **Confirmed**, `REVISION(6, 12)` — see §5 |
| 6 | Multi-axis margin fix | Unchanged; 6.12 also adds `GraphsView.dynamicLabelMargins` — see §6 |

**Corrected 6.12 schedule** (wiki.qt.io/Qt_6.12_Release): feature freeze 2026-06-02, Beta 1
2026-06-11, Beta 2 2026-07-14 (shipped), Beta 3 2026-08-18, RC 2026-09-08, GA 2026-09-22.

**Not relevant to Decenza in 6.12's Graphs work**: `QLogValueAxis` / logarithmic axis (with
`AxisTicker.isLogarithmic` + `base`, `REVISION(6, 12)`) and the 3D panning improvements. No
Decenza graph uses a log axis. Still **no built-in legend, no dashed stroke style, no pixel↔data
mapping API** — all four Stage 0 bridges stay ours.

## Deferred items

### 1. X / Y axis tick-mark length — CLOSED as "not addressable in 6.12"

**Symptom**: Qt Graphs draws ~6–10 px vertical strokes between the X axis spine and the tick labels (and equivalent horizontal strokes off the Y axis). Qt Charts drew them at ~2–3 px or omitted them entirely. The Decenza eye-test calls them "still too long" after every Qt 6.11 lever was tried.

**Levers attempted in Stage 0 (all kept in code, none sufficient)**:
- `axisX.subWidth: 0` / `axisY.subWidth: 0` — shortened them noticeably but not enough
- `axisX.mainWidth: 1` / `axisY.mainWidth: 1` — additional reduction
- `subTickCount: 0` on each `ValueAxis` — disabled minor ticks only

**Qt 6.11 limitation**: `GraphsTheme.axisX` (type `GraphsLine`) exposes only `mainColor`, `subColor`, `mainWidth`, `subWidth`, `labelTextColor`. No `tickLength`, `tickVisible`, or `labelsMargin` property exists. The remaining strokes may also be vertical gridlines spilling past the plot-area clip, which Qt 6.11 also gives no way to suppress.

**6.12 verdict: no API, and now we know why.** `GraphsTheme` / `GraphsLine` on the 6.12 branch carry
the same five members — nothing added. The tick geometry is in a shader with no length uniform for
main ticks:

- `src/graphs2d/data/tickershader.frag` (6.12) declares uniforms `subTickLength`, `tickLineWidth`,
  `subTickLineWidth`, `spacing`, `smoothing`, `subTickScale` — **there is no `tickLength`**.
- In `main()`, sub-ticks are length-gated (`if (flipped && fragCoord.y < iResolution.y *
  subTickLength) …`) but the major lines are drawn unconditionally: `lines += createBars(fragCoord.x,
  spacing, tickLineWidth);`. **Main ticks therefore span the full extent of the ticker item.**
- That extent is the axis area itself: `axisrenderer.cpp` does `ax.ticker->setWidth(rect.width());
  ax.ticker->setHeight(rect.height());` (≈:1160/:1225) from the X/Y axis rect. So main-tick length
  is a function of the axis label band, not of anything settable.
- `subTickLength` *is* a property — but of the private `AxisTicker` QQuickItem
  (`src/graphs2d/axis/axisticker_p.h:36`), not reachable from QML, and `axisrenderer.cpp` overwrites
  it internally (`setSubTickLength(0.5)` ≈:582, `(0.2)` ≈:615).
- Also note ticker and axis spine share one colour (`theme()->axisY().mainColor()` feeds
  `ax.ticker->setTickColor()` and the axis line), so there is no "hide the ticks, keep the spine"
  trick either.

**Action**: close the item as not addressable, and file the upstream suggestion — the concrete ask
is a `tickLength` uniform + `GraphsLine` property mirroring the existing `subTickLength`, citing
`tickershader.frag` `main()` where the major-line branch has no gate. Keep the four levers already
in `DecenzaGraphsTheme.qml`; they are the best available.

### 2. Leftmost X tick label alignment — CLOSED as "accept gap"

**Symptom**: The "0" label on the X axis sits noticeably right of where the Y axis spine is. Qt Charts kept the leftmost label flush with the axis edge; Qt Graphs centres each label on its tick and shifts the leftmost label inward to keep it inside plot bounds.

**6.12 verdict: no such property.** `ValueAxis` on 6.12 is `labelDecimals`, `labelFormat`,
`labelPostFormat` (new), `min`, `max`, `pan`, `subTickCount`, `tickAnchor`, `tickInterval`,
`visualMin`, `visualMax`, `zoom`. No `labelsAnchor` / `labelsAlignment` / `firstLabelAnchor` on the
axis or on `GraphsTheme`. Close as accept-gap — but re-look once after §6 flips
`dynamicLabelMargins`, since that reflows the axis label band and may move the leftmost label on its
own.

### 3. Switch each migrated `GraphsView` to the `QCanvasPainter` backend — BLOCKED ON A BUILD FEATURE

Per Pre-Stage 0 task §11 in `../archive/2026-05-15-migrate-charting-to-qt-graphs/tasks.md`. Qt 6.12 lands a second rendering backend ([QTBUG-140734](https://qt-project.atlassian.net/browse/QTBUG-140734)) selected via `useCanvasPainter: true`. Off by default in 6.12.

**The property is real but the one-line flip is not sufficient.** Verified on the 6.12 branch:

- `GraphsView` declares `Q_PROPERTY(bool useCanvasPainter … REVISION(6, 12))`
  (`src/graphs2d/qgraphsview_p.h:86`). It is absent from the 6.12 QML doc page, so docs alone say
  "doesn't exist" — the source is the authority here.
- **Both the getter and the setter are compiled out unless `USE_PAINTER_BACKEND` is defined.**
  `qgraphsview.cpp:1900-1925`: the setter body is `#ifdef USE_PAINTER_BACKEND … #else
  Q_UNUSED(newUseCanvasPainter);`. So on a Qt without the backend, `useCanvasPainter: true` is a
  **silent no-op** — no warning, no visual change, and no FPS change. A "we flipped it and measured
  nothing" result is indistinguishable from "the flag did nothing".
- `USE_PAINTER_BACKEND` comes from the CMake feature `graphs-2d-high-performance-backend`
  (`src/graphs2d/CMakeLists.txt:119-124`), and that feature is **`AUTODETECT OFF`** with
  `CONDITION TARGET Qt6::CanvasPainter` (`src/configure.cmake:49-54`). Off unless explicitly
  configured on. `graphs-2d-high-quality-backend` (Quick Shapes, `USE_SHAPE_BACKEND`) has no
  AUTODETECT clause and is the on-by-default path.
- Per-view default depends on which backends got compiled (`qgraphsview_p.h:360-366`):
  `USE_SHAPE_BACKEND` defined → default `false`; only `USE_PAINTER_BACKEND` → default `true`;
  neither → `false`.

**So the first task here is a build question, not a QML question**: does the Qt 6.12 binary from the
online installer ship `graphs-2d-high-performance-backend` ON? If yes, item 3 is the one-line flip as
written. If no, the options are (a) drop item 3 and stay on Quick Shapes, or (b) build `qtgraphs`
from source with `-DFEATURE_graphs_2d_high_performance_backend=ON`, which means shipping a
non-stock Qt module on Android/iOS/desktop — a much larger decision than a polish PR, and the
`upgrade-qt-6-12` change is where it belongs.

**Files to flip (if the feature turns out to be available)**:
All six migrated graphs are done (no `QtCharts` import remains anywhere in `qml/`), so the flip list
is final:

- `qml/pages/FlowCalibrationPage.qml`
- `qml/components/SteamGraph.qml`
- `qml/components/ShotGraph.qml`
- `qml/components/HistoryShotGraph.qml`
- `qml/components/ComparisonGraph.qml`
- `qml/components/ProfileGraph.qml`

One line per file. Re-measure FPS on the Decent tablet against the Stage 0 baseline (`docs/CLAUDE_MD/PERFORMANCE_BASELINE.md`).

### 4. Adopt the declarative `XYSeries.values` property where the per-point append loop is wasteful

Qt 6.12 adds the declarative point API on `XYSeries` ([QTBUG-134005](https://qt-project.atlassian.net/browse/QTBUG-134005), [QTBUG-141139](https://qt-project.atlassian.net/browse/QTBUG-141139)). **The property is `values`, not `data`** — verified `Q_PROPERTY(QVariantList values … REVISION(6, 12))` at `src/graphs2d/xychart/qxyseries.h:27`. Three siblings landed with it, all `REVISION(6, 12)`, and the original proposal missed all three:

- `valueMapping` (enum) — whether bare numbers in the list are X or Y components
- `valueMin` (qreal) and `stepSize` (qreal) — the implied other axis for a numbers-only list

`setValues()` converts once and calls `replace()` internally (`qxyseries.cpp:729-743`), so it is
**the same single-`replace()` cost as the C++ path**, not a per-point round-trip. An empty list
clears the series.

Live ~5 Hz extraction series keep the C++ `QXYSeries::replace()` pattern. The real win is the
**static/one-shot series that currently append point by point** — each `append()` is a separate
signal + geometry dirty:

- `qml/components/HistoryShotGraph.qml:143-158` — six series (`pressureSeries`, `flowSeries`,
  `weightFlowRateSeries`, `resistanceSeries`, `conductanceSeries`, `darcyResistanceSeries`) filled
  in one `for` loop of `append(x, y)` per point on shot load. Best candidate: one `values`
  assignment per series.
- `qml/pages/FlowCalibrationPage.qml:264-272` — `flowSeries.append()` / `weightFlowSeries.append()`
  in a loop.
- `qml/components/ProfileGraph.qml:342-490` — `updateCurves()` appends to `pressureSeries0` /
  `flowSeries0` across every frame-shape branch; a pure preview, no live data. Largest rewrite of
  the three (build the arrays, assign once); do it last.
- **`valueMin` + `stepSize` candidate**: any of the above whose X samples are evenly spaced can pass
  Y numbers only and let the axis imply X. Check the actual sample spacing before assuming it.

**Out of scope**: `qml/components/graphs/DashedLineSeries.qml` is a `Shape`/`ShapePath` overlay, not
an `XYSeries` — its `points` property is ours and `values` does not apply. The original proposal
listed it in error.

Clarity plus fewer signal emissions on load. Optional but cheap.

### 5. `ValueAxis.labelPostFormat` — CONFIRMED, ready to adopt

Verified on 6.12: `labelPostFormat` (string, since 6.12) exists on `ValueAxis`. Replaces the
`labelFormat` + separate unit-in-the-title plumbing. Current sites, all `labelFormat` + `titleText`
pairs where the unit could move into the labels:

| File | Lines |
|---|---|
| `qml/pages/FlowCalibrationPage.qml` | 50-51 (`"%.0f"` + `titleText: "s"`), 62-63 (`"%.1f"` + `"mL/s · g/s"`) |
| `qml/components/ShotGraph.qml` | 129/136, 147-148 (`"bar / mL·g/s"`) |
| `qml/components/HistoryShotGraph.qml` | 429/437, 446-448 (`titleText` is conditional on `showLabels`) |
| `qml/components/SteamGraph.qml` | 117/124, 135-136 |
| `qml/components/ComparisonGraph.qml` | 359-360, 373-374 |
| `qml/components/ProfileGraph.qml` | 144, 154 (no `titleText`) |

**Watch the i18n rule**: three of these titles go through `TranslationManager.translate(…)`
(`ShotGraph.qml:136`, `HistoryShotGraph.qml:437`, `SteamGraph.qml:124`). A unit suffix moved into
`labelPostFormat` must stay a translated binding, not a hardcoded string. The multi-unit shared axes
(`"bar / mL/s"`, `"bar / mL·g/s"`) are **not** candidates — one axis, several units, so the suffix
belongs in the title. Realistically only `FlowCalibrationPage`'s X axis (`"s"`) and any
single-unit axis benefit; keep the PR small rather than forcing every site.

### 6. Multi-axis margin fix (+ new `dynamicLabelMargins`)

Qt 6.12 fixes a `GraphsView` margin miscalculation when multiple series share the X and/or Y axis ([qt/qtgraphs commit on dev branch, see migrate-charting proposal §"Qt 6.12 Roadmap"](../archive/2026-05-15-migrate-charting-to-qt-graphs/proposal.md)). Decenza always shares axes across pressure/flow/temperature/weight series in the espresso graphs. Verify visual change after the upgrade — pre-migration screenshots may shift slightly.

**New in 6.12 and worth trying here**: `GraphsView.dynamicLabelMargins`
(`Q_PROPERTY(bool … REVISION(6, 12))`, `qgraphsview_p.h:87`; absent from 6.11's property list, so it
is genuinely new). Docs: "By default, labels wider than the allocated margin overlap other graph
elements. When enabled, the renderer reserves extra space for such labels and repositions axes to
prevent overlap." Default `false`.

Decenza's Y labels are widest in `HistoryShotGraph` / `ComparisonGraph` (dual-unit shared axes), and
those are exactly the graphs where the `marginLeft`/`marginRight` values were hand-tuned during the
migration. So flipping this on may (a) fix residual label overlap and (b) make some hand-tuned
margins redundant — or fight them. Test it in the same PR as the margin-fix verification, and
re-check item 2's leftmost-label complaint afterwards. `clipPlotArea` is **not** new (since 6.10,
default `true`), so item 1's "gridlines spilling past the clip" theory is testable on 6.11 today and
needs no upgrade.

## What Changes

Pure follow-up: one PR per item (or a small batch of related items), no architectural change. The `qml/components/graphs/` bridges stay. The `DecenzaGraphsTheme.qml` may gain or lose a property line per item. No new modules.

## Impact

- **Affected specs**: `charting` — the `Performance Parity` and `Rendering Backend` scenarios may tighten once `useCanvasPainter: true` ships
- **Affected code**: incremental edits to the six migrated graph files and `DecenzaGraphsTheme.qml`
- **Performance target**: re-measure on Decent tablet at each item-3 flip, document deltas in `docs/CLAUDE_MD/PERFORMANCE_BASELINE.md`

## Success Criteria

- All deferred items either landed (items 4, 5, 6 — item 3 depends on the Qt build feature) or formally closed as "not addressable in 6.12, accept gap or escalate to upstream". Items 1 and 2 are already closed by the 2026-07-29 verification above; only the upstream suggestion for item 1 remains.
- No new bridge components added (bridges from Stage 0 are still the contract)
- No FPS regression on Decent tablet

## Non-Goals

- No re-migration of any graph. Items 1–2 do not warrant a custom-overlay reimplementation just to match Charts pixel-for-pixel.
- No new graphing features. That belongs in a separate change after this archives.
