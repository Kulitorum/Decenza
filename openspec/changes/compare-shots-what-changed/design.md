# Design

## Context

See proposal.md for motivation. The current state:

- `ShotComparisonModel` keeps every selected id sorted ascending (`shotcomparisonmodel.cpp:60`),
  loads a window of `DISPLAY_WINDOW_SIZE = 3` on a worker thread, and copies ~25 metadata fields
  from `ShotRecord` into its own `ComparisonShot` struct. It has no tests.
- `ComparisonGraph.qml` hard-codes three shots (`showShot0..2`, `_allTraces` loops `s < 3`).
  Line pattern is per column; colour and width are per curve.
- The web `/compare/` page (`generateComparisonPage`) embeds its own field list and renders an
  undifferenced table in JS. It takes 2-10 ids in the given order.
- Four shot-to-shot diffs already exist, each with its own field list:
  - `DialingHelpers::buildShotChangeDiff` renders `"a -> b unit (+d)"` strings. It is used by
    `shots_compare` `changes[]` (consecutive shots) and by the advisor's `changeFromPrev` and
    `changeFromBest`. It mixes inputs (grind, RPM, bean, dose) with outcomes (yield, duration,
    rating).
  - `setupChangedFromPrior` returns a bool, with tolerances in `dialing_blocks.cpp:620-622`
    (grind 0.25, dose 0.3 g, yield 0.5 g) plus RPM 25. It uses `sameGrinderSetting` for
    notation, and "unknown is not a change".
  - `AIConversation::changesFromPreviousShot` diffs fields parsed back out of stored message
    payloads.
  - The compare page itself, which has no diff at all.
- Inputs that already exist and can be reused:
  - `ShotIdentity::fields()` (`dialing_helpers.h:105`) is the one table of equipment and bean
    identity fields, and `hoistSessionContext` splits shared from per-shot values.
  - `ShotRecord` carries every curve, `phaseSummariesJson` (per-phase duration and means,
    computed at save) and `profileJson`.
  - `loadShotRecordStatic` already runs `analyzeShot` and caches the detector results.
  - `Profile::dialInDeltas(a, b)` (`profile.cpp:1966`) diffs any two profiles.
    `ProfileManager::dialInDiffFor` maps its rows to `{kind, unit, frameIndex, …}` for
    `ProfileDialInDiffBlock.qml`.

## Goals / Non-Goals

**Goals:**
- Compute every comparison fact once, in one place: input diff, metrics, deltas, profile diff,
  stop reason, badges, phases. The app, the web page and MCP each render that result.
- Collapse the existing diffs onto it, so the advisor, MCP and the page cannot disagree about
  what changed.

**Non-Goals:**
- Showing more than three shots on screen at once. Paging beside a pinned base covers any
  number of selected shots.
- An "ask the advisor about this comparison" action. That is a new paid-AI surface and gets its
  own change.
- Changing how detectors, phase summaries or derived curves are computed.
- Saving the base choice or the pour-start alignment as settings.

## Decisions

**D1. One pure unit: `src/history/shotcomparison.{h,cpp}`.** Its functions take
`ShotProjection` (the converted `ShotRecord` that the advisor and MCP already use) and return
plain structs:

- `InputDiff diffInputs(base, shot)`
- `Metrics metricsFor(shot)`
- `ProfileDiff diffProfiles(base, shot)`
- `QJsonObject toJson(…)`

Every surface consumes the JSON: QML gets it as a `QVariantMap`, the web page embeds it, and MCP
returns it. The JSON carries raw values plus `kind` and `unit` tokens, never display strings, so
QML can apply `TranslationManager` and the user's temperature unit. This is the same contract
`ProfileDialInDiffBlock` already uses.

Alternatives considered:
- Computing in QML: the web page and MCP would each need a second copy.
- Extending `ShotSummarizer`: it is the advisor's renderer, and its peak and average helpers
  are per phase. Its `calculateMax` and `calculateAverage` are reused, not duplicated.

**D2. Input fields come from tables, not lists.** The identity rows walk
`ShotIdentity::fields()`. The shot-variable inputs (profile, temperature override, dose,
effective target yield via `effectiveTargetWeightG`, grind, RPM, barista) are one more table in
the new unit.

- Each field reports `Same`, `Changed` or `OneSided`.
- The tolerances and `sameGrinderSetting` move into the unit, and `dialing_blocks.cpp` includes
  them from there.
- The grind Δ is suppressed unless grinder model and burrs are `Same`.
- "Unchanged across all visible shots" is every field whose state is `Same` for every
  non-base shot. It is not `hoistSessionContext`: that compares strings exactly, so it would
  call 18.0 g and 18.2 g different while the diff calls them the same.
- Fine-grained identity fields display in groups (grinder = brand + model, basket, bean,
  roast). A group's state is the strongest of its members, and an identity field in no group
  shows on its own, so a new row in `ShotIdentity::fields()` still appears.

**D3. The existing diffs move onto D2.**
- `setupChangedFromPrior` becomes "any of {grind, rpm, dose, targetYield} is `Changed`". It keeps
  `OneSided` meaning not-a-change, so the advisor behaves as before.
- `buildShotChangeDiff` and `shots_compare` `changes[]` are replaced by the D1 JSON. The
  advisor's `changeFromPrev` and `changeFromBest` blocks keep their names but emit the same
  shape. `resources/ai/tools/dialing_get_context.md` and every system-prompt reference to the
  old string form are updated in the same change.
- `changesFromPreviousShot` has only stored payloads. If they carry the D2 fields, it builds
  input values from them and calls `diffInputs`. If they do not, it stays as it is, and a
  comment states which field is missing.

**D4. Metric definitions.** The pour window is the one shot analysis already derives from the
phase markers, `DetectorResults::pourStartSec`/`pourEndSec` (`shotanalysis.cpp:770-779`), so the
comparison, the alignment and the detectors agree on when the pour began. With no pour marker
(`pourStartSec == 0`) the window is the whole shot.

| Metric | Definition |
|---|---|
| First drop | First sample with cup weight ≥ 0.5 g |
| Preinfusion, pour | `pourStartSec`, and `pourEndSec − pourStartSec` (Decent's "Preinfusion") |
| Peak pressure | Maximum pressure over the whole shot |
| Peak flow, mean flow | Over the pour window, because the fill-phase flow spike says nothing about the puck |
| Average g/s | Yield ÷ (duration − first drop), Decent's "average g/s" |
| Group temp | Mean temperature over the pour window |
| Temperature sag | Temperature at pour start minus the minimum during the pour, floored at 0 |
| Resistance | Mean of the stored resistance curve over pour-window samples with flow ≥ 0.5 ml/s |
| Badges | The four badge flags on the shot (the temperature badge was removed earlier) |
| Stop reason | `stoppedBy` |

All of these are single linear passes on the worker thread that already loads the shot.

**D5. Profile diff.** The profile JSON from each shot is passed through `Profile::fromJson` and
then `Profile::dialInDeltas(base, shot)`. `dialInDiffFor`'s row-mapping loop is extracted so the
bundled-base diff and the shot-vs-shot diff share it, and `ProfileDialInDiffBlock.qml` renders
both, with a heading parameter. Encoding noise disappears because both sides are parsed into
`Profile` values before comparing. Different profile titles return names only.

**D6. Base and paging in the model.**
- The model gets `baseShotId` and `setBaseShot(id)`.
- Visible columns are the base followed by a window of two over the remaining ids, which stay
  in chronological order.
- `clearAll`/`addShots` reset the base to the oldest id.
- The loader always loads the base plus the window. Column 0 is always the base, so the graph's
  `showShot0` and the line pattern of column 0 belong to the base.
- The `ComparisonShot` metadata copy is replaced by the `ShotProjection` and its D1 result. The
  curves stay.

**D7. Graph.**
- The base's curves are drawn with width `graphLineWidth + 1`. Patterns stay per column.
- `ComparisonDataTable.qml` is removed, and its three jobs move out of the scroll path:
  - The crosshair readout becomes `ComparisonReadout`, a strip under the plot inside the
    graph card: one line per visible shot (line swatch, then each curve that is on as label
    and value, from `GraphSeries.entries`). A panel over the plot was built first and dropped:
    with every curve on it covered a quarter of a full-width plot and most of a half-width
    one, and hiding the columns of shots that had ended made the numbers jump on every tap.
  - Curve toggles and phase toggles merge into one wrapping chip row under the graph. Curves
    that are off collapse to a "+N" chip that expands the row.
  - Shot visibility becomes an eye toggle on each shot header card, next to the
    tap-to-make-base action.
- Pour-start alignment is a page-local "Align pours" chip in the chip row, off by default
  (a chip rather than an entry in the shared graph-options dialog, whose options are
  settings shared by three pages). It shifts
  each shot's x by its D4 pour start. Shots without one stay unshifted.
- The hard-coded `3` in `ComparisonGraph`, `ComparisonDataTable` and `ShotComparisonPage`
  becomes the model's visible count.

**D8a. Compact by default.** "What happened" renders the default rows from the spec, and
everything else sits behind one "Show more" toggle that is page-local and starts collapsed.
Units go in the row label, not in every cell. Numbers use tabular figures. Rows that carry
nothing (all "—", or a badge or stop reason the same on every shot) are not rendered.

**D8b. Summary sentence.** The unit produces a structured summary per non-base shot, made of
the changed inputs, the top three metrics by |Δ| ÷ that metric's noise floor (`notable` in the
metric table, e.g. 1 s, 1 g, 0.3 bar, 0.5 °C; below one floor a change is not named), a stop-reason difference, and appeared/disappeared badges. QML and the web
JS turn it into a sentence from translated fragments. Only the ranking lives in C++, so app and
web pick the same three. No AI provider is involved.

**D8c. Layout.** The graph panel (paging, plot, readout strip, chips) sits above a
`Flickable` that holds the comparison; only the comparison scrolls. The plot is capped at about
half the page height so a tall saved graph cannot squeeze the comparison out. Side by side was
built and dropped after trying it: at half the width the plot was too small to read.

**D8. Δ presentation.** A Δ uses the value's precision. Increase is `warningColor` (gold) and
decrease `primaryColor` (blue), as on the decentespresso.com page; `accentColor` was rejected
because its default is an alarm red. A Δ that rounds to zero shows no pill. This follows the decentespresso.com support page: up and down are not
good and bad.

**D9. Entry point.**
- Shot Detail and Post-Shot Review query, on a worker thread with `withTempDb`, for the previous
  shot on the same profile kb id and equipment package, keyed the way advisor threads are.
- The button appears only when the query returns a shot. It calls `addShots([previous, this])`.
  The previous shot is the oldest, so it becomes the base by the default rule.
- "Compare with best shot" was considered and left out: the previous shot is the common case,
  and a second button on two pages is clutter.

**D10. Web parity.** `generateComparisonPage` embeds the D1 JSON and ports the same layout to
JS: header tap re-bases, summary sentence, both sections with "Show more", Δs, the in-graph
readout, base line width and the alignment toggle. The route
sorts ids chronologically so the oldest is the default base, matching the app.

The layout follows the decentespresso.com support page: a two-column CSS grid with the graph on
the left (`position: sticky`, so it stays in view while the right column scrolls) and the
sections on the right. Below a breakpoint it becomes one column, graph first. A side-by-side
layout earns its place on the web because a desktop browser has width to spare and the crosshair
hover can be read against the table without scrolling. The breakpoint is the width at which the
right column fits three shot columns plus labels without wrapping values, measured during
implementation, not guessed.

## Risks / Trade-offs

- [Changing advisor payload shape (`changeFromPrev`/`changeFromBest`) shifts what the model
  reads] → The same model already reads the D1 shape through MCP, so this unifies two formats
  rather than adding one. Update the tool doc and prompt references, and run the existing
  advisor tests.
- [First drop depends on a correct tare, and a mis-tare moves the pour window] → A mis-tared
  shot already produces wrong yield. The window is visible as the alignment point, so a bad one
  is noticeable instead of hidden.
- [MCP clients relying on `changes[]`] → Bump `McpSurfaceVersion`. The tool description states
  the base rule.
- [Test build cost] → One new test file covers the unit (diff rules, metrics on a
  `tests/data/shots` corpus shot, profile diff, rating). `shotcomparison.cpp` goes into a narrow
  library linked only by its consumers, not into `decenza_testlib`.

## Migration Plan

Nothing is stored and no schema changes. Rollback is reverting the change.
