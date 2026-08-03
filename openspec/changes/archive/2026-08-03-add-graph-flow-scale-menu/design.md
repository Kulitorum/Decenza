## Context

Three Decenza graphs draw flow against the shared 0–12 pressure axis: `ShotGraph.qml` (live,
fixed `max: 12`), `HistoryShotGraph.qml` (auto-ranged via `pressureAxisMax`) and
`ComparisonGraph.qml`. The left axis titles itself `bar / mL·g/s` and reads for both quantities
at once, which is the DE1 convention and is correct only because the two happen to share a
numeric range.

They do not share a *useful* range. Pressure spends the shot between 6 and 10 bar; pour flow
spends it between 1.5 and 2.5 mL/s. The upper half of the axis carries pressure and the
puck-fill spike, and the pour — the part being dialled — is compressed into the bottom sixth.

The right axis is already a second, hand-rolled coordinate space: `tempAxis` and `weightAxis`
are plain `QtObject` value holders (Qt Graphs offers no sanctioned dual-Y path), read by
`DashedLineSeries`/`FastLineRenderer` for their own data→pixel mapping, with a manual label
column at `HistoryShotGraph.qml:724-750`. A `graph/showWeightAxis` boolean picks between them.
The flow scale reuses that machinery rather than introducing a new one.

Constraints that shaped the design:

- **Live rendering is on the hot path.** The multiplier must not add per-sample work to the
  live graph beyond a single multiply, and must not force a re-plot of history on every
  settings read.
- **`shot_samples` is the record of truth** and must keep true units, because Visualizer
  export, the detectors in `shotanalysis.cpp`, and `shot_eval` all read it.
- **The graph is diagnosed from screenshots** in user reports. A scale that is active but not
  legible in the image would make every future report ambiguous.

## Goals / Non-Goals

**Goals:**

- Make the pour legible at the default setting without clipping any pour detail.
- Keep one setting behind every graph surface, persisted, changed only by the user.
- Reuse the existing right-axis value-holder pattern for the flow mode.
- Guarantee that no numeric readout anywhere reports a plotted value.

**Non-Goals:**

- Per-shot or per-profile flow scale. One global preference.
- A continuously adjustable zoom (de1app Insight's tap-to-rescale, 1–15). Three discrete
  values are enough to cover the measured range and are far easier to make legible and
  accessible.
- Changing the live graph's fixed `max: 12`, or history's `pressureAxisMax` algorithm, beyond
  excluding the multiplier from it.
- Rescaling pressure. It reads correctly today.
- Any change to stored data, export, or the detectors.

## Decisions

### Multiply at the series, not at the axis

Qt Graphs will not give a second real Y axis, and the app already works around that with
value-holder objects. Two candidate implementations:

1. **Transform the points** — build the plotted point list as `y * scale`.
2. **Give flow its own value-holder axis** with `max = sharedMax / scale`, as `tempAxis` does,
   and let the renderer map it.

Chosen: **(2) for the trace, (1) nowhere.** The flow series are native Qt Graphs `LineSeries`
on the shared axis today, but the value-holder route means the multiplication happens once in
the mapping rather than per point per repaint, and it is the pattern the file already
documents. Where a flow-family series must remain a native `LineSeries`, the multiply is
applied when the point array is built and the source array is left untouched — never mutated
in place, so the un-scaling required for readouts reads from the original.

This is also what makes de1app's approach unattractive to copy literally: it keeps parallel
`_2x` vectors filled at capture time (`gui.tcl:3426`), which doubles memory, hard-codes the
multiplier at 2, and would have to be recomputed for every stored shot. We transform at plot
time instead.

### Auto-ranging must see unscaled flow

`HistoryShotGraph.qml:334` computes `pressureAxisMax` by walking `pressureData`, `flowData`,
`weightFlowRateData`, `pressureGoalData` and `flowGoalData` for a maximum, then padding to a
nice number. If the multiplied values fed that walk, the axis would grow by roughly the
multiplier and cancel the zoom exactly — the trace would land in the same place on screen and
the feature would appear to do nothing.

This is the single most likely way to ship this feature broken while every test passes, which
is why it is a requirement in the `charting` delta rather than a comment.

### Right-axis mode becomes a three-state enum

`graph/showWeightAxis` (bool) → `graph/rightAxisMode` (string: `weight` | `temperature` |
`flow`), read through a resolver that falls back to `weight` for anything unrecognised and
migrates the old boolean on first read. `toggleRightAxis()` becomes a cycle rather than a
negation.

Alternative considered: keep the boolean and add a second one for flow. Rejected — two
booleans encode four states for three modes, and the fourth is nonsense that some code path
will eventually reach.

### Multiplier and right-axis mode stay independent

Selecting 2x/3x does not move the right axis. That is a deliberate choice, and it has a cost:
a user on 3x with the right axis on weight has a flow trace with no numeric scale anywhere,
because the left axis title has dropped to `bar`. Shape only.

Accepted because auto-switching would silently discard a right-axis choice the user made, and
because the trace *shape* is what the multiplier exists to improve. The escape hatch is one
tap on the right axis.

### The left axis title tells the truth

At 1x the title stays `bar / mL·g/s`. At 2x/3x it becomes `bar`. This is the cheapest possible
signal that a transform is active, it appears in every screenshot, and it prevents the failure
mode where a reported graph is misread by whoever triages it.

### Readouts un-scale, and this is where the bug will be

Decaid demonstrates the exact defect in shipping code: its weight trace is plotted at ÷10
(`shot_chart.dart:359`) and the tooltip prints the plotted value, so a 36 g shot reads
"Weight: 3.6 g". Their temperature series carries an `isTemp` flag that triggers un-scaling and
weight simply was not given one (`:105`).

The structural lesson is that un-scaling driven by a per-series flag will be forgotten for some
series. So readouts read from the **source arrays**, which are never mutated, rather than from
plotted points that must be divided back. Where a readout genuinely only has the plotted value,
the un-scale is applied at the single point where plotted values enter the readout path, not at
each call site.

### Menu, not a growing row of buttons

The Advanced button on shot detail (`ShotDetailPage.qml:517-547`) and post-shot review
(`PostShotReviewPage.qml:1279+`) becomes a menu opener; the advanced toggle moves inside as an
option card. This mirrors `ExtractionViewSelector.qml`, which already holds the same advanced
toggle plus phase-indicator and stats toggles for the live screen — so the two surfaces
converge on one pattern instead of diverging further.

The live screen does **not** get a second menu; the flow scale is added to the selector it
already has, gated on chart mode like the advanced toggle at `:317`.

## Risks / Trade-offs

- **Autoscale cancels the zoom** → covered by a `charting` requirement and an explicit test
  asserting `pressureAxisMax` is identical at 1x and 3x for the same shot.
- **A readout reports a plotted value** → source arrays stay unmutated; tests assert the
  inspect bar reports true mL/s and g/s at 3x. The Decaid precedent is cited in the spec so
  the reviewer knows what shape of bug to look for.
- **The fill spike clips at every setting** — 93% of shots exceed 6 mL/s and 95% exceed 4, so
  the first ~4 s flat-tops at both 2x and 3x → accepted, and it is why the spike could not be
  used to choose between the multipliers. The clipped region carries no dialling information.
- **Channeling blowouts clip at 3x** (pour peak p99 5.47, max 9.36) → accepted; a trace pinned
  to the ceiling reads as "something went wrong", which it did. Users who want those legible
  have 2x and 1x.
- **Settings migration runs on every device** → the resolver is a pure read-time fallback with
  no write, so a device that never opens a graph is never migrated and nothing is lost.
- **Widget staleness** → `LastShotChartSource.qml:102` names `graph/showWeightAxis` in its
  watch-list. Its own comment records that this list has already failed once by omission. Both
  new keys go in, and the old key stays until the migration is retired.
- **Screenshot ambiguity in bug reports** → mitigated by the axis title, but a report taken at
  3x will still show flow at three times its true height. Triage guidance belongs in the wiki
  manual page.

## Migration Plan

1. Add `graph/flowScale` (int, default 2) and `graph/rightAxisMode` (string, default `weight`)
   to the graph settings domain.
2. Resolve `rightAxisMode` at read time: if unset and `graph/showWeightAxis` exists, map
   `true → weight`, `false → temperature`. No write-back, no schema migration, no database
   involvement.
3. Leave `graph/showWeightAxis` in place and readable. It can be dropped in a later release
   once no supported upgrade path still holds it.

Rollback is a revert: with the new keys unread, `graph/showWeightAxis` still holds the user's
last two-state choice and the graphs return to 1x behaviour.

## Open Questions

- Should the comparison graph offer the menu itself, or inherit the setting silently? Inheriting
  is assumed here; it draws flow, so it must honour the scale either way.
- Does the phase summary panel display any flow-family value that needs the un-scale treatment?
  To be confirmed while implementing rather than guessed now.
