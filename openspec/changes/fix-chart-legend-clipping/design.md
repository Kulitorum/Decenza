## Context

Decenza has **three** graph-legend implementations, and all three share the same root-cause layout bug. The one observed clipping on the shot graphs is `qml/components/GraphLegend.qml`:

```qml
Item {
    id: legendRoot
    Layout.fillWidth: true
    implicitHeight: legendRow.height

    Flow {
        id: legendRow
        anchors.horizontalCenter: parent.horizontalCenter   // centered…
        spacing: Theme.scaled(2)                             // …but NO width set
        Repeater { /* one delegate per series entry */ }
    }
}
```

A `Flow` only wraps its children against **its own `width`**. Here the `Flow` has no `width` binding, so it collapses to its implicit (single-line, unwrapped) width and never wraps. Because it is centered on the parent via `anchors.horizontalCenter`, once that unwrapped width exceeds the parent width the row overhangs **both** edges equally — so the *first* entry ("Pressure") is pushed off the left edge and the last off the right. There is no Flickable, so scrolling can't recover it; the content is simply painted outside the parent's bounds. It reproduces at narrow desktop widths and is latent on the Decent tablet's fixed width whenever the entry set is wide (advanced mode adds `Mix temp goal`, `Resist(P/F²)`, `Conduct(F²/P)`, `dC/dt`).

The other two legends have the same defect class:
- **`qml/components/graphs/CustomLegend.qml`** (used by the steam graph and flow-calibration graph) is structurally identical — a `Flow` with `anchors.horizontalCenter` and no `width`.
- **`qml/components/ProfileGraph.qml`**'s inline legend is a `Row` (which *never* wraps) also anchored with `horizontalCenter`. It carries only three short entries (Pressure/Flow/Temp), so it clips only at extreme narrowness today — but it is the same pattern and is brought to parity so it cannot clip either.

Rather than patch the same wrapping bug into three places, this change **consolidates** all graph legends onto the single shared `CustomLegend` primitive, so the wrapping/centering logic exists in exactly one component and every graph surface inherits it. This also brings the code in line with the `charting` spec, whose Legend scenarios already say "a `CustomLegend` SHALL render one entry per series" — today the shot graphs use the separate `GraphLegend` instead.

## Goals / Non-Goals

**Goals:**
- The legend wraps to additional lines instead of clipping when entries exceed the available width; no entry — first, last, or middle — is ever cut off at any supported width.
- When entries fit on one line, they stay horizontally centered (current appearance preserved).
- **One** component owns the wrapping/centering/hit-target/tooltip layout; the shot legend, steam legend, flow-calibration legend, and profile-editor legend all render through it.
- No regression to tap-to-toggle behavior, Settings persistence, long-press tooltips, muted-state opacity, `Theme` fonts/spacing, or the live-graph frame-time targets in the `charting` spec.

**Non-Goals:**
- No redesign of legend visuals, ordering, or interaction — consolidation preserves each surface's existing look and behavior.
- No horizontal scrolling / Flickable legend (wrapping is the chosen model, not scrolling).
- No change to *which* series each graph shows, their colors, their persistence keys, or their advanced/live-mode filtering rules — only where that logic renders.
- No new settings, C++, or CMake changes (all components already registered).
- Not touching series/axis/rendering code — legend-layout only.

## Decisions

### Decision 1 — `CustomLegend` becomes the single shared wrapping primitive
`qml/components/graphs/CustomLegend.qml` is promoted to *the* legend renderer for all graphs. It already takes a generic `entries` model (`{ label, color, active }`) and emits `entryToggled(index, nowActive)`; it is extended (backward-compatibly) to cover what the richer `GraphLegend` and the `ProfileGraph` legend need:
- optional per-entry `tip` (long-press + hover `ToolTip`, matching `GraphLegend`'s current tooltips);
- an optional swatch style so goal curves can show a **line** swatch (`ProfileGraph`) vs the default **dot** swatch;
- a touch-friendly minimum hit target (adopt `GraphLegend`'s ≥44px so touch targets don't shrink).

All new inputs default to today's `CustomLegend` behavior, so its existing consumers (`SteamGraph`, `FlowCalibrationPage`) keep working unchanged.

- **Alternatives considered**: *fix the same `Flow` bug in three files* — leaves three near-identical legends to drift and re-regress; rejected now that the user opted to consolidate. *A brand-new fourth component* — pointless when `CustomLegend` already exists and the spec already names it.

### Decision 2 — Make the shared `Flow` wrap, centered-when-it-fits
The wrapping fix lives once, inside `CustomLegend`. Its internal `Flow` gets a real width so wrapping engages: bind width to `min(availableWidth, unwrappedContentWidth)` and keep `anchors.horizontalCenter`.
- Content fits → width == content width → single row stays centered (unchanged appearance).
- Content overflows → width == available width → `Flow` fills and wraps; every entry lands inside the bounds.

`unwrappedContentWidth` is computed **without** reading the `Flow`'s own (now width-bounded) `implicitWidth`, to avoid a width→implicitWidth→width loop — e.g. via `FontMetrics.advanceWidth` over each entry's label plus swatch/padding/spacing (loop-free, cheap for ≤11 entries). `availableWidth` is the width the caller gives the component.

- **Alternatives considered**: *fill width unconditionally (accept left-align)* — changes today's centered look, violates the "centered on one line" scenario; rejected. *center wrapped rows too* — `Flow` has no per-row centering; not required by the spec (wrapped rows may left-align).

### Decision 3 — Reshape `GraphLegend` into a thin adapter over `CustomLegend`
`GraphLegend` keeps everything shot-specific — its translated labels, series colors, `graph/*` persistence keys, per-entry tips, and the `advancedMode`/`liveMode` filtering — but stops rendering its own `Flow`. It builds the `entries` model (filtered for advanced/live mode) and hands it to an embedded `CustomLegend`, wiring `entryToggled` to the existing `graph[key] = v; Settings.setValue(...)` persistence. Its public API (`graph`, `advancedMode`, `liveMode`) and all four call sites (`EspressoPage`, `ShotDetailPage`, `PostShotReviewPage`, `AutoFavoriteInfoPage`) stay unchanged.

### Decision 4 — Replace `ProfileGraph`'s inline legend with `CustomLegend`
The inline three-entry `Row` becomes a `CustomLegend` with static entries (Pressure/Flow/Temp goal), `toggleEnabled: false`, and the **line** swatch style. It stays bottom-anchored (`anchors.bottom` + `horizontalCenter`) so any wrap grows upward; confirm the plot area's existing `bottomMargin` (~`Theme.scaled(32)`) leaves headroom for a wrapped line.

### Decision 5 — Fix the two call sites that constrain width
Wrapping can only engage if the component is given a bounded width:
- `SteamGraph` currently sets `width: implicitWidth` (the unwrapped width) — change it to the plot-area width (`graphsView.plotArea.width`) so the steam legend can wrap within the plot.
- `FlowCalibrationPage` already uses `Layout.fillWidth: true` — no change.
- `GraphLegend`/`ProfileGraph` embeds size the `CustomLegend` to their own available/graph width.

## Risks / Trade-offs

- **Consolidation regresses a surface's look/behavior** → `CustomLegend`'s new inputs all default to current behavior; migrate one consumer at a time and eyeball each surface (dot vs line swatch, tooltip presence, toggle vs static). Steam/FlowCalibration must render identically since their inputs are unchanged.
- **Binding loop between `width` and `implicitWidth`** → compute unwrapped content width via `FontMetrics`, not the bounded `Flow.implicitWidth`; a loop prints `QML … binding loop detected` and is caught by the "clear all warnings" bar.
- **Per-frame relayout during a live shot** → the entry set changes only on toggle / advanced-mode change, never per 5 Hz sample; width binding depends on the component width and the static entry set. Verify the live graph still meets the Performance Parity frame-time targets.
- **A call site pins legend height** → a wrapped second line would re-clip; audit the four `GraphLegend` sites, the two `CustomLegend` sites, and `ProfileGraph`'s bottom-margin headroom (Decision 4).
- **Centering perceived as changed** → preserved by Decision 2's `min()` width; single-line case is visually identical to today.

## Migration Plan

Pure UI layout/refactor; no data or persisted state (persistence keys are unchanged). Land the `CustomLegend` extensions first (backward-compatible), then migrate `GraphLegend`, then `ProfileGraph`, then the `SteamGraph` width fix. Roll back by reverting the components. Verify by narrowing the window on the Shot Detail graph until wrap occurs and confirming "Pressure" (swatch + full label) stays visible, then repeat **in advanced mode** (widest entry set, earliest-clipping case) and on the live shot graph, steam graph, flow-calibration graph, and profile editor.

## Open Questions

- Do the tooltip and line-swatch needs justify parameterizing `CustomLegend`, or is a light internal branch enough? (Resolved in implementation — keep the added API minimal and defaulted so existing consumers are untouched.)
- Does `ProfileGraph`'s current `bottomMargin` reserve enough room for a wrapped second line, or does the plot area need a small headroom bump? (Resolved via the Decision 4 audit.)
