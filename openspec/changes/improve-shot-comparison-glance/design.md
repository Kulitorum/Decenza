# Design

## Context

`ShotHistoryPage.qml` inserts a selection row above the filter row when selection becomes nonempty. Search reloads do not clear `selectedShots`; an edited search does clear the exact initial filter. `ShotComparisonPage.qml` uses a resizable graph followed by a detailed `ComparisonShotTable`. The comparison assembler supplies changed inputs as per-shot cells and unchanged inputs as one value. Its temperature input is an override, not necessarily the profile's temperature.

## Goals / Non-Goals

**Goals:** Preserve the user's visual position, make common inputs available with the graph, and keep the shared native and Wasm behavior aligned.

**Non-Goals:** Change search semantics, sort ordering, the comparison algorithm, legacy web or MCP output, or add a persistent field picker.

## Decisions

- Reserve the selection toolbar's height even when no shots are selected. This keeps the list viewport fixed on every supported width and preserves the existing filter controls and destructive-action confirmation. Use responsive controls within that stable region; do not conditionally add a second row. Replacing the search field was considered but would hide a useful way to build a selection.
- Add a compact summary in the graph header area, above the graph card, with a row for each displayed shot and fixed grind, dose and temperature-override values. Use shot date/time and the existing line swatch/base indication to associate rows with curves. It follows the model's displayed window and base, including hidden graph curves. The detailed table remains the canonical full comparison. A customizable picker is deferred until a wider need is demonstrated.
- Derive values from the existing comparison's changed cells and unchanged values if their semantics preserve the recorded values; otherwise extend the comparison facade/source with explicitly per-shot values. Do not infer a profile temperature from the override or treat a missing override as zero. Use existing formatting and accessible labels. Keep the summary outside the graph card so crosshair gestures and readout are unaffected.
- Keep layout bounded and readable: wrap or internally scroll the summary when necessary without horizontal page scrolling; preserve a useful graph on narrow screens. Shared QML serves native and Wasm. New shared QML files, if any, require registration in both builds.

## Risks / Trade-offs

- [Reserved toolbar uses space before selection] → Keep it compact and check tablet and narrow-screen layouts; a stable viewport is the priority.
- [The summary adds height above the plot] → Limit it to three inputs and displayed shots, validate graph readability at default and resized heights, and use internal overflow where needed.
- [Unchanged input aggregation uses comparison tolerances] → Verify displayed values are actual recorded per-shot values; do not duplicate the base value as though it were exact for every shot when tolerances collapse a difference.
- [Long translated strings or several shots crowd the header] → Wrap, provide accessible shot/value names, and validate scaled text and narrow widths.
