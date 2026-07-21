## 1. Promote `CustomLegend` to the shared wrapping legend

- [x] 1.1 In `qml/components/graphs/CustomLegend.qml`, make the internal `Flow` wrap: bind its `width` to `min(availableWidth, unwrappedContentWidth)` and keep `anchors.horizontalCenter`, so a single line stays centered and an over-wide set wraps.
- [x] 1.2 Compute `unwrappedContentWidth` loop-free via `FontMetrics.advanceWidth` over each entry's label plus swatch + padding + spacing (do NOT read the now-bounded `Flow.implicitWidth`). Confirm `implicitWidth` still reports the unwrapped width and `implicitHeight` grows with wrapping.
- [x] 1.3 Extend the `entries` model (backward-compatibly) with an optional per-entry `tip` (string) and render a long-press + hover `ToolTip` when present, mirroring `GraphLegend`'s current tooltip (reuse its delay/width/long-press-hide behavior).
- [x] 1.4 Add an optional swatch style so an entry can show a **line** swatch (thin horizontal bar) instead of the default dot — needed for goal curves in `ProfileGraph`. Default = dot (current look; dot is now a round swatch to match the shot legend).
- [x] 1.5 Adopt a touch-friendly minimum hit target (≥ `Theme.scaled(44)` height, matching `GraphLegend`), keeping the current visual padding.
- [x] 1.6 Verify `SteamGraph.qml` and `FlowCalibrationPage.qml` (existing consumers) render identically with the extended component — all new inputs default to prior behavior. (Verified at task 4 build/run.)

## 2. Reshape `GraphLegend` into an adapter over `CustomLegend`

- [x] 2.1 In `qml/components/GraphLegend.qml`, keep the shot-specific entry model (translated labels, series colors, `graph/*` keys, tips, `advanced`/`postShotOnly` flags) but derive a filtered `entries` array (respecting `advancedMode`/`liveMode`) shaped as `CustomLegend` entries incl. `tip`.
- [x] 2.2 Replace the inline `Flow`/`Repeater` with an embedded `CustomLegend`; wire `onEntryToggled` to the existing persistence (`graph[key] = nowActive; Settings.setValue("graph/"+key, nowActive)`), mapping index→key through the filtered model.
- [x] 2.3 Keep the public API (`graph`, `advancedMode`, `liveMode`) and `Layout.fillWidth` unchanged so the four call sites (`EspressoPage`, `ShotDetailPage`, `PostShotReviewPage`, `AutoFavoriteInfoPage`) need no edits; confirm `implicitHeight` tracks wrapped height.
- [x] 2.4 Preserve `Accessible` roles/names/checked state and hit targets on each entry.

## 3. Replace `ProfileGraph`'s inline legend with `CustomLegend`

- [x] 3.1 In `qml/components/ProfileGraph.qml`, replace the inline three-entry `Row` (~line 512) with a `CustomLegend` carrying static entries (Pressure/Flow/Temp goal, goal colors), `toggleEnabled: false`, and the **line** swatch style.
- [x] 3.2 Keep it bottom-anchored (`anchors.bottom` + `horizontalCenter`) so a wrap grows upward; confirm the plot area's `bottomMargin` (~`Theme.scaled(32)`) reserves room for a wrapped second line, bumping it only if needed.

## 4. Fix width-constraining call sites

- [x] 4.1 In `qml/components/SteamGraph.qml`, change the legend `width: implicitWidth` to the plot-area width (`graphsView.plotArea.width`) so the steam legend can wrap within the plot; keep its `x`/`y` positioning.
- [x] 4.2 Confirm `FlowCalibrationPage.qml` (already `Layout.fillWidth: true`) and the `GraphLegend`/`ProfileGraph` embeds all give `CustomLegend` a bounded width.

## 5. Verify (no clipping, no regressions)

- [ ] 5.1 Build in Qt Creator; confirm zero new QML warnings and **no binding-loop warnings** in the running app log (the "clear all warnings" bar).
- [ ] 5.2 Narrow the window on the Shot Detail graph until the legend wraps; confirm the first entry ("Pressure") — swatch and full label — is never clipped at either edge, at multiple widths.
- [ ] 5.3 Repeat 5.2 in **advanced mode** (widest set: adds Mix temp / Mix temp goal / Resist(P/F) / Resist(P/F²) / Conduct(F²/P) / dC/dt) — the earliest-clipping case.
- [ ] 5.4 Repeat on the live shot graph (EspressoPage), post-shot review, steam graph, flow-calibration graph, and profile-editor graph.
- [ ] 5.5 Confirm single-line legends (wide window) still render centered, matching prior appearance, and per surface: toggles + persistence work (shot/steam/flow-cal), tooltips still appear (shot), the profile legend shows line swatches and does not toggle.
- [ ] 5.6 Sanity-check live-graph performance is unaffected (no per-sample relayout; frame-time targets from the `charting` Performance Parity requirement still met).

## 6. Docs

- [x] 6.1 Update the header doc comment in `CustomLegend.qml` to note the new `tip`/swatch-style/hit-target inputs and that it is the shared legend for all graphs. Confirm no user-facing manual/wiki update is needed (no feature change).
