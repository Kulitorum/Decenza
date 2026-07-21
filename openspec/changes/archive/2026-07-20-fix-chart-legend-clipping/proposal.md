## Why

The shot-graph legend (`Pressure, Flow, Temp, Mix temp, Mix temp goal, Weight, Wt flow, Resist(P/F), Resist(P/F²), Conduct(F²/P), dC/dt`) is laid out as a single non-wrapping row centered on the graph. At wide window widths every entry fits; at narrower widths the row overflows and — because it is centered — the overhang spills off **both** edges, clipping the **first entry ("Pressure") off the left edge** (color swatch and part of the label gone), with no way to scroll it back into view. The advanced-mode entry set is the widest and clips soonest.

This is not one component: Decenza has **three** legend implementations that share the same non-wrapping, center-anchored layout, so the defect appears everywhere a graph is shown — the live shot graph, Shot Detail, post-shot review, the profile-editor graph, the steam graph, and flow calibration. A legend that silently hides a series name is a real legibility regression on Decent tablets and narrow desktop windows alike.

## What Changes

- Every graph legend SHALL reflow (wrap onto additional lines) when its entries do not fit the available width, instead of overflowing and clipping. No entry — in particular the first — may be partially or fully cut off at any supported window width.
- Rather than patch the same bug in three places, the three legends are **consolidated onto the single shared `CustomLegend` component**, so the wrapping/centering logic lives in exactly one place and every graph surface inherits it:
  - `CustomLegend.qml` is promoted to the one legend renderer and extended (backward-compatibly) with the few features the richer legends need: optional per-entry long-press/hover tooltip, an optional **line** swatch style (for goal curves), and a touch-friendly minimum hit target.
  - `GraphLegend.qml` (the **shot**-graph legend, the one observed clipping) is reshaped into a thin adapter: it keeps its shot-specific model — translated labels, series colors, `graph/*` persistence keys, tips, and advanced/live-mode filtering — but renders through `CustomLegend` instead of its own `Flow`.
  - `ProfileGraph.qml`'s inline three-entry `Row` legend is replaced by a static (non-toggling) `CustomLegend`.
  - `SteamGraph.qml` and `FlowCalibrationPage.qml` already use `CustomLegend` and keep working unchanged (two call sites get a small width binding so the legend can wrap).
- Legend entries remain centered when they fit on one line, continue to use `Theme.qml` fonts and spacing, and retain all existing behavior on every surface (per-series color + name, tap-to-toggle visibility + Settings persistence, muted-state opacity, long-press tooltips, goal-curve line swatches).

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `charting`: the existing **Legend** requirement gains a responsive-layout guarantee — the shared legend must wrap rather than clip when entries exceed the available width, at every supported window size — and all graph surfaces render through the single `CustomLegend` component (aligning with the spec's existing Legend scenarios, which already name `CustomLegend`).

## Impact

- **Components edited**:
  - `qml/components/graphs/CustomLegend.qml` — promoted to the shared wrapping legend; gains optional tooltip, line-swatch style, touch target (all defaulted to current behavior).
  - `qml/components/GraphLegend.qml` — reshaped to build its model and render via `CustomLegend`; public API (`graph`, `advancedMode`, `liveMode`) unchanged.
  - `qml/components/ProfileGraph.qml` — inline legend `Row` replaced by a static `CustomLegend`.
  - `qml/components/SteamGraph.qml` — legend width binding changed from `implicitWidth` to the plot-area width so it can wrap (behavior/appearance otherwise unchanged).
- **Surfaces affected (behavior preserved)**: live shot graph (EspressoPage), Shot Detail, Post-Shot Review, AutoFavorite info, steam graph, flow calibration, profile-editor graph.
- **Risk**: layout/refactor; must preserve tap-to-toggle + persistence, long-press tooltips, muted-state styling, dot-vs-line swatches, centered single-line alignment, and the ~60 fps live-graph targets in the `charting` spec (no binding loops, no per-frame relayout during live traces). Consolidation adds surface-parity risk, mitigated by defaulting all new `CustomLegend` inputs to today's behavior and migrating one consumer at a time.
- **No** new settings, no C++ changes, no CMakeLists changes (all components already registered).
